# ZMK Feature: Input Stream

![ZMK Version](https://img.shields.io/badge/ZMK-master-blue)
[![Test](https://github.com/cormoran/zmk-feature-input-stream/actions/workflows/zmk-module.yml/badge.svg?branch=main)](https://github.com/cormoran/zmk-feature-input-stream/actions/workflows/zmk-module.yml) [![Devcontainer](https://github.com/cormoran/zmk-feature-input-stream/actions/workflows/devcontainer.yml/badge.svg?branch=main)](https://github.com/cormoran/zmk-feature-input-stream/actions/workflows/devcontainer.yml)

This module streams raw key presses and resolved behavior press/release events to a custom ZMK Studio RPC subsystem.
Its web UI reuses the official ZMK Studio keymap and behavior APIs for layout and metadata, then combines them with the custom live event stream.

## Summary

- **Firmware**: Custom Studio RPC handler in `src/studio/input_stream_handler.c`
- **Protocol**: Protobuf definition in `proto/zmk/input_stream/input_stream.proto`
- **Web UI**: React + TypeScript app in `web/`
- **Tests**: Native sim tests in `tests/` and build verification in `tests/zmk-config/`

## Module User Guide

1. Add the module to your `config/west.yml`. This module requires a patched ZMK with the custom Studio RPC protocol support.

   ```yml
   manifest:
       remotes:
           - name: cormoran
             url-base: https://github.com/cormoran
       projects:
           - name: zmk-feature-input-stream
             remote: cormoran
             revision: main
           - name: zmk
             remote: cormoran
             revision: main+custom-studio-protocol
             import:
                 file: app/west.yml
   ```

2. Enable the feature in your keyboard config.

   ```conf
   CONFIG_ZMK_INPUT_STREAM=y
   CONFIG_ZMK_STUDIO=y
   CONFIG_ZMK_INPUT_STREAM_STUDIO_RPC=y
   ```

3. Open the custom subsystem UI from ZMK Studio, or deploy the web app from this repository and connect to the device.

## What the UI Shows

- The current physical layout with key bindings from the default layer
- Live pressed key highlighting from raw key events
- Active behavior press/release events
- Behavior history in a terminal-like log
- A **Disturb host** toggle that temporarily keeps input inside Studio until the device restarts

## Web UI

See [web/README.md](./web/README.md) for local web development.

### Publishing Web UI

- **GitHub Pages**: Run `Actions > Test and Build Web UI`
- **Cloudflare Workers (PR previews)**: Configure `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID`

## Module Development Guide

### Setup for running test

#### Option0: Dev container (recommended)

Open this repository in VS Code with the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers).
The container automatically initializes the west workspace using the isolated layout.

#### Option1: west workspace directory layout

```bash
mkdir west-workspace
cd west-workspace
west init -l . --mf west/west-test-workspace.yml
west update --narrow
west zephyr-export
```

#### Option2: isolated directory layout

```bash
git clone <this repository>
cd <cloned directory>
west init -l west --mf west-test-isolated.yml
west update --narrow
west zephyr-export
```

### Pre-commit

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

### Running Test

```bash
python3 -m unittest
west zmk-build tests/zmk-config
west zmk-test tests -m .
cd web && npm test
```

### Sync changes from template

Run `Actions > Sync Changes in Template > Run workflow` to pull upstream template updates.

### Coding agent on actions

- Mention `@copilot`
- Setup `ANTHROPIC_API_KEY` and mention `@claude`
