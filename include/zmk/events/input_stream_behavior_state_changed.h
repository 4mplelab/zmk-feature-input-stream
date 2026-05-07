/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>

struct zmk_input_stream_behavior_state_changed {
    struct zmk_behavior_binding binding;
    struct zmk_behavior_binding_event event;
    bool pressed;
};

ZMK_EVENT_DECLARE(zmk_input_stream_behavior_state_changed);
