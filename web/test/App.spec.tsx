import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { setupZMKMocks } from "@cormoran/zmk-studio-react-hook/testing";
import { ZMKCustomSubsystem } from "@cormoran/zmk-studio-react-hook";
import * as zmkStudioClient from "@zmkfirmware/zmk-studio-ts-client";
import App from "../src/App";
import { Response } from "../src/proto/zmk/input_stream/input_stream";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn(),
}));

jest.mock("@zmkfirmware/zmk-studio-ts-client/transport/serial", () => ({
  connect: jest.fn(),
}));

function encodeStateResponse() {
  return Response.encode({
    state: {
      disturbHost: false,
    },
  }).finish();
}

function mockOfficialStudioMetadata() {
  jest.spyOn(zmkStudioClient, "call_rpc").mockImplementation(
    async (
      _connection,
      request: {
        keymap?: { getKeymap?: boolean; getPhysicalLayouts?: boolean };
        behaviors?: {
          listAllBehaviors?: boolean;
          getBehaviorDetails?: { behaviorId: number };
        };
      }
    ) => {
      if (request.keymap?.getPhysicalLayouts) {
        return {
          keymap: {
            getPhysicalLayouts: {
              activeLayoutIndex: 0,
              layouts: [
                {
                  name: "Demo Layout",
                  keys: [
                    {
                      width: 100,
                      height: 100,
                      x: 0,
                      y: 0,
                      r: 0,
                      rx: 0,
                      ry: 0,
                    },
                  ],
                },
              ],
            },
          },
        };
      }

      if (request.keymap?.getKeymap) {
        return {
          keymap: {
            getKeymap: {
              layers: [
                {
                  id: 0,
                  name: "Base",
                  bindings: [{ behaviorId: 1, param1: 0, param2: 0 }],
                },
              ],
              availableLayers: 0,
              maxLayerNameLength: 32,
            },
          },
        };
      }

      if (request.behaviors?.listAllBehaviors) {
        return { behaviors: { listAllBehaviors: { behaviors: [1] } } };
      }

      if (request.behaviors?.getBehaviorDetails) {
        return {
          behaviors: {
            getBehaviorDetails: {
              id: request.behaviors.getBehaviorDetails.behaviorId,
              displayName: "&kp",
              metadata: [],
            },
          },
        };
      }

      throw new Error("Unexpected Studio RPC request");
    }
  );
}

describe("App Component", () => {
  beforeEach(() => {
    jest
      .spyOn(ZMKCustomSubsystem.prototype, "callRPC")
      .mockResolvedValue(encodeStateResponse());
    mockOfficialStudioMetadata();
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("renders the disconnected state", () => {
    render(<App />);

    expect(screen.getByText(/Input Stream/i)).toBeInTheDocument();
    expect(screen.getByText(/Connect Serial/i)).toBeInTheDocument();
  });

  it("connects and loads the input stream panel", async () => {
    const mocks = setupZMKMocks();
    mocks.mockSuccessfulConnection({
      deviceName: "Test Keyboard",
      subsystems: ["zmk__input_stream"],
    });

    const { connect } =
      await import("@zmkfirmware/zmk-studio-ts-client/transport/serial");
    (connect as jest.Mock).mockResolvedValue(mocks.mockTransport);

    render(<App />);

    await userEvent.click(screen.getByText(/Connect Serial/i));

    await waitFor(() => {
      expect(screen.getByText(/Test Keyboard/i)).toBeInTheDocument();
      expect(screen.getByText(/Demo Layout/i)).toBeInTheDocument();
      expect(screen.getByLabelText(/Disturb host/i)).toBeInTheDocument();
    });
  });
});
