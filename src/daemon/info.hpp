#pragma once

#include <string>

namespace wrapper {

struct ServerInfo {
    std::string version = "3.0.0.1";
    bool apple_init_enabled = true;
};

}  // namespace wrapper
