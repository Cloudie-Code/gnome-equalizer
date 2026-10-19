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

/* Runs a sine through the DSP and returns the steady-state output level in dB. */
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
	double quiet = response_db(&params, 1000, 0.01f, 8) + 20.0 * log10(0.01 / sqrt(2));
	double loud = response_db(&params, 1000, 0.5f, 8) + 20.0 * log10(0.5 / sqrt(2));
	check(fabs(quiet - loud) < 12.0, "loudness eq shrinks a 34 dB gap below 12 dB", quiet - loud, 0);

	eq_params_init(&params);
	for (int b = 0; b < EQ_BANDS; b++)
		atomic_store(&params.gains[b], 12.0f);
	double peak = peak_out(&params, 1000, 0.9f);
	check(peak <= 0.8915, "limiter keeps +12 dB under -1 dBFS", peak, 0.891);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
