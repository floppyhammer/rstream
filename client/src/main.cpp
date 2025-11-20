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
#include "state.h"
#include "stream/connection.h"
#include "stream/input.h"
#include "stream/render/render.hpp"
#include "stream/render/render_api.h"
#include "stream/sample.h"
#include "stream/stream_app.h"

struct MyState state_ = {};
struct MySample *prev_sample;

namespace {

void onAppCmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_START:
            ALOGD("APP_CMD_START");
            break;
        case APP_CMD_RESUME:
            ALOGD("APP_CMD_RESUME");
            break;
        case APP_CMD_PAUSE:
            // The app is no longer the foreground priority.
            ALOGD("APP_CMD_PAUSE");
            break;
        case APP_CMD_STOP:
            // The app is no longer visible.
            ALOGD("APP_CMD_STOP");
            break;
        case APP_CMD_DESTROY:
            ALOGD("APP_CMD_DESTROY");
            break;
        case APP_CMD_INIT_WINDOW: {
            // The app's window (Surface) is being destroyed.
            ALOGD("APP_CMD_INIT_WINDOW");

            state_.egl_data = std::make_unique<EglData>(app->window);
            state_.egl_data->makeCurrent();

            eglQuerySurface(state_.egl_data->display, state_.egl_data->surface, EGL_WIDTH, &state_.window_width);
            eglQuerySurface(state_.egl_data->display, state_.egl_data->surface, EGL_HEIGHT, &state_.window_height);

            state_.stream_app = my_stream_app_new();
            stream_app_set_egl_context(state_.stream_app,
                                       state_.egl_data->context,
                                       state_.egl_data->display,
                                       state_.egl_data->surface);

            std::string websocket_uri = "ws://" + state_.host_ip + ":5600/ws";
            state_.connection = g_object_ref_sink(my_connection_new(websocket_uri.c_str(), state_.host_ip.c_str()));

            StreamConfig config{};
            config.video_width = 1280;
            config.video_height = 720;
            if (state_.video_quality.find("1080p") != std::string::npos) {
                config.video_width = 1920;
                config.video_height = 1080;
            } else if (state_.video_quality.find("1440p") != std::string::npos) {
                config.video_width = 2560;
                config.video_height = 1440;
            } else if (state_.video_quality.find("4k") != std::string::npos) {
                config.video_width = 3840;
                config.video_height = 2160;
            }

            config.framerate = state_.framerate;
            config.bitrate = state_.bitrate;

            my_connection_set_stream_config(state_.connection, &config);

            my_connection_connect(state_.connection);

            ALOGD("%s: starting stream client mainloop thread", __FUNCTION__);
            stream_app_spawn_thread(state_.stream_app, state_.connection);

            try {
                ALOGD("%s: Setup renderer...", __FUNCTION__);
                state_.renderer = std::make_unique<Renderer>();
                state_.renderer->setupRender();
            } catch (std::exception const &e) {
                ALOGE("%s: Caught exception setting up renderer: %s", __FUNCTION__, e.what());
                state_.renderer.reset();
                abort();
            }
        } break;
        case APP_CMD_TERM_WINDOW: {
            ALOGD("APP_CMD_TERM_WINDOW");

            stream_app_stop(state_.stream_app);

            g_clear_object(&state_.stream_app);

            my_connection_disconnect(state_.connection);

            g_clear_object(&state_.connection);

            ALOGD("Reset renderer and EGL data.");
            state_.renderer.reset();
            state_.egl_data.reset();
        } break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED: {
            ALOGD("APP_CMD_CONFIG_CHANGED");
            state_.window_width = ANativeWindow_getWidth(app->window);
            state_.window_height = ANativeWindow_getHeight(app->window);
            ALOGD("Native window size %d %d", state_.window_width, state_.window_height);
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

            if (app->destroyRequested) {
                return false;
            }
        } else {
            break;
        }
    }

    return true;
}

int32_t onInputEvent(struct android_app *app, AInputEvent *event) {
    return handle_input(event, state_);
}

} // namespace

std::string retrieve_data_string(JNIEnv *env, jobject intentObject, jmethodID getStringExtraMethod, const char *key) {
    jstring keyString = env->NewStringUTF(key);

    jstring nativeDataJString = (jstring)env->CallObjectMethod(intentObject, getStringExtraMethod, keyString);

    std::string result;

    // --- 4. Process the data and Clean Up ---
    if (nativeDataJString != NULL) {
        const char *finalData = env->GetStringUTFChars(nativeDataJString, 0);

        result = finalData;

        // Clean up
        env->ReleaseStringUTFChars(nativeDataJString, finalData);
        env->DeleteLocalRef(nativeDataJString);
    } else {
        ALOGE("Data key not found.");
    }

    // Final clean up
    env->DeleteLocalRef(keyString);

    return result;
}

void android_main(struct android_app *app) {
    JNIEnv *env = nullptr;
    (*app->activity->vm).AttachCurrentThread(&env, NULL);

    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInputEvent;

    // Retrieve data strings.
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

        state_.host_ip = retrieve_data_string(env, intentObject, getStringExtraMethod, "host_ip");
        state_.video_quality = retrieve_data_string(env, intentObject, getStringExtraMethod, "video_quality");
        state_.framerate = std::stoi(retrieve_data_string(env, intentObject, getStringExtraMethod, "framerate"));
        state_.bitrate = std::stoi(retrieve_data_string(env, intentObject, getStringExtraMethod, "bitrate"));

        ALOGI("Got intent strings from native: host_ip: %s video_quality: %s framerate: %d bitrate: %d",
              state_.host_ip.c_str(),
              state_.video_quality.c_str(),
              state_.framerate,
              state_.bitrate);

        env->DeleteLocalRef(intentClass);
        env->DeleteLocalRef(intentObject);
    }

    ALOGD("Initialize GStreamer.");
    gst_init(NULL, NULL);

    // Set up gst logger
    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    ALOGD("Starting main loop");

    bool server_close_notified = false;

    // Main rendering loop.
    while (!app->destroyRequested) {
        if (!poll_events(app)) {
            break;
        }

        if (!state_.egl_data || !state_.renderer || !state_.stream_app) {
            continue;
        }

        // Exit the native activity upon connection loss.
        if (my_connection_server_closed(state_.connection)) {
            if (!server_close_notified) {
                ALOGI("Server closed, call ANativeActivity_finish.");
                ANativeActivity_finish(app->activity);
                server_close_notified = true;
            }
            continue;
        }

        state_.egl_data->makeCurrent();

        struct timespec decodeEndTime{};
        struct MySample *sample = stream_app_try_pull_sample(state_.stream_app, &decodeEndTime);

        uint32_t video_width = stream_app_get_video_width(state_.stream_app);
        uint32_t video_height = stream_app_get_video_height(state_.stream_app);

        if (sample == nullptr || video_width * video_height == 0) {
            if (prev_sample) {
                // Reuse previous sample.
                // sample = prev_sample;
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

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(state_.h_margin, state_.v_margin, state_.render_width, state_.render_height);

        state_.renderer->draw(sample->frame_texture_id, sample->frame_texture_target);

        eglSwapBuffers(state_.egl_data->display, state_.egl_data->surface);

        // Release the previous sample
        if (prev_sample != NULL) {
            stream_app_release_sample(state_.stream_app, prev_sample);
        }
        prev_sample = sample;

        state_.egl_data->makeNotCurrent();
    }

    ALOGI("Exited main loop, cleaning up");

    //
    // Clean up
    //
    // Don't call this.
    //    gst_deinit();

    (*app->activity->vm).DetachCurrentThread();
}
