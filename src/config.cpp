#include "journal/config.hpp"

#include <format>
#include <fstream>
#include <sstream>

#include <c4/std/string.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>

namespace config
{

mw::E<Config> loadConfig(const std::string& path)
{
    std::ifstream file(path);
    if(!file.is_open())
    {
        return std::unexpected(mw::runtimeError(
            std::format("Failed to open config file: {}", path)));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    try
    {
        ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
        ryml::NodeRef root = tree.rootref();

        Config cfg;

        if(root.has_child("root_url"))
        {
            root["root_url"] >> cfg.root_url;
        }
        else
        {
            return std::unexpected(
                mw::runtimeError("Missing root_url in config"));
        }

        if(root.has_child("bind_address"))
        {
            root["bind_address"] >> cfg.bind_address;
        }
        else
        {
            cfg.bind_address = "127.0.0.1";
        }

        if(root.has_child("bind_port"))
        {
            root["bind_port"] >> cfg.bind_port;
        }
        else
        {
            cfg.bind_port = 8080;
        }

        if(root.has_child("database_path"))
        {
            root["database_path"] >> cfg.database_path;
        }
        else
        {
            return std::unexpected(
                mw::runtimeError("Missing database_path in config"));
        }

        if(root.has_child("oidc_url_prefix"))
        {
            root["oidc_url_prefix"] >> cfg.oidc_url_prefix;
        }
        else
        {
            return std::unexpected(
                mw::runtimeError("Missing oidc_url_prefix in config"));
        }

        if(root.has_child("oidc_client_id"))
        {
            root["oidc_client_id"] >> cfg.oidc_client_id;
        }
        else
        {
            return std::unexpected(
                mw::runtimeError("Missing oidc_client_id in config"));
        }

        if(root.has_child("oidc_client_secret"))
        {
            root["oidc_client_secret"] >> cfg.oidc_client_secret;
        }
        else
        {
            return std::unexpected(
                mw::runtimeError("Missing oidc_client_secret in config"));
        }

        return cfg;
    }
    catch(const std::exception& e)
    {
        return std::unexpected(mw::runtimeError(
            std::format("Failed to parse config file: {}", e.what())));
    }
}

} // namespace config
