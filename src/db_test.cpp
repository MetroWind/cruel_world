#include <gtest/gtest.h>

#include "journal/db.hpp"

TEST(DatabaseTest, InitAndUser)
{
    auto db_res = Database::open(":memory:");
    ASSERT_TRUE(db_res.has_value());
    auto db = std::move(db_res.value());

    ASSERT_TRUE(db.initSchema().has_value());

    std::vector<uint8_t> dek = {1, 2, 3, 4};
    std::vector<uint8_t> salt = {5, 6, 7, 8};
    auto res = db.insertUser("sub-123", dek, salt);
    ASSERT_TRUE(res.has_value());

    auto user_opt = db.getUserBySub("sub-123");
    ASSERT_TRUE(user_opt.has_value());
    EXPECT_TRUE(user_opt.value().has_value());
    EXPECT_EQ(user_opt.value().value().oidc_sub, "sub-123");
    EXPECT_EQ(user_opt.value().value().encrypted_dek.data, dek);
}

TEST(DatabaseTest, EntryOps)
{
    auto db_res = Database::open(":memory:");
    auto db = std::move(db_res.value());
    db.initSchema();

    db.insertUser("sub-1", {1}, {2});
    auto user = db.getUserBySub("sub-1").value().value();

    auto user_by_id = db.getUserById(user.id).value();
    EXPECT_TRUE(user_by_id.has_value());
    EXPECT_EQ(user_by_id->oidc_sub, "sub-1");

    Entry entry{0, user.id, "slug123", "2023-10-27", Blob{{9, 9, 9}}, ""};
    auto err = db.upsertEntry(entry);
    ASSERT_TRUE(err.has_value()) << mw::errorMsg(err.error());

    auto fetched = db.getEntry(user.id, "2023-10-27").value();
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched.value().slug, "slug123");
    EXPECT_EQ(fetched.value().encrypted_body.data,
              std::vector<uint8_t>({9, 9, 9}));

    auto fetched_by_slug = db.getEntryBySlug("slug123").value();
    EXPECT_TRUE(fetched_by_slug.has_value());
    EXPECT_EQ(fetched_by_slug->date, "2023-10-27");

    auto list = db.listEntries(user.id).value();
    EXPECT_EQ(list.size(), 1);

    Entry entry2{0, user.id, "slug123", "2023-10-27", Blob{{8, 8}}, ""};
    ASSERT_TRUE(db.upsertEntry(entry2).has_value());

    auto fetched2 = db.getEntry(user.id, "2023-10-27").value();
    EXPECT_EQ(fetched2.value().encrypted_body.data,
              std::vector<uint8_t>({8, 8}));
}

TEST(DatabaseTest, AttachmentOps)
{
    auto db_res = Database::open(":memory:");
    auto db = std::move(db_res.value());
    db.initSchema();

    db.insertUser("sub-1", {1}, {2});
    auto user = db.getUserBySub("sub-1").value().value();

    Attachment att{
        0, user.id, "attslug", Blob{{1, 2}}, Blob{{3, 4}}, Blob{{5, 6}}, ""};
    ASSERT_TRUE(db.insertAttachment(att).has_value());

    auto fetched = db.getAttachmentBySlug("attslug").value();
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->encrypted_data.data, std::vector<uint8_t>({5, 6}));

    auto list = db.listAttachments(user.id).value();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].slug, "attslug");
}
