#include <pb_decode.h>
#include <pb_encode.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/input_stream_behavior_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/input_stream.h>
#include <zmk/input_stream/input_stream.pb.h>
#include <zmk/studio/custom.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define INPUT_STREAM_SUBSYSTEM_IDENTIFIER "zmk__input_stream"

static bool input_stream_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                            pb_callback_t *encode_response);
static int input_stream_listener(const zmk_event_t *eh);

static struct zmk_rpc_custom_subsystem_meta input_stream_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("http://cormoran.github.io/zmk-feature-input-stream/"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__input_stream, &input_stream_meta, input_stream_rpc_handle_request);
ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__input_stream, zmk_input_stream_Response);
ZMK_LISTENER(input_stream, input_stream_listener);
ZMK_SUBSCRIPTION(input_stream, zmk_position_state_changed);
ZMK_SUBSCRIPTION(input_stream, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(input_stream, zmk_input_stream_behavior_state_changed);

static bool disturb_host;
static int subsystem_index = -1;

struct custom_message_ctx {
    const pb_msgdesc_t *fields;
    const void *message;
};

static uint32_t get_behavior_id(const struct zmk_behavior_binding *binding) {
    if (!binding || !binding->behavior_dev) {
        return 0;
    }

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
    if (binding->local_id > 0 && binding->local_id != UINT16_MAX) {
        return binding->local_id;
    }
#endif

    zmk_behavior_local_id_t local_id = zmk_behavior_get_local_id(binding->behavior_dev);
    return local_id == UINT16_MAX ? 0 : local_id;
}

static int get_subsystem_index(void) {
    if (subsystem_index >= 0) {
        return subsystem_index;
    }

    size_t subsystem_count;
    STRUCT_SECTION_COUNT(zmk_rpc_custom_subsystem, &subsystem_count);

    for (size_t i = 0; i < subsystem_count; i++) {
        struct zmk_rpc_custom_subsystem *custom_subsys;
        STRUCT_SECTION_GET(zmk_rpc_custom_subsystem, i, &custom_subsys);
        if (strcmp(custom_subsys->identifier, INPUT_STREAM_SUBSYSTEM_IDENTIFIER) == 0) {
            subsystem_index = (int)i;
            return subsystem_index;
        }
    }

    return -ENOENT;
}

static bool encode_custom_message(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    struct custom_message_ctx *ctx = (struct custom_message_ctx *)*arg;

    return zmk_rpc_custom_subsystem_encode_response_payload(stream, field, ctx->fields,
                                                            ctx->message);
}

static int raise_input_stream_notification(const zmk_input_stream_Notification *notification) {
    int index = get_subsystem_index();
    if (index < 0) {
        return index;
    }

    struct custom_message_ctx ctx = {
        .fields = zmk_input_stream_Notification_fields,
        .message = notification,
    };

    pb_callback_t payload = {
        .funcs = {.encode = encode_custom_message},
        .arg = &ctx,
    };

    return raise_zmk_studio_custom_notification((struct zmk_studio_custom_notification){
        .subsystem_index = index,
        .encode_payload = payload,
    });
}

static int notify_disturb_host_changed(bool enabled) {
    zmk_input_stream_Notification notification = zmk_input_stream_Notification_init_zero;
    notification.which_notification_type = zmk_input_stream_Notification_disturb_host_changed_tag;
    notification.notification_type.disturb_host_changed = enabled;
    LOG_DBG("input_stream disturb_host=%d", enabled);
    return raise_input_stream_notification(&notification);
}

static int set_disturb_host(bool enabled) {
    if (disturb_host == enabled) {
        return 0;
    }

    zmk_endpoint_clear_reports();
    disturb_host = enabled;
    return notify_disturb_host_changed(enabled);
}

static int fill_state_response(zmk_input_stream_StateResponse *state) {
    state->disturb_host = disturb_host;
    return 0;
}

static void set_error_response(zmk_input_stream_Response *resp, const char *message) {
    zmk_input_stream_ErrorResponse error = zmk_input_stream_ErrorResponse_init_zero;
    snprintf(error.message, sizeof(error.message), "%s", message);
    resp->which_response_type = zmk_input_stream_Response_error_tag;
    resp->response_type.error = error;
}

static int handle_get_state(zmk_input_stream_Response *resp) {
    zmk_input_stream_StateResponse state = zmk_input_stream_StateResponse_init_zero;
    int err = fill_state_response(&state);
    if (err < 0) {
        return err;
    }

    resp->which_response_type = zmk_input_stream_Response_state_tag;
    resp->response_type.state = state;
    return 0;
}

static int input_stream_rpc_handle_request_impl(const zmk_input_stream_Request *req,
                                                zmk_input_stream_Response *resp) {
    switch (req->which_request_type) {
    case zmk_input_stream_Request_get_state_tag:
        return handle_get_state(resp);
    case zmk_input_stream_Request_set_disturb_host_tag: {
        int err = set_disturb_host(req->request_type.set_disturb_host);
        if (err < 0) {
            return err;
        }
        return handle_get_state(resp);
    }
    default:
        return -ENOTSUP;
    }
}

static bool input_stream_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                            pb_callback_t *encode_response) {
    zmk_input_stream_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__input_stream, encode_response);
    zmk_input_stream_Request req = zmk_input_stream_Request_init_zero;
    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);

    if (!pb_decode(&req_stream, zmk_input_stream_Request_fields, &req)) {
        LOG_WRN("Failed to decode input stream request: %s", PB_GET_ERROR(&req_stream));
        set_error_response(resp, "Failed to decode request");
        return true;
    }

    int err = input_stream_rpc_handle_request_impl(&req, resp);
    if (err < 0) {
        LOG_WRN("Failed to handle input stream request: %d", err);
        set_error_response(resp, "Failed to handle request");
    }

    return true;
}

int zmk_input_stream_notify_behavior_event(const struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event, bool pressed) {
    return raise_zmk_input_stream_behavior_state_changed(
        (struct zmk_input_stream_behavior_state_changed){
            .binding = binding ? *binding : (struct zmk_behavior_binding){0},
            .event = event,
            .pressed = pressed,
        });
}

static int input_stream_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *position_event = as_zmk_position_state_changed(eh);
    if (position_event) {
        zmk_input_stream_Notification notification = zmk_input_stream_Notification_init_zero;
        notification.which_notification_type = zmk_input_stream_Notification_key_event_tag;
        notification.notification_type.key_event.position = position_event->position;
        notification.notification_type.key_event.pressed = position_event->state;

        LOG_DBG("input_stream key position=%d pressed=%d", position_event->position,
                position_event->state);
        raise_input_stream_notification(&notification);

        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_input_stream_behavior_state_changed *behavior_event =
        as_zmk_input_stream_behavior_state_changed(eh);
    if (behavior_event) {
        zmk_input_stream_Notification notification = zmk_input_stream_Notification_init_zero;

        notification.which_notification_type = zmk_input_stream_Notification_behavior_event_tag;
        notification.notification_type.behavior_event.behavior_id =
            get_behavior_id(&behavior_event->binding);
        notification.notification_type.behavior_event.layer = behavior_event->event.layer;
        notification.notification_type.behavior_event.position = behavior_event->event.position;
        notification.notification_type.behavior_event.param1 = behavior_event->binding.param1;
        notification.notification_type.behavior_event.param2 = behavior_event->binding.param2;
        notification.notification_type.behavior_event.pressed = behavior_event->pressed;

        LOG_DBG("input_stream behavior position=%d layer=%d behavior=%d pressed=%d",
                behavior_event->event.position, behavior_event->event.layer,
                notification.notification_type.behavior_event.behavior_id, behavior_event->pressed);

        raise_input_stream_notification(&notification);

        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_keycode_state_changed *keycode_event = as_zmk_keycode_state_changed(eh);
    if (keycode_event && disturb_host) {
        return ZMK_EV_EVENT_CAPTURED;
    }

    return ZMK_EV_EVENT_BUBBLE;
}
