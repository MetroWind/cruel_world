#include "db.hpp"

#include <format>

#include <mw/utils.hpp>
#include <sqlite3.h>

// ADL implementations for libmw database template binds
mw::E<void> bindOne(const mw::SQLiteStatement& sql, int i, const Blob& x)
{
    if(sqlite3_bind_blob(sql.data(), i, x.data.data(), x.data.size(),
                         SQLITE_TRANSIENT) != SQLITE_OK)
    {
        return std::unexpected(mw::runtimeError("Failed to bind blob"));
    }
    return {};
}

void getValue(mw::SQLiteStatement& sql, int i, Blob& x)
{
    const void* raw = sqlite3_column_blob(sql.data(), i);
    int bytes = sqlite3_column_bytes(sql.data(), i);
    if(raw == nullptr)
    {
        x.data.clear();
        return;
    }
    const uint8_t* ptr = static_cast<const uint8_t*>(raw);
    x.data.assign(ptr, ptr + bytes);
}

Database::Database(std::unique_ptr<mw::SQLite> db) : db_(std::move(db)) {}

mw::E<Database> Database::open(const std::string& path)
{
    ASSIGN_OR_RETURN(auto db_res, mw::SQLite::connectFile(path));

    auto db = std::move(db_res);
    DO_OR_RETURN(db->execute("PRAGMA foreign_keys = ON;"));
    DO_OR_RETURN(db->execute("PRAGMA journal_mode = WAL;"));

    return Database(std::move(db));
}

mw::E<void> Database::initSchema()
{
    DO_OR_RETURN(db_->execute(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            oidc_sub TEXT UNIQUE NOT NULL,    
            encrypted_dek BLOB NOT NULL,      
            salt BLOB NOT NULL,               
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )"));

    DO_OR_RETURN(db_->execute(R"(
        CREATE TABLE IF NOT EXISTS entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            slug TEXT UNIQUE NOT NULL,
            date TEXT NOT NULL,               
            encrypted_body BLOB NOT NULL,     
            last_modified DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(user_id, date)
        );
    )"));

    return db_->execute(R"(
        CREATE TABLE IF NOT EXISTS attachments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            slug TEXT UNIQUE NOT NULL,        
            encrypted_filename BLOB NOT NULL,
            encrypted_mime_type BLOB NOT NULL,
            encrypted_data BLOB NOT NULL,     
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )");
}

mw::E<void> Database::insertUser(const std::string& sub,
                                 const std::vector<uint8_t>& enc_dek,
                                 const std::vector<uint8_t>& salt)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr("INSERT INTO users (oidc_sub, encrypted_dek, "
                              "salt) VALUES (?, ?, ?);"));

    DO_OR_RETURN(stmt_res.bind(sub, Blob{enc_dek}, Blob{salt}));

    return db_->execute(std::move(stmt_res));
}

mw::E<std::optional<User>> Database::getUserBySub(const std::string& sub)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr("SELECT id, oidc_sub, encrypted_dek, salt, "
                              "created_at FROM users WHERE oidc_sub = ?;"));

    DO_OR_RETURN(stmt_res.bind(sub));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, std::string, Blob, Blob, std::string>(
            std::move(stmt_res))));

    if(eval_res.empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res[0];
    return User{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                std::get<3>(row), std::get<4>(row)};
}

mw::E<std::optional<User>> Database::getUserById(int id)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr("SELECT id, oidc_sub, encrypted_dek, salt, "
                              "created_at FROM users WHERE id = ?;"));

    DO_OR_RETURN(stmt_res.bind(id));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, std::string, Blob, Blob, std::string>(
            std::move(stmt_res))));

    if(eval_res.empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res[0];
    return User{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                std::get<3>(row), std::get<4>(row)};
}

mw::E<void> Database::upsertEntry(const Entry& entry)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr("INSERT INTO entries (user_id, slug, date, "
                              "encrypted_body) VALUES (?, ?, ?, ?) "
                              "ON CONFLICT(user_id, date) DO UPDATE SET "
                              "encrypted_body = excluded.encrypted_body, "
                              "last_modified = CURRENT_TIMESTAMP;"));

    DO_OR_RETURN(stmt_res.bind(entry.user_id, entry.slug, entry.date,
                               entry.encrypted_body));

    return db_->execute(std::move(stmt_res));
}

mw::E<std::optional<Entry>> Database::getEntry(int user_id,
                                               const std::string& date)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr(
            "SELECT id, user_id, slug, date, encrypted_body, last_modified FROM "
            "entries WHERE user_id = ? AND date = ?;"));

    DO_OR_RETURN(stmt_res.bind(user_id, date));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, int, std::string, std::string, Blob, std::string>(
            std::move(stmt_res))));

    if(eval_res.empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res[0];
    return Entry{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                 std::get<3>(row), std::get<4>(row), std::get<5>(row)};
}

mw::E<std::optional<Entry>> Database::getEntryBySlug(const std::string& slug)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr("SELECT id, user_id, slug, date, encrypted_body, "
                              "last_modified FROM entries WHERE slug = ?;"));

    DO_OR_RETURN(stmt_res.bind(slug));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, int, std::string, std::string, Blob, std::string>(
            std::move(stmt_res))));

    if(eval_res.empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res[0];
    return Entry{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                 std::get<3>(row), std::get<4>(row), std::get<5>(row)};
}

mw::E<std::vector<Entry>> Database::listEntries(int user_id)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr(
            "SELECT id, user_id, slug, date, encrypted_body, last_modified FROM "
            "entries WHERE user_id = ? ORDER BY date DESC;"));

    DO_OR_RETURN(stmt_res.bind(user_id));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, int, std::string, std::string, Blob, std::string>(
            std::move(stmt_res))));

    std::vector<Entry> entries;
    for(const auto& row : eval_res)
    {
        entries.push_back(Entry{std::get<0>(row), std::get<1>(row),
                                std::get<2>(row), std::get<3>(row),
                                std::get<4>(row), std::get<5>(row)});
    }
    return entries;
}

mw::E<void> Database::insertAttachment(const Attachment& attachment)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr(
            "INSERT INTO attachments (user_id, slug, encrypted_filename, "
            "encrypted_mime_type, encrypted_data) VALUES (?, ?, ?, ?, ?);"));

    DO_OR_RETURN(stmt_res.bind(
        attachment.user_id, attachment.slug, attachment.encrypted_filename,
        attachment.encrypted_mime_type, attachment.encrypted_data));

    return db_->execute(std::move(stmt_res));
}

mw::E<std::optional<Attachment>>
Database::getAttachmentBySlug(const std::string& slug)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr(
            "SELECT id, user_id, slug, encrypted_filename, encrypted_mime_type, "
            "encrypted_data, created_at FROM attachments WHERE slug = ?;"));

    DO_OR_RETURN(stmt_res.bind(slug));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, int, std::string, Blob, Blob, Blob, std::string>(
            std::move(stmt_res))));

    if(eval_res.empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res[0];
    return Attachment{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                      std::get<3>(row), std::get<4>(row), std::get<5>(row),
                      std::get<6>(row)};
}

mw::E<std::vector<Attachment>> Database::listAttachments(int user_id)
{
    ASSIGN_OR_RETURN(
        auto stmt_res,
        db_->statementFromStr(
            "SELECT id, user_id, slug, encrypted_filename, encrypted_mime_type, "
            "encrypted_data, created_at FROM attachments WHERE user_id = ? ORDER "
            "BY created_at DESC;"));

    DO_OR_RETURN(stmt_res.bind(user_id));

    ASSIGN_OR_RETURN(
        auto eval_res,
        (db_->eval<int, int, std::string, Blob, Blob, Blob, std::string>(
            std::move(stmt_res))));

    std::vector<Attachment> attachments;
    for(const auto& row : eval_res)
    {
        attachments.push_back(Attachment{std::get<0>(row), std::get<1>(row),
                                         std::get<2>(row), std::get<3>(row),
                                         std::get<4>(row), std::get<5>(row),
                                         std::get<6>(row)});
    }
    return attachments;
}

mw::E<void> Database::deleteAttachment(int user_id, const std::string& slug)
{
    ASSIGN_OR_RETURN(auto stmt_res, db_->statementFromStr(
        "DELETE FROM attachments WHERE user_id = ? AND slug = ?;"));

    DO_OR_RETURN(stmt_res.bind(user_id, slug));

    return db_->execute(std::move(stmt_res));
}
