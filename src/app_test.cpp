#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mw/auth_mock.hpp>
#include <mw/http_client_mock.hpp>
#include <mw/test_utils.hpp>

#include "journal/app.hpp"
#include "journal/config.hpp"
#include "journal/db.hpp"

using ::testing::_;
using ::testing::Return;

class AppTest : public ::testing::Test
{
protected:
    AppTest()
    {
        config.root_url = "http://localhost:8080/";
        config.bind_address = "127.0.0.1";
        config.bind_port = 8080;
        config.database_path = ":memory:";
        config.oidc_url_prefix = "https://auth/";
        config.oidc_client_id = "client";
        config.oidc_client_secret = "secret";

        auto auth = std::make_unique<mw::AuthMock>();

        EXPECT_CALL(*auth, initialURL())
            .WillRepeatedly(Return("https://auth/login"));

        mw::Tokens dummy_tokens{"access_123", std::nullopt, std::nullopt,
                                std::nullopt};
        EXPECT_CALL(*auth, authenticate("authcode_123"))
            .WillRepeatedly(Return(dummy_tokens));

        mw::UserInfo dummy_user{"test_sub_1", "Test User"};
        EXPECT_CALL(*auth, getUser(dummy_tokens))
            .WillRepeatedly(Return(dummy_user));

        auto db_res = Database::open(":memory:");
        EXPECT_TRUE(db_res.has_value());
        EXPECT_TRUE(db_res.value().initSchema().has_value());

        app = std::make_unique<JournalApp>(config, std::move(db_res), nullptr,
                                           std::move(auth));
    }

    Config config;
    // We can't easily access the db inside app for testing state changes
    // because db is encapsulated. For complete tests we might need access to
    // it, or we could test DB via App state.
    std::unique_ptr<JournalApp> app;
};

TEST_F(AppTest, CanAccessIndex)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        ASSIGN_OR_FAIL(const mw::HTTPResponse* res,
                       client.get(mw::HTTPRequest("http://127.0.0.1:8080/")));
        // Should redirect to login if no session
        EXPECT_EQ(res->status, 302);
        EXPECT_EQ(res->header.at("Location"),
                  "http://localhost:8080/auth/login");
    }
    app->stop();
    app->wait();
}

TEST_F(AppTest, LoginRedirectsToOIDC)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res,
            client.get(mw::HTTPRequest("http://127.0.0.1:8080/auth/login")));
        EXPECT_EQ(res->status, 302);
        EXPECT_TRUE(res->header.count("Location") > 0);
        EXPECT_EQ(res->header.at("Location"), "https://auth/login");
    }
    app->stop();
    app->wait();
}

TEST_F(AppTest, CallbackRedirectsToSetupForNewUser)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res,
            client.get(mw::HTTPRequest(
                "http://127.0.0.1:8080/auth/callback?code=authcode_123")));
        EXPECT_EQ(res->status, 302);
        EXPECT_TRUE(res->header.count("Location") > 0);
        EXPECT_EQ(res->header.at("Location"),
                  "http://localhost:8080/auth/setup");
        EXPECT_TRUE(res->header.count("Set-Cookie") > 0);
        EXPECT_TRUE(res->header.at("Set-Cookie").find("session_id=") !=
                    std::string::npos);
    }
    app->stop();
    app->wait();
}

TEST_F(AppTest, LogoutClearsCookieAndRedirects)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res,
            client.post(mw::HTTPRequest("http://127.0.0.1:8080/auth/logout")
                            .addHeader("Cookie", "session_id=123")));
        EXPECT_EQ(res->status, 302);
        EXPECT_EQ(res->header.at("Location"), "http://localhost:8080/");
        EXPECT_TRUE(res->header.count("Set-Cookie") > 0);
        EXPECT_TRUE(res->header.at("Set-Cookie").find("Max-Age=0") !=
                    std::string::npos);
    }
    app->stop();
    app->wait();
}

TEST_F(AppTest, CanHandleSetupFlow)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        // 1. Get callback to get a pending session
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res1,
            client.get(mw::HTTPRequest(
                "http://127.0.0.1:8080/auth/callback?code=authcode_123")));
        std::string cookie = res1->header.at("Set-Cookie");
        std::string session_id = cookie.substr(11, cookie.find(';') - 11);

        // 2. Access setup page
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res2,
            client.get(mw::HTTPRequest("http://127.0.0.1:8080/auth/setup")
                           .addHeader("Cookie", "session_id=" + session_id)));
        EXPECT_EQ(res2->status, 200);
        EXPECT_THAT(res2->payloadAsStr(),
                    testing::HasSubstr("Setup Your Journal"));

        // 3. Post setup data
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res3,
            client.post(
                mw::HTTPRequest("http://127.0.0.1:8080/auth/setup")
                    .addHeader("Cookie", "session_id=" + session_id)
                    .setPayload(
                        "passphrase=secret123&confirm_passphrase=secret123")
                    .setContentType("application/x-www-form-urlencoded")));
        EXPECT_EQ(res3->status, 302);
        EXPECT_EQ(res3->header.at("Location"), "http://localhost:8080/");
    }
    app->stop();
    app->wait();
}

TEST_F(AppTest, CanCreateAndGetEntry)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        // 1. Setup and Unlock
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res1,
            client.get(mw::HTTPRequest(
                "http://127.0.0.1:8080/auth/callback?code=authcode_123")));
        std::string cookie = res1->header.at("Set-Cookie");
        std::string session_id = cookie.substr(11, cookie.find(';') - 11);

        client.post(
            mw::HTTPRequest("http://127.0.0.1:8080/auth/setup")
                .addHeader("Cookie", "session_id=" + session_id)
                .setPayload("passphrase=secret123&confirm_passphrase=secret123")
                .setContentType("application/x-www-form-urlencoded"));

        // 2. Post an entry
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res2,
            client.post(
                mw::HTTPRequest("http://127.0.0.1:8080/entry/2023-10-27")
                    .addHeader("Cookie", "session_id=" + session_id)
                    .setPayload(R"({"body": "Hello World"})")));
        EXPECT_EQ(res2->status, 200);
        auto json = nlohmann::json::parse(res2->payloadAsStr());
        std::string slug = json["slug"];
        EXPECT_FALSE(slug.empty());

        // 3. Get the entry
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res3,
            client.get(mw::HTTPRequest("http://127.0.0.1:8080/entry/" + slug)
                           .addHeader("Cookie", "session_id=" + session_id)));
        EXPECT_EQ(res3->status, 200);
        EXPECT_THAT(res3->payloadAsStr(), testing::HasSubstr("Hello World"));
    }
    app->stop();
    app->wait();
}

TEST_F(AppTest, CanUploadAndDownloadAttachment)
{
    EXPECT_TRUE(mw::isExpected(app->start()));
    {
        mw::HTTPSession client;
        // 1. Setup and Unlock
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res1,
            client.get(mw::HTTPRequest(
                "http://127.0.0.1:8080/auth/callback?code=authcode_123")));
        std::string cookie = res1->header.at("Set-Cookie");
        std::string session_id = cookie.substr(11, cookie.find(';') - 11);

        client.post(
            mw::HTTPRequest("http://127.0.0.1:8080/auth/setup")
                .addHeader("Cookie", "session_id=" + session_id)
                .setPayload("passphrase=secret123&confirm_passphrase=secret123")
                .setContentType("application/x-www-form-urlencoded"));

        // 2. Post an attachment
        // Simple multipart body manual construction
        std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
        std::string body = "--" + boundary +
                           "\r\n"
                           "Content-Disposition: form-data; name=\"file\"; "
                           "filename=\"test.txt\"\r\n"
                           "Content-Type: text/plain\r\n\r\n"
                           "Attachment Content\r\n"
                           "--" +
                           boundary + "--\r\n";

        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res2,
            client.post(
                mw::HTTPRequest("http://127.0.0.1:8080/api/attachments")
                    .addHeader("Cookie", "session_id=" + session_id)
                    .addHeader("Content-Type",
                               "multipart/form-data; boundary=" + boundary)
                    .setPayload(body)));
        EXPECT_EQ(res2->status, 200);
        auto json = nlohmann::json::parse(res2->payloadAsStr());
        std::string slug = json["slug"];

        // 3. Get the attachment
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res3,
            client.get(
                mw::HTTPRequest("http://127.0.0.1:8080/attachments/" + slug)
                    .addHeader("Cookie", "session_id=" + session_id)));
        EXPECT_EQ(res3->status, 200);
        EXPECT_EQ(res3->payloadAsStr(), "Attachment Content");
        EXPECT_EQ(res3->header.at("Content-Type"), "text/plain");

        // 4. Check management page
        ASSIGN_OR_FAIL(
            const mw::HTTPResponse* res4,
            client.get(
                mw::HTTPRequest("http://127.0.0.1:8080/attachments/manage")
                    .addHeader("Cookie", "session_id=" + session_id)));
        EXPECT_EQ(res4->status, 200);
        EXPECT_THAT(res4->payloadAsStr(), testing::HasSubstr("test.txt"));
    }
    app->stop();
    app->wait();
}
