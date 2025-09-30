// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief Main file for WebRTC client.
 * @author Moshi Turner <moses@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <assert.h>
#include <errno.h>
#include <gst/gst.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <thread>

#include "EglData.hpp"
#include "stream/gst_common.h"
#include "stream/render/render.hpp"
#include "stream/render/render_api.h"
#include "stream/stream_app.h"
#include "stream/connection.h"

namespace {

    struct MyState {
        bool connected;

        // Window size, not video size
        int32_t window_width;
        int32_t window_height;
        int32_t render_width;
        int32_t render_height;
        int32_t h_margin;
        int32_t v_margin;

        bool pressed;
        float press_pos_x;
        float press_pos_y;
        float prev_pos_x;
        float prev_pos_y;

        MyConnection *connection;
        StreamApp *stream_app;

        std::unique_ptr<Renderer> renderer;

        std::unique_ptr<EglData> initialEglData;
    };

    MyState state_ = {};

    void gstAndroidLog(GstDebugCategory *category,
                       GstDebugLevel level,
                       const gchar *file,
                       const gchar *function,
                       gint line,
                       GObject *object,
                       GstDebugMessage *message,
                       gpointer data) {
        if (level <= gst_debug_category_get_threshold(category)) {
            if (level == GST_LEVEL_ERROR) {
                ALOGE("%s, %s: %s", file, function, gst_debug_message_get(message));
            } else {
                ALOGD("%s, %s: %s", file, function, gst_debug_message_get(message));
            }
        }
    }

    // Example input handler function
    int32_t handle_input(struct android_app *app, AInputEvent *event) {
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
            int32_t action = AMotionEvent_getAction(event);

            float x = AMotionEvent_getX(event, 0);
            float y = AMotionEvent_getY(event, 0);

            if (x < state_.h_margin) {
                x = state_.h_margin;
            }
            if (x > state_.window_width - state_.h_margin) {
                x = state_.window_width;
            }
            if (y < state_.v_margin) {
                y = state_.v_margin;
            }
            if (y > state_.window_height - state_.v_margin) {
                y = state_.window_height;
            }
            x -= state_.h_margin;
            y -= state_.v_margin;

            float x_ratio = x / (float) state_.render_width;
            float y_ratio = y / (float) state_.render_height;

            float client_x = x_ratio * stream_app_get_video_width(state_.stream_app);
            float client_y = y_ratio * stream_app_get_video_height(state_.stream_app);

            //AMotionEvent_getDownTime

            switch (action & AMOTION_EVENT_ACTION_MASK) {
                case AMOTION_EVENT_ACTION_DOWN:
                    ALOGI("INPUT: DOWN (%.1f, %.1f)", client_x, client_y);
                    state_.pressed = true;
                    state_.press_pos_x = client_x;
                    state_.press_pos_y = client_y;
                    state_.prev_pos_x = client_x;
                    state_.prev_pos_y = client_y;
                    my_connection_send_input_event(state_.connection, 0, client_x, client_y);
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    if (AMotionEvent_getPointerCount(event) > 1) {
                        float dx = client_x - state_.press_pos_x;
                        float dy = client_y - state_.press_pos_y;
                        ALOGI("INPUT: SCROLL (%.1f, %.1f)", dx, dy);
                        my_connection_send_input_event(state_.connection, 3,
                                                       dx,
                                                       dy);
                    } else {
                        ALOGI("INPUT: MOVE (%.1f, %.1f)", client_x, client_y);
                        my_connection_send_input_event(state_.connection, 2, client_x, client_y);
                    }

                    state_.prev_pos_x = client_x;
                    state_.prev_pos_y = client_y;
                    break;
                case AMOTION_EVENT_ACTION_UP:
                    ALOGI("INPUT: UP (%.1f, %.1f)", client_x, client_y);
                    state_.pressed = false;
                    my_connection_send_input_event(state_.connection, 1, client_x, client_y);

                    if (std::abs(state_.press_pos_x - client_x) < 10 &&
                        std::abs(state_.press_pos_y - client_y) < 10) {
                        ALOGI("INPUT: CLICK (%.1f, %.1f)", client_x, client_y);
                        my_connection_send_input_event(state_.connection, 2, client_x, client_y);
                    }

//                    if (AMotionEvent_getDownTime(event) > 2) {
//                        my_connection_send_input_event(state_.connection, 4, client_x, client_y);
//                    }
                    break;
            }
            return 1; // Event handled
        }
        return 0; // Event not handled
    }

    void onAppCmd(struct android_app *app, int32_t cmd) {
        switch (cmd) {
            case APP_CMD_START:
                ALOGI("APP_CMD_START");
                break;
            case APP_CMD_RESUME:
                ALOGI("APP_CMD_RESUME");
                break;
            case APP_CMD_PAUSE:
                ALOGI("APP_CMD_PAUSE");
                break;
            case APP_CMD_STOP:
                ALOGI("APP_CMD_STOP");
                break;
            case APP_CMD_DESTROY:
                ALOGI("APP_CMD_DESTROY");
                break;
            case APP_CMD_INIT_WINDOW: {
                ALOGI("APP_CMD_INIT_WINDOW");

                state_.initialEglData = std::make_unique<EglData>(app->window);
                state_.initialEglData->makeCurrent();

                eglQuerySurface(state_.initialEglData->display, state_.initialEglData->surface,
                                EGL_WIDTH,
                                &state_.window_width);
                eglQuerySurface(state_.initialEglData->display, state_.initialEglData->surface,
                                EGL_HEIGHT,
                                &state_.window_height);

                // Set up gstreamer
                gst_init(NULL, NULL);

#ifdef __ANDROID__
                gst_debug_add_log_function(&gstAndroidLog, NULL, NULL);
#endif

                // Set up gst logger
                //            gst_debug_set_default_threshold(GST_LEVEL_WARNING);
                //		gst_debug_set_threshold_for_name("webrtcbin", GST_LEVEL_MEMDUMP);
                //      gst_debug_set_threshold_for_name("webrtcbindatachannel", GST_LEVEL_TRACE);

                state_.stream_app = stream_app_new();
                stream_app_set_egl_context(state_.stream_app,
                                           state_.initialEglData->context,
                                           state_.initialEglData->display,
                                           state_.initialEglData->surface);

                state_.connection = g_object_ref_sink(my_connection_new_localhost());

                my_connection_connect(state_.connection);

                ALOGI("%s: starting stream client mainloop thread", __FUNCTION__);
                stream_app_spawn_thread(state_.stream_app, state_.connection);

                try {
                    ALOGI("%s: Setup renderer...", __FUNCTION__);
                    state_.renderer = std::make_unique<Renderer>();
                    state_.renderer->setupRender();
                } catch (std::exception const &e) {
                    ALOGE("%s: Caught exception setting up renderer: %s", __FUNCTION__, e.what());
                    state_.renderer->reset();
                    abort();
                }
            }
                break;
            case APP_CMD_TERM_WINDOW:
                ALOGI("APP_CMD_TERM_WINDOW");
                break;
            default:
                break;
        }
    }

/**
 * Poll for Android events, and handle them
 *
 * @param state app state
 *
 * @return true if we should go to the render code
 */
    bool poll_events(struct android_app *app) {
        // Poll Android events
        for (;;) {
            int events;
            struct android_poll_source *source;
            bool wait = !app->window || app->activityState != APP_CMD_RESUME;
            int timeout = wait ? -1 : 0;
            if (ALooper_pollAll(timeout, NULL, &events, (void **) &source) >= 0) {
                if (source) {
                    source->process(app, source);
                }

                if (timeout == 0 && (!app->window || app->activityState != APP_CMD_RESUME)) {
                    break;
                }
            } else {
                break;
            }
        }

        return true;
    }

} // namespace

struct MySample *prev_sample;

void android_main(struct android_app *app) {
    JNIEnv *env = nullptr;
    (*app->activity->vm).AttachCurrentThread(&env, NULL);
    app->onAppCmd = onAppCmd;

    app->onInputEvent = handle_input;

    // Main rendering loop.
    ALOGI("DEBUG: Starting main loop");
    while (!app->destroyRequested) {
        if (!poll_events(app)) {
            continue;
        }

        if (!state_.initialEglData || !state_.renderer || !state_.stream_app) {
            continue;
        }

        state_.initialEglData->makeCurrent();

        struct timespec decodeEndTime;
        struct MySample *sample = stream_app_try_pull_sample(state_.stream_app, &decodeEndTime);

        uint32_t video_width = stream_app_get_video_width(state_.stream_app);
        uint32_t video_height = stream_app_get_video_height(state_.stream_app);

        if (sample == nullptr || video_width * video_height == 0) {
            if (prev_sample) {
                // EM_POLL_RENDER_RESULT_REUSED_SAMPLE;
                //                sample = prev_sample;
            }
            continue;
        }

        float video_aspect = (float) video_width / (float) video_height;
        float window_aspect = (float) state_.window_width / (float) state_.window_height;

        // Align height
        if (window_aspect > video_aspect) {
            state_.render_height = state_.window_height;
            state_.render_width = state_.render_height * video_aspect;
        }
            // Align width
        else {
            state_.render_width = state_.window_width;
            state_.render_height = (float) state_.render_width / video_aspect;
        }

        state_.h_margin = (state_.window_width - state_.render_width) / 2;
        state_.v_margin = (state_.window_height - state_.render_height) / 2;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(state_.h_margin, state_.v_margin, state_.render_width, state_.render_height);

        state_.renderer->draw(sample->frame_texture_id, sample->frame_texture_target);

        eglSwapBuffers(state_.initialEglData->display, state_.initialEglData->surface);

        // Release the previous sample
        if (prev_sample != NULL) {
            stream_app_release_sample(state_.stream_app, prev_sample);
        }
        prev_sample = sample;

        state_.initialEglData->makeNotCurrent();
    }

    ALOGI("DEBUG: Exited main loop, cleaning up");

    //
    // Clean up
    //

    stream_app_destroy(&state_.stream_app);

    state_.initialEglData = nullptr;

    (*app->activity->vm).DetachCurrentThread();
}
