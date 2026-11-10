/* SPDX-License-Identifier: MIT */
#include <noise/protocol.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vector.h"
static size_t hex(const char *s, uint8_t *out) {
    size_t n = strlen(s) / 2;
    for (size_t i = 0; i < n; ++i) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = v;
    }
    return n;
}
int main(void) {
    NoiseHandshakeState *a = NULL, *b = NULL;
    assert(!noise_handshakestate_new_by_name(&a, "Noise_IK_25519_ChaChaPoly_SHA256",
                                             NOISE_ROLE_INITIATOR));
    assert(!noise_handshakestate_new_by_name(&b, "Noise_IK_25519_ChaChaPoly_SHA256",
                                             NOISE_ROLE_RESPONDER));
    uint8_t raw[256], expected[256], payload[256];
    size_t len;
    hex(INIT_STATIC, raw);
    assert(
        !noise_dhstate_set_keypair_private(noise_handshakestate_get_local_keypair_dh(a), raw, 32));
    hex(RESP_STATIC, raw);
    assert(
        !noise_dhstate_set_keypair_private(noise_handshakestate_get_local_keypair_dh(b), raw, 32));
    hex(INIT_REMOTE_STATIC, raw);
    assert(
        !noise_dhstate_set_public_key(noise_handshakestate_get_remote_public_key_dh(a), raw, 32));
    hex(INIT_EPHEMERAL, raw);
    assert(!noise_dhstate_set_keypair_private(noise_handshakestate_get_fixed_ephemeral_dh(a), raw,
                                              32));
    hex(RESP_EPHEMERAL, raw);
    assert(!noise_dhstate_set_keypair_private(noise_handshakestate_get_fixed_ephemeral_dh(b), raw,
                                              32));
    len = hex(INIT_PROLOGUE, raw);
    assert(!noise_handshakestate_set_prologue(a, raw, len));
    assert(!noise_handshakestate_set_prologue(b, raw, len));
    assert(!noise_handshakestate_start(a));
    assert(!noise_handshakestate_start(b));
    for (int i = 0; i < 2; ++i) {
        NoiseBuffer msg, p, received;
        len = hex(payloads[i], payload);
        noise_buffer_set_input(p, payload, len);
        noise_buffer_set_output(msg, raw, sizeof(raw));
        assert(!noise_handshakestate_write_message(i ? b : a, &msg, &p));
        size_t n = hex(ciphertexts[i], expected);
        assert(msg.size == n && !memcmp(raw, expected, n));
        noise_buffer_set_output(received, expected, sizeof(expected));
        assert(!noise_handshakestate_read_message(i ? a : b, &msg, &received));
        assert(received.size == len && !memcmp(expected, payload, len));
    }
    NoiseCipherState *at, *ar, *bt, *br;
    assert(!noise_handshakestate_split(a, &at, &ar));
    assert(!noise_handshakestate_split(b, &bt, &br));
    for (int i = 2; i < 6; ++i) {
        len = hex(payloads[i], raw);
        NoiseBuffer msg;
        noise_buffer_set_inout(msg, raw, len, sizeof(raw));
        assert(!noise_cipherstate_encrypt(i % 2 ? bt : at, &msg));
        size_t n = hex(ciphertexts[i], expected);
        assert(msg.size == n && !memcmp(raw, expected, n));
        assert(!noise_cipherstate_decrypt(i % 2 ? ar : br, &msg));
        assert(msg.size == len);
    }
    noise_cipherstate_free(at);
    noise_cipherstate_free(ar);
    noise_cipherstate_free(bt);
    noise_cipherstate_free(br);
    noise_handshakestate_free(a);
    noise_handshakestate_free(b);
    puts("Noise IK vector PASS");
    return 0;
}
