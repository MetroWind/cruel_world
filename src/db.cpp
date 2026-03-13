#include "journal/db.hpp"

#include <format>

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
    auto db_res = mw::SQLite::connectFile(path);
    if(!db_res.has_value())
    {
        return std::unexpected(db_res.error());
    }

    auto db = std::move(db_res.value());
    auto e1 = db->execute("PRAGMA foreign_keys = ON;");
    if(!e1.has_value())
    {
        return std::unexpected(e1.error());
    }

    auto e2 = db->execute("PRAGMA journal_mode = WAL;");
    if(!e2.has_value())
    {
        return std::unexpected(e2.error());
    }

    return Database(std::move(db));
}

mw::E<void> Database::initSchema()
{
    auto e1 = db_->execute(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            oidc_sub TEXT UNIQUE NOT NULL,    
            encrypted_dek BLOB NOT NULL,      
            salt BLOB NOT NULL,               
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )");
    if(!e1.has_value())
    {
        return e1;
    }

    auto e2 = db_->execute(R"(
        CREATE TABLE IF NOT EXISTS entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            slug TEXT UNIQUE NOT NULL,
            date TEXT NOT NULL,               
            encrypted_body BLOB NOT NULL,     
            last_modified DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(user_id, date)
        );
    )");
    if(!e2.has_value())
    {
        return e2;
    }

    auto e3 = db_->execute(R"(
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
    return e3;
}

mw::E<void> Database::insertUser(const std::string& sub,
                                 const std::vector<uint8_t>& enc_dek,
                                 const std::vector<uint8_t>& salt)
{
    auto stmt_res = db_->statementFromStr(
        "INSERT INTO users (oidc_sub, encrypted_dek, salt) VALUES (?, ?, ?);");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(sub, Blob{enc_dek}, Blob{salt});
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    return db_->execute(std::move(stmt_res.value()));
}

mw::E<std::optional<User>> Database::getUserBySub(const std::string& sub)
{
    auto stmt_res =
        db_->statementFromStr("SELECT id, oidc_sub, encrypted_dek, salt, "
                              "created_at FROM users WHERE oidc_sub = ?;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(sub);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res = db_->eval<int, std::string, Blob, Blob, std::string>(
        std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    if(eval_res.value().empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res.value()[0];
    return User{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                std::get<3>(row), std::get<4>(row)};
}

mw::E<std::optional<User>> Database::getUserById(int id)
{
    auto stmt_res =
        db_->statementFromStr("SELECT id, oidc_sub, encrypted_dek, salt, "
                              "created_at FROM users WHERE id = ?;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(id);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res = db_->eval<int, std::string, Blob, Blob, std::string>(
        std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    if(eval_res.value().empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res.value()[0];
    return User{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                std::get<3>(row), std::get<4>(row)};
}

mw::E<void> Database::upsertEntry(const Entry& entry)
{
    auto stmt_res =
        db_->statementFromStr("INSERT INTO entries (user_id, slug, date, "
                              "encrypted_body) VALUES (?, ?, ?, ?) "
                              "ON CONFLICT(user_id, date) DO UPDATE SET "
                              "encrypted_body = excluded.encrypted_body, "
                              "last_modified = CURRENT_TIMESTAMP;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(entry.user_id, entry.slug, entry.date,
                                          entry.encrypted_body);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    return db_->execute(std::move(stmt_res.value()));
}

mw::E<std::optional<Entry>> Database::getEntry(int user_id,
                                               const std::string& date)
{
    auto stmt_res = db_->statementFromStr(
        "SELECT id, user_id, slug, date, encrypted_body, last_modified FROM "
        "entries WHERE user_id = ? AND date = ?;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(user_id, date);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res =
        db_->eval<int, int, std::string, std::string, Blob, std::string>(
            std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    if(eval_res.value().empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res.value()[0];
    return Entry{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                 std::get<3>(row), std::get<4>(row), std::get<5>(row)};
}

mw::E<std::optional<Entry>> Database::getEntryBySlug(const std::string& slug)
{
    auto stmt_res =
        db_->statementFromStr("SELECT id, user_id, slug, date, encrypted_body, "
                              "last_modified FROM entries WHERE slug = ?;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(slug);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res =
        db_->eval<int, int, std::string, std::string, Blob, std::string>(
            std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    if(eval_res.value().empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res.value()[0];
    return Entry{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                 std::get<3>(row), std::get<4>(row), std::get<5>(row)};
}

mw::E<std::vector<Entry>> Database::listEntries(int user_id)
{
    auto stmt_res = db_->statementFromStr(
        "SELECT id, user_id, slug, date, encrypted_body, last_modified FROM "
        "entries WHERE user_id = ? ORDER BY date DESC;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(user_id);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res =
        db_->eval<int, int, std::string, std::string, Blob, std::string>(
            std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    std::vector<Entry> entries;
    for(const auto& row : eval_res.value())
    {
        entries.push_back(Entry{std::get<0>(row), std::get<1>(row),
                                std::get<2>(row), std::get<3>(row),
                                std::get<4>(row), std::get<5>(row)});
    }
    return entries;
}

mw::E<void> Database::insertAttachment(const Attachment& attachment)
{
    auto stmt_res = db_->statementFromStr(
        "INSERT INTO attachments (user_id, slug, encrypted_filename, "
        "encrypted_mime_type, encrypted_data) VALUES (?, ?, ?, ?, ?);");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(
        attachment.user_id, attachment.slug, attachment.encrypted_filename,
        attachment.encrypted_mime_type, attachment.encrypted_data);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    return db_->execute(std::move(stmt_res.value()));
}

mw::E<std::optional<Attachment>>
Database::getAttachmentBySlug(const std::string& slug)
{
    auto stmt_res = db_->statementFromStr(
        "SELECT id, user_id, slug, encrypted_filename, encrypted_mime_type, "
        "encrypted_data, created_at FROM attachments WHERE slug = ?;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(slug);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res =
        db_->eval<int, int, std::string, Blob, Blob, Blob, std::string>(
            std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    if(eval_res.value().empty())
    {
        return std::nullopt;
    }

    const auto& row = eval_res.value()[0];
    return Attachment{std::get<0>(row), std::get<1>(row), std::get<2>(row),
                      std::get<3>(row), std::get<4>(row), std::get<5>(row),
                      std::get<6>(row)};
}

mw::E<std::vector<Attachment>> Database::listAttachments(int user_id)
{
    auto stmt_res = db_->statementFromStr(
        "SELECT id, user_id, slug, encrypted_filename, encrypted_mime_type, "
        "encrypted_data, created_at FROM attachments WHERE user_id = ? ORDER "
        "BY created_at DESC;");
    if(!stmt_res.has_value())
    {
        return std::unexpected(stmt_res.error());
    }

    auto bind_res = stmt_res.value().bind(user_id);
    if(!bind_res.has_value())
    {
        return std::unexpected(bind_res.error());
    }

    auto eval_res =
        db_->eval<int, int, std::string, Blob, Blob, Blob, std::string>(
            std::move(stmt_res.value()));
    if(!eval_res.has_value())
    {
        return std::unexpected(eval_res.error());
    }

    std::vector<Attachment> attachments;
    for(const auto& row : eval_res.value())
    {
        attachments.push_back(Attachment{std::get<0>(row), std::get<1>(row),
                                         std::get<2>(row), std::get<3>(row),
                                         std::get<4>(row), std::get<5>(row),
                                         std::get<6>(row)});
    }
    return attachments;
}
