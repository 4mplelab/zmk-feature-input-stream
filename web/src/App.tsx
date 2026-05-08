import { useCallback, useContext, useEffect, useState } from "react";
import "./App.css";
import { connect as serial_connect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import type {
  Keymap,
  PhysicalLayouts,
  BehaviorBinding,
} from "@zmkfirmware/zmk-studio-ts-client/keymap";
import type { GetBehaviorDetailsResponse } from "@zmkfirmware/zmk-studio-ts-client/behaviors";
import type { CustomNotification } from "@zmkfirmware/zmk-studio-ts-client/custom";
import {
  ZMKConnection,
  ZMKCustomSubsystem,
  ZMKAppContext,
} from "@cormoran/zmk-studio-react-hook";
import {
  Request,
  Response,
  Notification,
} from "./proto/zmk/input_stream/input_stream";

export const SUBSYSTEM_IDENTIFIER = "zmk__input_stream";

// ─── HID keyboard usage → display label ───────────────────────────────────
const HID_KEY_NAMES: Record<number, string> = {
  0x04: "A",
  0x05: "B",
  0x06: "C",
  0x07: "D",
  0x08: "E",
  0x09: "F",
  0x0a: "G",
  0x0b: "H",
  0x0c: "I",
  0x0d: "J",
  0x0e: "K",
  0x0f: "L",
  0x10: "M",
  0x11: "N",
  0x12: "O",
  0x13: "P",
  0x14: "Q",
  0x15: "R",
  0x16: "S",
  0x17: "T",
  0x18: "U",
  0x19: "V",
  0x1a: "W",
  0x1b: "X",
  0x1c: "Y",
  0x1d: "Z",
  0x1e: "1",
  0x1f: "2",
  0x20: "3",
  0x21: "4",
  0x22: "5",
  0x23: "6",
  0x24: "7",
  0x25: "8",
  0x26: "9",
  0x27: "0",
  0x28: "Ent",
  0x29: "Esc",
  0x2a: "Bsp",
  0x2b: "Tab",
  0x2c: "Spc",
  0x2d: "-",
  0x2e: "=",
  0x2f: "[",
  0x30: "]",
  0x31: "\\",
  0x33: ";",
  0x34: "'",
  0x35: "`",
  0x36: ",",
  0x37: ".",
  0x38: "/",
  0x39: "Caps",
  0x3a: "F1",
  0x3b: "F2",
  0x3c: "F3",
  0x3d: "F4",
  0x3e: "F5",
  0x3f: "F6",
  0x40: "F7",
  0x41: "F8",
  0x42: "F9",
  0x43: "F10",
  0x44: "F11",
  0x45: "F12",
  0x46: "PrtSc",
  0x47: "ScrLk",
  0x48: "Pause",
  0x49: "Ins",
  0x4a: "Home",
  0x4b: "PgUp",
  0x4c: "Del",
  0x4d: "End",
  0x4e: "PgDn",
  0x4f: "→",
  0x50: "←",
  0x51: "↓",
  0x52: "↑",
  0xe0: "LCtl",
  0xe1: "LSft",
  0xe2: "LAlt",
  0xe3: "LGui",
  0xe4: "RCtl",
  0xe5: "RSft",
  0xe6: "RAlt",
  0xe7: "RGui",
};

function hidUsageToLabel(usage: number): string {
  const page = (usage >>> 16) & 0xff;
  const id = usage & 0xffff;
  if (page === 0x07 || page === 0x00) {
    return HID_KEY_NAMES[id] ?? `K${id.toString(16).toUpperCase()}`;
  }
  if (page === 0x0c) {
    return `M${id.toString(16).toUpperCase()}`;
  }
  return `0x${usage.toString(16).toUpperCase()}`;
}

// ─── Audio ────────────────────────────────────────────────────────────────
let audioCtx: AudioContext | null = null;

function playBeep(position: number) {
  try {
    if (!audioCtx) audioCtx = new AudioContext();
    if (audioCtx.state === "suspended") audioCtx.resume();
    const semitone = position % 24;
    const freq = 261.63 * Math.pow(2, semitone / 12);
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.connect(gain);
    gain.connect(audioCtx.destination);
    osc.type = "sine";
    osc.frequency.value = freq;
    gain.gain.setValueAtTime(0.25, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.08);
    osc.start(audioCtx.currentTime);
    osc.stop(audioCtx.currentTime + 0.08);
  } catch {
    // audio unavailable
  }
}

// ─── Types ────────────────────────────────────────────────────────────────
interface BehaviorEvent {
  position: number;
  pressed: boolean;
  behaviorName: string;
  param1: number;
  timestamp: number;
}

// ─── App ──────────────────────────────────────────────────────────────────
function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>ZMK Input Stream</h1>
        <p>Live key visualizer</p>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card">
            <h2>Device Connection</h2>
            {isLoading && <p>Connecting...</p>}
            {error && (
              <div className="error-message">
                <p>{error}</p>
              </div>
            )}
            {!isLoading && (
              <button
                className="btn btn-primary"
                onClick={() => connect(serial_connect)}
              >
                Connect Serial
              </button>
            )}
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card">
              <h2>Device Connection</h2>
              <div className="device-info">
                <span>
                  Connected to: <strong>{deviceName}</strong>
                </span>
              </div>
              <button className="btn btn-secondary" onClick={disconnect}>
                Disconnect
              </button>
            </section>
            <InputStreamSection />
          </>
        )}
      />

      <footer className="app-footer">
        <p>
          <strong>ZMK Input Stream</strong> — Live key visualizer
        </p>
      </footer>
    </div>
  );
}

// ─── Main section ─────────────────────────────────────────────────────────
export function InputStreamSection() {
  const zmkApp = useContext(ZMKAppContext);
  const connection = zmkApp?.state.connection ?? null;
  const subsystem = zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER) ?? null;

  const [keymap, setKeymap] = useState<Keymap | null>(null);
  const [physicalLayouts, setPhysicalLayouts] =
    useState<PhysicalLayouts | null>(null);
  const [behaviorMap, setBehaviorMap] = useState<
    Map<number, GetBehaviorDetailsResponse>
  >(new Map());
  const [loadError, setLoadError] = useState<string | null>(null);
  const [isLoading, setIsLoading] = useState(false);

  const [streamingEnabled, setStreamingEnabled] = useState(false);
  const [streamError, setStreamError] = useState<string | null>(null);
  const [activeLayer, setActiveLayer] = useState(0);

  const [pressedKeys, setPressedKeys] = useState<Set<number>>(new Set());
  const [everPressedKeys, setEverPressedKeys] = useState<Set<number>>(
    new Set()
  );
  const [behaviorHistory, setBehaviorHistory] = useState<BehaviorEvent[]>([]);
  const [jsKeycodes, setJsKeycodes] = useState<string[]>([]);
  const [currentJsKey, setCurrentJsKey] = useState<string | null>(null);

  // Load keymap + behaviors when connection is established
  useEffect(() => {
    if (!connection) return;
    let cancelled = false;
    const load = async () => {
      setIsLoading(true);
      setLoadError(null);
      try {
        const [layoutResp, keymapResp, behaviorListResp] = await Promise.all([
          call_rpc(connection, { keymap: { getPhysicalLayouts: true } }),
          call_rpc(connection, { keymap: { getKeymap: true } }),
          call_rpc(connection, { behaviors: { listAllBehaviors: true } }),
        ]);
        if (cancelled) return;

        if (layoutResp.keymap?.getPhysicalLayouts)
          setPhysicalLayouts(layoutResp.keymap.getPhysicalLayouts);
        if (keymapResp.keymap?.getKeymap)
          setKeymap(keymapResp.keymap.getKeymap);

        const ids =
          behaviorListResp.behaviors?.listAllBehaviors?.behaviors ?? [];
        const detailResults = await Promise.all(
          ids.map((id) =>
            call_rpc(connection, {
              behaviors: { getBehaviorDetails: { behaviorId: id } },
            })
          )
        );
        if (cancelled) return;
        const bMap = new Map<number, GetBehaviorDetailsResponse>();
        for (const r of detailResults) {
          const d = r.behaviors?.getBehaviorDetails;
          if (d) bMap.set(d.id, d);
        }
        setBehaviorMap(bMap);
      } catch (e) {
        if (!cancelled)
          setLoadError(
            e instanceof Error ? e.message : "Failed to load layout"
          );
      } finally {
        if (!cancelled) setIsLoading(false);
      }
    };
    void load();
    return () => {
      cancelled = true;
    };
  }, [connection]);

  // Subscribe to custom notifications
  useEffect(() => {
    if (!zmkApp || !subsystem) return;
    const unsubscribe = zmkApp.onNotification({
      type: "custom",
      subsystemIndex: subsystem.index,
      callback: (notif: CustomNotification) => {
        try {
          const decoded = Notification.decode(notif.payload);
          if (decoded.keyEvent) {
            const { position, pressed, behaviorId, param1 } = decoded.keyEvent;
            if (pressed) playBeep(position);
            setPressedKeys((prev) => {
              const next = new Set(prev);
              if (pressed) next.add(position);
              else next.delete(position);
              return next;
            });
            if (pressed) {
              setEverPressedKeys((prev) => new Set([...prev, position]));
              setBehaviorHistory((prev) => {
                const bDet = behaviorMap.get(behaviorId);
                const name = bDet?.displayName ?? `#${behaviorId}`;
                const entry: BehaviorEvent = {
                  position,
                  pressed,
                  behaviorName: name,
                  param1,
                  timestamp: Date.now(),
                };
                return [entry, ...prev].slice(0, 20);
              });
            }
          }
          if (decoded.layerChange) {
            setActiveLayer(decoded.layerChange.layerIndex);
          }
        } catch {
          // ignore decode errors
        }
      },
    });
    return unsubscribe;
  }, [zmkApp, subsystem, behaviorMap]);

  // JS keycode capture
  useEffect(() => {
    const pressedSet = new Set<string>();
    const onKeyDown = (e: KeyboardEvent) => {
      const label = e.key.length === 1 ? e.key.toUpperCase() : e.key;
      if (pressedSet.has(label)) return;
      pressedSet.add(label);
      setCurrentJsKey(label);
      setJsKeycodes((prev) => [label, ...prev].slice(0, 20));
    };
    const onKeyUp = (e: KeyboardEvent) => {
      const label = e.key.length === 1 ? e.key.toUpperCase() : e.key;
      pressedSet.delete(label);
      setCurrentJsKey((cur) => (cur === label ? null : cur));
    };
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
    };
  }, []);

  // Streaming toggle
  const toggleStreaming = useCallback(async () => {
    if (!connection || !subsystem) return;
    setStreamError(null);
    try {
      const service = new ZMKCustomSubsystem(connection, subsystem.index);
      const req = streamingEnabled
        ? Request.create({ disableStream: {} })
        : Request.create({ enableStream: {} });
      const payload = Request.encode(req).finish();
      const respPayload = await service.callRPC(payload);
      if (respPayload) {
        const resp = Response.decode(respPayload);
        if (resp.error) {
          setStreamError(resp.error.message);
          return;
        }
      }
      setStreamingEnabled((v) => !v);
    } catch (e) {
      setStreamError(e instanceof Error ? e.message : "RPC failed");
    }
  }, [connection, subsystem, streamingEnabled]);

  if (!zmkApp) return null;

  if (!subsystem) {
    return (
      <section className="card">
        <div className="warning-message">
          <p>
            Subsystem &quot;{SUBSYSTEM_IDENTIFIER}&quot; not found. Make sure
            your firmware includes the input stream module.
          </p>
        </div>
      </section>
    );
  }

  const layout =
    physicalLayouts?.layouts[physicalLayouts.activeLayoutIndex] ?? null;
  const activeLayerData =
    keymap?.layers[activeLayer] ?? keymap?.layers[0] ?? null;

  return (
    <>
      {/* Streaming controls */}
      <section className="card">
        <div className="stream-controls">
          <div className="stream-status">
            <span
              className={`stream-indicator ${streamingEnabled ? "active" : ""}`}
            />
            <span>{streamingEnabled ? "Streaming" : "Stream off"}</span>
            {activeLayer !== undefined && (
              <span className="layer-badge">Layer {activeLayer}</span>
            )}
          </div>
          <button
            className={`btn ${streamingEnabled ? "btn-danger" : "btn-primary"}`}
            onClick={toggleStreaming}
          >
            {streamingEnabled ? "Disable Stream" : "Enable Stream"}
          </button>
        </div>
        {streamError && <p className="error-text">{streamError}</p>}
        {isLoading && <p className="hint-text">Loading layout…</p>}
        {loadError && <p className="error-text">Layout error: {loadError}</p>}
      </section>

      {/* Keyboard layout */}
      {layout && (
        <section className="card">
          <h2>Keyboard Layout</h2>
          <KeyboardLayoutView
            layout={layout}
            bindings={activeLayerData?.bindings ?? []}
            behaviorMap={behaviorMap}
            pressedKeys={pressedKeys}
            everPressedKeys={everPressedKeys}
          />
        </section>
      )}

      {/* History panels */}
      <section className="card history-section">
        <div className="history-panels">
          <BehaviorHistoryPanel
            history={behaviorHistory}
            behaviorMap={behaviorMap}
          />
          <JsKeycodePanel current={currentJsKey} history={jsKeycodes} />
        </div>
      </section>
    </>
  );
}

// ─── Keyboard layout view ─────────────────────────────────────────────────
const SCALE = 0.48; // px per "100th-of-U" unit

interface KeyboardLayoutViewProps {
  layout: PhysicalLayouts["layouts"][number];
  bindings: BehaviorBinding[];
  behaviorMap: Map<number, GetBehaviorDetailsResponse>;
  pressedKeys: Set<number>;
  everPressedKeys: Set<number>;
}

function KeyboardLayoutView({
  layout,
  bindings,
  behaviorMap,
  pressedKeys,
  everPressedKeys,
}: KeyboardLayoutViewProps) {
  const maxX = layout.keys.reduce((m, k) => Math.max(m, k.x + k.width), 0);
  const maxY = layout.keys.reduce((m, k) => Math.max(m, k.y + k.height), 0);

  return (
    <div
      className="keyboard-layout"
      style={{ width: maxX * SCALE, height: maxY * SCALE }}
    >
      {layout.keys.map((key, i) => {
        const binding = bindings[i];
        const label = binding ? getBindingLabel(binding, behaviorMap) : "";
        const pressed = pressedKeys.has(i);
        const everPressed = everPressedKeys.has(i);
        return (
          <KeyCap
            key={i}
            x={key.x * SCALE}
            y={key.y * SCALE}
            width={key.width * SCALE}
            height={key.height * SCALE}
            r={key.r / 100}
            rx={key.rx * SCALE}
            ry={key.ry * SCALE}
            label={label}
            pressed={pressed}
            everPressed={everPressed}
          />
        );
      })}
    </div>
  );
}

function getBindingLabel(
  binding: BehaviorBinding,
  behaviorMap: Map<number, GetBehaviorDetailsResponse>
): string {
  const det = behaviorMap.get(binding.behaviorId);
  if (!det) return "";
  const name = det.displayName.toLowerCase();
  if (name === "key press" || name === "kp") {
    return hidUsageToLabel(binding.param1);
  }
  return "";
}

interface KeyCapProps {
  x: number;
  y: number;
  width: number;
  height: number;
  r: number;
  rx: number;
  ry: number;
  label: string;
  pressed: boolean;
  everPressed: boolean;
}

function KeyCap({
  x,
  y,
  width,
  height,
  r,
  rx,
  ry,
  label,
  pressed,
  everPressed,
}: KeyCapProps) {
  const transform = r !== 0 ? `rotate(${r}deg)` : undefined;
  const transformOrigin = r !== 0 ? `${rx}px ${ry}px` : undefined;

  return (
    <div
      className={`keycap ${pressed ? "keycap-pressed" : ""} ${everPressed ? "keycap-ever-pressed" : ""}`}
      style={{ left: x, top: y, width, height, transform, transformOrigin }}
    >
      <span className="keycap-label">{label}</span>
      {everPressed && <span className="keycap-dot" />}
    </div>
  );
}

// ─── Behavior history panel ───────────────────────────────────────────────
function BehaviorHistoryPanel({
  history,
  behaviorMap,
}: {
  history: BehaviorEvent[];
  behaviorMap: Map<number, GetBehaviorDetailsResponse>;
}) {
  return (
    <div className="history-panel">
      <h3>Behavior Events</h3>
      {history.length === 0 ? (
        <p className="hint-text">No events yet</p>
      ) : (
        <ul className="history-list">
          {history.map((ev, i) => {
            const det = behaviorMap.get(
              history.find((h) => h.behaviorName === ev.behaviorName)
                ? ([...behaviorMap.values()].find(
                    (d) => d.displayName === ev.behaviorName
                  )?.id ?? -1)
                : -1
            );
            const isKp =
              det?.displayName.toLowerCase() === "key press" ||
              det?.displayName.toLowerCase() === "kp";
            const paramStr = isKp ? hidUsageToLabel(ev.param1) : "";
            return (
              <li key={i} className="history-item">
                <span className="history-pos">#{ev.position}</span>
                <span className="history-name">{ev.behaviorName}</span>
                {paramStr && <span className="history-param">{paramStr}</span>}
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}

// ─── JS keycode panel ─────────────────────────────────────────────────────
function JsKeycodePanel({
  current,
  history,
}: {
  current: string | null;
  history: string[];
}) {
  return (
    <div className="history-panel">
      <h3>JS Key Events</h3>
      {current && <div className="current-key">{current}</div>}
      {history.length === 0 ? (
        <p className="hint-text">Press a key to capture</p>
      ) : (
        <ul className="history-list">
          {history.map((k, i) => (
            <li
              key={i}
              className={`history-item ${i === 0 && current === k ? "history-item-active" : ""}`}
            >
              {k}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

export default App;
