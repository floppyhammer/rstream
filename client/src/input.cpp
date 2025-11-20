#include "input.h"

#include <android/input.h>

#include "state.h"
#include "stream/input.h"
#include "stream/utils/logger.h"

int32_t handle_gamepad_key_event(const AInputEvent* event, MyState& state_) {
    int32_t source = AInputEvent_getSource(event);

    if (!((source & AINPUT_SOURCE_GAMEPAD) || (source & AINPUT_SOURCE_CLASS_JOYSTICK))) {
        return 0;
    }

    int32_t action = AKeyEvent_getAction(event);
    int32_t key_code = AKeyEvent_getKeyCode(event);

    //    ALOGI("Gamepad source %d, action %d, key code %d", source, action, key_code);

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        if (source & AINPUT_SOURCE_JOYSTICK) {
            float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            float rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            float ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);

            // --- Optional: Handling D-pad as Analog HAT Axis ---
            //            float hat_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
            //            float hat_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
            //            ALOGI("Gamepad D-Pad HAT (%.1f, %.1f)", hat_x, hat_y);

            float lt_value = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
            float rt_value = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);

            if (abs(state_.prev_lt - lt_value) > 0.001) {
                ALOGD("Gamepad Left Trigger pressed: %.3f", lt_value);
                state_.prev_lt = lt_value;
                my_connection_send_input_event(state_.connection, InputType::GamepadButtonLT, lt_value, 0);
                return 1;
            }

            if (abs(state_.prev_rt - rt_value) > 0.001) {
                ALOGD("Gamepad Right Trigger pressed: %.3f", rt_value);
                state_.prev_rt = rt_value;
                my_connection_send_input_event(state_.connection, InputType::GamepadButtonRT, rt_value, 0);
                return 1;
            }

            if (abs(state_.prev_lx - lx) > 0.001 || abs(state_.prev_ly - ly) > 0.001) {
                ALOGD("Gamepad JOYSTICK L(%.1f, %.1f) ", lx, ly);
                state_.prev_lx = lx;
                state_.prev_ly = ly;
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadLeftStick),
                                               lx,
                                               ly);
                return 1;
            }

            if (abs(state_.prev_rx - rx) > 0.001 || abs(state_.prev_ry - ry) > 0.001) {
                ALOGD("Gamepad JOYSTICK R(%.1f, %.1f)", rx, ry);
                state_.prev_rx = rx;
                state_.prev_ry = ry;
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

                ALOGD("Gamepad A pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_B: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonB),
                                               pressed ? 1 : 0,
                                               0);

                ALOGD("Gamepad B pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_X: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonX),
                                               pressed ? 1 : 0,
                                               0);

                ALOGD("Gamepad X pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_Y: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonY),
                                               pressed ? 1 : 0,
                                               0);

                ALOGD("Gamepad Y pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_L1: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonLB),
                                               pressed ? 1 : 0,
                                               0);

                ALOGD("Gamepad L1 pressed: %d", pressed);
            } break;
            case AKEYCODE_BUTTON_R1: {
                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonRB),
                                               pressed ? 1 : 0,
                                               0);

                ALOGD("Gamepad R1 pressed: %d", pressed);
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
                ALOGD("Gamepad D-Pad UP pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadUp),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_DPAD_DOWN: {
                ALOGD("Gamepad D-Pad DOWN pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadDown),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_DPAD_LEFT: {
                ALOGD("Gamepad D-Pad LEFT pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadLeft),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_DPAD_RIGHT: {
                ALOGD("Gamepad D-Pad RIGHT pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadRight),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_BUTTON_START: {
                ALOGD("Gamepad START pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonStart),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            case AKEYCODE_BUTTON_SELECT: {
                ALOGD("Gamepad SELECT pressed: %d", pressed);

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::GamepadButtonSelect),
                                               pressed ? 1 : 0,
                                               0);
            } break;
            default: {
                ALOGD("Gamepad Unhandled key: %d", key_code);
                return 0;
            } break;
        }
        return 1; // Event handled
    }

    return 0; // Event not handled by our logic
}

int32_t handle_input(AInputEvent* event, MyState& state_) {
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

    int res = handle_gamepad_key_event(event, state_);
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
                ALOGD("INPUT: DOWN (%.1f, %.1f)", client_x, client_y);
                state_.pressed = true;
                state_.press_time = g_get_monotonic_time();
                state_.press_pos_x = client_x;
                state_.press_pos_y = client_y;
                state_.prev_pos_x = client_x;
                state_.prev_pos_y = client_y;

                int64_t now = g_get_monotonic_time();

                auto new_client_x = client_x;
                auto new_client_y = client_y;

                // Double click
                auto down_interval = float(now - state_.last_time_cursor_down) / 1.0e3f;
                if (down_interval < 500) {
                    new_client_x = state_.last_cursor_down_pos_x;
                    new_client_y = state_.last_cursor_down_pos_y;

                    ALOGD("INPUT: double click (%.1f, %.1f), interval %.1f", new_client_x, new_client_y, down_interval);
                }

                my_connection_send_input_event(state_.connection,
                                               static_cast<int>(InputType::CursorLeftDown),
                                               new_client_x,
                                               new_client_y);

                state_.last_time_cursor_down = now;
                state_.last_cursor_down_pos_x = client_x;
                state_.last_cursor_down_pos_y = client_y;
            }
                // A second touch is down.
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                int32_t action_code = action & AMOTION_EVENT_ACTION_MASK;
                size_t pointer_index =
                    (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

                ALOGD("INPUT: pointer index %zu, action code %d", pointer_index, action_code);
                size_t pointer_count = AMotionEvent_getPointerCount(event);

                if (pointer_count > 1) {
                    if (pointer_count > 2) {
                        my_connection_send_input_event(state_.connection, InputType::KeyboardSuper, 1, 0);
                        break;
                    }

                    // Release left cursor down in situ, so it won't trigger a selection action.
                    if (state_.pressed) {
                        my_connection_send_input_event(state_.connection,
                                                       InputType::CursorLeftUp,
                                                       state_.press_pos_x,
                                                       state_.press_pos_y);

                        state_.pressed = false;
                        state_.press_time.reset();
                    }

                    float x0 = AMotionEvent_getX(event, 0);
                    float y0 = AMotionEvent_getY(event, 0);

                    float x1 = AMotionEvent_getX(event, 1);
                    float y1 = AMotionEvent_getY(event, 1);

                    float x_center = (x0 + x1) * 0.5f;
                    float y_center = (y0 + y1) * 0.5f;

                    state_.prev_pos_center_x = x_center;
                    state_.prev_pos_center_y = y_center;

                    ALOGD("INPUT: Multiple touch down %zu", pointer_count);
                }
            } break;
            case AMOTION_EVENT_ACTION_MOVE:
                if (AMotionEvent_getPointerCount(event) > 1) {
                    float x0 = AMotionEvent_getX(event, 0);
                    float y0 = AMotionEvent_getY(event, 0);

                    float x1 = AMotionEvent_getX(event, 1);
                    float y1 = AMotionEvent_getY(event, 1);

                    float x_center = (x0 + x1) / 2;
                    float y_center = (y0 + y1) / 2;

                    float dx = x_center - state_.prev_pos_center_x;
                    float dy = y_center - state_.prev_pos_center_y;

                    if (dx == 0 || dy == 0) {
                        break;
                    }
                    ALOGD("INPUT: SCROLL (%.1f, %.1f)", dx, dy);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorScroll),
                                                   dx,
                                                   dy);

                    state_.prev_pos_center_x = x_center;
                    state_.prev_pos_center_y = y_center;

                    state_.scrolling = true;
                } else {
                    ALOGD("INPUT: MOVE (%.1f, %.1f)", client_x, client_y);
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
                ALOGD("INPUT: UP (%.1f, %.1f)", client_x, client_y);

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
                    ALOGD("INPUT: RIGHT CLICK (%.1f, %.1f)", client_x, client_y);
                    my_connection_send_input_event(state_.connection,
                                                   static_cast<int>(InputType::CursorRightClick),
                                                   client_x,
                                                   client_y);
                    break;
                }

                if (std::abs(state_.press_pos_x - client_x) < 10 && std::abs(state_.press_pos_y - client_y) < 10) {
                    ALOGD("INPUT: CLICK (%.1f, %.1f)", client_x, client_y);

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
