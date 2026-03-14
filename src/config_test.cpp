#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "journal/config.hpp"

TEST(ConfigTest, LoadValidConfig)
{
    std::string path = "test_config.yaml";
    std::ofstream out(path);
    out << "root_url: http://localhost/\n";
    out << "data_dir: /var/lib/journal\n";
    out << "oidc_url_prefix: https://auth/\n";
    out << "oidc_client_id: client\n";
    out << "oidc_client_secret: secret\n";
    out.close();

    auto cfg = config::loadConfig(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg.value().root_url, "http://localhost/");

    std::filesystem::remove(path);
}
