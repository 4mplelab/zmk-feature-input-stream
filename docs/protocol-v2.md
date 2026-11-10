# input-stream v2 protocol

The authoritative schema is `proto/zmk/input_stream/input_stream.proto`, with
bounded nanopb storage in the matching `.options` file. The subsystem remains
`zmk__input_stream`. Legacy fields keep their numbers. No 64-bit protobuf fields
are used (ZMK Studio builds use nanopb's `WITHOUT_64BIT`).

## Discovery and legacy behavior

`get_capabilities` is readable while locked. Version 2 reports feature bits:
1 = separate subscriptions, 2 = authoritative state/resynchronization,
4 = active/default/ordered layers, 8 = Noise security required.
`max_clients` and `noise_protocol` describe the secure build.

The subsystem metadata allows discovery; individual requests enforce their
own permissions. In non-secure builds all remaining operations still require
Studio unlock. v1 `enable_stream` sends the original key/layer events. v2
`subscribe {keys,layers}` replaces the current subscription and returns `state`.
A disabled key subscription omits pressed keys even from state responses.
`get_state` repairs a detected sequence gap. `disable_stream` clears the
subscription and pending queue. Unknown request kinds fail without side effects.

## State and sequence

`State.layer_order` contains stable layer IDs in low-to-high priority order.
`active_layer_ids` describes the layers active now, including ZMK's implicit
`default_layer_id`. These are not a list of all usable layers or just the top
layer. A client resolves transparent keys through active lower-priority layers,
stopping at the firmware default layer. Map stable IDs to the downloaded map;
do not assume ID equals display index or layer zero is the effective base.

`pressed` is a position bitmap (maximum 512 keys). `key_count` is the selected
physical layout's binding count. `epoch` changes on subscription/reset;
`sequence` counts generated notifications, including dropped ones. State
responses/snapshots carry their corresponding cursor. On a gap, request a
fresh state and replace the display state; do not replay speculative keys.

A full state is provided on subscription and on layer changes. If the bounded
queue overflows, a fresh state is sent after draining it. A full keymap is never
transmitted for each press. Reordering or adding keymap layers still requires
clients to refresh their downloaded map as appropriate.

## Registration and Noise

The fixed suite is `Noise_IK_25519_ChaChaPoly_SHA256`. Both peers use prologue
ASCII `4mplelab/input-stream/v2` (24 bytes). Use established Noise implementations.

While Studio is physically unlocked, `register_client.public_key` (32 bytes)
adds a client X25519 public key; `registration.device_public_key` returns the
keyboard's static X25519 public key. Persist the local private key and pinned
remote public key securely before completing setup. Never hardcode a shared
secret into a public application. Registration is idempotent for the same key.
The firmware record reserves eight slots; `MAX_CLIENTS` selects 1..8 accepted
slots without changing the persisted record size.

`revoke_client.public_key` removes that key. `revoke_client.all=true` removes
all authorized clients. Both require Studio unlock, clear existing sessions,
and do not change keymaps/bonds. An OS reset or lost credential requires explicit
re-enrollment; the client must not silently replace a key and downgrade security.

On a new connection:

1. Initiator sends Noise IK message 1 as `handshake`, with empty Noise payload
   (96 bytes). The keyboard accepts only registered initiator static keys.
2. Responder returns Noise IK message 2 in `HandshakeResponse.payload`. Its
   authenticated Noise payload is a nonzero session ID encoded as four little-
   endian bytes. Check it equals the outer `session_id`.
3. Split into independent transmit/receive transport keys. Both counters start
   at zero. Wrap an encoded `Request` in `Request.secure`; decrypt the encoded
   `Response` from `Response.secure`. Notifications are similarly wrapped in
   `Notification.secure`. No plaintext telemetry is accepted in secure mode.

`SecurePacket` contains the session ID, 32-bit counter and Noise transport
ciphertext with its 16-byte tag. Use that counter as Noise's nonce (no additional
AD). The session is bound by its handshake and keys; changing a counter changes
the nonce and causes authentication failure. Reject a mismatched session and
replayed counters. Authenticated commands must arrive in sequence. Replies and
notifications share the keyboard's outgoing counter, and scheduling can reorder
them; a client can use a bounded replay window with stateless Noise decryption.
Never reuse a counter; reconnect before `UINT32_MAX`. A failed authentication must
not advance the accepted replay cursor. No secrets appear in capabilities.

Authenticated inner commands permit only subscriptions, state retrieval and
stopping telemetry. They do not grant Studio editing or enrollment permissions.
Registration/revocation remain separately protected by physical Studio unlock.
A failed handshake must not destroy an existing authenticated session.

## Lifecycle and host adapters

On active-host BLE disconnect, selected endpoint change or USB disconnect, the
module stops producing notifications, purges queued events and clears its Noise
session. An already executing lower-level ZMK transport operation is outside the
module's cancellation boundary. The dedicated worker isolates notification
sending from the shared low-priority queue, but is not a replacement for
transport-level timeout/error handling in the ZMK version used by the keyboard.

Native clients should keep long-term secrets in the OS credential store and
crypto operations outside an untrusted display renderer. Keymap Viewer injects
a host security adapter into its reusable ZMK source interface, so firmware
protocol code does not depend on Tauri or a particular OS.
