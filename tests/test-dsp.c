/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "dsp.h"

#define RATE 48000.0f
#define BLOCK 512

static int failures;

static void check(int ok, const char *what, double got, double want)
{
	printf("%s %-44s got %8.2f want %8.2f\n", ok ? "ok  " : "FAIL", what, got, want);
	if (!ok)
		failures++;
}

static double response_db(struct eq_params *params, float freq, float amplitude, float seconds)
{
	static float in_l[BLOCK], in_r[BLOCK], out_l[BLOCK], out_r[BLOCK];
	struct eq_dsp dsp;
	eq_dsp_init(&dsp, RATE);

	uint32_t total = (uint32_t)(seconds * RATE), measured_from = total - (uint32_t)(0.2f * RATE);
	double sum_in = 0, sum_out = 0;

	for (uint32_t pos = 0; pos < total; pos += BLOCK) {
		for (int i = 0; i < BLOCK; i++) {
			float x = amplitude * sinf(2.0f * (float)M_PI * freq * (float)(pos + i) / RATE);
			in_l[i] = in_r[i] = x;
		}
		const float *in[2] = { in_l, in_r };
		float *out[2] = { out_l, out_r };
		eq_dsp_process(&dsp, params, in, out, BLOCK);
		if (pos >= measured_from) {
			for (int i = 0; i < BLOCK; i++) {
				sum_in += in_l[i] * in_l[i];
				sum_out += out_l[i] * out_l[i];
			}
		}
	}
	return 10.0 * log10(sum_out / sum_in);
}

static double peak_out(struct eq_params *params, float freq, float amplitude)
{
	static float in_l[BLOCK], in_r[BLOCK], out_l[BLOCK], out_r[BLOCK];
	struct eq_dsp dsp;
	eq_dsp_init(&dsp, RATE);
	double peak = 0;

	for (uint32_t pos = 0; pos < (uint32_t)RATE; pos += BLOCK) {
		for (int i = 0; i < BLOCK; i++)
			in_l[i] = in_r[i] = amplitude * sinf(2.0f * (float)M_PI * freq * (float)(pos + i) / RATE);
		const float *in[2] = { in_l, in_r };
		float *out[2] = { out_l, out_r };
		eq_dsp_process(&dsp, params, in, out, BLOCK);
		for (int i = 0; i < BLOCK; i++)
			peak = fmax(peak, fabs(out_l[i]));
	}
	return peak;
}

/* A steady vocal tone plus intermittent bass: measure the vocal component,
 * so the bass itself cannot disguise whole-mix gain pumping. */
static void check_loudness_stability(bool bass_eq, float rate, uint32_t block)
{
	struct eq_params params;
	struct eq_dsp dsp;
	float left[1024], right[1024];
	eq_params_init(&params);
	atomic_store(&params.loudness, true);
	if (bass_eq) {
		atomic_store(&params.gains[0], 11.5f);
		atomic_store(&params.gains[1], 3.5f);
		atomic_store(&params.bass_boost, true);
		atomic_store(&params.bass_boost_level, 8.0f);
	}
	eq_dsp_init(&dsp, rate);
	double low = 1e9, high = -1e9, peak = 0;
	double projection = 0;
	uint32_t measured = 0;
	for (uint32_t pos = 0; pos < (uint32_t)(40 * rate); pos += block) {
		for (uint32_t i = 0; i < block; i++) {
			double t = (pos + i) / (double)rate;
			/* Include a full two-second break in the bass every four seconds. */
			float bass = fmod(t, 4.0) < 2.0 && fmod(t, 0.5) < 0.12
				? 0.3f * sin(2 * M_PI * 60 * t) : 0;
			left[i] = right[i] = 0.08f * sin(2 * M_PI * 1000 * t) + bass;
		}
		const float *in[2] = { left, right };
		float *out[2] = { left, right };
		eq_dsp_process(&dsp, &params, in, out, block);
		for (uint32_t i = 0; i < block; i++) {
			peak = fmax(peak, fabs(left[i]));
			if (pos + i >= (uint32_t)(25 * rate)) {
				projection += left[i] * sin(2 * M_PI * 1000 * (pos + i) / (double)rate);
				if (++measured == (uint32_t)(rate / 10)) {
					double gain = 20 * log10(fabs(2 * projection / (0.08 * measured)));
					low = fmin(low, gain);
					high = fmax(high, gain);
					projection = 0;
					measured = 0;
				}
			}
		}
	}
	check(high - low < 1.0, bass_eq ? "vocal stays stable with boosted bass" :
	      "vocal stays stable with intermittent bass", high - low, 0);
	check(peak <= 0.89101, "bass transients stay below the peak ceiling", peak, 0.891);

	float gain_before = dsp.agc_gain_db;
	float target_before = dsp.agc_target_db;
	float short_before = dsp.loudness_short_power;
	float long_before = dsp.loudness_long_power;
	for (uint32_t pos = 0; pos < (uint32_t)(8 * rate); pos += block) {
		for (uint32_t i = 0; i < block; i++)
			left[i] = right[i] = 0;
		const float *in[2] = { left, right };
		float *out[2] = { left, right };
		eq_dsp_process(&dsp, &params, in, out, block);
		/* Let the EQ and weighting filters ring out before measuring silence. */
		if (pos < (uint32_t)rate) {
			short_before = dsp.loudness_short_power;
			long_before = dsp.loudness_long_power;
		}
	}
	check(fabs(dsp.agc_gain_db - gain_before) < 0.5,
	      "silence does not increase normalizer gain", dsp.agc_gain_db - gain_before, 0);
	check(fabs(dsp.agc_target_db - target_before) < 0.5,
	      "silence does not request extra boost", dsp.agc_target_db - target_before, 0);
	check(dsp.loudness_short_power == short_before && dsp.loudness_long_power == long_before,
	      "silence freezes the loudness meters", dsp.loudness_long_power, long_before);

	atomic_store(&params.loudness, false);
	for (int j = 0; j < (int)(rate / block); j++) {
		const float *in[2] = { left, right };
		float *out[2] = { left, right };
		eq_dsp_process(&dsp, &params, in, out, block);
	}
	check(dsp.agc_gain_db == 0, "disabling loudness restores unity gain", dsp.agc_gain_db, 0);
}

static void check_level_changes(void)
{
	struct eq_params params;
	struct eq_dsp dsp;
	float left[BLOCK], right[BLOCK];
	double power[3] = { 0 };
	uint32_t count[3] = { 0 };
	eq_params_init(&params);
	atomic_store(&params.loudness, true);
	eq_dsp_init(&dsp, RATE);
	for (uint32_t pos = 0; pos < (uint32_t)(90 * RATE); pos += BLOCK) {
		for (int i = 0; i < BLOCK; i++) {
			float amplitude = pos + i >= (uint32_t)(30 * RATE) &&
				pos + i < (uint32_t)(60 * RATE) ? 0.4f : 0.04f;
			left[i] = right[i] = amplitude * sin(2 * M_PI * 1000 * (pos + i) / RATE);
		}
		const float *in[2] = { left, right };
		float *out[2] = { left, right };
		eq_dsp_process(&dsp, &params, in, out, BLOCK);
		for (int i = 0; i < BLOCK; i++) {
			int section = (int)((pos + i) / (30 * RATE));
			if (section < 3 && fmod((pos + i) / (double)RATE, 30) >= 29) {
				power[section] += left[i] * left[i];
				count[section]++;
			}
		}
	}
	for (int i = 1; i < 3; i++) {
		double gap = 10 * log10((power[i] / count[i]) / (power[0] / count[0]));
		check(fabs(gap) < 1.0, i == 1 ? "normalizes a sustained 20 dB increase" :
		      "normalizes a sustained 20 dB decrease", gap, 0);
	}
	for (int i = 0; i < 3; i++) {
		double rms = sqrt(power[i] / count[i]);
		check(rms >= 0.4 / sqrt(2.0) * pow(10.0, -1.0 / 20.0),
		      "reaches loud input within 1 dB while settling", rms, 0.4 / sqrt(2.0));
	}
}

/* Bass-free material must not be driven to full-scale merely because its
 * waveform leaves more peak headroom. Repeat after a bass-heavy passage. */
static void check_bass_free_loudness(float rate, uint32_t block)
{
	struct eq_params params;
	struct eq_dsp dsp;
	float left[1024], right[1024];
	double power[2] = { 0 };
	uint32_t count[2] = { 0 };
	eq_params_init(&params);
	atomic_store(&params.loudness, true);
	eq_dsp_init(&dsp, rate);
	for (uint32_t pos = 0; pos < (uint32_t)(90 * rate); pos += block) {
		for (uint32_t i = 0; i < block; i++) {
			double t = (pos + i) / (double)rate;
			float bass = t >= 30 && t < 60 ? 0.3f * sin(2 * M_PI * 60 * t) : 0;
			left[i] = right[i] = 0.08f * sin(2 * M_PI * 1000 * t) + bass;
		}
		const float *in[2] = { left, right };
		float *out[2] = { left, right };
		eq_dsp_process(&dsp, &params, in, out, block);
		for (uint32_t i = 0; i < block; i++) {
			double t = (pos + i) / (double)rate;
			int section = t >= 29 && t < 30 ? 0 : t >= 89 && t < 90 ? 1 : -1;
			if (section >= 0) {
				power[section] += left[i] * left[i];
				count[section]++;
			}
		}
	}
	for (int i = 0; i < 2; i++) {
		double rms = sqrt(power[i] / count[i]);
		check(rms < 0.34, i ? "removing bass does not over-amplify the mix" :
		      "bass-free audio has a loudness boost ceiling", rms, 0.34);
		check(rms > 0.08 / sqrt(2.0), "quiet bass-free audio is still raised", rms, 0.08 / sqrt(2.0));
	}
}

int main(void)
{
	struct eq_params params;
	char what[64];

	eq_params_init(&params);
	check(fabs(response_db(&params, 1000, 0.1f, 1)) < 0.05, "flat EQ is transparent at 1 kHz", response_db(&params, 1000, 0.1f, 1), 0);

	for (int b = 0; b < EQ_BANDS; b++) {
		eq_params_init(&params);
		atomic_store(&params.gains[b], 6.0f);
		double db = response_db(&params, eq_band_freqs[b], 0.05f, 1);
		snprintf(what, sizeof(what), "+6 dB band at %.0f Hz", eq_band_freqs[b]);
		check(fabs(db - 6.0) < 0.5, what, db, 6);
	}

	eq_params_init(&params);
	atomic_store(&params.gains[0], -12.0f);
	check(fabs(response_db(&params, 60, 0.1f, 1) + 12.0) < 0.5, "-12 dB band at 60 Hz", response_db(&params, 60, 0.1f, 1), -12);

	eq_params_init(&params);
	atomic_store(&params.enabled, false);
	atomic_store(&params.gains[3], 12.0f);
	check(fabs(response_db(&params, 1600, 0.1f, 1)) < 0.05, "disabled EQ is transparent", response_db(&params, 1600, 0.1f, 1), 0);

	eq_params_init(&params);
	atomic_store(&params.bass_boost, true);
	double bass = response_db(&params, 40, 0.05f, 1), mid = response_db(&params, 2000, 0.05f, 1);
	check(bass > 4.5 && bass < 6.5, "bass boost +6 dB at 40 Hz", bass, 6);
	check(fabs(mid) < 0.5, "bass boost leaves 2 kHz alone", mid, 0);

	eq_params_init(&params);
	atomic_store(&params.loudness, true);
	const float levels[] = { 0.15f, 0.5f, 0.8f, 0.88f, 0.95f };
	for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
		atomic_store(&params.loudness, false);
		double baseline = response_db(&params, 1000, levels[i], 20);
		atomic_store(&params.loudness, true);
		double gain = response_db(&params, 1000, levels[i], 20);
		snprintf(what, sizeof(what), "loudness preserves volume at amplitude %.2f", levels[i]);
		check(gain >= baseline - 0.05, what, gain, baseline);
	}
	double quiet = response_db(&params, 1000, 0.01f, 8) + 20.0 * log10(0.01 / sqrt(2));
	double loud = response_db(&params, 1000, 0.5f, 8) + 20.0 * log10(0.5 / sqrt(2));
	check(fabs(quiet - loud) < 12.0, "loudness eq shrinks a 34 dB gap below 12 dB", quiet - loud, 0);

	eq_params_init(&params);
	for (int b = 0; b < EQ_BANDS; b++)
		atomic_store(&params.gains[b], 12.0f);
	double peak = peak_out(&params, 1000, 0.9f);
	check(peak <= 0.8915, "limiter keeps +12 dB under -1 dBFS", peak, 0.891);

	check_level_changes();
	check_bass_free_loudness(48000, 512);
	check_bass_free_loudness(44100, 128);
	check_bass_free_loudness(48000, 1024);
	check_loudness_stability(false, RATE, BLOCK);
	check_loudness_stability(true, RATE, BLOCK);
	check_loudness_stability(true, 44100, 128);
	check_loudness_stability(true, 48000, 1024);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
