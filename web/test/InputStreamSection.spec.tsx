import { render, screen } from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { InputStreamSection, SUBSYSTEM_IDENTIFIER } from "../src/App";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn().mockResolvedValue({}),
}));

const PROPS = { deviceName: "Test Device", disconnect: jest.fn() };

describe("InputStreamSection Component", () => {
  describe("With Subsystem", () => {
    it("should render stream toggle and device name", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <InputStreamSection {...PROPS} />
        </ZMKAppProvider>
      );

      expect(screen.getByText(/Enable Stream/i)).toBeInTheDocument();
      expect(screen.getByText("Test Device")).toBeInTheDocument();
    });
  });

  describe("Without Subsystem", () => {
    it("should show 'No subsystem' warning", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <InputStreamSection {...PROPS} />
        </ZMKAppProvider>
      );

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
