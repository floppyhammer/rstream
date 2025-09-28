// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief Encapsulate EGL setup/teardown.
 * @author Moshi Turner <moses@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 */
#include "EglData.hpp"

#include <EGL/egl.h>
#include <android_native_app_glue.h>

#include <stdexcept>

#include "stream/gst_common.h"
#include "stream/render/GLError.h"

EglData::EglData(ANativeWindow *window) {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (display == EGL_NO_DISPLAY) {
        ALOGE("Failed to get EGL display");
        return;
    }

    bool success = eglInitialize(display, NULL, NULL);

    if (!success) {
        ALOGE("Failed to initialize EGL");
        return;
    }

#define MAX_CONFIGS 1024
    EGLConfig configs[MAX_CONFIGS];

    // RGBA8, multisample not required, ES3, and window
    const EGLint attributes[] = {
        EGL_RED_SIZE,
        8,

        EGL_GREEN_SIZE,
        8,

        EGL_BLUE_SIZE,
        8,

        EGL_ALPHA_SIZE,
        8,

        EGL_SAMPLES,
        1,

        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,

        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,

        EGL_NONE,
    };

    EGLint num_configs = 0;
    CHK_EGL(eglChooseConfig(display, attributes, configs, MAX_CONFIGS, &num_configs));

    if (num_configs == 0) {
        ALOGE("Failed to find suitable EGL config");
        throw std::runtime_error("Failed to find suitable EGL config");
    }
    ALOGI("Got %d egl configs, just taking the first one.", num_configs);

    config = configs[0];

    EGLint format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

    EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    CHK_EGL(context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes));

    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create EGL context");
        throw std::runtime_error("Failed to create EGL context");
    }
    CHECK_EGL_ERROR();
    ALOGI("EGL: Created context");

    ANativeWindow_setBuffersGeometry(window, 0, 0, format);
    surface = eglCreateWindowSurface(display, config, window, NULL);

    if (surface == EGL_NO_SURFACE) {
        ALOGE("Failed to create EGL surface");
        eglDestroyContext(display, context);
        throw std::runtime_error("Failed to create EGL surface");
    }

    CHECK_EGL_ERROR();
    ALOGI("EGL: Created surface");
}

EglData::~EglData() {
    EGLDisplay d = display;
    if (d == EGL_NO_DISPLAY) {
        d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (surface != EGL_NO_SURFACE) {
        eglDestroySurface(d, surface);
        surface = EGL_NO_SURFACE;
    }

    if (context != EGL_NO_CONTEXT) {
        eglDestroyContext(d, context);
        context = EGL_NO_CONTEXT;
    }
}

bool EglData::isReady() const {
    return display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE;
}

void EglData::makeCurrent() const {
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        ALOGE("Failed to make EGL context current");
        CHECK_EGL_ERROR();
        throw std::runtime_error("Could not make EGL context current");
    }
}

void EglData::makeNotCurrent() const {
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}
