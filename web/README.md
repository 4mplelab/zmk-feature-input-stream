# Input Stream Web UI

This web app connects to the `zmk__input_stream` custom Studio RPC subsystem.
It renders the active physical layout, highlights pressed keys, shows active behaviors, and lets you toggle whether input should also be sent to the connected host.

## Quick Start

```bash
npm install
npm run generate
npm run dev
```

## Commands

```bash
npm run generate
npm test
npm run build
npm run lint
```

## Protocol

The app uses the protobuf schema in `../proto/zmk/input_stream/input_stream.proto` for live notifications and the official ZMK Studio `keymap` / `behaviors` RPC APIs for layout and binding metadata.
Generated TypeScript files are written to `src/proto/`.

## Notes

- The UI expects the firmware to expose the `zmk__input_stream` custom subsystem.
- The **Disturb host** toggle is off by default and resets after a reboot.
- Behavior notifications only send IDs; names and layout metadata are loaded from the official Studio APIs.
