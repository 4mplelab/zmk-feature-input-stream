// jest-dom adds custom jest matchers for asserting on DOM nodes.
import "@testing-library/jest-dom";
import { TextDecoder, TextEncoder } from "util";

Object.defineProperty(globalThis, "TextEncoder", {
  value: TextEncoder,
  writable: true,
});

Object.defineProperty(globalThis, "TextDecoder", {
  value: TextDecoder,
  writable: true,
});
