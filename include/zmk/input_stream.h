#pragma once

#include <stdbool.h>
#include <zmk/behavior.h>

int zmk_input_stream_notify_behavior_event(const struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event, bool pressed);
