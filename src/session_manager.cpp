#include "session_manager.hpp"

#include <chrono>

#include <openssl/rand.h>

#include "crypto_engine.hpp"

namespace
{
uint64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

SessionManager::~SessionManager()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& pair : sessions_)
    {
        CryptoEngine::secureZero(pair.second.decrypted_dek);
    }
    sessions_.clear();
}

mw::E<std::string> SessionManager::createSession(int user_id,
                                                 const std::string& pending_sub)
{
    const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    std::string session_id;
    session_id.reserve(32);
    std::vector<uint8_t> random_bytes(32);
    if(RAND_bytes(random_bytes.data(), random_bytes.size()) != 1)
    {
        return std::unexpected(
            mw::runtimeError("Failed to generate random session ID"));
    }
    for(int i = 0; i < 32; ++i)
    {
        session_id.push_back(charset[random_bytes[i] % (sizeof(charset) - 1)]);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session_id] =
        Session{session_id,           user_id, pending_sub,
                SessionState::LOCKED, {},      nowSeconds()};
    return session_id;
}

std::optional<Session> SessionManager::getSession(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

mw::E<void>
SessionManager::unlockSession(const std::string& session_id,
                              const std::vector<uint8_t>& decrypted_dek)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        it->second.state = SessionState::UNLOCKED;
        it->second.decrypted_dek = decrypted_dek;
        it->second.last_activity_time = nowSeconds();
        return {};
    }
    return std::unexpected(mw::runtimeError("Session not found"));
}

mw::E<void> SessionManager::updateSessionUserId(const std::string& session_id,
                                                int user_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        it->second.user_id = user_id;
        it->second.pending_sub.clear();
        return {};
    }
    return std::unexpected(mw::runtimeError("Session not found"));
}

void SessionManager::pingSession(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        it->second.last_activity_time = nowSeconds();
    }
}

void SessionManager::destroySession(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        CryptoEngine::secureZero(it->second.decrypted_dek);
        sessions_.erase(it);
    }
}

void SessionManager::reapSessions(uint64_t max_idle_time_seconds)
{
    uint64_t current_time = nowSeconds();
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto it = sessions_.begin(); it != sessions_.end();)
    {
        if(current_time - it->second.last_activity_time > max_idle_time_seconds)
        {
            CryptoEngine::secureZero(it->second.decrypted_dek);
            it = sessions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
