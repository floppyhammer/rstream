#pragma once

#include <android/input.h>

#include "state.h"

int32_t handle_input(AInputEvent* event, MyState& state_);
