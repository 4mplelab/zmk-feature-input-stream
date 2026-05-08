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

describe("InputStreamSection Component", () => {
  describe("With Subsystem", () => {
    it("should render streaming controls when subsystem is found", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <InputStreamSection />
        </ZMKAppProvider>
      );

      expect(screen.getByText(/Enable Stream/i)).toBeInTheDocument();
    });
  });

  describe("Without Subsystem", () => {
    it("should show warning when subsystem is not found", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
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
  });

  describe("Without ZMKAppContext", () => {
    it("should not render when ZMKAppContext is not provided", () => {
      const { container } = render(<InputStreamSection />);
      expect(container.firstChild).toBeNull();
    });
  });
});
