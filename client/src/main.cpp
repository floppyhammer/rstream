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
#include "stream/connection.h"
#include "stream/gst_common.h"
#include "stream/input.h"
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
    bool scrolling;

    std::optional<int64_t> press_time;

    MyConnection *connection;
    StreamApp *stream_app;

    std::string host_ip;

    std::unique_ptr<Renderer> renderer;

    std::unique_ptr<EglData> initialEglData;

    pthread_t listener_tid;
};

MyState state_ = {};

int32_t handle_gamepad_key_event(const AInputEvent *event) {
    int32_t source = AInputEvent_getSource(event);

    if (!((source & AINPUT_SOURCE_GAMEPAD) || (source & AINPUT_SOURCE_CLASS_JOYSTICK))) {
        return 0;
    }

    int32_t action = AKeyEvent_getAction(event);
    int32_t key_code = AKeyEvent_getKeyCode(event);

    ALOGE("Gamepad source %d, action %d, key code %d", source, action, key_code);

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        if (source & AINPUT_SOURCE_JOYSTICK) {
            float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            float rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            float ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);

            // --- Optional: Handling D-pad as Analog HAT Axis ---
            float hat_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hat_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);

            // 2. Get the value of the Left Trigger (LT)
            float lt_value = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);

            // 3. Get the value of the Right Trigger (RT)
            float rt_value = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);

            // 4. Process the values
            if (lt_value > 0.0f) {
                ALOGI("Gamepad Left Trigger pressed: %.3f", lt_value);
                my_connection_send_input_event(state_.connection, InputType::GamepadButtonL2, lt_value, 0);
                return 0;
            }

            if (rt_value > 0.0f) {
                ALOGI("Gamepad Right Trigger pressed: %.3f", rt_value);
                my_connection_send_input_event(state_.connection, InputType::GamepadButtonR2, rt_value, 0);
                return 0;
            }

            if (hat_x != 0.0f || hat_y != 0.0f) {
                // If hat_x is 1.0, RIGHT is pressed. If -1.0, LEFT is pressed.
                // If hat_y is 1.0, DOWN is pressed. If -1.0, UP is pressed.
                ALOGI("Gamepad D-Pad HAT (%.1f, %.1f)", hat_x, hat_y);
                return 0;
            } else {
                ALOGI("Gamepad JOYSTICK L(%.1f, %.1f) R(%.1f, %.1f)", lx, ly, rx, ry);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadLeftStick),
                                               lx,
                                               ly);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadRightStick),
                                               rx,
                                               ry);
                return 1;
            }
            // ---------------------------------------------------
        }
    }

    // 3. Process the action (DOWN, UP)
    if (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_UP) {
        bool pressed = action == AKEY_EVENT_ACTION_DOWN;

        // Button was pressed
        switch (key_code) {
            case AKEYCODE_BUTTON_A: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonA),
                                               pressed ? 1 : 0,
                                               0);

                ALOGI("Gamepad A pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_B: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonB),
                                               pressed ? 1 : 0,
                                               0);

                ALOGI("Gamepad B pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_X: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonX),
                                               pressed ? 1 : 0,
                                               0);

                ALOGI("Gamepad X pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_Y: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonY),
                                               pressed ? 1 : 0,
                                               0);

                ALOGI("Gamepad Y pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_L1: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonL1),
                                               pressed ? 1 : 0,
                                               0);

                ALOGI("Gamepad L1 pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_R1: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonR1),
                                               pressed ? 1 : 0,
                                               0);

                ALOGI("Gamepad R1 pressed: %d", pressed);
            } break;
                //            case AKEYCODE_BUTTON_L2: {
                //                my_connection_send_input_event(state_.connection,
                //                                               static_cast<int>(InputType::GamepadButtonL2),
                //                                               pressed ? 1 : 0,
                //                                               0);
                //
                //                ALOGI("Gamepad L2 pressed: %d", pressed);
                //            } break;
                //            case AKEYCODE_BUTTON_R2: {
                //                my_connection_send_input_event(state_.connection,
                //                                               static_cast<int>(InputType::GamepadButtonR2),
                //                                               pressed ? 1 : 0,
                //                                               0);
                //
                //                ALOGI("Gamepad R2 pressed: %d", pressed);
                //            } break;
            case AKEYCODE_DPAD_UP: {
                ALOGI("Gamepad D-Pad UP pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadUp),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_DPAD_DOWN: {
                ALOGI("Gamepad D-Pad DOWN pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadDown),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_DPAD_LEFT: {
                ALOGI("Gamepad D-Pad LEFT pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadLeft),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_DPAD_RIGHT: {
                ALOGI("Gamepad D-Pad RIGHT pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadRight),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_BUTTON_START: {
                ALOGI("Gamepad START pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonStart),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_BUTTON_SELECT: {
                ALOGI("Gamepad SELECT pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonSelect),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            default: {
                ALOGI("Gamepad Unhandled key: %d", key_code);
            } break;
        }
        return 1; // Event handled
    }

    return 0; // Event not handled by our logic
}

// Example input handler function
int32_t handle_input(struct android_app *app, AInputEvent *event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t key_code = AKeyEvent_getKeyCode(event);
        if (key_code == AKEYCODE_BACK && AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
            // The back button was released.
            // By default, the system will finish the activity.
            // You could add custom logic here, like showing a confirmation dialog.
            // If you handle the event and don't want the activity to close, return 1.
            // Otherwise, return 0 to let the system handle it (which closes the activity).

            return 0;
        }
    }

    int res = handle_gamepad_key_event(event);
    if (res) {
        return res;
    }

    // Do not handle edge actions.
    //    if (AMotionEvent_getEdgeFlags(event) != AMOTION_EVENT_EDGE_FLAG_NONE) {
    //        return 1;
    //    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
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

        //        bool single_touch = AMotionEvent_getPointerCount(event) == 1;

        float x_ratio = x / (float)state_.render_width;
        float y_ratio = y / (float)state_.render_height;

        float client_x = x_ratio * stream_app_get_video_width(state_.stream_app);
        float client_y = y_ratio * stream_app_get_video_height(state_.stream_app);

        // AMotionEvent_getDownTime

        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN: {
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
            }
                // A second touch is down.
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                int32_t action_code = action & AMOTION_EVENT_ACTION_MASK;
                size_t pointer_index =
                    (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

                ALOGI("INPUT: pointer index %zu, action code %d", pointer_index, action_code);
                size_t pointer_count = AMotionEvent_getPointerCount(event);

                if (pointer_count > 1) {
                    state_.pressed = false;
                    state_.press_time.reset();
                    ALOGI("INPUT: Multiple touch down %zu", pointer_count);
                    break;
                }
            } break;
            case AMOTION_EVENT_ACTION_MOVE:
                if (AMotionEvent_getPointerCount(event) > 1) {
                    float dx = client_x - state_.prev_pos_x;
                    float dy = client_y - state_.prev_pos_y;

                    if (dx == 0 || dy == 0) {
                        break;
                    }
                    ALOGI("INPUT: SCROLL (%.1f, %.1f)", dx, dy);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorScroll),
                                                   dx,
                                                   dy);
                    state_.prev_pos_x = client_x;
                    state_.prev_pos_y = client_y;

                    state_.scrolling = true;
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

                if (state_.scrolling) {
                    state_.scrolling = false;
                    break;
                }

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
        return 0; // Event handled
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

            ALOGI("Initialize GStreamer.");
            gst_init(NULL, NULL);

            // Set up gst logger
            gst_debug_set_default_threshold(GST_LEVEL_WARNING);

            state_.stream_app = stream_app_new();
            stream_app_set_egl_context(state_.stream_app,
                                       state_.initialEglData->context,
                                       state_.initialEglData->display,
                                       state_.initialEglData->surface);

            std::string websocket_uri = "ws://" + state_.host_ip + ":5600/ws";
            state_.connection = g_object_ref_sink(my_connection_new(websocket_uri.c_str(), state_.host_ip.c_str()));

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

            stream_app_destroy(&state_.stream_app);

            my_connection_disconnect(state_.connection);

            ALOGI("Reset renderer and EGL data.");
            state_.renderer->reset();
            state_.initialEglData.reset();
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

// This function might be called when the user presses a back button in your native UI,
// or when the stream ends.
void return_to_main_menu(struct android_app *app) {
    __android_log_print(ANDROID_LOG_INFO, "NativeApp", "Finishing native activity to return to main menu.");

    // This function signals the Android system to destroy and finish the current NativeActivity.
    // The user will be returned to the previous activity in the back stack (MainMenuActivity).
    ANativeActivity_finish(app->activity);
}

void android_main(struct android_app *app) {
    JNIEnv *env = nullptr;
    (*app->activity->vm).AttachCurrentThread(&env, NULL);
    app->onAppCmd = onAppCmd;

    app->onInputEvent = handle_input;

    // Retrieve host IP.
    {
        // The NativeActivity's Java object is available here:
        jobject nativeActivity = app->activity->clazz;

        // --- 2. Find the Activity Class and its getIntent() method ---
        // The activity object is already available via nativeActivity
        jclass activityClass = env->GetObjectClass(nativeActivity);

        // The signature for 'getIntent()' is ()Landroid/content/Intent;
        jmethodID getIntentMethod = env->GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;");

        if (!getIntentMethod) {
            __android_log_print(ANDROID_LOG_ERROR, "NATIVE_LOG", "Failed to find getIntent() method.");
            // Detach and return or handle error
            app->activity->vm->DetachCurrentThread();
            return;
        }

        // --- 3. Call the getIntent() method on the nativeActivity object ---
        jobject intentObject = env->CallObjectMethod(nativeActivity, getIntentMethod);

        // --- 4. Now, proceed to call getStringExtra() on the intentObject ---
        // (This part is the same as the previous example, using the 'env' pointer)

        jclass intentClass = env->FindClass("android/content/Intent");
        jmethodID getStringExtraMethod =
            env->GetMethodID(intentClass, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");

        const char *DATA_KEY = "host_ip";
        jstring keyString = env->NewStringUTF(DATA_KEY);

        jstring nativeDataJString = (jstring)env->CallObjectMethod(intentObject, getStringExtraMethod, keyString);

        // --- 4. Process the data and Clean Up ---
        if (nativeDataJString != NULL) {
            const char *finalData = env->GetStringUTFChars(nativeDataJString, 0);

            state_.host_ip = finalData;
            ALOGI("host_ip received: %s", state_.host_ip.c_str());

            // Clean up
            env->ReleaseStringUTFChars(nativeDataJString, finalData);
            env->DeleteLocalRef(nativeDataJString);
        } else {
            ALOGI("Data key not found.");
        }

        // Final clean up
        env->DeleteLocalRef(keyString);
        env->DeleteLocalRef(intentClass);
        env->DeleteLocalRef(intentObject);
    }

    // Main rendering loop.
    ALOGI("Starting main loop");
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

    ALOGI("Exited main loop, cleaning up");

    //
    // Clean up
    //
    // Don't call this.
    //    gst_deinit();

    (*app->activity->vm).DetachCurrentThread();
}
