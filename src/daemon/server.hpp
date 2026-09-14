// HTTP server wiring.
//
// The Server class owns an httplib::Server and mounts the routes
// wrapper-v2 exposes. References to the runtime modules are
// captured by reference - the Server does not own them.

#pragma once

#include <string>

#include <httplib.h>

#include "apple/auth.hpp"
#include "apple/loader.hpp"
#include "apple/runtime.hpp"

namespace wrapper {

struct ServerInfo {
    // API contract version surfaced as `version` via /health and /me.
    // gamdl >= 3.8.0 compares this against "0.0.2" exactly and refuses to
    // talk to the daemon on a mismatch, so it tracks the wrapper-v2 API and
    // not this fork's own releases.
    std::string version = "0.0.2";

    // This fork's own release, surfaced as `build_version`. Purely
    // informational; clients ignore unknown fields.
    std::string build_version = "2.0.0";

    // True iff Apple lib initialization is enabled (controlled by
    // WRAPPER_APPLE_INIT). Surfaced via /health so it is obvious when
    // the daemon is running in stub-only mode.
    bool apple_init_enabled = true;
};

class Server {
public:
    Server(httplib::Server& svr,
           apple::Runtime& rt,
           apple::Loader& loader,
           apple::Account& account,
           ServerInfo info);

    // Mount all routes onto the underlying httplib::Server.
    void mount();

private:
    httplib::Server& svr_;
    apple::Runtime& rt_;
    apple::Loader& loader_;
    apple::Account& account_;
    ServerInfo info_;
};

}  // namespace wrapper
