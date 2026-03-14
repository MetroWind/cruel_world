#pragma once

#include <string>
#include <vector>
#include <mw/crypto.hpp>
#include <mw/error.hpp>

class CryptoEngine {
public:
    CryptoEngine() = default;

    // Generate a random DEK (32 bytes)
    mw::E<std::vector<uint8_t>> generateDEK();

    // Derive KEK from passphrase and salt
    mw::E<std::vector<uint8_t>> deriveKEK(const std::string& passphrase, const std::vector<uint8_t>& salt);

    // Encrypt DEK using KEK
    mw::E<std::vector<uint8_t>> encryptDEK(const std::vector<uint8_t>& dek, const std::vector<uint8_t>& kek);

    // Decrypt DEK using KEK
    mw::E<std::vector<uint8_t>> decryptDEK(const std::vector<uint8_t>& encrypted_dek, const std::vector<uint8_t>& kek);

    // Encrypt general data using DEK
    mw::E<std::vector<uint8_t>> encryptData(const std::vector<uint8_t>& data, const std::vector<uint8_t>& dek);

    // Decrypt general data using DEK
    mw::E<std::vector<uint8_t>> decryptData(const std::vector<uint8_t>& encrypted_data, const std::vector<uint8_t>& dek);

    // Generate a secure random salt (e.g. 16 bytes)
    mw::E<std::vector<uint8_t>> generateSalt(int length = 16);

    // Generate a secure random URL-safe slug
    mw::E<std::string> generateSlug(int length = 8);

    // Securely zero out memory
    static void secureZero(std::vector<uint8_t>& data);

private:
    mw::Crypto crypto_;
};
