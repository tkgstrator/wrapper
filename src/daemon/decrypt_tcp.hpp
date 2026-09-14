// WV2D TCP decrypt listener.
//
// gamdl >= 3.8.0 does FairPlay sample decryption over a dedicated TCP
// socket instead of HTTP. Its Rust extension (gamdl._ammuxer) opens one
// connection per track, sends a batch of ciphertext samples, and reads the
// plaintexts back. The HTTP `POST /decrypt` route stays untouched so that
// gamdl 3.7.x clients keep working.
//
// Frame header - 16 bytes, big-endian:
//
//   offset size field
//   0      u32  magic        0x57563244 ("WV2D")
//   4      u16  version      1
//   6      u16  kind         BATCH=1 OK=2 ERROR=3 CLOSE=9
//   8      u32  request_id   echoed back on the response
//   12     u32  payload_len
//
// BATCH payload:
//
//   u16 adam_len | u16 uri_len | u32 sample_count
//   u32 * sample_count          (per-sample ciphertext lengths)
//   adam_id bytes | skd uri bytes | sample bytes * sample_count
//
// OK payload:
//
//   u32 count | u32 * count (lengths) | plaintext bytes * count
//
// ERROR payload: UTF-8 message text.
//
// The OK payload is byte-identical to the body the HTTP `POST /decrypt`
// route already returns, and the BATCH payload differs from that route's
// request body in exactly one way: adam_len/uri_len are u16 here and u32
// there. So this module transcodes a BATCH into an HTTP /decrypt body and
// hands it to the supervisor, which owns worker restart/retry. Nothing in
// the FairPlay path is reimplemented here.
//
// A connection is a session: several BATCH frames may arrive on it, and it
// ends on CLOSE or EOF.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace wrapper {

// Default listener address, overridden by WRAPPER_DECRYPT_HOST / _PORT.
constexpr const char* kDefaultDecryptHost = "0.0.0.0";
constexpr int         kDefaultDecryptPort = 10020;

// Result of one decrypt request handed to the backend.
struct DecryptOutcome {
    // True when `payload` holds the OK payload to return verbatim.
    bool ok = false;

    // OK payload: u32 count | u32 * count lengths | plaintext bytes.
    // This is exactly the HTTP `POST /decrypt` response body.
    std::string payload;

    // Text for the ERROR frame when `ok` is false.
    std::string error;
};

// Performs one decrypt. `http_body` is an HTTP `POST /decrypt` request body
// (u32 adam_len | u32 uri_len | u32 sample_count | ...). Implemented by the
// supervisor so that the existing restart/retry recovery is inherited.
using DecryptBackend = std::function<DecryptOutcome(const std::string& http_body)>;

struct DecryptTcpOptions {
    std::string host = kDefaultDecryptHost;
    int         port = kDefaultDecryptPort;
};

// Rewrites a WV2D BATCH payload into an HTTP `POST /decrypt` request body.
// The two layouts differ only in the width of the first two fields, but the
// payload is validated in full so that a malformed batch is rejected here
// with a useful message instead of reaching the worker.
//
// `adam_id` and `uri` are optional out-params for logging. On failure
// `*error` is set and false is returned.
//
// Exposed for tests.
bool transcode_wv2d_batch(const std::string& batch_payload,
                          std::string* http_body,
                          std::string* adam_id,
                          std::string* uri,
                          std::string* error);

// Binds and serves the decrypt listener. Blocks until the listening socket
// fails, so callers run it on its own thread. Returns false if the socket
// could not be bound; a per-connection failure only drops that connection.
//
// One detached thread is spawned per connection.
bool run_decrypt_tcp(const DecryptTcpOptions& options, DecryptBackend backend);

}  // namespace wrapper
