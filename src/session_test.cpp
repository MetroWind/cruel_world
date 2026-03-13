#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "journal/session_manager.hpp"

TEST(SessionManagerTest, CreateAndGet)
{
    SessionManager sm;
    auto sid_res = sm.createSession(42);
    ASSERT_TRUE(sid_res.has_value());

    auto sess = sm.getSession(sid_res.value());
    ASSERT_TRUE(sess.has_value());
    EXPECT_EQ(sess.value().user_id, 42);
    EXPECT_EQ(sess.value().state, SessionState::LOCKED);
}

TEST(SessionManagerTest, UnlockAndPing)
{
    SessionManager sm;
    auto sid = sm.createSession(1).value();

    std::vector<uint8_t> dek = {10, 20, 30};
    ASSERT_TRUE(sm.unlockSession(sid, dek).has_value());

    auto sess = sm.getSession(sid).value();
    EXPECT_EQ(sess.state, SessionState::UNLOCKED);
    EXPECT_EQ(sess.decrypted_dek, dek);

    uint64_t old_time = sess.last_activity_time;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    sm.pingSession(sid);

    EXPECT_GT(sm.getSession(sid).value().last_activity_time, old_time);
}

TEST(SessionManagerTest, UpdateUserId)
{
    SessionManager sm;
    auto sid = sm.createSession(-1, "sub-1").value();
    ASSERT_TRUE(sm.updateSessionUserId(sid, 100).has_value());

    auto sess = sm.getSession(sid).value();
    EXPECT_EQ(sess.user_id, 100);
    EXPECT_EQ(sess.pending_sub, "");
}

TEST(SessionManagerTest, DestroySession)
{
    SessionManager sm;
    auto sid = sm.createSession(1).value();
    sm.destroySession(sid);
    EXPECT_FALSE(sm.getSession(sid).has_value());
}

TEST(SessionManagerTest, ReapSessions)
{
    SessionManager sm;
    auto sid = sm.createSession(1).value();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    sm.reapSessions(0); // Max idle 0 seconds

    EXPECT_FALSE(sm.getSession(sid).has_value());
}
