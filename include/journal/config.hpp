#pragma once

#include <string>
#include <mw/error.hpp>

struct Config {
    std::string root_url;
    std::string bind_address;
    int bind_port;
    std::string database_path;
    
    std::string oidc_url_prefix;
    std::string oidc_client_id;
    std::string oidc_client_secret;
};

namespace config {
    mw::E<Config> loadConfig(const std::string& path);
}
