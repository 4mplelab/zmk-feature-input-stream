/* SPDX-License-Identifier: MIT */
/* Native-simulator only: execute the real authorization and wire handlers. */
#include <assert.h>
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
#include <noise/protocol.h>
#endif
static zmk_input_stream_Response test_call(zmk_input_stream_Request *req) {
    zmk_custom_CallRequest raw = zmk_custom_CallRequest_init_zero;
    pb_ostream_t out = pb_ostream_from_buffer(raw.payload.bytes, sizeof(raw.payload.bytes));
    assert(pb_encode(&out, zmk_input_stream_Request_fields, req));
    raw.payload.size = out.bytes_written;
    pb_callback_t encode = {0};
    assert(input_stream_rpc_handle_request(&raw, &encode));
    return zmk_rpc_custom_subsystem_response_buffer_zmk__input_stream;
}
static int stream_selftest(void) {
    zmk_input_stream_Request req = zmk_input_stream_Request_init_zero;
    zmk_studio_core_lock();
    req.which_request_type = zmk_input_stream_Request_get_capabilities_tag;
    zmk_input_stream_Response response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_capabilities_tag);
    assert(response.response_type.capabilities.version == 2);
    req.which_request_type = zmk_input_stream_Request_subscribe_tag;
    req.request_type.subscribe.layers = true;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_error_tag);
#if IS_ENABLED(CONFIG_ZMK_INPUT_STREAM_SECURE)
    NoiseDHState *client;
    assert(!noise_dhstate_new_by_name(&client, "25519"));
    uint8_t private_key[32], client_public[32], server_public[32];
    memset(private_key, 0x22, 32);
    assert(!noise_dhstate_set_keypair_private(client, private_key, 32));
    assert(!noise_dhstate_get_public_key(client, client_public, 32));
    noise_dhstate_free(client);
    req = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
    req.which_request_type = zmk_input_stream_Request_register_client_tag;
    req.request_type.register_client.public_key.size = 32;
    memcpy(req.request_type.register_client.public_key.bytes, client_public, 32);
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_error_tag);
    zmk_studio_core_unlock();
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_registration_tag);
    memcpy(server_public, response.response_type.registration.device_public_key.bytes, 32);
    zmk_studio_core_lock();
    NoiseHandshakeState *unknown;
    uint8_t unknown_key[32];
    memset(unknown_key, 0x33, 32);
    assert(!noise_handshakestate_new_by_name(&unknown, INPUT_STREAM_NOISE, NOISE_ROLE_INITIATOR));
    assert(!noise_dhstate_set_keypair_private(noise_handshakestate_get_local_keypair_dh(unknown),
                                              unknown_key, 32));
    assert(!noise_dhstate_set_public_key(noise_handshakestate_get_remote_public_key_dh(unknown),
                                         server_public, 32));
    assert(!noise_handshakestate_set_prologue(unknown, "4mplelab/input-stream/v2", 24));
    assert(!noise_handshakestate_start(unknown));
    req = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
    req.which_request_type = zmk_input_stream_Request_handshake_tag;
    NoiseBuffer untrusted;
    noise_buffer_set_output(untrusted, req.request_type.handshake.bytes,
                            sizeof(req.request_type.handshake.bytes));
    assert(!noise_handshakestate_write_message(unknown, &untrusted, NULL));
    req.request_type.handshake.size = untrusted.size;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_error_tag);
    noise_handshakestate_free(unknown);
    NoiseHandshakeState *hs;
    assert(!noise_handshakestate_new_by_name(&hs, INPUT_STREAM_NOISE, NOISE_ROLE_INITIATOR));
    assert(!noise_dhstate_set_keypair_private(noise_handshakestate_get_local_keypair_dh(hs),
                                              private_key, 32));
    assert(!noise_dhstate_set_public_key(noise_handshakestate_get_remote_public_key_dh(hs),
                                         server_public, 32));
    assert(!noise_handshakestate_set_prologue(hs, "4mplelab/input-stream/v2", 24));
    assert(!noise_handshakestate_start(hs));
    req = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
    req.which_request_type = zmk_input_stream_Request_handshake_tag;
    NoiseBuffer message;
    noise_buffer_set_output(message, req.request_type.handshake.bytes,
                            sizeof(req.request_type.handshake.bytes));
    assert(!noise_handshakestate_write_message(hs, &message, NULL));
    req.request_type.handshake.size = message.size;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_handshake_tag);
    noise_buffer_set_input(message, response.response_type.handshake.payload.bytes,
                           response.response_type.handshake.payload.size);
    uint8_t sid[4];
    NoiseBuffer received;
    noise_buffer_set_output(received, sid, sizeof(sid));
    assert(!noise_handshakestate_read_message(hs, &message, &received));
    assert(received.size == 4);
    NoiseCipherState *send, *receive;
    assert(!noise_handshakestate_split(hs, &send, &receive));
    noise_handshakestate_free(hs);
    uint8_t plaintext[224];
    zmk_input_stream_Request inner = zmk_input_stream_Request_init_zero;
    inner.which_request_type = zmk_input_stream_Request_subscribe_tag;
    inner.request_type.subscribe.layers = true;
    pb_ostream_t output = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
    assert(pb_encode(&output, zmk_input_stream_Request_fields, &inner));
    req = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
    req.which_request_type = zmk_input_stream_Request_secure_tag;
    req.request_type.secure.session_id = response.response_type.handshake.session_id;
    memcpy(req.request_type.secure.payload.bytes, plaintext, output.bytes_written);
    noise_buffer_set_inout(message, req.request_type.secure.payload.bytes, output.bytes_written,
                           sizeof(req.request_type.secure.payload.bytes));
    assert(!noise_cipherstate_encrypt(send, &message));
    req.request_type.secure.payload.size = message.size;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_secure_tag);
    noise_buffer_set_input(message, response.response_type.secure.payload.bytes,
                           response.response_type.secure.payload.size);
    assert(!noise_cipherstate_decrypt(receive, &message));
    memcpy(plaintext, message.data, message.size);
    pb_istream_t input = pb_istream_from_buffer(plaintext, message.size);
    response = (zmk_input_stream_Response)zmk_input_stream_Response_init_zero;
    assert(pb_decode(&input, zmk_input_stream_Response_fields, &response));
    assert(response.which_response_type == zmk_input_stream_Response_state_tag);
    assert(atomic_get(&subscriptions) == LAYERS);
    /* A replayed authenticated command is refused without changing subscriptions. */
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_error_tag);
    req.request_type.secure.counter = 1;
    req.request_type.secure.payload.bytes[0] ^= 1;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_error_tag);
    /* Failed authentication must not consume the legitimate command nonce. */
    inner = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
    inner.which_request_type = zmk_input_stream_Request_get_state_tag;
    output = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
    assert(pb_encode(&output, zmk_input_stream_Request_fields, &inner));
    memcpy(req.request_type.secure.payload.bytes, plaintext, output.bytes_written);
    noise_buffer_set_inout(message, req.request_type.secure.payload.bytes, output.bytes_written,
                           sizeof(req.request_type.secure.payload.bytes));
    assert(!noise_cipherstate_encrypt(send, &message));
    req.request_type.secure.payload.size = message.size;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_secure_tag);
    noise_buffer_set_input(message, response.response_type.secure.payload.bytes,
                           response.response_type.secure.payload.size);
    assert(!noise_cipherstate_decrypt(receive, &message));
    noise_cipherstate_free(send);
    noise_cipherstate_free(receive);
    req = (zmk_input_stream_Request)zmk_input_stream_Request_init_zero;
    req.which_request_type = zmk_input_stream_Request_revoke_client_tag;
    req.request_type.revoke_client.all = true;
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_error_tag);
    zmk_studio_core_unlock();
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_ok_tag);
    assert(!atomic_get(&subscriptions));
    atomic_set(&subscriptions, LAYERS); /* queue tests below need an active stream */
#else
    zmk_studio_core_unlock();
    response = test_call(&req);
    assert(response.which_response_type == zmk_input_stream_Response_state_tag);
    assert(atomic_get(&subscriptions) == LAYERS);
    assert(response.response_type.state.pressed.size == 0);
    req.request_type.subscribe.keys = true;
    response = test_call(&req);
    assert(atomic_get(&subscriptions) == (KEYS | LAYERS));
    zmk_studio_core_lock();
    assert(atomic_get(&subscriptions) == (KEYS | LAYERS));
#endif
    assert(!zmk_keymap_layer_activate(2, false));
    assert(!zmk_keymap_layer_activate(1, false));
    zmk_input_stream_State state;
    current_state(&state);
    assert(state.active_layer_ids_count == 3);
    assert(zmk_keymap_highest_layer_active() == 2);
    assert(!zmk_keymap_layer_deactivate(1, false));
    current_state(&state);
    assert(state.active_layer_ids_count == 2);
    assert(zmk_keymap_highest_layer_active() == 2);
    /* Queue overflow is repaired with an authoritative state, not ignored. */
    zmk_input_stream_Notification n = zmk_input_stream_Notification_init_zero;
    for (int i = 0; i < 32; ++i)
        enqueue(&n);
    assert(atomic_get(&resync) == 1);
    lost_connection();
    assert(!atomic_get(&subscriptions));
    assert(k_msgq_num_used_get(&notif_queue) == 0);
    printk("INPUT_STREAM_TEST: PASS\n");
    return 0;
}
SYS_INIT(stream_selftest, APPLICATION, 99);
