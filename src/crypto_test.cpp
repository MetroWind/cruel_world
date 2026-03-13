#include <gtest/gtest.h>

#include "journal/crypto_engine.hpp"

TEST(CryptoEngineTest, DEKGeneration)
{
    CryptoEngine engine;
    auto dek1 = engine.generateDEK();
    ASSERT_TRUE(dek1.has_value());
    EXPECT_EQ(dek1.value().size(), 32);

    auto dek2 = engine.generateDEK();
    ASSERT_TRUE(dek2.has_value());
    EXPECT_NE(dek1.value(), dek2.value());
}

TEST(CryptoEngineTest, KEKDerivation)
{
    CryptoEngine engine;
    auto salt = engine.generateSalt(16);
    ASSERT_TRUE(salt.has_value());

    auto kek1 = engine.deriveKEK("my-secret-passphrase", salt.value());
    ASSERT_TRUE(kek1.has_value());
    EXPECT_EQ(kek1.value().size(), 32);

    auto kek2 = engine.deriveKEK("my-secret-passphrase", salt.value());
    ASSERT_TRUE(kek2.has_value());
    EXPECT_EQ(kek1.value(), kek2.value());

    auto kek3 = engine.deriveKEK("different-passphrase", salt.value());
    EXPECT_NE(kek1.value(), kek3.value());
}

TEST(CryptoEngineTest, EncryptionDecryptionRoundtrip)
{
    CryptoEngine engine;
    auto kek = engine.generateDEK().value(); // Just need 32 bytes
    auto dek = engine.generateDEK().value();

    auto encrypted_dek = engine.encryptDEK(dek, kek);
    ASSERT_TRUE(encrypted_dek.has_value());

    auto decrypted_dek = engine.decryptDEK(encrypted_dek.value(), kek);
    ASSERT_TRUE(decrypted_dek.has_value());
    EXPECT_EQ(dek, decrypted_dek.value());
}

TEST(CryptoEngineTest, DataEncryptionDecryption)
{
    CryptoEngine engine;
    auto dek = engine.generateDEK().value();
    std::string plaintext = "This is a secret journal entry.";
    std::vector<uint8_t> data(plaintext.begin(), plaintext.end());

    auto encrypted = engine.encryptData(data, dek);
    ASSERT_TRUE(encrypted.has_value());

    auto decrypted = engine.decryptData(encrypted.value(), dek);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(data, decrypted.value());
}

TEST(CryptoEngineTest, SlugGeneration)
{
    CryptoEngine engine;
    auto slug = engine.generateSlug(8);
    ASSERT_TRUE(slug.has_value());
    EXPECT_EQ(slug.value().length(), 8);
}

TEST(CryptoEngineTest, SecureZero)
{
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    CryptoEngine::secureZero(data);
    EXPECT_TRUE(data.empty());
}
