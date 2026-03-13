#include "journal/app.hpp"

#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

#include <macrodown.h>
#include <mw/http_client.hpp>
#include <mw/url.hpp>
#include <nlohmann/json.hpp>

JournalApp::JournalApp(const Config& config, mw::E<Database>&& db,
                       std::unique_ptr<mw::HTTPSessionInterface> http_client,
                       std::unique_ptr<mw::AuthInterface> auth)
    : mw::HTTPServer(mw::IPSocketInfo{config.bind_address, config.bind_port}),
      config_(config), db_(std::move(db.value())), auth_(std::move(auth))
{

    auto url_res = mw::URL::fromStr(config_.root_url);
    if(url_res.has_value())
    {
        root_path_ = url_res.value().path();
        if(root_path_.empty() || root_path_.back() != '/')
        {
            root_path_ += "/";
        }
    }
    else
    {
        root_path_ = "/";
    }

    if(!auth_)
    {
        discoverOIDC(http_client ? std::move(http_client)
                                 : std::make_unique<mw::HTTPSession>());
    }

    env_.set_trim_blocks(true);
    env_.set_lstrip_blocks(true);

    reaper_thread_ = std::thread(
        [this]()
        {
            std::unique_lock<std::mutex> lock(reaper_mutex_);
            while(!stop_reaper_)
            {
                if(reaper_cv_.wait_for(lock, std::chrono::minutes(5)) ==
                   std::cv_status::timeout)
                {
                    session_manager_.reapSessions(30 * 60); // 30 minutes
                }
            }
        });
}

JournalApp::~JournalApp()
{
    {
        std::lock_guard<std::mutex> lock(reaper_mutex_);
        stop_reaper_ = true;
    }
    reaper_cv_.notify_all();
    if(reaper_thread_.joinable())
    {
        reaper_thread_.join();
    }
}

void JournalApp::render(Response& res, const std::string& template_name,
                        nlohmann::json data)
{
    data["root_url"] = config_.root_url;
    if(!data.contains("past_entries"))
    {
        data["past_entries"] = nlohmann::json::array();
    }

    try
    {
        std::string content =
            env_.render_file("templates/" + template_name, data);
        res.set_content(content, "text/html");
    }
    catch(const std::exception& e)
    {
        res.status = 500;
        res.set_content("Template error: " + std::string(e.what()),
                        "text/plain");
    }
}

void JournalApp::discoverOIDC(
    std::unique_ptr<mw::HTTPSessionInterface> http_client)
{
    auto redirect_url_res = mw::URL::fromStr(config_.root_url);
    if(!redirect_url_res.has_value())
    {
        std::cerr << "Invalid root URL." << std::endl;
        return;
    }
    auto redirect_url = redirect_url_res.value();
    redirect_url.appendPath("auth/callback");

    auto auth_res = mw::AuthOpenIDConnect::create(
        config_.oidc_url_prefix, config_.oidc_client_id,
        config_.oidc_client_secret, redirect_url.str(), std::move(http_client));

    if(auth_res.has_value())
    {
        auth_ = std::move(auth_res.value());
        std::cout << "OIDC Discovery successful." << std::endl;
    }
    else
    {
        std::cerr << "Failed to discover OIDC endpoints: "
                  << mw::errorMsg(auth_res.error()) << std::endl;
    }
}

void JournalApp::setup()
{
    server.Get(mw::URL::fromStr(config_.root_url).value().path(),
               [this](const Request& req, Response& res)
               { handleIndex(req, res); });
    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("auth/login")
                   .path(),
               [this](const Request& req, Response& res)
               { handleLogin(req, res); });
    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("auth/callback")
                   .path(),
               [this](const Request& req, Response& res)
               { handleCallback(req, res); });
    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("auth/setup")
                   .path(),
               [this](const Request& req, Response& res)
               { handleSetupGet(req, res); });
    server.Post(mw::URL::fromStr(config_.root_url)
                    .value()
                    .appendPath("auth/setup")
                    .path(),
                [this](const Request& req, Response& res)
                { handleSetupPost(req, res); });
    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("auth/unlock")
                   .path(),
               [this](const Request& req, Response& res)
               { handleUnlockGet(req, res); });
    server.Post(mw::URL::fromStr(config_.root_url)
                    .value()
                    .appendPath("auth/unlock")
                    .path(),
                [this](const Request& req, Response& res)
                { handleUnlockPost(req, res); });
    server.Post(mw::URL::fromStr(config_.root_url)
                    .value()
                    .appendPath("auth/logout")
                    .path(),
                [this](const Request& req, Response& res)
                { handleLogout(req, res); });

    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("entry/:slug")
                   .path(),
               [this](const Request& req, Response& res)
               { handleEntryGet(req, res); });
    server.Post(mw::URL::fromStr(config_.root_url)
                    .value()
                    .appendPath("entry/:date")
                    .path(),
                [this](const Request& req, Response& res)
                { handleEntryPost(req, res); });

    server.Post(mw::URL::fromStr(config_.root_url)
                    .value()
                    .appendPath("api/attachments")
                    .path(),
                [this](const Request& req, Response& res)
                { handleAttachmentPost(req, res); });
    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("attachments/manage")
                   .path(),
               [this](const Request& req, Response& res)
               { handleAttachmentManage(req, res); });
    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("attachments/:slug")
                   .path(),
               [this](const Request& req, Response& res)
               { handleAttachmentGet(req, res); });

    server.Get(mw::URL::fromStr(config_.root_url)
                   .value()
                   .appendPath("static/(.*)")
                   .path(),
               [&](const Request& req, Response& res)
               {
                   std::string path = req.matches[1];
                   std::ifstream file("static/" + path);
                   if(file.is_open())
                   {
                       std::stringstream buffer;
                       buffer << file.rdbuf();
                       res.set_content(buffer.str(), "text/css");
                   }
                   else
                   {
                       res.status = 404;
                   }
               });
}

void JournalApp::handleIndex(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    if(session_id.empty())
    {
        res.set_redirect(mw::URL::fromStr(config_.root_url)
                             .value()
                             .appendPath("auth/login")
                             .str());
        return;
    }

    auto session_opt = session_manager_.getSession(session_id);
    if(!session_opt.has_value())
    {
        res.set_redirect(mw::URL::fromStr(config_.root_url)
                             .value()
                             .appendPath("auth/login")
                             .str());
        return;
    }

    if(session_opt.value().state == SessionState::LOCKED)
    {
        if(session_opt.value().user_id == -1)
        {
            res.set_redirect(mw::URL::fromStr(config_.root_url)
                                 .value()
                                 .appendPath("auth/setup")
                                 .str());
        }
        else
        {
            res.set_redirect(mw::URL::fromStr(config_.root_url)
                                 .value()
                                 .appendPath("auth/unlock")
                                 .str());
        }
        return;
    }

    std::string today = getCurrentDate();
    auto entry_res = db_.getEntry(session_opt.value().user_id, today);
    if(entry_res && entry_res.value())
    {
        res.set_redirect(mw::URL::fromStr(config_.root_url)
                             .value()
                             .appendPath("entry")
                             .appendPath(entry_res.value().value().slug)
                             .str());
        return;
    }

    render(res, "editor.html",
           {{"date", today},
            {"markdown_body", ""},
            {"html_content", ""},
            {"past_entries", getPastEntries(session_opt.value().user_id)}});
}

void JournalApp::handleLogin(const Request&, Response& res)
{
    if(!auth_)
    {
        res.status = 500;
        res.set_content("OIDC not configured", "text/plain");
        return;
    }
    res.set_redirect(auth_->initialURL());
}

void JournalApp::handleCallback(const Request& req, Response& res)
{
    if(!auth_)
    {
        res.status = 500;
        res.set_content("OIDC not configured", "text/plain");
        return;
    }

    if(!req.has_param("code"))
    {
        res.status = 400;
        res.set_content("Missing code", "text/plain");
        return;
    }

    auto tokens_res = auth_->authenticate(req.get_param_value("code"));
    if(!tokens_res.has_value())
    {
        res.status = 500;
        res.set_content("Failed to authenticate: " +
                            mw::errorMsg(tokens_res.error()),
                        "text/plain");
        return;
    }

    auto user_info_res = auth_->getUser(tokens_res.value());
    if(!user_info_res.has_value())
    {
        res.status = 500;
        res.set_content("Failed to get user info: " +
                            mw::errorMsg(user_info_res.error()),
                        "text/plain");
        return;
    }

    auto sub = user_info_res.value().id;
    auto user_opt_res = db_.getUserBySub(sub);
    if(!user_opt_res.has_value())
    {
        res.status = 500;
        res.set_content("Database error: " + mw::errorMsg(user_opt_res.error()),
                        "text/plain");
        return;
    }

    std::string redirect_path;
    mw::E<std::string> session_id_res;

    if(user_opt_res.value().has_value())
    {
        int user_id = user_opt_res.value().value().id;
        session_id_res = session_manager_.createSession(user_id);
        redirect_path = "auth/unlock";
    }
    else
    {
        session_id_res = session_manager_.createSession(-1, sub);
        redirect_path = "auth/setup";
    }

    if(!session_id_res.has_value())
    {
        res.status = 500;
        res.set_content("Failed to create session: " +
                            mw::errorMsg(session_id_res.error()),
                        "text/plain");
        return;
    }

    setSessionCookie(res, session_id_res.value());
    res.set_redirect(mw::URL::fromStr(config_.root_url)
                         .value()
                         .appendPath(redirect_path)
                         .str());
}

void JournalApp::handleSetupGet(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->user_id != -1)
    {
        res.set_redirect(config_.root_url);
        return;
    }
    render(res, "setup.html");
}

void JournalApp::handleSetupPost(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->user_id != -1)
    {
        res.status = 403;
        return;
    }

    std::string passphrase = req.get_param_value("passphrase");
    std::string confirm = req.get_param_value("confirm_passphrase");
    if(passphrase != confirm)
    {
        res.status = 400;
        res.set_content("Passphrases do not match", "text/plain");
        return;
    }

    auto salt_res = crypto_.generateSalt();
    auto dek_res = crypto_.generateDEK();
    if(!salt_res || !dek_res)
    {
        res.status = 500;
        return;
    }

    auto kek_res = crypto_.deriveKEK(passphrase, salt_res.value());
    if(!kek_res)
    {
        res.status = 500;
        return;
    }

    auto enc_dek_res = crypto_.encryptDEK(dek_res.value(), kek_res.value());
    if(!enc_dek_res)
    {
        res.status = 500;
        return;
    }

    auto db_err = db_.insertUser(session->pending_sub, enc_dek_res.value(),
                                 salt_res.value());
    if(!db_err)
    {
        res.status = 500;
        return;
    }

    auto user = db_.getUserBySub(session->pending_sub).value().value();
    session_manager_.updateSessionUserId(session_id, user.id);
    session_manager_.unlockSession(session_id, dek_res.value());

    res.set_redirect(config_.root_url);
}

void JournalApp::handleUnlockGet(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state == SessionState::UNLOCKED ||
       session->user_id == -1)
    {
        res.set_redirect(config_.root_url);
        return;
    }
    render(res, "unlock.html");
}

void JournalApp::handleUnlockPost(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state == SessionState::UNLOCKED ||
       session->user_id == -1)
    {
        res.set_redirect(config_.root_url);
        return;
    }

    std::string passphrase = req.get_param_value("passphrase");
    auto user_res = db_.getUserById(session->user_id);
    if(!user_res || !user_res.value())
    {
        res.status = 500;
        return;
    }

    auto kek_res = crypto_.deriveKEK(passphrase, user_res.value()->salt.data);
    if(!kek_res)
    {
        res.status = 500;
        return;
    }

    auto dek_res = crypto_.decryptDEK(user_res.value()->encrypted_dek.data,
                                      kek_res.value());
    if(!dek_res)
    {
        render(res, "unlock.html", {{"error", "Invalid passphrase"}});
        return;
    }

    session_manager_.unlockSession(session_id, dek_res.value());
    res.set_redirect(config_.root_url);
}

void JournalApp::handleLogout(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    if(!session_id.empty())
    {
        session_manager_.destroySession(session_id);
    }
    clearSessionCookie(res);
    res.set_redirect(config_.root_url);
}

void JournalApp::handleEntryGet(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state != SessionState::UNLOCKED)
    {
        res.set_redirect(config_.root_url);
        return;
    }

    std::string slug = req.path_params.at("slug");
    auto entry_res = db_.getEntryBySlug(slug);
    if(!entry_res || !entry_res.value())
    {
        res.status = 404;
        return;
    }

    auto entry = entry_res.value().value();
    if(entry.user_id != session->user_id)
    {
        res.status = 403;
        return;
    }

    auto decrypted_body_res =
        crypto_.decryptData(entry.encrypted_body.data, session->decrypted_dek);
    if(!decrypted_body_res)
    {
        res.status = 500;
        return;
    }

    std::string markdown(decrypted_body_res.value().begin(),
                         decrypted_body_res.value().end());
    std::string html = renderMarkdown(markdown);

    render(res, "editor.html",
           {{"date", entry.date},
            {"markdown_body", markdown},
            {"html_content", html},
            {"past_entries", getPastEntries(session->user_id)}});
}

void JournalApp::handleEntryPost(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state != SessionState::UNLOCKED)
    {
        res.status = 401;
        return;
    }

    std::string date = req.path_params.at("date");
    auto json = nlohmann::json::parse(req.body);
    std::string body = json["body"];

    auto existing_res = db_.getEntry(session->user_id, date);
    std::string slug;
    if(existing_res && existing_res.value())
    {
        slug = existing_res.value()->slug;
    }
    else
    {
        slug = crypto_.generateSlug().value();
    }

    std::vector<uint8_t> data(body.begin(), body.end());
    auto encrypted_res = crypto_.encryptData(data, session->decrypted_dek);
    if(!encrypted_res)
    {
        res.status = 500;
        return;
    }

    Entry entry{0, session->user_id, slug, date, Blob{encrypted_res.value()},
                ""};
    auto db_err = db_.upsertEntry(entry);
    if(!db_err)
    {
        res.status = 500;
        return;
    }

    std::string html = renderMarkdown(body);
    res.set_content(nlohmann::json{{"slug", slug}, {"html", html}}.dump(), "application/json");
}
void JournalApp::handleAttachmentPost(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state != SessionState::UNLOCKED)
    {
        res.status = 401;
        return;
    }

    if(!req.has_file("file"))
    {
        res.status = 400;
        res.set_content("No file uploaded", "text/plain");
        return;
    }

    const auto& file = req.get_file_value("file");
    std::string filename = file.filename;
    std::string mime_type = file.content_type;
    std::string data_str = file.content;

    auto slug = crypto_.generateSlug().value();

    auto enc_filename = crypto_
                            .encryptData({filename.begin(), filename.end()},
                                         session->decrypted_dek)
                            .value();
    auto enc_mime = crypto_
                        .encryptData({mime_type.begin(), mime_type.end()},
                                     session->decrypted_dek)
                        .value();
    auto enc_data = crypto_
                        .encryptData({data_str.begin(), data_str.end()},
                                     session->decrypted_dek)
                        .value();

    Attachment att{0,
                   session->user_id,
                   slug,
                   Blob{enc_filename},
                   Blob{enc_mime},
                   Blob{enc_data},
                   ""};
    db_.insertAttachment(att);

    res.set_content(
        nlohmann::json{{"slug", slug},
                       {"url", config_.root_url + "attachments/" + slug}}
            .dump(),
        "application/json");
}

void JournalApp::handleAttachmentGet(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state != SessionState::UNLOCKED)
    {
        res.status = 401;
        return;
    }

    std::string slug = req.path_params.at("slug");
    auto att_res = db_.getAttachmentBySlug(slug);
    if(!att_res || !att_res.value())
    {
        res.status = 404;
        return;
    }

    auto att = att_res.value().value();
    if(att.user_id != session->user_id)
    {
        res.status = 403;
        return;
    }

    auto dec_mime =
        crypto_
            .decryptData(att.encrypted_mime_type.data, session->decrypted_dek)
            .value();
    auto dec_data =
        crypto_.decryptData(att.encrypted_data.data, session->decrypted_dek)
            .value();

    std::string mime(dec_mime.begin(), dec_mime.end());
    res.set_content(std::string(dec_data.begin(), dec_data.end()), mime);
}

void JournalApp::handleAttachmentManage(const Request& req, Response& res)
{
    std::string session_id = getSessionCookie(req);
    auto session = session_manager_.getSession(session_id);
    if(!session || session->state != SessionState::UNLOCKED)
    {
        res.set_redirect(config_.root_url);
        return;
    }

    auto atts_res = db_.listAttachments(session->user_id);
    nlohmann::json atts_json = nlohmann::json::array();
    if(atts_res.has_value())
    {
        for(const auto& att : atts_res.value())
        {
            auto dec_filename = crypto_
                                    .decryptData(att.encrypted_filename.data,
                                                 session->decrypted_dek)
                                    .value();
            auto dec_mime = crypto_
                                .decryptData(att.encrypted_mime_type.data,
                                             session->decrypted_dek)
                                .value();

            atts_json.push_back(
                {{"slug", att.slug},
                 {"filename",
                  std::string(dec_filename.begin(), dec_filename.end())},
                 {"mime_type", std::string(dec_mime.begin(), dec_mime.end())}});
        }
    }

    render(res, "attachments.html", {{"attachments", atts_json}});
}

std::string JournalApp::getSessionCookie(const Request& req) const
{
    if(req.has_header("Cookie"))
    {
        std::string cookies = req.get_header_value("Cookie");
        auto pos = cookies.find("session_id=");
        if(pos != std::string::npos)
        {
            auto end = cookies.find(';', pos);
            if(end == std::string::npos)
            {
                end = cookies.length();
            }
            return cookies.substr(pos + 11, end - pos - 11);
        }
    }
    return "";
}

void JournalApp::setSessionCookie(Response& res,
                                  const std::string& session_id) const
{
    res.set_header("Set-Cookie", "session_id=" + session_id + "; Path=" +
                                     root_path_ + "; HttpOnly; SameSite=Lax");
}

void JournalApp::clearSessionCookie(Response& res) const
{
    res.set_header("Set-Cookie", "session_id=; Path=" + root_path_ +
                                     "; HttpOnly; SameSite=Lax; Max-Age=0");
}

std::string JournalApp::getCurrentDate() const
{
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%d}", now);
}

nlohmann::json JournalApp::getPastEntries(int user_id)
{
    auto entries_res = db_.listEntries(user_id);
    nlohmann::json arr = nlohmann::json::array();
    if(entries_res.has_value())
    {
        for(const auto& e : entries_res.value())
        {
            arr.push_back({{"date", e.date}, {"slug", e.slug}});
        }
    }
    return arr;
}

std::string JournalApp::renderMarkdown(const std::string& markdown)
{
    macrodown::MacroDown md;
    auto root = md.parse(markdown);
    if(!root)
    {
        return "";
    }
    return md.render(*root);
}
