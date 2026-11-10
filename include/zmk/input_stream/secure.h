/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <zmk/input_stream/input_stream.pb.h>
#define INPUT_STREAM_NOISE "Noise_IK_25519_ChaChaPoly_SHA256"
int stream_register(const uint8_t *pub, size_t len, uint8_t out[32]);
int stream_revoke(const uint8_t *pub, size_t len, bool all);
int stream_handshake(const uint8_t *message, size_t len,
                     zmk_input_stream_HandshakeResponse *response);
int stream_encrypt(const uint8_t *plain, size_t len, zmk_input_stream_SecurePacket *packet);
int stream_decrypt(const zmk_input_stream_SecurePacket *packet, uint8_t *out, size_t *len);
void stream_secure_reset(void);

uint32_t stream_secure_generation(void);
void stream_secure_reset_if_generation(uint32_t generation);
