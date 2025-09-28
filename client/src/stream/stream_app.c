// Copyright 2020-2023, Collabora, Ltd.
// Copyright 2022-2023, PlutoVR
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Pipeline module ElectricMaple XR streaming solution
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @ingroup em_client
 */

#include "stream_app.h"

#include <gst/app/gstappsink.h>
#include <gst/gl/gl.h>
#include <gst/gl/gstglsyncmeta.h>
#include <gst/gst.h>
#include <gst/gstbus.h>
#include <gst/gstelement.h>
#include <gst/gstinfo.h>
#include <gst/gstmessage.h>
#include <gst/gstsample.h>
#include <gst/gstutils.h>
#include <gst/video/video-frame.h>
#include <gst/webrtc/webrtc.h>

#include "gst_common.h"
#include "connection.h"

// clang-format off
#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
// clang-format on

#include <linux/time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct MySampleImpl {
    struct MySample base;
    GstSample *sample;
};

/*!
 * All in one helper that handles locking, waiting for change and starting a
 * thread.
 */
struct os_thread_helper {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool initialized;
    bool running;
};

/*!
 * Run function.
 *
 * @public @memberof os_thread
 */
typedef void *(*os_run_func_t)(void *);

/*!
 * Start the internal thread.
 *
 * @public @memberof os_thread_helper
 */
static inline int
os_thread_helper_start(struct os_thread_helper *oth, os_run_func_t func, void *ptr) {
    pthread_mutex_lock(&oth->mutex);

    g_assert(oth->initialized);
    if (oth->running) {
        pthread_mutex_unlock(&oth->mutex);
        return -1;
    }

    int ret = pthread_create(&oth->thread, NULL, func, ptr);
    if (ret != 0) {
        pthread_mutex_unlock(&oth->mutex);
        return ret;
    }

    oth->running = true;

    pthread_mutex_unlock(&oth->mutex);

    return 0;
}

/*!
 * Zeroes the correct amount of memory based on the type pointed-to by the
 * argument.
 *
 * Use instead of memset(..., 0, ...) on a structure or pointer to structure.
 *
 * @ingroup aux_util
 */
#define U_ZERO(PTR) memset((PTR), 0, sizeof(*(PTR)))

/*!
 * Initialize the thread helper.
 *
 * @public @memberof os_thread_helper
 */
static inline int os_thread_helper_init(struct os_thread_helper *oth) {
    U_ZERO(oth);

    int ret = pthread_mutex_init(&oth->mutex, NULL);
    if (ret != 0) {
        return ret;
    }

    ret = pthread_cond_init(&oth->cond, NULL);
    if (ret) {
        pthread_mutex_destroy(&oth->mutex);
        return ret;
    }
    oth->initialized = true;

    return 0;
}

struct _StreamApp {
    GMainLoop *loop;
    MyConnection *connection;
    GstElement *pipeline;

    GstGLDisplay *gst_gl_display;
    GstGLContext *gst_gl_context;
    GstGLContext *gst_gl_other_context;

    GstGLDisplay *display;

    /// Wrapped version of the android_main/render context
    GstGLContext *android_main_context;

    /// GStreamer-created EGL context for its own use
    GstGLContext *context;

    GstElement *appsink;

    GLenum frame_texture_target;

    int width;
    int height;

    struct {
        EGLDisplay display;
        EGLContext android_main_context;
        // 16x16 pbuffer surface
        EGLSurface surface;
    } egl;

    struct os_thread_helper play_thread;

    bool received_first_frame;

    GMutex sample_mutex;
    GstSample *sample;
    struct timespec sample_decode_end_ts;

    guint timeout_src_id_dot_data;
    guint timeout_src_id_print_stats;
};

// clang-format off
#define VIDEO_SINK_CAPS \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "              \
    "format = (string) RGBA, "                                          \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE ", "                             \
    "texture-target = (string) { 2D, external-oes } "
// clang-format on

/*
 * Callbacks
 */

static void *stream_app_thread_func(void *ptr);

static void my_stream_client_set_connection(StreamApp *app, MyConnection *connection);

/* GObject method implementations */

static void stream_app_init(StreamApp *app) {
    ALOGI("%s: creating stuff", __FUNCTION__);

    memset(app, 0, sizeof(StreamApp));
    app->loop = g_main_loop_new(NULL, FALSE);
    g_assert(os_thread_helper_init(&app->play_thread) >= 0);
    g_mutex_init(&app->sample_mutex);
    ALOGI("%s: done creating stuff", __FUNCTION__);
}

void stream_app_set_egl_context(StreamApp *app, EGLContext context, EGLDisplay display,
                                EGLSurface surface) {
    ALOGI("Wrapping egl context");

    app->egl.display = display;
    app->egl.android_main_context = context;
    app->egl.surface = surface;

    const GstGLPlatform egl_platform = GST_GL_PLATFORM_EGL;
    guintptr android_main_egl_context_handle = gst_gl_context_get_current_gl_context(egl_platform);
    GstGLAPI gl_api = gst_gl_context_get_current_gl_api(egl_platform, NULL, NULL);
    app->gst_gl_display = g_object_ref_sink(gst_gl_display_new());
    app->android_main_context = g_object_ref_sink(
            gst_gl_context_new_wrapped(app->gst_gl_display, android_main_egl_context_handle,
                                       egl_platform, gl_api));
}

static void stream_app_dispose(StreamApp *self) {
    // May be called multiple times during destruction.
    // Stop things and clear ref counted things here.
    // StreamApp *self = stream_app(object);
    stream_app_stop(self);
    g_clear_object(&self->loop);
    gst_clear_object(&self->sample);
    gst_clear_object(&self->pipeline);
    gst_clear_object(&self->gst_gl_display);
    gst_clear_object(&self->gst_gl_context);
    gst_clear_object(&self->gst_gl_other_context);
    gst_clear_object(&self->display);
    gst_clear_object(&self->context);
    gst_clear_object(&self->appsink);
}

static void stream_app_finalize(StreamApp *self) {
    // Only called once, after dispose
}

/*
 * Callbacks
 */

static GstBusSyncReply bus_sync_handler_cb(GstBus *bus, GstMessage *msg, StreamApp *app) {
    // LOG_MSG(msg);

    /* Do not let GstGL retrieve the display handle on its own
     * because then it believes it owns it and calls eglTerminate()
     * when disposed */
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_NEED_CONTEXT) {
        const gchar *type;
        gst_message_parse_context_type(msg, &type);
        if (g_str_equal(type, GST_GL_DISPLAY_CONTEXT_TYPE)) {
            ALOGI("Got message: Need display context");
            g_autoptr(GstContext) context = gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
            gst_context_set_gl_display(context, app->display);
            gst_element_set_context(GST_ELEMENT(msg->src), context);
        } else if (g_str_equal(type, "gst.gl.app_context")) {
            ALOGI("Got message: Need app context");
            g_autoptr(GstContext) app_context = gst_context_new("gst.gl.app_context", TRUE);
            GstStructure *s = gst_context_writable_structure(app_context);
            gst_structure_set(s, "context", GST_TYPE_GL_CONTEXT, app->android_main_context, NULL);
            gst_element_set_context(GST_ELEMENT(msg->src), app_context);
        }
    }

    return GST_BUS_PASS;
}

static gboolean gst_bus_cb(GstBus *bus, GstMessage *message, gpointer user_data) {
    // LOG_MSG(message);

    GstBin *pipeline = GST_BIN(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gerr = NULL;
            gchar *debug_msg = NULL;
            gst_message_parse_error(message, &gerr, &debug_msg);

            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-error");

            gchar *dot_data = gst_debug_bin_to_dot_data(pipeline, GST_DEBUG_GRAPH_SHOW_ALL);
            ALOGE("gst_bus_cb: DOT data: %s", dot_data);
            g_free(dot_data);

            ALOGE("gst_bus_cb: Error: %s (%s)", gerr->message, debug_msg);
            g_error("gst_bus_cb: Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        }
            break;
        case GST_MESSAGE_WARNING: {
            GError *gerr = NULL;
            gchar *debug_msg = NULL;
            gst_message_parse_warning(message, &gerr, &debug_msg);
            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-warning");
            ALOGW("gst_bus_cb: Warning: %s (%s)", gerr->message, debug_msg);
            g_warning("gst_bus_cb: Warning: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        }
            break;
        case GST_MESSAGE_EOS: {
            g_error("gst_bus_cb: Got EOS!");
        }
            break;
        default:
            break;
    }
    return TRUE;
}

static GstFlowReturn on_new_sample_cb(GstAppSink *appsink, gpointer user_data) {
    StreamApp *app = (StreamApp *) user_data;

    // TODO record the frame ID, get frame pose
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret != 0) {
        ALOGE("%s: clock_gettime failed, which is very bizarre.", __FUNCTION__);
        return GST_FLOW_ERROR;
    }

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    g_assert_nonnull(sample);

    GstSample *prevSample = NULL;

    // Update client sample
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&app->sample_mutex);
        prevSample = app->sample;
        app->sample = sample;
        app->sample_decode_end_ts = ts;
        app->received_first_frame = true;
    }

    // Previous client sample is not used.
    if (prevSample) {
        ALOGI("Discarding unused, replaced sample");
        gst_sample_unref(prevSample);
    }

    return GST_FLOW_OK;
}

static gboolean print_stats(StreamApp *app) {
    if (!app) {
        return G_SOURCE_CONTINUE;
    }

    GstElement *rtpulpfecdec = gst_bin_get_by_name(GST_BIN(app->pipeline), "ulpfec");

    if (rtpulpfecdec) {
        GValue pt = G_VALUE_INIT;
        GValue recovered = G_VALUE_INIT;
        GValue unrecovered = G_VALUE_INIT;

        g_object_get_property(G_OBJECT(rtpulpfecdec), "pt", &pt);
        g_object_get_property(G_OBJECT(rtpulpfecdec), "recovered", &recovered);
        g_object_get_property(G_OBJECT(rtpulpfecdec), "unrecovered", &unrecovered);

        g_object_set(G_OBJECT(rtpulpfecdec), "passthrough", FALSE, NULL);

        g_print("FEC stats: pt %u, recovered %u, unrecovered %u\n",
                g_value_get_uint(&pt),
                g_value_get_uint(&recovered),
                g_value_get_uint(&unrecovered));

        g_value_unset(&pt);
        g_value_unset(&recovered);
        g_value_unset(&unrecovered);
    }

    return G_SOURCE_CONTINUE;
}

static gboolean check_pipeline_dot_data(StreamApp *app) {
    if (!app || !app->pipeline) {
        return G_SOURCE_CONTINUE;
    }

    gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(app->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    g_free(dot_data);

    return G_SOURCE_CONTINUE;
}

static void create_pipeline_rtp(StreamApp *app) {
    GError *error = NULL;

    gchar *pipeline_string = g_strdup_printf(
            "udpsrc port=5600 buffer-size=10000000 "
            "caps=\"application/x-rtp,media=video,clock-rate=90000,encoding-name=H264\" ! "
            "rtpjitterbuffer do-lost=1 latency=5 ! "
            "decodebin3 ! "
            "glsinkbin name=glsink");

    app->pipeline = gst_object_ref_sink(gst_parse_launch(pipeline_string, &error));
    if (app->pipeline == NULL) {
        ALOGE("Failed creating pipeline : Bad source: %s", error->message);
        abort();
    }
    if (error) {
        ALOGE("Error creating a pipeline from string: %s", error ? error->message : "Unknown");
        abort();
    }
}

static void create_pipeline(StreamApp *app) {
    g_assert_nonnull(app);

    GError *error = NULL;

    // We'll need an active egl context below before setting up gstgl (as explained previously)

    create_pipeline_rtp(app);

    GstElement *glsinkbin = gst_bin_get_by_name(GST_BIN(app->pipeline), "glsink");

    // Set a custom appsink for glsinkbin
    {
        // We convert the string SINK_CAPS above into a GstCaps that elements below can understand.
        // the "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY ")," part of the caps is read :
        // video/x-raw(memory:GLMemory) and is really important for getting zero-copy gl textures.
        // It tells the pipeline (especially the decoder) that an internal android:Surface should
        // get created internally (using the provided gstgl contexts above) so that the appsink
        // can basically pull the samples out using an GLConsumer (this is just for context, as
        // all of those constructs will be hidden from you, but are turned on by that CAPS).
        g_autoptr(GstCaps) caps = gst_caps_from_string(VIDEO_SINK_CAPS);

        // FRED: We create the appsink 'manually' here because glsink's ALREADY a sink and so if we stick
        //       glsinkbin ! appsink in our pipeline_string for automatic linking, gst_parse will NOT like this,
        //       as glsinkbin (a sink) cannot link to anything upstream (appsink being 'another' sink). So we
        //       manually link them below using glsinkbin's 'sink' pad -> appsink.
        app->appsink = gst_element_factory_make("appsink", NULL);
        g_object_set(app->appsink,
                // Set caps
                     "caps",
                     caps,
                // Fixed size buffer
                     "max-buffers",
                     1,
                // Drop old buffers when queue is filled
                     "drop",
                     true,
                // Terminator
                     NULL);

        // Lower overhead than new-sample signal.
        GstAppSinkCallbacks callbacks = {};
        callbacks.new_sample = on_new_sample_cb;
        gst_app_sink_set_callbacks(GST_APP_SINK(app->appsink), &callbacks, app, NULL);
        app->received_first_frame = false;

        g_object_set(glsinkbin, "sink", app->appsink, NULL);
    }

    g_autoptr(GstBus) bus = gst_element_get_bus(app->pipeline);

    // We set this up to inject the EGL context
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler) bus_sync_handler_cb, app, NULL);

    // This just watches for errors and such
    gst_bus_add_watch(bus, gst_bus_cb, app->pipeline);
    g_object_unref(bus);

    app->timeout_src_id_dot_data = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data),
                                                         app);
    app->timeout_src_id_print_stats = g_timeout_add_seconds(3, G_SOURCE_FUNC(print_stats), app);
}

static void drop_pipeline(StreamApp *app) {
    if (app->pipeline) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
    }
    gst_clear_object(&app->pipeline);
    gst_clear_object(&app->appsink);
}

static void *stream_app_thread_func(void *ptr) {
    StreamApp *app = (StreamApp *) ptr;

    create_pipeline(app);
    g_assert(gst_element_set_state(app->pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    ALOGI("%s: running GMainLoop", __FUNCTION__);
    g_main_loop_run(app->loop);
    ALOGI("%s: g_main_loop_run returned", __FUNCTION__);

    return NULL;
}

/*
 * Public functions
 */

StreamApp *stream_app_new() {
    StreamApp *self = calloc(1, sizeof(StreamApp));
    stream_app_init(self);
    return self;
}

void stream_app_destroy(StreamApp **ptr_app) {
    if (ptr_app == NULL) {
        return;
    }
    StreamApp *app = *ptr_app;
    if (app == NULL) {
        return;
    }
    stream_app_dispose(app);
    stream_app_finalize(app);
    free(app);
    *ptr_app = NULL;
}

void stream_app_spawn_thread(StreamApp *app, MyConnection *connection) {
    ALOGI("%s: Starting stream client mainloop thread", __FUNCTION__);
    my_stream_client_set_connection(app, connection);
    int ret = os_thread_helper_start(&app->play_thread, &stream_app_thread_func, app);
    (void) ret;
    g_assert(ret == 0);
}

void stream_app_stop(StreamApp *app) {
    ALOGI("%s: Stopping pipeline and ending thread", __FUNCTION__);

    if (app->pipeline != NULL) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
    }

    gst_clear_object(&app->pipeline);
    gst_clear_object(&app->appsink);
    gst_clear_object(&app->context);
}

struct MySample *stream_app_try_pull_sample(StreamApp *app, struct timespec *out_decode_end) {
    if (!app->appsink) {
        // Not setup yet.
        return NULL;
    }

    // We actually pull the sample in the new-sample signal handler,
    // so here we're just receiving the sample already pulled.
    GstSample *sample = NULL;
    struct timespec decode_end;
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&app->sample_mutex);
        sample = app->sample;
        app->sample = NULL;
        decode_end = app->sample_decode_end_ts;
    }

    if (sample == NULL) {
        if (gst_app_sink_is_eos(GST_APP_SINK(app->appsink))) {
            //            ALOGW("%s: EOS", __FUNCTION__);
            // TODO trigger teardown?
        }
        return NULL;
    }
    *out_decode_end = decode_end;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);

    GstVideoInfo info;
    gst_video_info_from_caps(&info, caps);
    gint width = GST_VIDEO_INFO_WIDTH(&info);
    gint height = GST_VIDEO_INFO_HEIGHT(&info);
    //    ALOGI("%s: frame %d (w) x %d (h)", __FUNCTION__, width, height);

    // TODO: Handle resize?
#if 0
    if (width != app->width || height != app->height) {
        app->width = width;
        app->height = height;
    }
#endif

    struct MySampleImpl *ret = calloc(1, sizeof(struct MySampleImpl));

    GstVideoFrame frame;
    GstMapFlags flags = (GstMapFlags) (GST_MAP_READ | GST_MAP_GL);
    gst_video_frame_map(&frame, &info, buffer, flags);
    ret->base.frame_texture_id = *(GLuint *) frame.data[0];

    if (app->context == NULL) {
        ALOGI("%s: Retrieving the GStreamer EGL context", __FUNCTION__);
        /* Get GStreamer's gl context. */
        gst_gl_query_local_gl_context(app->appsink, GST_PAD_SINK, &app->context);

        /* Check if we have 2D or OES textures */
        GstStructure *s = gst_caps_get_structure(caps, 0);
        const gchar *texture_target_str = gst_structure_get_string(s, "texture-target");
        if (g_str_equal(texture_target_str, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR)) {
            app->frame_texture_target = GL_TEXTURE_EXTERNAL_OES;
        } else if (g_str_equal(texture_target_str, GST_GL_TEXTURE_TARGET_2D_STR)) {
            app->frame_texture_target = GL_TEXTURE_2D;
            ALOGE("Got GL_TEXTURE_2D instead of expected GL_TEXTURE_EXTERNAL_OES");
        } else {
            g_assert_not_reached();
        }
    }
    ret->base.frame_texture_target = app->frame_texture_target;

    GstGLSyncMeta *sync_meta = gst_buffer_get_gl_sync_meta(buffer);
    if (sync_meta) {
        /* MOSHI: the set_sync() seems to be needed for resizing */
        gst_gl_sync_meta_set_sync_point(sync_meta, app->context);
        gst_gl_sync_meta_wait(sync_meta, app->context);
    }

    gst_video_frame_unmap(&frame);
    // Move sample ownership into the return value
    ret->sample = sample;

    return (struct MySample *) ret;
}

void stream_app_release_sample(StreamApp *app, struct MySample *sample) {
    struct MySampleImpl *impl = (struct MySampleImpl *) sample;
    //    ALOGI("Releasing sample with texture ID %d", impl->base.frame_texture_id);
    gst_sample_unref(impl->sample);
    free(impl);
}

static void on_need_pipeline_cb(MyConnection *my_conn, StreamApp *app) {
    g_info("%s", __FUNCTION__);
    g_assert_nonnull(app);
    g_assert_nonnull(my_conn);

    //    GList *decoders = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODABLE,
    //                                                            GST_RANK_MARGINAL);
    //
    //    // Iterate through the list
    //    for (GList *iter = decoders; iter != NULL; iter = iter->next) {
    //        GstElementFactory *factory = (GstElementFactory *) iter->data;
    //
    //        // Get the factory name suitable for use in a string pipeline
    //        const gchar *name = gst_element_get_name(factory);
    //
    //        // Print the factory name
    //        g_print("Decoder: %s\n", name);
    //    }

    // We'll need an active egl context below before setting up gstgl (as explained previously)

    //    // clang-format off
    //    gchar *pipeline_string = g_strdup_printf(
    //        "webrtcbin name=webrtc bundle-policy=max-bundle latency=0 ! "
    //        "decodebin3 ! "
    ////        "amcviddec-c2qtiavcdecoder ! "        // Hardware
    ////        "amcviddec-omxqcomvideodecoderavc ! " // Hardware
    ////        "amcviddec-c2androidavcdecoder ! "    // Software
    ////        "amcviddec-omxgoogleh264decoder ! "   // Software
    ////
    ///"video/x-raw(memory:GLMemory),format=(string)RGBA,width=(int)1280,height=(int)720,texture-target=(string)external-oes
    ///! "
    //        "glsinkbin name=glsink");
    //    // clang-format on
    //
    //    sc->pipeline = gst_object_ref_sink(gst_parse_launch(pipeline_string, &error));
    //    if (sc->pipeline == NULL) {
    //        ALOGE("Failed creating pipeline : Bad source: %s", error->message);
    //        abort();
    //    }
    //    if (error) {
    //        ALOGE("Error creating a pipeline from string: %s", error ? error->message : "Unknown");
    //        abort();
    //    }

    app->pipeline = gst_pipeline_new("webrtc-recv-pipeline");

    GstElement *webrtcbin = gst_element_factory_make("webrtcbin", "webrtc");
    // Matching this to the offerer's bundle policy is necessary for negotiation
    g_object_set(webrtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);
    g_object_set(webrtcbin, "latency", 50, NULL);


    gst_bin_add_many(GST_BIN(app->pipeline), webrtcbin, NULL);

    {
        GstBus *bus = gst_element_get_bus(app->pipeline);

#ifdef ANDROID
        // We set this up to inject the EGL context
        gst_bus_set_sync_handler(bus, (GstBusSyncHandler)bus_sync_handler_cb, app, NULL);
#endif

        // This just watches for errors and such
        gst_bus_add_watch(bus, gst_bus_cb, app->pipeline);

        g_object_unref(bus);
    }

    // This actually hands over the pipeline. Once our own handler returns,
    // the pipeline will be started by the connection.
    g_signal_emit_by_name(my_conn, "set-pipeline", GST_PIPELINE(app->pipeline), NULL);

    app->timeout_src_id_dot_data = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data), app->pipeline);
}

static void on_drop_pipeline_cb(MyConnection *my_conn, StreamApp *app) {
    if (app->pipeline) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
    }
    gst_clear_object(&app->pipeline);
//    gst_clear_object(&app->app_sink);
}

/*
 * Helper functions
 */

static void my_stream_client_set_connection(StreamApp *app, MyConnection *connection) {
    g_clear_object(&app->connection);
    if (connection != NULL) {
        app->connection = g_object_ref(connection);
        g_signal_connect(app->connection, "on-need-pipeline", G_CALLBACK(on_need_pipeline_cb), app);
        g_signal_connect(app->connection, "on-drop-pipeline", G_CALLBACK(on_drop_pipeline_cb), app);
        ALOGI("%s: a connection assigned to the stream client", __FUNCTION__);
    }
}
