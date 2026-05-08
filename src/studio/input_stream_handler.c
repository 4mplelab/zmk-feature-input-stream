#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>
#include <zmk/studio/custom.h>
#include <zmk/input_stream/input_stream.pb.h>
#include <zmk/workqueue.h>

#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static atomic_t streaming_enabled = ATOMIC_INIT(0);

static struct zmk_rpc_custom_subsystem_meta input_stream_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://cormoran.github.io/zmk-feature-input-stream/"),
    .security = ZMK_STUDIO_RPC_HANDLER_SECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__input_stream, &input_stream_meta, input_stream_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__input_stream, zmk_input_stream_Response);

static int get_own_subsystem_index(void) {
    size_t count;
    STRUCT_SECTION_COUNT(zmk_rpc_custom_subsystem, &count);
    for (size_t i = 0; i < count; i++) {
        struct zmk_rpc_custom_subsystem *s;
        STRUCT_SECTION_GET(zmk_rpc_custom_subsystem, i, &s);
        if (strcmp(s->identifier, "zmk__input_stream") == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ── Direct notification send (called only from the low-priority work handler) ── */

static zmk_input_stream_Notification s_notification;

static bool encode_notification_payload(pb_ostream_t *stream, const pb_field_t *field,
                                        void *const *arg) {
    const zmk_input_stream_Notification *notif = (const zmk_input_stream_Notification *)*arg;
    return zmk_rpc_custom_subsystem_encode_response_payload(
        stream, field, zmk_input_stream_Notification_fields, notif);
}

static void send_notification_direct(const zmk_input_stream_Notification *notif) {
    int idx = get_own_subsystem_index();
    if (idx < 0) {
        LOG_ERR("input_stream subsystem not found");
        return;
    }
    s_notification = *notif;
    struct zmk_studio_custom_notification custom_notif = {
        .subsystem_index = (uint8_t)idx,
        .encode_payload =
            {
                .funcs.encode = encode_notification_payload,
                .arg = &s_notification,
            },
    };
    raise_zmk_studio_custom_notification(custom_notif);
}

/* ── Notification queue + low-priority sender ──────────────────────────── */

/* Fixed-capacity message queue. Alignment 4 matches ZMK's behavior_queue.c pattern. */
K_MSGQ_DEFINE(notif_queue, sizeof(zmk_input_stream_Notification),
              CONFIG_ZMK_INPUT_STREAM_NOTIF_QUEUE_SIZE, 4);

/* Tracks items currently in the queue (incremented on put, decremented on get). */
static atomic_t notif_pending = ATOMIC_INIT(0);

static void notif_send_work_handler(struct k_work *work);
static K_WORK_DEFINE(notif_send_work, notif_send_work_handler);

static void notif_send_work_handler(struct k_work *work) {
    zmk_input_stream_Notification notif;
    while (k_msgq_get(&notif_queue, &notif, K_NO_WAIT) == 0) {
        atomic_dec(&notif_pending);
        send_notification_direct(&notif);
    }
}

/**
 * Enqueue a notification for low-priority delivery.
 * Called from event handlers; returns immediately so the event thread is not stalled.
 * Drops silently if the queue is full (limit = CONFIG_ZMK_INPUT_STREAM_NOTIF_QUEUE_SIZE).
 */
static void enqueue_notification(const zmk_input_stream_Notification *notif) {
    if (atomic_get(&notif_pending) >= CONFIG_ZMK_INPUT_STREAM_NOTIF_QUEUE_SIZE) {
        LOG_DBG("Notification queue full, dropping");
        return;
    }
    if (k_msgq_put(&notif_queue, notif, K_NO_WAIT) == 0) {
        atomic_inc(&notif_pending);
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &notif_send_work);
    }
}

/* ── RPC handler ───────────────────────────────────────────────────────── */

static bool input_stream_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                            pb_callback_t *encode_response) {
    zmk_input_stream_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__input_stream, encode_response);

    zmk_input_stream_Request req = zmk_input_stream_Request_init_zero;
    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_input_stream_Request_fields, &req)) {
        LOG_WRN("Failed to decode input_stream request: %s", PB_GET_ERROR(&req_stream));
        zmk_input_stream_ErrorResponse err = zmk_input_stream_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to decode request");
        resp->which_response_type = zmk_input_stream_Response_error_tag;
        resp->response_type.error = err;
        return true;
    }

    switch (req.which_request_type) {
    case zmk_input_stream_Request_enable_stream_tag:
        LOG_DBG("Enabling input stream");
        atomic_set(&streaming_enabled, 1);
        resp->which_response_type = zmk_input_stream_Response_ok_tag;
        break;
    case zmk_input_stream_Request_disable_stream_tag:
        LOG_DBG("Disabling input stream");
        atomic_set(&streaming_enabled, 0);
        resp->which_response_type = zmk_input_stream_Response_ok_tag;
        break;
    default:
        LOG_WRN("Unsupported input_stream request type: %d", req.which_request_type);
        {
            zmk_input_stream_ErrorResponse err = zmk_input_stream_ErrorResponse_init_zero;
            snprintf(err.message, sizeof(err.message), "Unknown request type");
            resp->which_response_type = zmk_input_stream_Response_error_tag;
            resp->response_type.error = err;
        }
        break;
    }
    return true;
}

static int on_position_state_changed(const zmk_event_t *eh) {
    if (!atomic_get(&streaming_enabled)) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_keymap_layer_index_t layer_idx = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(layer_idx);
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(layer_id, ev->position);

    zmk_input_stream_Notification notif = zmk_input_stream_Notification_init_zero;
    notif.which_notification_type = zmk_input_stream_Notification_key_event_tag;
    notif.notification_type.key_event.position = ev->position;
    notif.notification_type.key_event.pressed = ev->state;
    if (binding != NULL) {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS)
        notif.notification_type.key_event.behavior_id =
            zmk_behavior_get_local_id(binding->behavior_dev);
#endif
        notif.notification_type.key_event.param1 = binding->param1;
        notif.notification_type.key_event.param2 = binding->param2;
    }

    enqueue_notification(&notif);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(input_stream_position, on_position_state_changed);
ZMK_SUBSCRIPTION(input_stream_position, zmk_position_state_changed);

static int on_layer_state_changed(const zmk_event_t *eh) {
    if (!atomic_get(&streaming_enabled)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_input_stream_Notification notif = zmk_input_stream_Notification_init_zero;
    notif.which_notification_type = zmk_input_stream_Notification_layer_change_tag;
    notif.notification_type.layer_change.layer_index = zmk_keymap_highest_layer_active();

    enqueue_notification(&notif);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(input_stream_layer, on_layer_state_changed);
ZMK_SUBSCRIPTION(input_stream_layer, zmk_layer_state_changed);
