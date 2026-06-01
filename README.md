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
