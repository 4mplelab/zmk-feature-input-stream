# ZMK Input Stream — Live Key Visualizer

![ZMK Version](https://img.shields.io/badge/ZMK-master-blue)

A ZMK module that streams key press/release events and layer changes to a web UI for live visualization.

## Features

- **Live key streaming**: forwards every physical key press/release over the custom Studio RPC protocol
- **Layer tracking**: notifies the web UI whenever the active layer changes
- **Web visualizer**: renders the physical key layout, highlights pressed keys, plays a unique beep per key, and marks keys that have been pressed at least once
- **Behavior & keycode history**: shows a rolling log of firmware behavior events and browser key events

## Usage

### 1. Add the module to your ZMK config

In your `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk
      remote: cormoran
      revision: main+custom-studio-protocol
      import: app/west.yml
    - name: zmk-feature-input-stream
      remote: cormoran
      revision: main
  self:
    path: config
```

### 2. Enable the feature in your keyboard config

In your keyboard's `.conf` file:

```
CONFIG_ZMK_INPUT_STREAM_FEATURE=y
CONFIG_ZMK_INPUT_STREAM_FEATURE_STUDIO_RPC=y

# Encoding nanopb requires more stack space
CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048
```

`CONFIG_ZMK_STUDIO=y` is also required (and the `studio-rpc-usb-uart` snippet or equivalent transport).

### 3. Bind the Studio unlock behavior

The input stream subsystem is **secured** — Studio must be unlocked before streaming can be enabled. Add `&studio_unlock` to a key in your keymap.

### 4. Open the web UI

Connect your keyboard via USB, open the web UI, click **Connect Serial**, unlock Studio, then click **Enable Stream**.

## Web UI layout

```
┌──────────────────────────────────────┐
│ • Streaming  Layer 0   [Enable Stream]│
├──────────────────────────────────────┤
│ Keyboard Layout                       │
│  ┌───┬───┬───┬───┐                   │
│  │ A │ B │ C │ D │  (pressed = blue) │
│  └───┴───┴───┴───┘                   │
│        ● = pressed at least once      │
├──────────────────────────────────────┤
│ Behavior Events   │ JS Key Events     │
│ #0  Key Press  A  │ A                 │
│ #0  Key Press  A  │ A                 │
└──────────────────────────────────────┘
```

## Development

```bash
# Run all lint + tests
pre-commit run

# Run firmware unit + build tests
python3 -m unittest

# Run web tests
cd web && npm test
```

## Known issue

If device is disconnected from web UI before stopping stream, zmk zmk_workqueue_lowprio_work_q becomes in stuck to wait finishing sending streaming data.
Timeout improvement in zmk is required for improvement.

## Fork extensions: input-stream v2

This fork retains the original subsystem identifier and v1 field numbers.
With `CONFIG_ZMK_INPUT_STREAM_SECURE=n` (the default), the existing web UI
and `enable_stream` / `disable_stream` continue to require Studio unlock.
No ZMK keymap-editing permissions are changed.

New clients can discover protocol version/capabilities, subscribe separately
to physical keys and layers, and obtain an authoritative state containing
pressed keys, all currently active layers, the implicit default layer and
layer priority order. State is returned when subscribing and can be fetched
again after a gap. Notifications carry an epoch and sequence number. A full
state is sent after queue overflow, avoiding a permanently highlighted key
when a release was dropped. Disabled key subscriptions do not send pressed
key state in layer snapshots.

Notifications use a dedicated low-priority worker. Disconnecting the active
BLE host, disconnecting USB, or changing the selected output clears the
subscription and pending notifications. Disconnecting a split peripheral
alone does not stop the host's stream. A transport operation already in
progress still depends on the ZMK transport implementation; this module
cannot guarantee recovery from every lower-level USB/BLE transport stall.

### Optional authenticated, encrypted display channel

The v2 security integration is experimental. It uses the standard
`Noise_IK_25519_ChaChaPoly_SHA256` protocol, implemented by a pinned subset of
[Noise-C](https://github.com/rweather/noise-c). It has not received an
independent security audit. It does not implement a new cryptographic primitive.

Use this fork in your `config/west.yml` (pin a reviewed commit for a release):

```yaml
manifest:
  remotes:
    - name: 4mplelab
      url-base: https://github.com/4mplelab
  projects:
    - name: zmk-feature-input-stream
      remote: 4mplelab
      revision: feature/secure-stream-v2
```

Retain the other required ZMK/custom-Studio projects and the existing module
feature settings. On the central/host-facing device, add:

```conf
CONFIG_ZMK_INPUT_STREAM_FEATURE=y
CONFIG_ZMK_INPUT_STREAM_FEATURE_STUDIO_RPC=y
CONFIG_ZMK_INPUT_STREAM_SECURE=y
CONFIG_ZMK_INPUT_STREAM_MAX_CLIENTS=4
CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES=256
CONFIG_ZMK_STUDIO_RPC_THREAD_STACK_SIZE=6000
```

Persistent Zephyr settings storage and a cryptographically secure entropy
source are required. `SETTINGS_NONE` is rejected. Changing security mode is
a build-time setting: rebuild and flash the firmware. The security code is
not linked into a disabled build. The normal Bluetooth pairing/encryption
and Studio's editing lock remain in effect.

In a compatible native client, such as Keymap Viewer:

1. Connect on a trusted computer, physically unlock Studio, and explicitly
   choose **register for input notifications**. Registering the client public
   key and returning the keyboard public key are allowed only while unlocked.
2. The computer stores its private key in its OS credential store; the
   keyboard stores its private key and authorized client public keys in settings.
3. Later connections automatically authenticate and derive fresh session keys.
   Encrypted display commands and notifications work while Studio is locked.
   The keymap-editing lock is not unlocked by this authentication.
4. To remove one client or all notification registrations, physically unlock
   Studio and use the compatible client's registration controls. This does
   not reset the keymap or Bluetooth bonds.

There is no plaintext input-stream fallback in a secure build. The original
web example has no native credential store or v2 Noise adapter and is for the
default, non-secure mode. Do not enable secure mode and expect the old example
to display input. An old client can still use its other Studio features.

### Security and performance notes

- Physical key positions and keymaps can reveal typed content. In non-secure
  mode, Studio unlock permits starting notifications; relocking does not stop
  an already enabled stream. This is not per-application authentication.
- Secure mode authorizes holders of registered keys. Enrollment must be done
  on a trusted computer. It is not executable attestation and cannot protect
  against compromise of the authorized application, its OS, or extracted keys.
- The added encryption protects this display channel. It does not encrypt
  other Studio extensions or replace the keyboard's normal HID/OS protections.
- Authentication/key exchange happens on connection, not on every key. Each
  notification still requires symmetric authenticated encryption and an
  authentication tag, increasing CPU work, traffic, memory use and energy use.
- Encryption runs on the notification worker, outside the physical input
  event handler. CPU and radio resources are shared, so zero impact on typing,
  highlighting latency or battery life is not guaranteed. Hardware crypto
  acceleration is not assumed by this portable Noise-C backend.
- Firmware builds, published Noise test vectors and native-simulator tests
  are checked. End-to-end latency, peak RAM and battery impact on actual LisM /
  LisM Pro hardware have not yet been measured. Compile-time RAM reports do
  not include every transient allocation or stack high-water mark.

See [the v2 wire protocol](docs/protocol-v2.md) for client integration. Keymap
Viewer's highlight toggle uses independent key subscriptions with this fork;
with non-secure firmware relocked, it falls back to application-side filtering
until unlock, preserving the original Studio lock condition.
