import { act, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { ZMKCustomSubsystem } from "@cormoran/zmk-studio-react-hook";
import { InputStreamSection, SUBSYSTEM_IDENTIFIER } from "../src/App";
import {
  Notification,
  Response,
} from "../src/proto/zmk/input_stream/input_stream";

function encodeStateResponse(disturbHost = false) {
  return Response.encode({
    state: {
      disturbHost,
      layoutName: "Custom Layout",
      keys: [
        {
          position: 0,
          width: 100,
          height: 100,
          x: 0,
          y: 0,
          r: 0,
          rx: 0,
          ry: 0,
          behaviorId: 7,
          param1: 4,
          param2: 0,
        },
      ],
      behaviors: [{ id: 7, displayName: "&kp" }],
    },
  }).finish();
}

describe("InputStreamSection", () => {
  beforeEach(() => {
    jest
      .spyOn(ZMKCustomSubsystem.prototype, "callRPC")
      .mockResolvedValue(encodeStateResponse());
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("renders state from the firmware and subscribes to notifications", async () => {
    const mockZMKApp = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });

    render(
      <ZMKAppProvider value={mockZMKApp}>
        <InputStreamSection />
      </ZMKAppProvider>
    );

    await waitFor(() => {
      expect(screen.getByText(/Custom Layout/i)).toBeInTheDocument();
      expect(screen.getByText(/&kp 4/i)).toBeInTheDocument();
    });

    expect(mockZMKApp.onNotification).toHaveBeenCalledWith(
      expect.objectContaining({ type: "custom", subsystemIndex: 0 })
    );
  });

  it("updates active behavior and history from notifications", async () => {
    const mockZMKApp = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });

    render(
      <ZMKAppProvider value={mockZMKApp}>
        <InputStreamSection />
      </ZMKAppProvider>
    );

    await waitFor(() => {
      expect(screen.getByText(/Custom Layout/i)).toBeInTheDocument();
    });

    const subscription = (mockZMKApp.onNotification as jest.Mock).mock
      .calls[0][0];
    act(() => {
      subscription.callback({
        subsystemIndex: 0,
        payload: Notification.encode({
          keyEvent: { position: 0, pressed: true },
        }).finish(),
      });
      subscription.callback({
        subsystemIndex: 0,
        payload: Notification.encode({
          behaviorEvent: {
            behaviorId: 7,
            layer: 0,
            position: 0,
            param1: 4,
            param2: 0,
            pressed: true,
          },
        }).finish(),
      });
    });

    await waitFor(() => {
      expect(screen.getByText(/key 0 · layer 0/i)).toBeInTheDocument();
      expect(screen.getByLabelText(/Behavior history/i)).toHaveTextContent(
        /press/
      );
    });
  });

  it("toggles disturb host", async () => {
    (ZMKCustomSubsystem.prototype.callRPC as jest.Mock)
      .mockResolvedValueOnce(encodeStateResponse(false))
      .mockResolvedValueOnce(encodeStateResponse(true));

    const mockZMKApp = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });

    render(
      <ZMKAppProvider value={mockZMKApp}>
        <InputStreamSection />
      </ZMKAppProvider>
    );

    const checkbox = await screen.findByLabelText(/Disturb host/i);
    expect(checkbox).not.toBeChecked();

    await userEvent.click(checkbox);

    await waitFor(() => {
      expect(screen.getByLabelText(/Disturb host/i)).toBeChecked();
    });
  });

  it("shows a warning when the subsystem is missing", () => {
    const mockZMKApp = createConnectedMockZMKApp({
      subsystems: [],
    });

    render(
      <ZMKAppProvider value={mockZMKApp}>
        <InputStreamSection />
      </ZMKAppProvider>
    );

    expect(
      screen.getByText(/Subsystem "zmk__input_stream" not found/i)
    ).toBeInTheDocument();
  });

  it("renders nothing without the app context", () => {
    const { container } = render(<InputStreamSection />);
    expect(container.firstChild).toBeNull();
  });
});
