/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define EQ_BANDS 6
#define EQ_CHANNELS 2
#define EQ_GAIN_MIN -12.0f
#define EQ_GAIN_MAX 12.0f

extern const float eq_band_freqs[EQ_BANDS];

/* Written by the main thread, read by the audio thread once per block. */
struct eq_params {
	_Atomic bool enabled;
	_Atomic float gains[EQ_BANDS];
	_Atomic bool loudness;
	_Atomic bool bass_boost;
	_Atomic float bass_boost_level;
};

struct eq_biquad {
	float b0, b1, b2, a1, a2;
	float z1[EQ_CHANNELS], z2[EQ_CHANNELS];
};

struct eq_dsp {
	float rate;

	float band_gain[EQ_BANDS];
	struct eq_biquad bands[EQ_BANDS];
	float bass_gain;
	struct eq_biquad bass;

	struct eq_biquad kw_shelf, kw_highpass;
	float loudness_short_power;
	float loudness_long_power;
	bool loudness_measured;
	float loudness_peak;
	uint32_t loudness_peak_hold;
	float agc_gain_db;
	float agc_target_db;

	float limiter_gain;
	uint32_t limiter_hold;
};

void eq_params_init(struct eq_params *params);
void eq_dsp_init(struct eq_dsp *dsp, float rate);
void eq_dsp_process(struct eq_dsp *dsp, const struct eq_params *params,
		    const float *in[EQ_CHANNELS], float *out[EQ_CHANNELS],
		    uint32_t n_samples);
