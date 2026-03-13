#pragma once

#include <mw/http_server.hpp>
#include <mw/http_client.hpp>
#include <mw/auth.hpp>
#include <nlohmann/json.hpp>
#include <inja.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "journal/config.hpp"
#include "journal/db.hpp"
#include "journal/crypto_engine.hpp"
#include "journal/session_manager.hpp"

class JournalApp : public mw::HTTPServer {
public:
    JournalApp(const Config& config, mw::E<Database>&& db, std::unique_ptr<mw::HTTPSessionInterface> http_client = nullptr, std::unique_ptr<mw::AuthInterface> auth = nullptr);
    ~JournalApp();

protected:
    void setup() override;

private:
    Config config_;
    Database db_;
    CryptoEngine crypto_;
    SessionManager session_manager_;
    std::string root_path_;

    std::unique_ptr<mw::AuthInterface> auth_;
    inja::Environment env_;

    std::atomic<bool> stop_reaper_{false};
    std::thread reaper_thread_;
    std::mutex reaper_mutex_;
    std::condition_variable reaper_cv_;

    void discoverOIDC(std::unique_ptr<mw::HTTPSessionInterface> http_client);
    
    void render(Response& res, const std::string& template_name, nlohmann::json data = nlohmann::json::object());

    // Handlers
    void handleIndex(const Request& req, Response& res);
    void handleLogin(const Request& req, Response& res);
    void handleCallback(const Request& req, Response& res);
    void handleSetupGet(const Request& req, Response& res);
    void handleSetupPost(const Request& req, Response& res);
    void handleUnlockGet(const Request& req, Response& res);
    void handleUnlockPost(const Request& req, Response& res);
    void handleLogout(const Request& req, Response& res);

    void handleEntryGet(const Request& req, Response& res);
    void handleEntryPost(const Request& req, Response& res);
    void handleAttachmentPost(const Request& req, Response& res);
    void handleAttachmentGet(const Request& req, Response& res);
    void handleAttachmentManage(const Request& req, Response& res);

    // Helpers
    std::string getSessionCookie(const Request& req) const;
    void setSessionCookie(Response& res, const std::string& session_id) const;
    void clearSessionCookie(Response& res) const;
    
    std::string renderMarkdown(const std::string& markdown);
    std::string getCurrentDate() const;
    nlohmann::json getPastEntries(int user_id);
};
