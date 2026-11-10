/* SPDX-License-Identifier: MIT */
#include "protocol/internal.h"
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
void noise_rand_bytes(void *out, size_t len) {
    if (sys_csrand_get(out, len)) {
        k_panic();
    }
}
NoiseCipherState *noise_aesgcm_new(void) { return NULL; }
NoiseHashState *noise_blake2s_new(void) { return NULL; }
NoiseHashState *noise_blake2b_new(void) { return NULL; }
NoiseHashState *noise_sha512_new(void) { return NULL; }
NoiseDHState *noise_curve448_new(void) { return NULL; }
NoiseDHState *noise_newhope_new(void) { return NULL; }
