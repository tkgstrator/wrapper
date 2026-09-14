// Public HTTP supervisor for the private Apple-runtime worker process.

#pragma once

#include <string>

#include <httplib.h>

namespace wrapper {

class Supervisor {
public:
    Supervisor(std::string argv0,
               std::string version,
               std::string build_version,
               int worker_port);
    ~Supervisor();

    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    void mount(httplib::Server& svr);
    void stop_worker();

    // Runs one decrypt through the worker, reusing the same restart/retry
    // recovery the HTTP POST /decrypt route uses. `http_body` is an HTTP
    // /decrypt request body; on success `*payload` receives the response body
    // verbatim, which is also the WV2D OK payload. Used by the TCP listener.
    bool decrypt(const std::string& http_body,
                 std::string* payload,
                 std::string* error);

private:
    class Impl;
    Impl* impl_;
};

}  // namespace wrapper
