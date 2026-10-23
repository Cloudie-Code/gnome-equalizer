/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dsp.h"

#include <math.h>
#include <string.h>

const float eq_band_freqs[EQ_BANDS] = { 60.0f, 180.0f, 550.0f, 1600.0f, 5000.0f, 15000.0f };

/* The bands are ~1.6 octaves apart, Q 0.9 makes neighbours overlap smoothly. */
#define BAND_Q 0.9f
#define BASS_FREQ 110.0f
#define BASS_SLOPE 0.8f

/* Parameter changes glide over this time so moving a slider never clicks. */
#define PARAM_SMOOTH_SEC 0.04f

#define LOUDNESS_TARGET_DB -18.0f
#define LOUDNESS_GATE_DB -50.0f
#define LOUDNESS_MAX_BOOST_DB 15.0f
#define LOUDNESS_MAX_CUT_DB -10.0f
#define LOUDNESS_WINDOW_SEC 3.0f
#define LOUDNESS_ATTACK_SEC 1.0f
#define LOUDNESS_RELEASE_SEC 4.0f
#define LOUDNESS_DEADBAND_DB 0.5f
#define LOUDNESS_PEAK_HOLD_SEC 3.0f
#define LOUDNESS_PEAK_RELEASE_SEC 6.0f
#define LOUDNESS_PEAK_MARGIN_DB 1.0f

#define LIMITER_CEILING 0.891f /* -1 dBFS */
#define LIMITER_RELEASE_SEC 0.5f
#define LIMITER_HOLD_SEC 0.1f

#define DB_EPSILON 0.005f

static inline float db_to_gain(float db)
{
	return powf(10.0f, db / 20.0f);
}

static inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static float limit_freq(float freq, float rate)
{
	/* Keep the top band meaningful on low-rate sinks (e.g. Bluetooth HFP). */
	return fminf(freq, rate * 0.45f);
}

static void biquad_set(struct eq_biquad *bq, double b0, double b1, double b2,
		       double a0, double a1, double a2)
{
	bq->b0 = (float)(b0 / a0);
	bq->b1 = (float)(b1 / a0);
	bq->b2 = (float)(b2 / a0);
	bq->a1 = (float)(a1 / a0);
	bq->a2 = (float)(a2 / a0);
}

/* Coefficients from the RBJ Audio EQ Cookbook. */
static void biquad_peaking(struct eq_biquad *bq, float rate, float freq, float q, float db)
{
	double A = pow(10.0, db / 40.0);
	double w0 = 2.0 * M_PI * limit_freq(freq, rate) / rate;
	double alpha = sin(w0) / (2.0 * q);
	double c = cos(w0);

	biquad_set(bq, 1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A,
		   1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
}

static void biquad_shelf(struct eq_biquad *bq, float rate, float freq, float slope,
			 float db, bool high)
{
	double A = pow(10.0, db / 40.0);
	double w0 = 2.0 * M_PI * limit_freq(freq, rate) / rate;
	double c = cos(w0);
	double alpha = sin(w0) / 2.0 * sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
	double s = 2.0 * sqrt(A) * alpha;
	double sign = high ? -1.0 : 1.0;

	biquad_set(bq,
		   A * ((A + 1.0) - sign * (A - 1.0) * c + s),
		   sign * 2.0 * A * ((A - 1.0) - sign * (A + 1.0) * c),
		   A * ((A + 1.0) - sign * (A - 1.0) * c - s),
		   (A + 1.0) + sign * (A - 1.0) * c + s,
		   -sign * 2.0 * ((A - 1.0) + sign * (A + 1.0) * c),
		   (A + 1.0) + sign * (A - 1.0) * c - s);
}

static void biquad_highpass(struct eq_biquad *bq, float rate, float freq, float q)
{
	double w0 = 2.0 * M_PI * freq / rate;
	double alpha = sin(w0) / (2.0 * q);
	double c = cos(w0);

	biquad_set(bq, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0,
		   1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

/* Transposed direct form II, in place. */
static void biquad_run(struct eq_biquad *bq, int ch, float *buf, uint32_t n)
{
	float b0 = bq->b0, b1 = bq->b1, b2 = bq->b2, a1 = bq->a1, a2 = bq->a2;
	float z1 = bq->z1[ch], z2 = bq->z2[ch];

	for (uint32_t i = 0; i < n; i++) {
		float x = buf[i];
		float y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		buf[i] = y;
	}
	/* Flush denormals so silence does not burn CPU. */
	bq->z1[ch] = fabsf(z1) < 1e-20f ? 0.0f : z1;
	bq->z2[ch] = fabsf(z2) < 1e-20f ? 0.0f : z2;
}

static inline float kw_step(struct eq_biquad *bq, int ch, float x)
{
	float y = bq->b0 * x + bq->z1[ch];
	bq->z1[ch] = bq->b1 * x - bq->a1 * y + bq->z2[ch];
	bq->z2[ch] = bq->b2 * x - bq->a2 * y;
	return y;
}

void eq_params_init(struct eq_params *params)
{
	atomic_init(&params->enabled, true);
	for (int i = 0; i < EQ_BANDS; i++)
		atomic_init(&params->gains[i], 0.0f);
	atomic_init(&params->loudness, false);
	atomic_init(&params->bass_boost, false);
	atomic_init(&params->bass_boost_level, 6.0f);
}

void eq_dsp_init(struct eq_dsp *dsp, float rate)
{
	memset(dsp, 0, sizeof(*dsp));
	dsp->rate = rate;

	for (int i = 0; i < EQ_BANDS; i++)
		biquad_peaking(&dsp->bands[i], rate, eq_band_freqs[i], BAND_Q, 0.0f);
	biquad_shelf(&dsp->bass, rate, BASS_FREQ, BASS_SLOPE, 0.0f, false);

	/* ITU-R BS.1770 K-weighting, recomputed for the actual rate. */
	biquad_shelf(&dsp->kw_shelf, rate, 1681.97f, 1.0f, 4.0f, true);
	biquad_highpass(&dsp->kw_highpass, rate, 38.13f, 0.5f);

	dsp->mean_square = 0.0f;
	dsp->limiter_gain = 1.0f;
}

static bool glide(float *current, float target, float k)
{
	if (*current == target)
		return false;
	float next = *current + (target - *current) * k;
	*current = fabsf(target - next) < DB_EPSILON ? target : next;
	return true;
}

static void update_filters(struct eq_dsp *dsp, const struct eq_params *params, uint32_t n)
{
	bool enabled = atomic_load_explicit(&params->enabled, memory_order_relaxed);
	float k = 1.0f - expf(-(float)n / (PARAM_SMOOTH_SEC * dsp->rate));

	for (int i = 0; i < EQ_BANDS; i++) {
		float target = enabled
			? clampf(atomic_load_explicit(&params->gains[i], memory_order_relaxed),
				 EQ_GAIN_MIN, EQ_GAIN_MAX)
			: 0.0f;
		if (glide(&dsp->band_gain[i], target, k))
			biquad_peaking(&dsp->bands[i], dsp->rate, eq_band_freqs[i], BAND_Q,
				       dsp->band_gain[i]);
	}

	float bass = 0.0f;
	if (enabled && atomic_load_explicit(&params->bass_boost, memory_order_relaxed))
		bass = clampf(atomic_load_explicit(&params->bass_boost_level, memory_order_relaxed),
			      0.0f, EQ_GAIN_MAX);
	if (glide(&dsp->bass_gain, bass, k))
		biquad_shelf(&dsp->bass, dsp->rate, BASS_FREQ, BASS_SLOPE, dsp->bass_gain, false);
}

static void update_loudness(struct eq_dsp *dsp, const struct eq_params *params,
			    float *const buf[EQ_CHANNELS], uint32_t n)
{
	bool active = atomic_load_explicit(&params->enabled, memory_order_relaxed) &&
		      atomic_load_explicit(&params->loudness, memory_order_relaxed);
	double power_sum = 0.0;
	float peak_release = expf(-1.0f / (LOUDNESS_PEAK_RELEASE_SEC * dsp->rate));

	for (uint32_t i = 0; i < n; i++) {
		float peak = 0.0f;
		for (int ch = 0; ch < EQ_CHANNELS; ch++) {
			peak = fmaxf(peak, fabsf(buf[ch][i]));
			float x = kw_step(&dsp->kw_shelf, ch, buf[ch][i]);
			x = kw_step(&dsp->kw_highpass, ch, x);
			power_sum += (double)x * x;
		}
		/* Remember bass peaks across beats and short breaks. Otherwise the
		 * normalizer boosts between beats and repeatedly drives the limiter. */
		if (peak >= dsp->loudness_peak) {
			dsp->loudness_peak = peak;
			dsp->loudness_peak_hold = (uint32_t)(LOUDNESS_PEAK_HOLD_SEC * dsp->rate);
		} else if (dsp->loudness_peak_hold > 0) {
			dsp->loudness_peak_hold--;
		} else {
			dsp->loudness_peak *= peak_release;
		}
	}

	float power = (float)(power_sum / n);
	float level_db = 10.0f * log10f(power + 1e-12f) - 0.691f;
	/* Exclude silence from the average, rather than letting it decay and
	 * asking for maximum boost when playback resumes. */
	if (level_db > LOUDNESS_GATE_DB) {
		float a = 1.0f - expf(-(float)n / (LOUDNESS_WINDOW_SEC * dsp->rate));
		if (!dsp->loudness_measured) {
			dsp->mean_square = power;
			dsp->loudness_measured = true;
		} else {
			dsp->mean_square += a * (power - dsp->mean_square);
		}
	}

	if (!active) {
		dsp->agc_target_db = 0.0f;
	} else if (level_db > LOUDNESS_GATE_DB) {
		float average_db = 10.0f * log10f(dsp->mean_square + 1e-12f) - 0.691f;
		float target = clampf(LOUDNESS_TARGET_DB - average_db,
				      LOUDNESS_MAX_CUT_DB, LOUDNESS_MAX_BOOST_DB);
		float headroom = 20.0f * log10f(LIMITER_CEILING /
					       fmaxf(dsp->loudness_peak, 1e-12f)) - LOUDNESS_PEAK_MARGIN_DB;
		target = fminf(target, headroom);
		if (fabsf(target - dsp->agc_target_db) > LOUDNESS_DEADBAND_DB)
			dsp->agc_target_db = target;
	} else {
		dsp->agc_target_db = dsp->agc_gain_db;
	}
}

void eq_dsp_process(struct eq_dsp *dsp, const struct eq_params *params,
		    const float *in[EQ_CHANNELS], float *out[EQ_CHANNELS],
		    uint32_t n)
{
	if (n == 0)
		return;

	for (int ch = 0; ch < EQ_CHANNELS; ch++) {
		if (out[ch] != in[ch])
			memcpy(out[ch], in[ch], n * sizeof(float));
	}

	update_filters(dsp, params, n);

	for (int b = 0; b < EQ_BANDS; b++) {
		if (dsp->band_gain[b] == 0.0f)
			continue;
		for (int ch = 0; ch < EQ_CHANNELS; ch++)
			biquad_run(&dsp->bands[b], ch, out[ch], n);
	}
	if (dsp->bass_gain != 0.0f) {
		for (int ch = 0; ch < EQ_CHANNELS; ch++)
			biquad_run(&dsp->bass, ch, out[ch], n);
	}

	/* Level is measured after the EQ, so boosting a band is compensated too. */
	update_loudness(dsp, params, out, n);

	float tau = dsp->agc_target_db < dsp->agc_gain_db ? LOUDNESS_ATTACK_SEC : LOUDNESS_RELEASE_SEC;
	if (!atomic_load_explicit(&params->enabled, memory_order_relaxed) ||
	    !atomic_load_explicit(&params->loudness, memory_order_relaxed))
		tau = PARAM_SMOOTH_SEC;
	float start_db = dsp->agc_gain_db;
	glide(&dsp->agc_gain_db, dsp->agc_target_db, 1.0f - expf(-(float)n / (tau * dsp->rate)));
	float g0 = db_to_gain(start_db), g1 = db_to_gain(dsp->agc_gain_db);
	bool enabled = atomic_load_explicit(&params->enabled, memory_order_relaxed);

	/* Bypassed and fully settled: the copy above is the output. */
	bool processing = enabled || dsp->bass_gain != 0.0f || dsp->agc_gain_db != 0.0f ||
			  dsp->limiter_gain < 1.0f;
	for (int b = 0; b < EQ_BANDS && !processing; b++)
		processing = dsp->band_gain[b] != 0.0f;
	if (!processing)
		return;

	float release = 1.0f - expf(-1.0f / (LIMITER_RELEASE_SEC * dsp->rate));
	float lim = dsp->limiter_gain;
	float step = (g1 - g0) / (float)n;

	for (uint32_t i = 0; i < n; i++) {
		float g = g0 + step * (float)(i + 1);
		float peak = 0.0f;
		for (int ch = 0; ch < EQ_CHANNELS; ch++) {
			out[ch][i] *= g;
			peak = fmaxf(peak, fabsf(out[ch][i]));
		}
		if (peak * lim > LIMITER_CEILING) {
			lim = LIMITER_CEILING / peak;
			dsp->limiter_hold = (uint32_t)(LIMITER_HOLD_SEC * dsp->rate);
		} else if (dsp->limiter_hold > 0) {
			dsp->limiter_hold--;
		} else {
			lim += (1.0f - lim) * release;
			if (peak > 0.0f)
				lim = fminf(lim, LIMITER_CEILING / peak);
		}
		for (int ch = 0; ch < EQ_CHANNELS; ch++)
			out[ch][i] *= lim;
	}
	dsp->limiter_gain = lim > 0.9999f ? 1.0f : lim;
}
