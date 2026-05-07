import {
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
} from "react";
import "./App.css";
import {
  call_rpc,
  type RpcConnection,
} from "@zmkfirmware/zmk-studio-ts-client";
import { connect as serialConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import type {
  BehaviorBinding,
  KeyPhysicalAttrs,
  Keymap,
  PhysicalLayouts,
} from "@zmkfirmware/zmk-studio-ts-client/keymap";
import {
  ZMKConnection,
  ZMKCustomSubsystem,
  ZMKAppContext,
} from "@cormoran/zmk-studio-react-hook";
import {
  BehaviorEvent,
  Notification,
  Request,
  Response,
  StateResponse,
} from "./proto/zmk/input_stream/input_stream";

export const SUBSYSTEM_IDENTIFIER = "zmk__input_stream";
const MAX_HISTORY = 32;
const KEY_UNIT = 0.72;

type ActiveBehavior = BehaviorEvent & { id: string };
type HistoryEntry = { id: string; text: string };
type KeyDisplayInfo = KeyPhysicalAttrs & {
  position: number;
  behaviorId: number;
  param1: number;
  param2: number;
};
type LoadedInputStreamState = {
  disturbHost: boolean;
  layoutName: string;
  keys: KeyDisplayInfo[];
  behaviors: Map<number, string>;
};

function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>⌨️ Input Stream</h1>
        <p>Live key and behavior visualization for ZMK Studio.</p>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card connection-card">
            <h2>Device Connection</h2>
            {isLoading && <p>Connecting…</p>}
            {error && <p className="error-message">{error}</p>}
            {!isLoading && (
              <button
                className="btn btn-primary"
                onClick={() => connect(serialConnect)}
              >
                Connect Serial
              </button>
            )}
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card connection-card">
              <div>
                <h2>{deviceName}</h2>
                <p>Connected and listening for input stream notifications.</p>
              </div>
              <button className="btn btn-secondary" onClick={disconnect}>
                Disconnect
              </button>
            </section>

            <InputStreamSection />
          </>
        )}
      />
    </div>
  );
}

function decodeStateResponse(payload: Uint8Array | null): StateResponse {
  if (!payload) {
    throw new Error("Missing response payload");
  }

  const response = Response.decode(payload);
  if (response.error) {
    throw new Error(response.error.message);
  }
  if (!response.state) {
    throw new Error("Missing state response");
  }

  return response.state;
}

function decodeKeymapMetadata(
  physicalLayouts: PhysicalLayouts,
  keymap: Keymap
): Pick<LoadedInputStreamState, "layoutName" | "keys"> {
  const activeLayout =
    physicalLayouts.layouts[physicalLayouts.activeLayoutIndex];
  if (!activeLayout) {
    throw new Error("Missing active physical layout");
  }

  const baseLayer = keymap.layers[0];
  if (!baseLayer) {
    throw new Error("Missing keymap layer data");
  }

  const keyCount = Math.min(
    activeLayout.keys.length,
    baseLayer.bindings.length
  );
  const keys = activeLayout.keys.slice(0, keyCount).map((key, position) => {
    const binding = baseLayer.bindings[position] ?? createEmptyBinding();
    return {
      ...key,
      position,
      behaviorId: binding.behaviorId,
      param1: binding.param1,
      param2: binding.param2,
    };
  });

  return {
    layoutName: activeLayout.name || "Layout",
    keys,
  };
}

function createEmptyBinding(): BehaviorBinding {
  return {
    behaviorId: 0,
    param1: 0,
    param2: 0,
  };
}

async function loadBehaviorNames(
  connection: RpcConnection
): Promise<Map<number, string>> {
  const response = await call_rpc(connection, {
    behaviors: { listAllBehaviors: true },
  });
  const behaviorIds = response.behaviors?.listAllBehaviors?.behaviors ?? [];

  const detailResponses = await Promise.all(
    behaviorIds.map((behaviorId) =>
      call_rpc(connection, {
        behaviors: { getBehaviorDetails: { behaviorId } },
      })
    )
  );

  return new Map(
    detailResponses.flatMap((detailResponse) => {
      const details = detailResponse.behaviors?.getBehaviorDetails;
      return details ? [[details.id, details.displayName] as const] : [];
    })
  );
}

async function loadInputStreamState(
  connection: RpcConnection,
  service: ZMKCustomSubsystem
): Promise<LoadedInputStreamState> {
  const statePayload = Request.encode(
    Request.create({ getState: true })
  ).finish();
  const [state, keymapResponse, physicalLayoutsResponse, behaviors] =
    await Promise.all([
      service.callRPC(statePayload).then(decodeStateResponse),
      call_rpc(connection, { keymap: { getKeymap: true } }),
      call_rpc(connection, { keymap: { getPhysicalLayouts: true } }),
      loadBehaviorNames(connection),
    ]);

  const keymap = keymapResponse.keymap?.getKeymap;
  const physicalLayouts = physicalLayoutsResponse.keymap?.getPhysicalLayouts;
  if (!keymap || !physicalLayouts) {
    throw new Error("Missing official Studio keymap metadata");
  }

  return {
    disturbHost: state.disturbHost,
    ...decodeKeymapMetadata(physicalLayouts, keymap),
    behaviors,
  };
}

function formatBehaviorLabel(
  behaviorId: number,
  behaviors: Map<number, string>,
  param1?: number,
  param2?: number
): string {
  const name =
    behaviors.get(behaviorId) ??
    (behaviorId ? `Behavior ${behaviorId}` : "Unassigned");
  const params = [param1, param2].filter(
    (value) => value !== undefined && value !== 0
  );
  return params.length ? `${name} ${params.join(", ")}` : name;
}

function formatHistoryEntry(
  event: BehaviorEvent,
  behaviors: Map<number, string>
): string {
  const action = event.pressed ? "press" : "release";
  const label = formatBehaviorLabel(
    event.behaviorId,
    behaviors,
    event.param1,
    event.param2
  );
  return `${action.padEnd(7)} key ${event.position.toString().padStart(2, "0")}  layer ${event.layer}  ${label}`;
}

function buildKeyStyle(keys: KeyDisplayInfo[]) {
  const width = Math.max(...keys.map((key) => key.x + key.width), 100);
  const height = Math.max(...keys.map((key) => key.y + key.height), 100);
  return {
    "--layout-width": `${width * KEY_UNIT}px`,
    "--layout-height": `${height * KEY_UNIT}px`,
  } as CSSProperties;
}

export function InputStreamSection() {
  const zmkApp = useContext(ZMKAppContext);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [disturbHost, setDisturbHost] = useState(false);
  const [layoutName, setLayoutName] = useState("");
  const [keys, setKeys] = useState<KeyDisplayInfo[]>([]);
  const [behaviors, setBehaviors] = useState<Map<number, string>>(new Map());
  const behaviorNamesRef = useRef<Map<number, string>>(new Map());
  const [activePositions, setActivePositions] = useState<Set<number>>(
    new Set()
  );
  const [activeBehaviors, setActiveBehaviors] = useState<ActiveBehavior[]>([]);
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [isToggling, setIsToggling] = useState(false);

  const subsystem = zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER) ?? null;

  useEffect(() => {
    if (!zmkApp?.state.connection || !subsystem) {
      return;
    }

    const connection = zmkApp.state.connection;
    const service = new ZMKCustomSubsystem(connection, subsystem.index);
    let cancelled = false;

    const applyState = (state: LoadedInputStreamState) => {
      if (cancelled) {
        return;
      }

      setDisturbHost(state.disturbHost);
      setLayoutName(state.layoutName);
      setKeys(state.keys);
      behaviorNamesRef.current = state.behaviors;
      setBehaviors(state.behaviors);
      setError(null);
    };

    const loadState = async () => {
      setIsLoading(true);
      try {
        applyState(await loadInputStreamState(connection, service));
      } catch (loadError) {
        if (!cancelled) {
          setError(
            loadError instanceof Error
              ? loadError.message
              : "Failed to load state"
          );
        }
      } finally {
        if (!cancelled) {
          setIsLoading(false);
        }
      }
    };

    loadState();

    const unsubscribe = zmkApp.onNotification({
      type: "custom",
      subsystemIndex: subsystem.index,
      callback: (notification) => {
        const decoded = Notification.decode(notification.payload);
        const keyEvent = decoded.keyEvent;
        const behaviorEvent = decoded.behaviorEvent;

        if (keyEvent) {
          setActivePositions((previous) => {
            const next = new Set(previous);
            if (keyEvent.pressed) {
              next.add(keyEvent.position);
            } else {
              next.delete(keyEvent.position);
            }
            return next;
          });
        }

        if (behaviorEvent) {
          const entryId = `${behaviorEvent.position}:${behaviorEvent.layer}:${behaviorEvent.behaviorId}:${behaviorEvent.param1}:${behaviorEvent.param2}`;

          setActiveBehaviors((previous) => {
            const remaining = previous.filter((item) => item.id !== entryId);
            return behaviorEvent.pressed
              ? [...remaining, { ...behaviorEvent, id: entryId }]
              : remaining;
          });

          setHistory((previous) => {
            const next = [
              ...previous,
              {
                id: `${entryId}:${previous.length}`,
                text: formatHistoryEntry(
                  behaviorEvent,
                  behaviorNamesRef.current
                ),
              },
            ];
            return next.slice(-MAX_HISTORY);
          });
        }

        if (decoded.disturbHostChanged !== undefined) {
          setDisturbHost(decoded.disturbHostChanged);
        }
      },
    });

    return () => {
      cancelled = true;
      unsubscribe();
    };
  }, [subsystem, zmkApp]);

  const keyStyle = useMemo(() => buildKeyStyle(keys), [keys]);

  if (!zmkApp) {
    return null;
  }

  if (!subsystem) {
    return (
      <section className="card">
        <p className="warning-message">
          Subsystem "{SUBSYSTEM_IDENTIFIER}" not found. Make sure the firmware
          enables the input stream module.
        </p>
      </section>
    );
  }

  const toggleDisturbHost = async () => {
    if (!zmkApp.state.connection) {
      return;
    }

    setIsToggling(true);
    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );
      const payload = Request.encode(
        Request.create({ setDisturbHost: !disturbHost })
      ).finish();
      const state = decodeStateResponse(await service.callRPC(payload));
      setDisturbHost(state.disturbHost);
      setError(null);
    } catch (toggleError) {
      setError(
        toggleError instanceof Error
          ? toggleError.message
          : "Failed to update host output"
      );
    } finally {
      setIsToggling(false);
    }
  };

  return (
    <div className="input-stream-grid">
      <section className="card control-card">
        <div className="card-header">
          <div>
            <h2>Input Stream</h2>
            <p>{layoutName || "Loading layout"}</p>
          </div>
          <label className="toggle-row">
            <span>Disturb host</span>
            <input
              aria-label="Disturb host"
              checked={disturbHost}
              disabled={isToggling || isLoading}
              onChange={() => toggleDisturbHost()}
              type="checkbox"
            />
          </label>
        </div>
        <p className="helper-text">
          When enabled, key output stays inside Studio and is not sent to the
          connected host.
        </p>
        {error && <p className="error-message">{error}</p>}
      </section>

      <section className="card layout-card">
        <div className="card-header compact">
          <h2>Layout</h2>
          <span>{activePositions.size} key(s) pressed</span>
        </div>
        {isLoading ? (
          <p>Loading layout…</p>
        ) : keys.length ? (
          <div className="key-layout" style={keyStyle}>
            {keys.map((key) => (
              <div
                className={`keycap ${activePositions.has(key.position) ? "pressed" : ""}`}
                key={key.position}
                style={{
                  left: `${key.x * KEY_UNIT}px`,
                  top: `${key.y * KEY_UNIT}px`,
                  width: `${key.width * KEY_UNIT}px`,
                  height: `${key.height * KEY_UNIT}px`,
                }}
              >
                <span className="keycap-position">{key.position}</span>
                <span className="keycap-label">
                  {formatBehaviorLabel(
                    key.behaviorId,
                    behaviors,
                    key.param1,
                    key.param2
                  )}
                </span>
              </div>
            ))}
          </div>
        ) : (
          <p>No layout data available.</p>
        )}
      </section>

      <section className="card behavior-card">
        <div className="card-header compact">
          <h2>Active Behaviors</h2>
          <span>{activeBehaviors.length}</span>
        </div>
        {activeBehaviors.length ? (
          <ul className="active-behavior-list">
            {activeBehaviors.map((event) => (
              <li key={event.id}>
                {formatBehaviorLabel(
                  event.behaviorId,
                  behaviors,
                  event.param1,
                  event.param2
                )}
                <span>
                  key {event.position} · layer {event.layer}
                </span>
              </li>
            ))}
          </ul>
        ) : (
          <p className="muted-text">No active behaviors.</p>
        )}
      </section>

      <section className="card history-card">
        <div className="card-header compact">
          <h2>Behavior History</h2>
          <span>{history.length}</span>
        </div>
        <div aria-label="Behavior history" className="history-terminal">
          {history.length ? (
            history.map((entry) => <div key={entry.id}>{entry.text}</div>)
          ) : (
            <div className="muted-text">Waiting for behavior events…</div>
          )}
        </div>
      </section>
    </div>
  );
}

export default App;
