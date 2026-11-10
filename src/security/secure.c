/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <noise/protocol.h>
#include <zmk/input_stream/secure.h>

/* One settings record, written only on enrollment/revocation, never on input. */
struct registration {
    uint8_t version;
    uint8_t private_key[32];
    uint8_t used[8];
    uint8_t clients[8][32];
};
static struct registration stored;
static bool loaded;
static NoiseCipherState *tx, *rx;
static uint32_t session_id, tx_counter, rx_counter;
K_MUTEX_DEFINE(crypto_mutex);

static void reset_locked(void) {
    if (tx)
        noise_cipherstate_free(tx);
    if (rx)
        noise_cipherstate_free(rx);
    tx = rx = NULL;
    session_id = tx_counter = rx_counter = 0;
}
void stream_secure_reset(void) {
    k_mutex_lock(&crypto_mutex, K_FOREVER);
    reset_locked();
    k_mutex_unlock(&crypto_mutex);
}
static int load_record(const char *key, size_t len, settings_read_cb read, void *arg) {
    if (strcmp(key, "keys"))
        return -ENOENT;
    if (len != sizeof(stored))
        return -EINVAL;
    struct registration next;
    if (read(arg, &next, len) != len || next.version != 1)
        return -EINVAL;
    stored = next;
    noise_clean(&next, sizeof(next));
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(input_stream, "input_stream", NULL, load_record, NULL, NULL);
static int ensure_loaded(void) {
    if (loaded)
        return 0;
    int rc = settings_subsys_init();
    if (rc)
        return rc;
    rc = settings_load_subtree("input_stream");
    if (!rc)
        loaded = true;
    return rc;
}
static int public_key(uint8_t out[32]) {
    NoiseDHState *dh = NULL;
    int rc = noise_dhstate_new_by_name(&dh, "25519");
    if (!rc)
        rc = noise_dhstate_set_keypair_private(dh, stored.private_key, 32);
    if (!rc)
        rc = noise_dhstate_get_public_key(dh, out, 32);
    if (dh)
        noise_dhstate_free(dh);
    return rc ? -EIO : 0;
}
int stream_register(const uint8_t *pub, size_t len, uint8_t out[32]) {
    if (len != 32 || noise_is_zero(pub, len))
        return -EINVAL;
    k_mutex_lock(&crypto_mutex, K_FOREVER);
    int rc = ensure_loaded();
    if (rc)
        goto done;
    struct registration previous = stored;
    if (stored.version != 1) {
        memset(&stored, 0, sizeof(stored));
        stored.version = 1;
        rc = sys_csrand_get(stored.private_key, 32);
        if (rc)
            goto rollback;
    }
    int slot = -1;
    for (int i = 0; i < CONFIG_ZMK_INPUT_STREAM_MAX_CLIENTS; ++i) {
        if (stored.used[i] && noise_is_equal(pub, stored.clients[i], 32)) {
            slot = i;
            break;
        }
        if (!stored.used[i] && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        rc = -ENOSPC;
        goto rollback;
    }
    memcpy(stored.clients[slot], pub, 32);
    stored.used[slot] = 1;
    rc = public_key(out);
    if (!rc)
        rc = settings_save_one("input_stream/keys", &stored, sizeof(stored));
rollback:
    if (rc)
        stored = previous;
    noise_clean(&previous, sizeof(previous));
done:
    k_mutex_unlock(&crypto_mutex);
    return rc;
}
int stream_revoke(const uint8_t *pub, size_t len, bool all) {
    if (!all && len != 32)
        return -EINVAL;
    k_mutex_lock(&crypto_mutex, K_FOREVER);
    int rc = ensure_loaded();
    if (!rc) {
        struct registration previous = stored;
        for (int i = 0; i < 8; ++i) {
            if (stored.used[i] && (all || noise_is_equal(pub, stored.clients[i], 32))) {
                noise_clean(stored.clients[i], 32);
                stored.used[i] = 0;
            }
        }
        rc = settings_save_one("input_stream/keys", &stored, sizeof(stored));
        if (rc)
            stored = previous;
        else
            reset_locked();
        noise_clean(&previous, sizeof(previous));
    }
    k_mutex_unlock(&crypto_mutex);
    return rc;
}
int stream_handshake(const uint8_t *message, size_t len,
                     zmk_input_stream_HandshakeResponse *response) {
    if (len != 96)
        return -EINVAL; /* IK message 1, empty payload */
    k_mutex_lock(&crypto_mutex, K_FOREVER);
    NoiseHandshakeState *hs = NULL;
    NoiseCipherState *next_tx = NULL, *next_rx = NULL;
    int rc = ensure_loaded();
    uint8_t buf[128], peer[32], sid[4];
    NoiseBuffer b;
    if (rc || stored.version != 1) {
        rc = -EACCES;
        goto done;
    }
    rc = noise_handshakestate_new_by_name(&hs, INPUT_STREAM_NOISE, NOISE_ROLE_RESPONDER);
    if (!rc)
        rc = noise_dhstate_set_keypair_private(noise_handshakestate_get_local_keypair_dh(hs),
                                               stored.private_key, 32);
    if (!rc)
        rc = noise_handshakestate_set_prologue(hs, "4mplelab/input-stream/v2", 24);
    if (!rc)
        rc = noise_handshakestate_start(hs);
    memcpy(buf, message, len);
    noise_buffer_set_input(b, buf, len);
    if (!rc)
        rc = noise_handshakestate_read_message(hs, &b, NULL);
    if (!rc)
        rc = noise_dhstate_get_public_key(noise_handshakestate_get_remote_public_key_dh(hs), peer,
                                          32);
    bool allowed = false;
    if (!rc)
        for (int i = 0; i < CONFIG_ZMK_INPUT_STREAM_MAX_CLIENTS; ++i)
            allowed |= stored.used[i] && noise_is_equal(peer, stored.clients[i], 32);
    if (!allowed) {
        rc = -EACCES;
        goto done;
    }
    rc = sys_csrand_get(sid, sizeof(sid));
    if (rc)
        goto done;
    if (!sys_get_le32(sid))
        sid[0] = 1;
    NoiseBuffer payload;
    noise_buffer_set_input(payload, sid, sizeof(sid));
    noise_buffer_set_output(b, response->payload.bytes, sizeof(response->payload.bytes));
    rc = noise_handshakestate_write_message(hs, &b, &payload);
    if (!rc)
        rc = noise_handshakestate_split(hs, &next_tx, &next_rx);
    if (!rc) {
        reset_locked();
        tx = next_tx;
        rx = next_rx;
        next_tx = next_rx = NULL;
        session_id = sys_get_le32(sid);
        response->session_id = session_id;
        response->payload.size = b.size;
    }
done:
    if (next_tx)
        noise_cipherstate_free(next_tx);
    if (next_rx)
        noise_cipherstate_free(next_rx);
    if (hs)
        noise_handshakestate_free(hs);
    noise_clean(buf, sizeof(buf));
    noise_clean(peer, sizeof(peer));
    k_mutex_unlock(&crypto_mutex);
    return rc ? -EACCES : 0;
}
int stream_encrypt(const uint8_t *plain, size_t len, zmk_input_stream_SecurePacket *packet) {
    k_mutex_lock(&crypto_mutex, K_FOREVER);
    int rc = -EACCES;
    if (!tx || tx_counter == UINT32_MAX || len + 16 > sizeof(packet->payload.bytes))
        goto done;
    memcpy(packet->payload.bytes, plain, len);
    NoiseBuffer b;
    noise_buffer_set_inout(b, packet->payload.bytes, len, sizeof(packet->payload.bytes));
    rc = noise_cipherstate_encrypt(tx, &b);
    if (!rc) {
        packet->session_id = session_id;
        packet->counter = tx_counter++;
        packet->payload.size = b.size;
    }
done:
    k_mutex_unlock(&crypto_mutex);
    return rc;
}
int stream_decrypt(const zmk_input_stream_SecurePacket *packet, uint8_t *out, size_t *len) {
    k_mutex_lock(&crypto_mutex, K_FOREVER);
    int rc = -EACCES;
    if (!rx || packet->session_id != session_id || packet->counter == UINT32_MAX ||
        packet->counter != rx_counter || packet->payload.size > *len)
        goto done;
    memcpy(out, packet->payload.bytes, packet->payload.size);
    NoiseBuffer b;
    noise_buffer_set_inout(b, out, packet->payload.size, *len);
    rc = noise_cipherstate_set_nonce(rx, packet->counter);
    if (!rc)
        rc = noise_cipherstate_decrypt(rx, &b);
    if (!rc) {
        *len = b.size;
        rx_counter++;
    }
done:
    k_mutex_unlock(&crypto_mutex);
    return rc;
}
