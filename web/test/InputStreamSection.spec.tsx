import { render, screen, waitFor, act } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { ZMKCustomSubsystem } from "@cormoran/zmk-studio-react-hook";
import { InputStreamSection, SUBSYSTEM_IDENTIFIER } from "../src/App";
import { ErrorConditions } from "@zmkfirmware/zmk-studio-ts-client/meta";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn().mockResolvedValue({}),
  MetaError: class MetaError extends Error {
    condition: number;
    constructor(condition: number) {
      super("meta error");
      this.condition = condition;
    }
  },
}));

function makeUnlockError() {
  const { MetaError } = jest.requireMock(
    "@zmkfirmware/zmk-studio-ts-client"
  ) as { MetaError: new (c: number) => Error };
  return new MetaError(ErrorConditions.UNLOCK_REQUIRED);
}

const PROPS = { deviceName: "Test Device", disconnect: jest.fn() };

function renderWithApp(subsystems: string[] = [SUBSYSTEM_IDENTIFIER]) {
  const mockZMKApp = createConnectedMockZMKApp({
    deviceName: "Test Device",
    subsystems,
  });
  return render(
    <ZMKAppProvider value={mockZMKApp}>
      <InputStreamSection {...PROPS} />
    </ZMKAppProvider>
  );
}

let callRPCSpy: jest.SpyInstance;

beforeEach(() => {
  // Default: stream enable/disable RPCs succeed
  callRPCSpy = jest
    .spyOn(ZMKCustomSubsystem.prototype, "callRPC")
    .mockResolvedValue(null);
});

afterEach(() => {
  callRPCSpy.mockRestore();
});

describe("InputStreamSection Component", () => {
  describe("With Subsystem", () => {
    it("renders device name and stream button", () => {
      renderWithApp();
      expect(screen.getByText("Test Device")).toBeInTheDocument();
      expect(
        screen.queryByText(/Enable Stream|Disable Stream|Cancel/i)
      ).toBeInTheDocument();
    });

    it("auto-starts streaming after loading completes", async () => {
      renderWithApp();
      await waitFor(() =>
        expect(screen.getByText(/Disable Stream/i)).toBeInTheDocument()
      );
    });

    it("shows unlock-pending info bar when UNLOCK_REQUIRED on auto-start", async () => {
      callRPCSpy.mockRejectedValue(makeUnlockError());
      renderWithApp();

      await waitFor(() =>
        expect(screen.getByText(/Studio is locked/i)).toBeInTheDocument()
      );
      expect(screen.getByText(/Cancel/i)).toBeInTheDocument();
    });

    it("dismissing unlock info bar shows Enable Stream and stops retry", async () => {
      callRPCSpy.mockRejectedValue(makeUnlockError());
      renderWithApp();

      await waitFor(() =>
        expect(screen.getByText(/Studio is locked/i)).toBeInTheDocument()
      );

      await act(async () => {
        const dismissBtn = document.querySelector(
          ".info-bar-dismiss"
        ) as HTMLButtonElement;
        await userEvent.click(dismissBtn);
      });

      expect(screen.queryByText(/Studio is locked/i)).not.toBeInTheDocument();
      expect(screen.getByText(/Enable Stream/i)).toBeInTheDocument();
    });
  });

  describe("Without Subsystem", () => {
    it("should show 'No subsystem' warning", () => {
      renderWithApp([]);
      expect(screen.getByText(/No subsystem/i)).toBeInTheDocument();
    });
  });

  describe("Without ZMKAppContext", () => {
    it("should not render when ZMKAppContext is not provided", () => {
      const { container } = render(<InputStreamSection {...PROPS} />);
      expect(container.firstChild).toBeNull();
    });
  });
});
