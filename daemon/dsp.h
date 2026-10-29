/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define EQ_BANDS 6
#define EQ_CHANNELS 2
#define EQ_GAIN_MIN -12.0f
#define EQ_GAIN_MAX 12.0f
#define EQ_LOOKAHEAD_CAPACITY 2048
#define EQ_LOOKAHEAD_MS 5

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

struct eq_peak_window {
	uint64_t head, tail;
	uint64_t frame[EQ_LOOKAHEAD_CAPACITY];
	float peak[EQ_LOOKAHEAD_CAPACITY];
};

struct eq_dsp {
	float rate;

	float band_gain[EQ_BANDS];
	struct eq_biquad bands[EQ_BANDS];
	float bass_gain;
	struct eq_biquad bass;

	struct eq_biquad kw_shelf, kw_highpass;
	struct eq_biquad loudness_sidechain[2];
	struct eq_biquad crossover_low[2], crossover_high[2];
	float loudness_mix;
	float dynamics_gain[2];
	struct eq_peak_window peak_window[3];
	float delay_bands[2][EQ_CHANNELS][EQ_LOOKAHEAD_CAPACITY];
	float delay_dry[EQ_CHANNELS][EQ_LOOKAHEAD_CAPACITY];
	float delay_mix[EQ_LOOKAHEAD_CAPACITY];
	float delay_output[EQ_CHANNELS][EQ_LOOKAHEAD_CAPACITY];
	uint32_t delay_frames, delay_position;
	uint64_t frame;
	float loudness_power;
	bool loudness_measured;
	bool agc_fast;
	uint32_t agc_fast_hold;
	float agc_gain_db;
	float agc_target_db;

	float limiter_gain;
};

void eq_params_init(struct eq_params *params);
void eq_dsp_init(struct eq_dsp *dsp, float rate);
void eq_dsp_process(struct eq_dsp *dsp, const struct eq_params *params,
		    const float *in[EQ_CHANNELS], float *out[EQ_CHANNELS],
		    uint32_t n_samples);
