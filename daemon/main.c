/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gnome-equalizer-daemon: a PipeWire smart filter that runs the equalizer on
 * whatever output device is the default, configured through GSettings.
 */
#include "config.h"

#include <errno.h>
#include <locale.h>
#include <signal.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib/gi18n.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>
#include <spa/utils/ringbuffer.h>

#include "dsp.h"

#define SCHEMA_ID "io.github.cloudie_code.equalizer"
#define LINK_GROUP "gnome-equalizer"

/* Frames kept between the two sides; a power of two for the index mask. */
#define RING_FRAMES 16384
#define MAX_BLOCK 8192

/* The playback side reads the ring one quantum behind at most; anything
 * beyond this many quanta is stale and gets skipped. */
#define MAX_QUEUED_QUANTA 3

struct app {
	GMainLoop *main_loop;
	GSettings *settings;
	int exit_code;

	struct pw_thread_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct spa_hook core_listener;

	struct pw_stream *capture;
	struct spa_hook capture_listener;
	struct pw_stream *playback;
	struct spa_hook playback_listener;

	struct eq_params params;
	_Atomic uint32_t rate;
	struct eq_dsp dsp;

	/* Processed audio, from the capture side to the playback side. */
	struct spa_ringbuffer ring;
	float ring_data[EQ_CHANNELS][RING_FRAMES];
	float scratch[EQ_CHANNELS][MAX_BLOCK];
};

static void capture_process(void *data)
{
	struct app *app = data;
	struct pw_buffer *b = pw_stream_dequeue_buffer(app->capture);

	if (b == NULL)
		return;

	if (b->buffer->n_datas >= EQ_CHANNELS) {
		const float *cin[EQ_CHANNELS];
		float *cout[EQ_CHANNELS];
		uint32_t size = MAX_BLOCK * sizeof(float);

		for (int i = 0; i < EQ_CHANNELS; i++) {
			struct spa_data *d = &b->buffer->datas[i];
			uint32_t offs = SPA_MIN(d->chunk->offset, d->maxsize);
			cin[i] = SPA_PTROFF(d->data, offs, const float);
			cout[i] = app->scratch[i];
			size = SPA_MIN(size, SPA_MIN(d->chunk->size, d->maxsize - offs));
		}
		uint32_t frames = size / sizeof(float);

		uint32_t rate = atomic_load_explicit(&app->rate, memory_order_relaxed);
		if (rate != 0 && (float)rate != app->dsp.rate)
			eq_dsp_init(&app->dsp, (float)rate);

		eq_dsp_process(&app->dsp, &app->params, cin, cout, frames);

		uint32_t index;
		int32_t filled = spa_ringbuffer_get_write_index(&app->ring, &index);
		if (filled >= 0 && (uint32_t)filled + frames <= RING_FRAMES) {
			for (int i = 0; i < EQ_CHANNELS; i++)
				spa_ringbuffer_write_data(&app->ring, app->ring_data[i],
							  RING_FRAMES * sizeof(float),
							  (index & (RING_FRAMES - 1)) * sizeof(float),
							  app->scratch[i], frames * sizeof(float));
			spa_ringbuffer_write_update(&app->ring, index + frames);
		}
	}

	pw_stream_queue_buffer(app->capture, b);
}

static void playback_process(void *data)
{
	struct app *app = data;
	struct pw_buffer *b = pw_stream_dequeue_buffer(app->playback);

	if (b == NULL)
		return;
	if (b->buffer->n_datas < EQ_CHANNELS) {
		pw_stream_queue_buffer(app->playback, b);
		return;
	}

	uint32_t frames = MAX_BLOCK;
	for (int i = 0; i < EQ_CHANNELS; i++)
		frames = SPA_MIN(frames, b->buffer->datas[i].maxsize / sizeof(float));
	if (b->requested)
		frames = SPA_MIN(frames, (uint32_t)b->requested);

	uint32_t index;
	int32_t avail = spa_ringbuffer_get_read_index(&app->ring, &index);

	if (avail > (int32_t)(frames * MAX_QUEUED_QUANTA)) {
		/* Fell behind (e.g. after a hiccup): drop the backlog, keep latency low. */
		index += avail - frames;
		avail = frames;
	}

	for (int i = 0; i < EQ_CHANNELS; i++) {
		struct spa_data *d = &b->buffer->datas[i];
		if (avail >= (int32_t)frames)
			spa_ringbuffer_read_data(&app->ring, app->ring_data[i],
						 RING_FRAMES * sizeof(float),
						 (index & (RING_FRAMES - 1)) * sizeof(float),
						 d->data, frames * sizeof(float));
		else
			/* Not enough yet (start-up): silence, and let the ring fill. */
			memset(d->data, 0, frames * sizeof(float));
		d->chunk->offset = 0;
		d->chunk->size = frames * sizeof(float);
		d->chunk->stride = sizeof(float);
	}
	if (avail >= (int32_t)frames)
		spa_ringbuffer_read_update(&app->ring, index + frames);

	pw_stream_queue_buffer(app->playback, b);
}

static void capture_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
	struct app *app = data;
	struct spa_audio_info_raw info = { 0 };

	if (id != SPA_PARAM_Format || param == NULL)
		return;
	if (spa_format_audio_raw_parse(param, &info) < 0 || info.rate == 0)
		return;

	atomic_store(&app->rate, info.rate);
	g_debug("capture format negotiated at %u Hz", info.rate);
}

static void stream_state_changed(void *data, enum pw_stream_state old,
				 enum pw_stream_state state, const char *error)
{
	struct app *app = data;

	if (state == PW_STREAM_STATE_ERROR) {
		g_warning("stream error: %s", error ? error : "unknown");
		app->exit_code = EXIT_FAILURE;
		g_main_loop_quit(app->main_loop);
	}
}

static const struct pw_stream_events capture_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = capture_process,
	.param_changed = capture_param_changed,
	.state_changed = stream_state_changed,
};

static const struct pw_stream_events playback_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = playback_process,
	.state_changed = stream_state_changed,
};

static void core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
	struct app *app = data;

	g_warning("PipeWire error id:%u seq:%d res:%d (%s): %s",
		  id, seq, res, spa_strerror(res), message);

	/* Losing the connection is fatal; systemd restarts us with PipeWire. */
	if (id == PW_ID_CORE && res == -EPIPE) {
		app->exit_code = EXIT_FAILURE;
		g_main_loop_quit(app->main_loop);
	}
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.error = core_error,
};

static int connect_stream(struct pw_stream *stream, enum spa_direction direction,
			  enum pw_stream_flags flags)
{
	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	struct spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(
		.format = SPA_AUDIO_FORMAT_F32P,
		.channels = EQ_CHANNELS,
		.position = { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR });
	const struct spa_pod *params[1];

	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

	return pw_stream_connect(stream, direction, PW_ID_ANY,
				 flags | PW_STREAM_FLAG_AUTOCONNECT |
				 PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
				 params, 1);
}

static bool create_streams(struct app *app)
{
	const char *description = _("Equalizer");
	int res;

	app->capture = pw_stream_new(app->core, "gnome-equalizer input", pw_properties_new(
		PW_KEY_NODE_NAME, "gnome_equalizer_sink",
		PW_KEY_NODE_DESCRIPTION, description,
		PW_KEY_MEDIA_NAME, description,
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CLASS, "Audio/Sink",
		PW_KEY_NODE_LINK_GROUP, LINK_GROUP,
		PW_KEY_NODE_VIRTUAL, "true",
		PW_KEY_DEVICE_ICON_NAME, "audio-speakers",
		SPA_KEY_AUDIO_POSITION, "[ FL FR ]",
		"filter.smart", "true",
		"filter.smart.name", LINK_GROUP,
		NULL));
	app->playback = pw_stream_new(app->core, "gnome-equalizer output", pw_properties_new(
		PW_KEY_NODE_NAME, "gnome_equalizer_output",
		PW_KEY_NODE_DESCRIPTION, description,
		PW_KEY_MEDIA_NAME, description,
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
		PW_KEY_NODE_LINK_GROUP, LINK_GROUP,
		PW_KEY_NODE_VIRTUAL, "true",
		PW_KEY_NODE_PASSIVE, "true",
		PW_KEY_STREAM_DONT_REMIX, "true",
		SPA_KEY_AUDIO_POSITION, "[ FL FR ]",
		NULL));
	if (app->capture == NULL || app->playback == NULL) {
		g_warning("cannot create streams: %s", g_strerror(errno));
		return false;
	}

	pw_stream_add_listener(app->capture, &app->capture_listener, &capture_events, app);
	pw_stream_add_listener(app->playback, &app->playback_listener, &playback_events, app);

	if ((res = connect_stream(app->capture, PW_DIRECTION_INPUT, 0)) < 0 ||
	    (res = connect_stream(app->playback, PW_DIRECTION_OUTPUT, 0)) < 0) {
		g_warning("cannot connect streams: %s", spa_strerror(res));
		return false;
	}
	return true;
}

static void load_settings(struct app *app)
{
	g_autoptr(GVariant) bands = g_settings_get_value(app->settings, "bands");
	gsize n = 0;
	const gdouble *gains = g_variant_get_fixed_array(bands, &n, sizeof(gdouble));

	for (gsize i = 0; i < EQ_BANDS; i++)
		atomic_store(&app->params.gains[i], i < n ? (float)gains[i] : 0.0f);

	atomic_store(&app->params.enabled, g_settings_get_boolean(app->settings, "enabled"));
	atomic_store(&app->params.loudness,
		     g_settings_get_boolean(app->settings, "loudness-equalization"));
	atomic_store(&app->params.bass_boost, g_settings_get_boolean(app->settings, "bass-boost"));
	atomic_store(&app->params.bass_boost_level,
		     (float)g_settings_get_double(app->settings, "bass-boost-level"));
}

static void settings_changed(GSettings *settings, const char *key, struct app *app)
{
	load_settings(app);
}

static gboolean on_quit_signal(gpointer data)
{
	struct app *app = data;

	g_main_loop_quit(app->main_loop);
	return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
	static struct app app;

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	pw_init(&argc, &argv);

	app.main_loop = g_main_loop_new(NULL, FALSE);
	app.settings = g_settings_new(SCHEMA_ID);
	app.exit_code = EXIT_SUCCESS;
	eq_params_init(&app.params);
	eq_dsp_init(&app.dsp, 48000.0f);
	spa_ringbuffer_init(&app.ring);
	load_settings(&app);
	g_signal_connect(app.settings, "changed", G_CALLBACK(settings_changed), &app);

	g_unix_signal_add(SIGINT, on_quit_signal, &app);
	g_unix_signal_add(SIGTERM, on_quit_signal, &app);

	app.loop = pw_thread_loop_new("gnome-equalizer", NULL);
	app.context = pw_context_new(pw_thread_loop_get_loop(app.loop), NULL, 0);
	if (app.context == NULL) {
		g_warning("cannot create PipeWire context");
		return EXIT_FAILURE;
	}

	pw_thread_loop_lock(app.loop);
	pw_thread_loop_start(app.loop);

	app.core = pw_context_connect(app.context, pw_properties_new(
		PW_KEY_APP_NAME, "GNOME Equalizer",
		PW_KEY_APP_ID, "io.github.cloudie_code.equalizer",
		NULL), 0);
	if (app.core == NULL) {
		g_warning("cannot connect to PipeWire: %s", g_strerror(errno));
		pw_thread_loop_unlock(app.loop);
		return EXIT_FAILURE;
	}
	pw_core_add_listener(app.core, &app.core_listener, &core_events, &app);

	bool ok = create_streams(&app);
	pw_thread_loop_unlock(app.loop);

	if (ok)
		g_main_loop_run(app.main_loop);
	else
		app.exit_code = EXIT_FAILURE;

	pw_thread_loop_lock(app.loop);
	if (app.capture)
		pw_stream_destroy(app.capture);
	if (app.playback)
		pw_stream_destroy(app.playback);
	pw_core_disconnect(app.core);
	pw_thread_loop_unlock(app.loop);
	pw_thread_loop_stop(app.loop);
	pw_context_destroy(app.context);
	pw_thread_loop_destroy(app.loop);

	g_object_unref(app.settings);
	g_main_loop_unref(app.main_loop);
	pw_deinit();

	return app.exit_code;
}
