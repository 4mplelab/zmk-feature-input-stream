import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { setupZMKMocks } from "@cormoran/zmk-studio-react-hook/testing";
import App from "../src/App";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn().mockResolvedValue({}),
}));

jest.mock("@zmkfirmware/zmk-studio-ts-client/transport/serial", () => ({
  connect: jest.fn(),
}));

describe("App Component", () => {
  describe("Basic Rendering", () => {
    it("should render the application header", () => {
      render(<App />);
      expect(screen.getAllByText(/ZMK Input Stream/i).length).toBeGreaterThan(
        0
      );
      expect(
        screen.getAllByText(/Live key visualizer/i).length
      ).toBeGreaterThan(0);
    });

    it("should render connection button when disconnected", () => {
      render(<App />);
      expect(screen.getByText(/Connect Serial/i)).toBeInTheDocument();
    });

    it("should render footer", () => {
      render(<App />);
      expect(
        screen.getAllByText(/Live key visualizer/i).length
      ).toBeGreaterThan(0);
    });
  });

  describe("Connection Flow", () => {
    let mocks: ReturnType<typeof setupZMKMocks>;

    beforeEach(() => {
      mocks = setupZMKMocks();
    });

    it("should connect to device when connect button is clicked", async () => {
      mocks.mockSuccessfulConnection({
        deviceName: "Test Keyboard",
        subsystems: ["zmk__input_stream"],
      });

      const { connect: serial_connect } =
        await import("@zmkfirmware/zmk-studio-ts-client/transport/serial");
      (serial_connect as jest.Mock).mockResolvedValue(mocks.mockTransport);

      render(<App />);
      expect(screen.getByText(/Connect Serial/i)).toBeInTheDocument();

      const user = userEvent.setup();
      await user.click(screen.getByText(/Connect Serial/i));

      // "Connected to:" and device name are in separate elements — match via textContent
      await waitFor(
        () => {
          const found = screen.getAllByText(
            (_, el) =>
              el?.textContent
                ?.toLowerCase()
                .includes("connected to: test keyboard") ?? false
          );
          expect(found.length).toBeGreaterThan(0);
        },
        { timeout: 3000 }
      );

      expect(screen.getByText(/Disconnect/i)).toBeInTheDocument();
    });
  });
});
