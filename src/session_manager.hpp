#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <optional>
#include <mw/error.hpp>

enum class SessionState {
    LOCKED,
    UNLOCKED
};

struct Session {
    std::string id;
    int user_id; // -1 if pending setup
    std::string pending_sub; // Valid if user_id == -1
    SessionState state;
    std::vector<uint8_t> decrypted_dek;
    uint64_t last_activity_time;
};

class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager();

    // Create a new locked session for user_id (use -1 for pending setup)
    mw::E<std::string> createSession(int user_id, const std::string& pending_sub = "");

    // Get session info
    std::optional<Session> getSession(const std::string& session_id);

    // Update session state to UNLOCKED with decrypted DEK
    mw::E<void> unlockSession(const std::string& session_id, const std::vector<uint8_t>& decrypted_dek);

    // Update user_id after setup
    mw::E<void> updateSessionUserId(const std::string& session_id, int user_id);

    // Update last activity time
    void pingSession(const std::string& session_id);

    // Destroy session and securely wipe DEK
    void destroySession(const std::string& session_id);

    // Reaper task: run periodically to remove expired sessions
    void reapSessions(uint64_t max_idle_time_seconds);

private:
    std::unordered_map<std::string, Session> sessions_;
    std::mutex mutex_;
};
