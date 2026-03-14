#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <mw/database.hpp>
#include <mw/error.hpp>

struct Blob {
    std::vector<uint8_t> data;
};

// ADL functions for libmw
mw::E<void> bindOne(const mw::SQLiteStatement& sql, int i, const Blob& x);
void getValue(mw::SQLiteStatement& sql, int i, Blob& x);

struct User {
    int id;
    std::string oidc_sub;
    Blob encrypted_dek;
    Blob salt;
    std::string created_at;
};

struct Entry {
    int id;
    int user_id;
    std::string slug;
    std::string date;
    Blob encrypted_body;
    std::string last_modified;
};

struct Attachment {
    int id;
    int user_id;
    std::string slug;
    Blob encrypted_filename;
    Blob encrypted_mime_type;
    Blob encrypted_data;
    std::string created_at;
};

class Database {
public:
    explicit Database(std::unique_ptr<mw::SQLite> db);

    static mw::E<Database> open(const std::string& path);

    mw::E<void> initSchema();

    mw::E<void> insertUser(const std::string& sub, const std::vector<uint8_t>& enc_dek, const std::vector<uint8_t>& salt);
    mw::E<std::optional<User>> getUserBySub(const std::string& sub);
    mw::E<std::optional<User>> getUserById(int id);
    
    mw::E<void> upsertEntry(const Entry& entry);
    mw::E<std::optional<Entry>> getEntry(int user_id, const std::string& date);
    mw::E<std::optional<Entry>> getEntryBySlug(const std::string& slug);
    mw::E<std::vector<Entry>> listEntries(int user_id);

    mw::E<void> insertAttachment(const Attachment& attachment);
    mw::E<std::optional<Attachment>> getAttachmentBySlug(const std::string& slug);
    mw::E<std::vector<Attachment>> listAttachments(int user_id);
    mw::E<void> deleteAttachment(int user_id, const std::string& slug);

private:
    std::unique_ptr<mw::SQLite> db_;
};
