/* SPDX-License-Identifier: MIT */
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#include <zephyr/random/random.h>
#include <zmk/studio/custom.h>
#include <zmk/studio/core.h>
#include <zmk/input_stream/input_stream.pb.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/physical_layouts.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
#include <zmk/input_stream/secure.h>
BUILD_ASSERT(!IS_ENABLED(CONFIG_SETTINGS_NONE), "Secure input-stream requires persistent settings");
BUILD_ASSERT(CONFIG_ZMK_STUDIO_RPC_THREAD_STACK_SIZE >= 6000,
             "Secure input-stream requires Studio RPC stack >= 6000");
BUILD_ASSERT(CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES >= 256,
             "Secure input-stream requires RPC custom payload >= 256");
#endif

#define KEYS 1
#define LAYERS 2
static atomic_t subscriptions, modern, epoch, sequence, resync;
static uint8_t pressed[64];
static struct k_spinlock pressed_lock;
static struct k_work_q stream_queue;
K_THREAD_STACK_DEFINE(stream_stack, CONFIG_ZMK_INPUT_STREAM_THREAD_STACK_SIZE);
K_MSGQ_DEFINE(notif_queue, sizeof(zmk_input_stream_Notification),
              CONFIG_ZMK_INPUT_STREAM_NOTIF_QUEUE_SIZE, 4);

/* Discovery is public. Every operation is authorized individually below. */
static struct zmk_rpc_custom_subsystem_meta input_stream_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://cormoran.github.io/zmk-feature-input-stream/"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};
ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__input_stream, &input_stream_meta, input_stream_rpc_handle_request);
ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__input_stream, zmk_input_stream_Response);

static bool unlocked(void) {
    return zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
}
static void clear_stream(void) {
    atomic_set(&subscriptions, 0);
    atomic_set(&resync, 0);
    atomic_inc(&epoch);
    k_msgq_purge(&notif_queue);
}
static void reset_work_handler(struct k_work *work) {
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
    stream_secure_reset();
#endif
}
K_WORK_DEFINE(reset_work, reset_work_handler);
static void lost_connection(void) {
    clear_stream();
    k_work_submit_to_queue(&stream_queue, &reset_work);
}
#if IS_ENABLED(CONFIG_ZMK_BLE)
static void ble_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_BLE &&
        bt_addr_le_cmp(bt_conn_get_dst(conn), zmk_ble_active_profile_addr()) == 0)
        lost_connection();
}
BT_CONN_CB_DEFINE(input_stream_connections) = {.disconnected = ble_disconnected};
#endif
static int on_endpoint(const zmk_event_t *event) {
    lost_connection();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(input_stream_endpoint, on_endpoint);
ZMK_SUBSCRIPTION(input_stream_endpoint, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
static int on_usb(const zmk_event_t *event) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(event);
    if (ev->conn_state != ZMK_USB_CONN_HID)
        lost_connection();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(input_stream_usb, on_usb);
ZMK_SUBSCRIPTION(input_stream_usb, zmk_usb_conn_state_changed);
#endif
static void current_state(zmk_input_stream_State *s) {
    *s = (zmk_input_stream_State)zmk_input_stream_State_init_zero;
    s->epoch = atomic_get(&epoch);
    s->sequence = atomic_get(&sequence);
    s->default_layer_id = zmk_keymap_layer_default();
    for (int i = 0; i < 32; ++i) {
        uint8_t id = zmk_keymap_layer_index_to_id(i);
        if (id == ZMK_KEYMAP_LAYER_ID_INVAL)
            break;
        s->layer_order[s->layer_order_count++] = id;
        if (zmk_keymap_layer_active(id))
            s->active_layer_ids[s->active_layer_ids_count++] = id;
    }
    const uint32_t *map;
    int count = zmk_physical_layouts_get_selected_to_stock_position_map(&map);
    s->key_count = count > 0 ? MIN(count, 512) : 0;
    if (atomic_get(&subscriptions) & KEYS) {
        k_spinlock_key_t key = k_spin_lock(&pressed_lock);
        s->pressed.size = DIV_ROUND_UP(s->key_count, 8);
        memcpy(s->pressed.bytes, pressed, s->pressed.size);
        k_spin_unlock(&pressed_lock, key);
    }
}
static bool encode_notification(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    return zmk_rpc_custom_subsystem_encode_response_payload(
        stream, field, zmk_input_stream_Notification_fields, *arg);
}
static void send_direct(zmk_input_stream_Notification *n) {
    if (!atomic_get(&subscriptions) || n->epoch != (uint32_t)atomic_get(&epoch))
        return;
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
    uint8_t plain[208];
    pb_ostream_t encoded = pb_ostream_from_buffer(plain, sizeof(plain));
    if (!pb_encode(&encoded, zmk_input_stream_Notification_fields, n))
        return;
    zmk_input_stream_Notification secure = zmk_input_stream_Notification_init_zero;
    secure.which_notification_type = zmk_input_stream_Notification_secure_tag;
    if (stream_encrypt(plain, encoded.bytes_written, &secure.notification_type.secure))
        return;
    n = &secure;
#endif
    int idx = -1;
    size_t count;
    STRUCT_SECTION_COUNT(zmk_rpc_custom_subsystem, &count);
    for (size_t i = 0; i < count; ++i) {
        struct zmk_rpc_custom_subsystem *s;
        STRUCT_SECTION_GET(zmk_rpc_custom_subsystem, i, &s);
        if (!strcmp(s->identifier, "zmk__input_stream")) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return;
    struct zmk_studio_custom_notification custom = {
        .subsystem_index = idx, .encode_payload = {.funcs.encode = encode_notification, .arg = n}};
    raise_zmk_studio_custom_notification(custom);
}
static void send_work_handler(struct k_work *work) {
    zmk_input_stream_Notification n;
    while (k_msgq_get(&notif_queue, &n, K_NO_WAIT) == 0)
        send_direct(&n);
    if (atomic_cas(&resync, 1, 0) && atomic_get(&subscriptions) && atomic_get(&modern)) {
        n = (zmk_input_stream_Notification)zmk_input_stream_Notification_init_zero;
        n.which_notification_type = zmk_input_stream_Notification_state_tag;
        current_state(&n.notification_type.state);
        n.epoch = n.notification_type.state.epoch;
        n.sequence = n.notification_type.state.sequence;
        send_direct(&n);
    }
}
K_WORK_DEFINE(send_work, send_work_handler);
static void enqueue(zmk_input_stream_Notification *n) {
    n->epoch = atomic_get(&epoch);
    n->sequence = atomic_inc(&sequence) + 1;
    if (n->which_notification_type == zmk_input_stream_Notification_state_tag) {
        n->notification_type.state.epoch = n->epoch;
        n->notification_type.state.sequence = n->sequence;
    }
    if (k_msgq_put(&notif_queue, n, K_NO_WAIT))
        atomic_set(&resync, 1);
    k_work_submit_to_queue(&stream_queue, &send_work);
}
static void error_response(zmk_input_stream_Response *resp, const char *message) {
    *resp = (zmk_input_stream_Response)zmk_input_stream_Response_init_zero;
    resp->which_response_type = zmk_input_stream_Response_error_tag;
    snprintf(resp->response_type.error.message, sizeof(resp->response_type.error.message), "%s",
             message);
}
static void process_request(const zmk_input_stream_Request *req, zmk_input_stream_Response *resp,
                            bool authenticated) {
    resp->which_response_type = zmk_input_stream_Response_ok_tag;
    if (req->which_request_type == zmk_input_stream_Request_get_capabilities_tag) {
        resp->which_response_type = zmk_input_stream_Response_capabilities_tag;
        zmk_input_stream_Capabilities *c = &resp->response_type.capabilities;
        c->version = 2;
        c->features = 7 | (IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE) ? 8 : 0);
        c->secure_required = IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE);
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
        c->max_clients = CONFIG_ZMK_INPUT_STREAM_MAX_CLIENTS;
        strcpy(c->noise_protocol, INPUT_STREAM_NOISE);
#endif
        return;
    }
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
    if (!authenticated) {
        if (req->which_request_type == zmk_input_stream_Request_handshake_tag) {
            resp->which_response_type = zmk_input_stream_Response_handshake_tag;
            if (stream_handshake(req->request_type.handshake.bytes,
                                 req->request_type.handshake.size, &resp->response_type.handshake))
                error_response(resp, "Authentication failed");
            else
                clear_stream();
            return;
        }
        if (req->which_request_type == zmk_input_stream_Request_register_client_tag && unlocked()) {
            const zmk_input_stream_RegisterRequest *r = &req->request_type.register_client;
            resp->which_response_type = zmk_input_stream_Response_registration_tag;
            if (stream_register(r->public_key.bytes, r->public_key.size,
                                resp->response_type.registration.device_public_key.bytes))
                error_response(resp, "Registration failed or client slots full");
            else
                resp->response_type.registration.device_public_key.size = 32;
            return;
        }
        if (req->which_request_type == zmk_input_stream_Request_revoke_client_tag && unlocked()) {
            const zmk_input_stream_RevokeRequest *r = &req->request_type.revoke_client;
            if (stream_revoke(r->public_key.bytes, r->public_key.size, r->all))
                error_response(resp, "Revocation failed");
            else
                clear_stream();
            return;
        }
        error_response(resp,
                       "Registered encrypted session required; registration needs Studio unlock");
        return;
    }
#else
    if (!unlocked()) {
        error_response(resp, "Studio unlock required");
        return;
    }
#endif
    switch (req->which_request_type) {
    case zmk_input_stream_Request_enable_stream_tag:
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
        error_response(resp, "Use v2 subscribe in encrypted mode");
#else
        clear_stream();
        atomic_set(&modern, 0);
        atomic_set(&sequence, 0);
        atomic_set(&subscriptions, KEYS | LAYERS);
#endif
        break;
    case zmk_input_stream_Request_disable_stream_tag:
        clear_stream();
        break;
    case zmk_input_stream_Request_subscribe_tag: {
        clear_stream();
        atomic_set(&modern, 1);
        atomic_set(&sequence, 0);
        const zmk_input_stream_SubscribeRequest *s = &req->request_type.subscribe;
        atomic_set(&subscriptions, (s->keys ? KEYS : 0) | (s->layers ? LAYERS : 0));
    }
        /* fall through: subscription replies with an initial full state */
    case zmk_input_stream_Request_get_state_tag:
        resp->which_response_type = zmk_input_stream_Response_state_tag;
        current_state(&resp->response_type.state);
        break;
    default:
        error_response(resp, "Unsupported input-stream request");
        break;
    }
}
static bool input_stream_rpc_handle_request(const zmk_custom_CallRequest *raw,
                                            pb_callback_t *encode_response) {
    zmk_input_stream_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__input_stream, encode_response);
    zmk_input_stream_Request req = zmk_input_stream_Request_init_zero;
    pb_istream_t in = pb_istream_from_buffer(raw->payload.bytes, raw->payload.size);
    if (!pb_decode(&in, zmk_input_stream_Request_fields, &req)) {
        error_response(resp, "Malformed request");
        return true;
    }
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
    if (req.which_request_type == zmk_input_stream_Request_secure_tag) {
        uint8_t plain[224];
        size_t len = sizeof(plain);
        if (stream_decrypt(&req.request_type.secure, plain, &len)) {
            error_response(resp, "Invalid encrypted request");
            return true;
        }
        req = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
        in = pb_istream_from_buffer(plain, len);
        if (!pb_decode(&in, zmk_input_stream_Request_fields, &req))
            error_response(resp, "Malformed request");
        else
            process_request(&req, resp, true);
        pb_ostream_t out = pb_ostream_from_buffer(plain, sizeof(plain) - 16);
        if (!pb_encode(&out, zmk_input_stream_Response_fields, resp)) {
            error_response(resp, "Response too large");
            return true;
        }
        *resp = (zmk_input_stream_Response)zmk_input_stream_Response_init_zero;
        resp->which_response_type = zmk_input_stream_Response_secure_tag;
        if (stream_encrypt(plain, out.bytes_written, &resp->response_type.secure))
            error_response(resp, "Session ended");
        memset(plain, 0, sizeof(plain));
        return true;
    }
#endif
    process_request(&req, resp, false);
    return true;
}
static int on_position(const zmk_event_t *event) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(event);
    if (ev->position >= 512)
        return ZMK_EV_EVENT_BUBBLE;
    k_spinlock_key_t key = k_spin_lock(&pressed_lock);
    WRITE_BIT(pressed[ev->position / 8], ev->position % 8, ev->state);
    k_spin_unlock(&pressed_lock, key);
    if (!(atomic_get(&subscriptions) & KEYS))
        return ZMK_EV_EVENT_BUBBLE;
    zmk_input_stream_Notification n = zmk_input_stream_Notification_init_zero;
    n.which_notification_type = zmk_input_stream_Notification_key_event_tag;
    n.notification_type.key_event.position = ev->position;
    n.notification_type.key_event.pressed = ev->state;
    if (!atomic_get(&modern)) {
        uint8_t layer = zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
        const struct zmk_behavior_binding *b =
            zmk_keymap_get_layer_binding_at_idx(layer, ev->position);
        if (b) {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS)
            n.notification_type.key_event.behavior_id = zmk_behavior_get_local_id(b->behavior_dev);
#endif
            n.notification_type.key_event.param1 = b->param1;
            n.notification_type.key_event.param2 = b->param2;
        }
    }
    enqueue(&n);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(input_stream_position, on_position);
ZMK_SUBSCRIPTION(input_stream_position, zmk_position_state_changed);
static int on_layer(const zmk_event_t *event) {
    if (!(atomic_get(&subscriptions) & LAYERS))
        return ZMK_EV_EVENT_BUBBLE;
    zmk_input_stream_Notification n = zmk_input_stream_Notification_init_zero;
    if (atomic_get(&modern)) {
        n.which_notification_type = zmk_input_stream_Notification_state_tag;
        current_state(&n.notification_type.state);
    } else {
        n.which_notification_type = zmk_input_stream_Notification_layer_change_tag;
        n.notification_type.layer_change.layer_index = zmk_keymap_highest_layer_active();
    }
    enqueue(&n);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(input_stream_layer, on_layer);
ZMK_SUBSCRIPTION(input_stream_layer, zmk_layer_state_changed);
static int init_stream(void) {
    atomic_set(&epoch, k_cycle_get_32());
    k_work_queue_start(&stream_queue, stream_stack, K_THREAD_STACK_SIZEOF(stream_stack),
                       K_LOWEST_APPLICATION_THREAD_PRIO, NULL);
    return 0;
}
SYS_INIT(init_stream, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_TEST)
#include "../test/stream_selftest.inc"
#endif
