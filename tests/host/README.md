The IK test vector is from rweather/noise-c (cfe25410979a87391bb9ac8d4d4bef64e9f268c6),
tests/vector/cacophony.txt. All keys in this fixture are public test values.
The test compiles the exact subset used by the firmware and checks both
handshake messages and bidirectional transport ciphertext against the vector.
