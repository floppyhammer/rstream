#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <gst/gst.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <optional>
#include <thread>

#include "egl_data.hpp"
#include "input.h"
#include "stream/connection.h"
#include "stream/gst_common.h"
#include "stream/render/render.hpp"
#include "stream/render/render_api.h"
#include "stream/stream_app.h"

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

    std::optional<int64_t> press_time;

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

int32_t handle_gamepad_key_event(const AInputEvent *event) {
    // 1. Check if the event came from a gamepad or D-pad
    int32_t source = AInputEvent_getSource(event);
    if (!((source & AINPUT_SOURCE_GAMEPAD) || (source & AINPUT_SOURCE_DPAD))) {
        return 0; // Not a gamepad or D-pad event
    }

    // 2. Get the action and key code
    int32_t action = AKeyEvent_getAction(event);
    int32_t key_code = AKeyEvent_getKeyCode(event);

    // 3. Process the action (DOWN, UP)
    if (action == AKEY_EVENT_ACTION_DOWN) {
        // Button was pressed
        switch (key_code) {
            case AKEYCODE_BUTTON_A:
                ALOGI("Gamepad button A pressed");
                break;
            case AKEYCODE_BUTTON_B:
                ALOGI("Gamepad button B pressed");
                break;
            case AKEYCODE_BUTTON_X: {
                my_connection_send_input_event(state_.connection, static_cast<int>(InputType::GamepadButtonX), 0, 0);

                ALOGI("Gamepad button X button pressed");
            } break;
            case AKEYCODE_BUTTON_Y:
                ALOGI("Gamepad button Y button pressed");
                break;
            case AKEYCODE_DPAD_UP:
                ALOGI("D-Pad UP pressed");
                break;
            case AKEYCODE_BUTTON_START:
                ALOGI("Gamepad START button pressed");
                break;
            default:
                ALOGI("Unhandled gamepad key pressed: %d", key_code);
                break;
        }
        return 1; // Event handled
    } else if (action == AKEY_EVENT_ACTION_UP) {
        // Button was released (optional: for non-instant actions)
        // LOGI("Button released: %d", keyCode);
        return 1; // Event handled
    }

    return 0; // Event not handled by our logic
}

// Example input handler function
int32_t handle_input(struct android_app *app, AInputEvent *event) {
    int res = handle_gamepad_key_event(event);
    if (res) {
        return 1;
    }

    // Do not handle edge actions.
    if (AMotionEvent_getEdgeFlags(event) != AMOTION_EVENT_EDGE_FLAG_NONE) {
        return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t source = AInputEvent_getSource(event);
        if (source & AINPUT_SOURCE_JOYSTICK) {
            // Get joystick axis values
            float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            float rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            float ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);

            ALOGI("INPUT: JOYSTICK L(%.1f, %.1f) R(%.1f, %.1f)", lx, ly, rx, ry);

            my_connection_send_input_event(state_.connection, static_cast<int>(InputType::GamepadLeftStick), lx, ly);

            my_connection_send_input_event(state_.connection, static_cast<int>(InputType::GamepadRightStick), rx, ry);

            return 0;
        }

        int32_t action = AMotionEvent_getAction(event);

        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);

        bool outside_host_screen = false;
        if (x < state_.h_margin) {
            x = state_.h_margin;
            outside_host_screen = true;
        }
        if (x > state_.window_width - state_.h_margin) {
            x = state_.window_width;
            outside_host_screen = true;
        }
        if (y < state_.v_margin) {
            y = state_.v_margin;
            outside_host_screen = true;
        }
        if (y > state_.window_height - state_.v_margin) {
            y = state_.window_height;
            outside_host_screen = true;
        }
        x -= state_.h_margin;
        y -= state_.v_margin;

        if (outside_host_screen) {
            return 0;
        }

        float x_ratio = x / (float)state_.render_width;
        float y_ratio = y / (float)state_.render_height;

        float client_x = x_ratio * stream_app_get_video_width(state_.stream_app);
        float client_y = y_ratio * stream_app_get_video_height(state_.stream_app);

        // AMotionEvent_getDownTime

        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN:
                ALOGI("INPUT: DOWN (%.1f, %.1f)", client_x, client_y);
                state_.pressed = true;
                state_.press_time = g_get_monotonic_time();
                state_.press_pos_x = client_x;
                state_.press_pos_y = client_y;
                state_.prev_pos_x = client_x;
                state_.prev_pos_y = client_y;
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::CursorLeftDown),
                                               client_x,
                                               client_y);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                if (AMotionEvent_getPointerCount(event) > 1) {
                    float dx = client_x - state_.press_pos_x;
                    float dy = client_y - state_.press_pos_y;
                    ALOGI("INPUT: SCROLL (%.1f, %.1f)", dx, dy);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorScroll),
                                                   dx,
                                                   dy);
                } else {
                    ALOGI("INPUT: MOVE (%.1f, %.1f)", client_x, client_y);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorMove),
                                                   client_x,
                                                   client_y);
                }

                // Cancel right click
                state_.press_time.reset();

                state_.prev_pos_x = client_x;
                state_.prev_pos_y = client_y;
                break;
            case AMOTION_EVENT_ACTION_UP:
                ALOGI("INPUT: UP (%.1f, %.1f)", client_x, client_y);

                bool right_click = false;
                if (state_.press_time.has_value()) {
                    int64_t now = g_get_monotonic_time();
                    float duration_s = float(now - state_.press_time.value()) / 1e6;
                    if (duration_s > 1) {
                        right_click = true;
                    }
                    state_.press_time.reset();
                }

                state_.pressed = false;
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::CursorLeftUp),
                                               client_x,
                                               client_y);

                if (right_click) {
                    ALOGI("INPUT: RIGHT CLICK (%.1f, %.1f)", client_x, client_y);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorRightClick),
                                                   client_x,
                                                   client_y);
                    break;
                }

                if (std::abs(state_.press_pos_x - client_x) < 10 && std::abs(state_.press_pos_y - client_y) < 10) {
                    ALOGI("INPUT: CLICK (%.1f, %.1f)", client_x, client_y);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorLeftClick),
                                                   client_x,
                                                   client_y);
                    break;
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

            eglQuerySurface(state_.initialEglData->display,
                            state_.initialEglData->surface,
                            EGL_WIDTH,
                            &state_.window_width);
            eglQuerySurface(state_.initialEglData->display,
                            state_.initialEglData->surface,
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
        } break;
        case APP_CMD_TERM_WINDOW: {
            ALOGI("APP_CMD_TERM_WINDOW");
            stream_app_stop(state_.stream_app);

            my_connection_disconnect(state_.connection);

            gst_deinit();
        } break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED: {
            ALOGI("APP_CMD_CONFIG_CHANGED");
            state_.window_width = ANativeWindow_getWidth(app->window);
            state_.window_height = ANativeWindow_getHeight(app->window);
        } break;
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
        if (ALooper_pollAll(timeout, NULL, &events, (void **)&source) >= 0) {
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

        float video_aspect = (float)video_width / (float)video_height;
        float window_aspect = (float)state_.window_width / (float)state_.window_height;

        // Align height
        if (window_aspect > video_aspect) {
            state_.render_height = state_.window_height;
            state_.render_width = state_.render_height * video_aspect;
        }
        // Align width
        else {
            state_.render_width = state_.window_width;
            state_.render_height = (float)state_.render_width / video_aspect;
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
