set(NOISE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/noise-c)
target_include_directories(app PRIVATE ${NOISE}/include ${NOISE}/src ${NOISE}/src/protocol)
target_sources(app PRIVATE
  ${NOISE}/src/protocol/cipherstate.c
  ${NOISE}/src/protocol/dhstate.c
  ${NOISE}/src/protocol/errors.c
  ${NOISE}/src/protocol/handshakestate.c
  ${NOISE}/src/protocol/hashstate.c
  ${NOISE}/src/protocol/names.c
  ${NOISE}/src/protocol/patterns.c
  ${NOISE}/src/protocol/symmetricstate.c
  ${NOISE}/src/protocol/util.c
  ${NOISE}/src/backend/ref/cipher-chachapoly.c
  ${NOISE}/src/backend/ref/dh-curve25519.c
  ${NOISE}/src/backend/ref/hash-sha256.c
  ${NOISE}/src/crypto/chacha/chacha.c
  ${NOISE}/src/crypto/donna/poly1305-donna.c
  ${NOISE}/src/crypto/sha2/sha256.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/security/noise_port.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/security/secure.c)
