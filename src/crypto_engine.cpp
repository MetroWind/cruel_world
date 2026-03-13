#include "journal/crypto_engine.hpp"

#include <format>

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace
{
std::string vecToStr(const std::vector<uint8_t>& vec)
{
    return std::string(reinterpret_cast<const char*>(vec.data()), vec.size());
}

std::vector<uint8_t> strToVec(const std::string& str)
{
    return std::vector<uint8_t>(str.begin(), str.end());
}
} // namespace

mw::E<std::vector<uint8_t>> CryptoEngine::generateDEK()
{
    std::vector<uint8_t> dek(32);
    if(RAND_bytes(dek.data(), dek.size()) != 1)
    {
        return std::unexpected(
            mw::runtimeError("Failed to generate random DEK"));
    }
    return dek;
}

mw::E<std::vector<uint8_t>>
CryptoEngine::deriveKEK(const std::string& passphrase,
                        const std::vector<uint8_t>& salt)
{
    auto res =
        crypto_.deriveKeyArgon2id(passphrase, vecToStr(salt), 3, 4096, 1, 32);
    if(!res.has_value())
    {
        return std::unexpected(res.error());
    }
    return res.value();
}

mw::E<std::vector<uint8_t>>
CryptoEngine::encryptDEK(const std::vector<uint8_t>& dek,
                         const std::vector<uint8_t>& kek)
{
    auto res = crypto_.encrypt(mw::EncryptionAlgorithm::AES_256_GCM,
                               vecToStr(kek), vecToStr(dek));
    if(!res.has_value())
    {
        return std::unexpected(res.error());
    }
    return strToVec(res.value());
}

mw::E<std::vector<uint8_t>>
CryptoEngine::decryptDEK(const std::vector<uint8_t>& encrypted_dek,
                         const std::vector<uint8_t>& kek)
{
    auto res = crypto_.decrypt(mw::EncryptionAlgorithm::AES_256_GCM,
                               vecToStr(kek), vecToStr(encrypted_dek));
    if(!res.has_value())
    {
        return std::unexpected(res.error());
    }
    return strToVec(res.value());
}

mw::E<std::vector<uint8_t>>
CryptoEngine::encryptData(const std::vector<uint8_t>& data,
                          const std::vector<uint8_t>& dek)
{
    auto res = crypto_.encrypt(mw::EncryptionAlgorithm::AES_256_GCM,
                               vecToStr(dek), vecToStr(data));
    if(!res.has_value())
    {
        return std::unexpected(res.error());
    }
    return strToVec(res.value());
}

mw::E<std::vector<uint8_t>>
CryptoEngine::decryptData(const std::vector<uint8_t>& encrypted_data,
                          const std::vector<uint8_t>& dek)
{
    auto res = crypto_.decrypt(mw::EncryptionAlgorithm::AES_256_GCM,
                               vecToStr(dek), vecToStr(encrypted_data));
    if(!res.has_value())
    {
        return std::unexpected(res.error());
    }
    return strToVec(res.value());
}

mw::E<std::vector<uint8_t>> CryptoEngine::generateSalt(int length)
{
    std::vector<uint8_t> salt(length);
    if(RAND_bytes(salt.data(), salt.size()) != 1)
    {
        return std::unexpected(
            mw::runtimeError("Failed to generate random salt"));
    }
    return salt;
}

mw::E<std::string> CryptoEngine::generateSlug(int length)
{
    const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string slug;
    slug.reserve(length);
    std::vector<uint8_t> random_bytes(length);
    if(RAND_bytes(random_bytes.data(), random_bytes.size()) != 1)
    {
        return std::unexpected(
            mw::runtimeError("Failed to generate random slug"));
    }
    for(int i = 0; i < length; ++i)
    {
        slug.push_back(charset[random_bytes[i] % (sizeof(charset) - 1)]);
    }
    return slug;
}

void CryptoEngine::secureZero(std::vector<uint8_t>& data)
{
    OPENSSL_cleanse(data.data(), data.size());
    data.clear();
}
