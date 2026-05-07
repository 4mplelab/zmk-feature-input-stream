#include <pb_decode.h>
#include <pb_encode.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include <drivers/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/input_stream.h>
#include <zmk/input_stream/input_stream.pb.h>
#include <zmk/keymap.h>
#include <zmk/physical_layouts.h>
#include <zmk/studio/custom.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define INPUT_STREAM_SUBSYSTEM_IDENTIFIER "zmk__input_stream"
#define INPUT_STREAM_ENDPOINT_PREFERRED_SETTING "endpoints/preferred2"

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

static bool disturb_host;
static enum zmk_transport previous_transport = ZMK_TRANSPORT_NONE;
static int subsystem_index = -1;

struct custom_message_ctx {
    const pb_msgdesc_t *fields;
    const void *message;
};

struct state_encode_ctx {
    const struct zmk_physical_layout *layout;
    const uint32_t *position_map;
    size_t position_count;
    zmk_keymap_layer_id_t layer_id;
};

static struct state_encode_ctx state_encode_ctx;

#if IS_ENABLED(CONFIG_SETTINGS)
static enum zmk_transport persisted_transport;

static void persist_preferred_transport_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    int err = settings_save_one(INPUT_STREAM_ENDPOINT_PREFERRED_SETTING, &persisted_transport,
                                sizeof(persisted_transport));
    if (err < 0) {
        LOG_WRN("Failed to persist preferred transport override: %d", err);
    }
}

K_WORK_DELAYABLE_DEFINE(persist_preferred_transport_work, persist_preferred_transport_work_handler);
#endif

static enum zmk_transport default_transport(void) {
    enum zmk_transport transport = zmk_endpoint_get_preferred_transport();

    if (transport != ZMK_TRANSPORT_NONE) {
        return transport;
    }

    transport = zmk_endpoint_get_selected().transport;
    if (transport != ZMK_TRANSPORT_NONE) {
        return transport;
    }

#if IS_ENABLED(CONFIG_ZMK_USB)
    return ZMK_TRANSPORT_USB;
#elif IS_ENABLED(CONFIG_ZMK_BLE)
    return ZMK_TRANSPORT_BLE;
#else
    return ZMK_TRANSPORT_NONE;
#endif
}

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

static const char *get_behavior_display_name(const struct device *device) {
    if (!device) {
        return "";
    }

    STRUCT_SECTION_FOREACH(zmk_behavior_ref, item) {
        if (item->device == device) {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
            if (item->metadata.display_name) {
                return item->metadata.display_name;
            }
#endif
            break;
        }
    }

    return device->name;
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

static void update_disturb_host_setting_persistence(enum zmk_transport transport) {
#if IS_ENABLED(CONFIG_SETTINGS)
    persisted_transport = transport;
    k_work_reschedule(&persist_preferred_transport_work,
                      K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE + 50));
#else
    ARG_UNUSED(transport);
#endif
}

static int set_disturb_host(bool enabled) {
    if (disturb_host == enabled) {
        return 0;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_cancel_delayable(&persist_preferred_transport_work);
#endif

    if (enabled) {
        previous_transport = default_transport();
        zmk_endpoint_clear_reports();
        int err = zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_NONE);
        if (err < 0) {
            return err;
        }
        update_disturb_host_setting_persistence(previous_transport);
    } else {
        enum zmk_transport transport =
            previous_transport != ZMK_TRANSPORT_NONE ? previous_transport : default_transport();
        int err = zmk_endpoint_set_preferred_transport(transport);
        if (err < 0) {
            return err;
        }
        zmk_endpoint_clear_reports();
    }

    disturb_host = enabled;
    return notify_disturb_host_changed(enabled);
}

static bool encode_behavior_infos(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    ARG_UNUSED(arg);

    STRUCT_SECTION_FOREACH(zmk_behavior_local_id_map, item) {
        zmk_input_stream_BehaviorInfo info = zmk_input_stream_BehaviorInfo_init_zero;
        info.id = item->local_id;
        snprintf(info.display_name, sizeof(info.display_name), "%s",
                 get_behavior_display_name(item->device));

        if (!pb_encode_tag_for_field(stream, field)) {
            return false;
        }

        if (!pb_encode_submessage(stream, &zmk_input_stream_BehaviorInfo_msg, &info)) {
            return false;
        }
    }

    return true;
}

static bool encode_key_infos(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    struct state_encode_ctx *ctx = (struct state_encode_ctx *)*arg;
    size_t key_count = MIN(ctx->layout->keys_len, ctx->position_count);

    for (size_t i = 0; i < key_count; i++) {
        uint32_t position = ctx->position_map[i];
        const struct zmk_key_physical_attrs *layout_key = &ctx->layout->keys[i];
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(ctx->layer_id, position);
        zmk_input_stream_KeyInfo key = zmk_input_stream_KeyInfo_init_zero;

        key.position = position;
        key.width = layout_key->width;
        key.height = layout_key->height;
        key.x = layout_key->x;
        key.y = layout_key->y;
#if IS_ENABLED(CONFIG_ZMK_PHYSICAL_LAYOUT_KEY_ROTATION)
        key.r = layout_key->r;
        key.rx = layout_key->rx;
        key.ry = layout_key->ry;
#endif
        key.behavior_id = get_behavior_id(binding);
        if (binding) {
            key.param1 = binding->param1;
            key.param2 = binding->param2;
        }

        if (!pb_encode_tag_for_field(stream, field)) {
            return false;
        }

        if (!pb_encode_submessage(stream, &zmk_input_stream_KeyInfo_msg, &key)) {
            return false;
        }
    }

    return true;
}

static int fill_state_response(zmk_input_stream_StateResponse *state) {
    const struct zmk_physical_layout *const *layouts;
    size_t layout_count = zmk_physical_layouts_get_list(&layouts);
    int active_layout_index = zmk_physical_layouts_get_selected();
    const uint32_t *position_map = NULL;
    int position_count = zmk_physical_layouts_get_selected_to_stock_position_map(&position_map);

    if (active_layout_index < 0 || (size_t)active_layout_index >= layout_count ||
        position_count < 0) {
        return -EINVAL;
    }

    const struct zmk_physical_layout *layout = layouts[active_layout_index];

    state_encode_ctx.layout = layout;
    state_encode_ctx.position_map = position_map;
    state_encode_ctx.position_count = (size_t)position_count;
    state_encode_ctx.layer_id = zmk_keymap_layer_default();

    state->disturb_host = disturb_host;
    snprintf(state->layout_name, sizeof(state->layout_name), "%s",
             layout->display_name ? layout->display_name : "Layout");
    state->keys.funcs.encode = encode_key_infos;
    state->keys.arg = &state_encode_ctx;
    state->behaviors.funcs.encode = encode_behavior_infos;

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
    zmk_input_stream_Notification notification = zmk_input_stream_Notification_init_zero;

    notification.which_notification_type = zmk_input_stream_Notification_behavior_event_tag;
    notification.notification_type.behavior_event.behavior_id = get_behavior_id(binding);
    notification.notification_type.behavior_event.layer = event.layer;
    notification.notification_type.behavior_event.position = event.position;
    notification.notification_type.behavior_event.param1 = binding ? binding->param1 : 0;
    notification.notification_type.behavior_event.param2 = binding ? binding->param2 : 0;
    notification.notification_type.behavior_event.pressed = pressed;

    LOG_DBG("input_stream behavior position=%d layer=%d behavior=%d pressed=%d", event.position,
            event.layer, notification.notification_type.behavior_event.behavior_id, pressed);

    return raise_input_stream_notification(&notification);
}

static int input_stream_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);

    if (!event) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_input_stream_Notification notification = zmk_input_stream_Notification_init_zero;
    notification.which_notification_type = zmk_input_stream_Notification_key_event_tag;
    notification.notification_type.key_event.position = event->position;
    notification.notification_type.key_event.pressed = event->state;

    LOG_DBG("input_stream key position=%d pressed=%d", event->position, event->state);
    raise_input_stream_notification(&notification);

    return ZMK_EV_EVENT_BUBBLE;
}
