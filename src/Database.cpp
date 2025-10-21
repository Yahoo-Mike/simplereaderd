#include <syslog.h>
#include <stdexcept>

#include <sodium.h>

#include "Database.h"
#include "utils.h"

Database& Database::get() {
    static Database instance;   // created once, destroyed at program exit
    return instance;
}

void Database::open(const std::string& path) {

    if (db_) {
        return; // db already open
    }

    // open the database
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("sqlite open failed: " + std::string(sqlite3_errmsg(db_)));
    }

    //
    // setup schema (if it doesn't already exist)
    //
    initSchema();
}

void Database::close(void) {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

//****************************************************************
// database design for simplereaderd server daemon (this is what we sync)
//
// users: a list of all users
// books: a library of all books
// user_books: a list of a user's books
// user_highlights: a list of a user's highlights for a book
// user_bookmarks: a list of a user's bookmarks for a book
// user_notes: a list of a user's notes for a book
//
//****************************************************************
static void execOrThrow(sqlite3* db, const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : sqlite3_errmsg(db);
        if (errmsg) sqlite3_free(errmsg);  // free exactly once
        throw std::runtime_error(msg);
    }
}

void Database::initSchema(void) {
    execOrThrow(db_, "PRAGMA foreign_keys = ON;");

    try {
        execOrThrow(db_, "BEGIN IMMEDIATE;");

        //
        //****************************************************************
        //  users:  list of all valid users and their (hashed) passwords
        //
        //  CREATE TABLE IF NOT EXISTS users (
        //    username   TEXT PRIMARY KEY,      // a unique username
        //    pwd_hash   TEXT NOT NULL,         // hashed password
        //    created_at INTEGER NOT NULL )     // when the user was added
        //
        //****************************************************************
        execOrThrow(db_, R"SQL(
            CREATE TABLE IF NOT EXISTS users (
              username   TEXT PRIMARY KEY,
              pwd_hash   TEXT NOT NULL,
              created_at INTEGER NOT NULL
            );
        )SQL");

        //
        //****************************************************************
        //  books:  list of all epub/pdf books we have in our library
        //          note: "sha256" should be enough to identify the same physical book (epub/pdf),
        //                and use "filesize" as a sanity check
        //
        // CREATE TABLE IF NOT EXISTS books (
        //    file_id    TEXT PRIMARY KEY,                          // server UUID
        //    sha256     TEXT NOT NULL CHECK (length(sha256) = 64), // checksum of file
        //    filesize   INTEGER NOT NULL CHECK (filesize >= 0),    // size of the file
        //    location   TEXT NOT NULL,                             // physical location on server (/var/lib/simplereader/library/...)
        //    filename   TEXT NOT NULL,                             // filename used by the client (not by the server).  
        //                                                          // Used when downloading to tell client what to call the file.
        //    updated_at INTEGER NOT NULL );                        // UTC time when book was added to the library
        //
        //****************************************************************
        execOrThrow(db_, R"SQL(
            CREATE TABLE IF NOT EXISTS books (
              file_id    TEXT PRIMARY KEY,
              sha256     TEXT NOT NULL CHECK (length(sha256) = 64),
              filesize   INTEGER NOT NULL CHECK (filesize >= 0),
              location   TEXT NOT NULL,
              filename   TEXT NOT NULL,
              updated_at INTEGER NOT NULL,
              UNIQUE (sha256, filesize)
            );
        )SQL");

        //
        //****************************************************************
        //  user_books: list of all books that a username has ever had (including deleted ones)
        //
        // CREATE TABLE IF NOT EXISTS user_books (
        //    username    TEXT NOT NULL,            // primary key
        //    file_id     TEXT NOT NULL,            // primary key
        //    progress    TEXT,                     // client-only:  JSON blob
        //
        //    updated_at  INTEGER NOT NULL,         // UTC timestamp this record last updated
        //    deleted_at  INTEGER,                  // zero means still active, non-zero is UTC tombstone
        //
        //    PRIMARY KEY (username, file_id),
        //
        //    FOREIGN KEY (username)                // username must link to a user in "users"
        //      REFERENCES users(username)
        //      ON DELETE CASCADE                   // when username is deleted from "users", delete their records from "user_books" too
        //      ON UPDATE NO ACTION,
        //
        //    FOREIGN KEY (file_id)                 // file_id must link to a book in "books"
        //      REFERENCES books(file_id)
        //      ON DELETE RESTRICT
        //      ON UPDATE NO ACTION );
        //
        //  CREATE INDEX IF NOT EXISTS idx_user_books_user_updated ON user_books (username, updated_at);
        //  CREATE INDEX IF NOT EXISTS idx_user_books_user_deleted ON user_books (username, deleted_at);
        //
        //****************************************************************
        execOrThrow(db_, R"SQL(
            CREATE TABLE IF NOT EXISTS user_books (
              username    TEXT NOT NULL,
              file_id     TEXT NOT NULL,
              progress    TEXT,
              updated_at  INTEGER NOT NULL,
              deleted_at  INTEGER,
              PRIMARY KEY (username, file_id),
              FOREIGN KEY (username) REFERENCES users(username) ON DELETE CASCADE ON UPDATE NO ACTION,
              FOREIGN KEY (file_id)  REFERENCES books(file_id)  ON DELETE RESTRICT ON UPDATE NO ACTION
            );
            CREATE INDEX IF NOT EXISTS idx_user_books_user_updated ON user_books (username, updated_at);
            CREATE INDEX IF NOT EXISTS idx_user_books_user_deleted ON user_books (username, deleted_at);
        )SQL");


        //
        //****************************************************************
        //  user_highlights: list of all highlights that a username has ever had (including deleted ones)
        //
        // CREATE TABLE IF NOT EXISTS user_highlights (
        //   username    TEXT NOT NULL,
        //   file_id     TEXT NOT NULL,                 -- -> books.file_id
        //   id          INTEGER NOT NULL,              -- unique sequential number of this highlight
        //   selection   TEXT NOT NULL,                 -- JSON-serialized position
        //   label       TEXT,                          -- user label
        //   colour      TEXT,                          
        //   updated_at  INTEGER NOT NULL,              -- epoch millis (UTC)
        //   deleted_at  INTEGER,                       -- NULL = active; non-NULL = tombstone
        //
        //   PRIMARY KEY (username, file_id, id),
        //
        //   FOREIGN KEY (username)
        //     REFERENCES users(username)
        //     ON DELETE CASCADE
        //     ON UPDATE NO ACTION,
        //
        //   FOREIGN KEY (file_id)
        //     REFERENCES books(file_id)
        //     ON DELETE RESTRICT
        //     ON UPDATE NO ACTION
        // );
        execOrThrow(db_, R"SQL(
            CREATE TABLE IF NOT EXISTS user_highlights (
                username    TEXT NOT NULL,
                file_id     TEXT NOT NULL,
                id          INTEGER NOT NULL,
                selection   TEXT NOT NULL,
                label       TEXT,
                colour      TEXT,
                updated_at  INTEGER NOT NULL,
                deleted_at  INTEGER,
                PRIMARY KEY (username, file_id, id),
                FOREIGN KEY (username) REFERENCES users(username) ON DELETE CASCADE ON UPDATE NO ACTION,
                FOREIGN KEY (file_id) REFERENCES books(file_id) ON DELETE RESTRICT ON UPDATE NO ACTION
            );
            CREATE INDEX IF NOT EXISTS idx_user_highlights_user_updated ON user_highlights (username, updated_at);
            CREATE INDEX IF NOT EXISTS idx_user_highlights_user_deleted ON user_highlights (username, deleted_at);
        )SQL");

        //
        //****************************************************************
        //  user_bookmarks: list of all bookmarks that a username has ever had (including deleted ones)
        //
        // CREATE TABLE IF NOT EXISTS user_bookmarks (
        //   username    TEXT NOT NULL,
        //   file_id     TEXT NOT NULL,                 -- -> books.file_id
        //   id          INTEGER NOT NULL,              -- unique sequential index for bookmark
        //   locator     TEXT NOT NULL,                 -- JSON-serialized position (string)
        //   label       TEXT,                          -- user label
        //   updated_at  INTEGER NOT NULL,              -- epoch millis (UTC)
        //   deleted_at  INTEGER,                       -- NULL = active; non-NULL = tombstone
        //
        //   PRIMARY KEY (username, file_id, id),
        //
        //   FOREIGN KEY (username)
        //     REFERENCES users(username)
        //     ON DELETE CASCADE
        //     ON UPDATE NO ACTION,
        //
        //   FOREIGN KEY (file_id)
        //     REFERENCES books(file_id)
        //     ON DELETE RESTRICT
        //     ON UPDATE NO ACTION );
        execOrThrow(db_, R"SQL(
            CREATE TABLE IF NOT EXISTS user_bookmarks (
                username    TEXT NOT NULL,
                file_id     TEXT NOT NULL,
                id          INTEGER NOT NULL,
                locator     TEXT NOT NULL,
                label       TEXT,
                updated_at  INTEGER NOT NULL,
                deleted_at  INTEGER,
                PRIMARY KEY (username, file_id, id),
                FOREIGN KEY (username) REFERENCES users(username) ON DELETE CASCADE ON UPDATE NO ACTION,
                FOREIGN KEY (file_id) REFERENCES books(file_id) ON DELETE RESTRICT ON UPDATE NO ACTION
            );
            CREATE INDEX IF NOT EXISTS idx_user_bookmarks_user_updated ON user_bookmarks (username, updated_at);
            CREATE INDEX IF NOT EXISTS idx_user_bookmarks_user_deleted ON user_bookmarks (username, deleted_at);
        )SQL");

        //
        //****************************************************************
        //  user_notes: list of all notes that a username has ever had (including deleted ones)
        //
        // CREATE TABLE IF NOT EXISTS user_notes (
        //   username    TEXT NOT NULL,
        //   file_id     TEXT NOT NULL,                 -- -> books.file_id
        //   id          INTEGER NOT NULL,              -- unique sequential index for note
        //   locator     TEXT NOT NULL,                 -- JSON-serialized position (string)
        //   content     TEXT NOT NULL,                 -- text of the note
        //   updated_at  INTEGER NOT NULL,              -- epoch millis (UTC)
        //   deleted_at  INTEGER,                       -- NULL = active; non-NULL = tombstone
        //
        //   PRIMARY KEY (username, file_id, id),
        //
        //   FOREIGN KEY (username)
        //     REFERENCES users(username)
        //     ON DELETE CASCADE
        //     ON UPDATE NO ACTION,
        //
        //   FOREIGN KEY (file_id)
        //     REFERENCES books(file_id)
        //     ON DELETE RESTRICT
        //     ON UPDATE NO ACTION );
        execOrThrow(db_, R"SQL(
            CREATE TABLE IF NOT EXISTS user_notes (
                username    TEXT NOT NULL,
                file_id     TEXT NOT NULL,
                id          INTEGER NOT NULL,
                locator     TEXT NOT NULL,
                content     TEXT NOT NULL,
                updated_at  INTEGER NOT NULL,
                deleted_at  INTEGER,
                PRIMARY KEY (username, file_id, id),
                FOREIGN KEY (username) REFERENCES users(username) ON DELETE CASCADE ON UPDATE NO ACTION,
                FOREIGN KEY (file_id) REFERENCES books(file_id) ON DELETE RESTRICT ON UPDATE NO ACTION
            );
            CREATE INDEX IF NOT EXISTS idx_user_notes_user_updated ON user_notes (username, updated_at);
            CREATE INDEX IF NOT EXISTS idx_user_notes_user_deleted ON user_notes (username, deleted_at);
        )SQL");

        execOrThrow(db_, "COMMIT;");
    } catch (...) {
        // Only needed because we started a transaction above.
        // Safe to call even if no tx is open; SQLite will no-op/return OK.
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

// helper function: does book with fileId exist in the "books" table?
bool Database::bookExists(const std::string& fileId) {
    static const char* SQL = "SELECT 1 FROM books WHERE file_id=?1 LIMIT 1";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (bookExists)");
    sqlite3_bind_text(s, 1, fileId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return (rc == SQLITE_ROW);
}

/////////////////////////////////////////////////////////////
// POST /check
//
static Database::RowState fetchRowState(sqlite3* db, sqlite3_stmt* stmt) {
    Database::RowState st;
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const long long upd = sqlite3_column_int64(stmt, 0);
        const bool hasDel   = (sqlite3_column_type(stmt, 1) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 1) : 0;

        st.exists    = !hasDel;
        st.deleted   = hasDel;
        st.updatedAt = upd;
        st.deletedAt = del;
        return st;
    }
    if (rc == SQLITE_DONE)
        return st; // default: exists=false, deleted=false

    // rc is an error code
    syslog(SYSLOG_ERR,"fetchRowState() rc=%d %s", rc, sqlite3_errmsg(db));

    throw std::runtime_error(std::string("sqlite step failed: ") + sqlite3_errmsg(db));
}

static void prepOrThrow(sqlite3* db, const char* sql, sqlite3_stmt** out) {
    if (sqlite3_prepare_v2(db, sql, -1, out, nullptr) != SQLITE_OK)
        throw std::runtime_error("sqlite prepare failed");
}

Database::RowState Database::select_userBooks_byUserAndFileId(
    const std::string& username, const std::string& fileId) {
    static const char* SQL =
        "SELECT updated_at, deleted_at FROM user_books "
        "WHERE username = ?1 AND file_id = ?2 LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    prepOrThrow(db_, SQL, &stmt);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);

    auto st = fetchRowState(db_, stmt);
    sqlite3_finalize(stmt);
    return st;
}

Database::RowState Database::select_byUserFileAndItemId(const std::string& table,
                                                        const std::string& username,
                                                        const std::string& fileId,
                                                        long long itemId) {
    const std::string sql =
        "SELECT updated_at, deleted_at FROM " + table +
        " WHERE username = ?1 AND file_id = ?2 AND id = ?3 LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
       throw std::runtime_error("prepare failed (composite key)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(itemId));

    auto st = fetchRowState(db_, stmt);
    sqlite3_finalize(stmt);
    return st;
}

/////////////////////////////////////////////////////////////
// POST /resolve
//  returns: fileId if found, otherwise null
//
std::string Database::lookupFileIdByHashSize(const std::string& sha256, long long filesize) {
    static const char* SQL =
        "SELECT file_id FROM books WHERE sha256 = ?1 AND filesize = ?2 LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (resolve lookup)");

    sqlite3_bind_text (stmt, 1, sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(filesize));

    std::string fileId;
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) fileId.assign(reinterpret_cast<const char*>(txt));
    } else if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"lookupFileIdByHashSize() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw std::runtime_error(std::string("sqlite step failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
    return fileId; // if fileId is empty, means file not found
}

/////////////////////////////////////////////////////////////
// POST /get
//
void Database::listUserBook(const std::string& username, const std::string& fileId, Json::Value& rowsOut) {
    static const char* SQL =
        "SELECT progress, updated_at, deleted_at "
        "FROM user_books WHERE username=?1 AND file_id=?2 LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (listUserBook)");

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char* prog = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const long long upd = sqlite3_column_int64(stmt, 1);
        const bool hasDel   = (sqlite3_column_type(stmt, 2) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 2) : 0;

        Json::Value row(Json::objectValue);
        row["progress"]  = prog ? prog : "";
        row["updatedAt"] = static_cast<Json::Int64>(upd);
        if (del != 0)
            row["deletedAt"]   = static_cast<Json::Int64>(del);
        rowsOut.append(row);
    } else if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"listUserBook() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw std::runtime_error(std::string("sqlite step failed (listUserBook): ") + sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
}

void Database::listUserBookmarks(const std::string& username, const std::string& fileId, const int& id, Json::Value& rowsOut) {
    static const char* SQL_ALL = "SELECT id, locator, label, updated_at, deleted_at "
                                 "FROM user_bookmarks WHERE username=?1 AND file_id=?2 ORDER BY id ASC";
    static const char* SQL_ONE = "SELECT id, locator, label, updated_at, deleted_at "
                                 "FROM user_bookmarks WHERE username=?1 AND file_id=?2 AND id=?3";

    const char* SQL = (id<0) ? SQL_ALL : SQL_ONE;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (listUserBookmarks)");

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    if (id >= 0)
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));

    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserBookmarks() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (listUserBookmarks): ") + sqlite3_errmsg(db_));
        }

        Json::Value row(Json::objectValue);
        row["id"]        = static_cast<Json::Int64>(sqlite3_column_int64(stmt, 0));
        const char* loc  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* lab  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const long long upd = sqlite3_column_int64(stmt, 3);
        const bool hasDel = (sqlite3_column_type(stmt, 4) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 4) : 0;

        row["locator"]   = loc ? loc : "";
        if (lab) row["label"] = lab;
        row["updatedAt"] = static_cast<Json::Int64>(upd);
        if (del != 0)
            row["deletedAt"]   = static_cast<Json::Int64>(del);

        rowsOut.append(row);
    }

    sqlite3_finalize(stmt);
}

void Database::listUserHighlights(const std::string& username, const std::string& fileId, const int& id, Json::Value& rowsOut) {
    static const char* SQL_ALL = "SELECT id, selection, label, colour, updated_at, deleted_at "
                                 "FROM user_highlights WHERE username=?1 AND file_id=?2 ORDER BY id ASC";
    static const char* SQL_ONE = "SELECT id, selection, label, colour, updated_at, deleted_at "
                                 "FROM user_highlights WHERE username=?1 AND file_id=?2 AND id=?3";

    const char* SQL = (id<0) ? SQL_ALL : SQL_ONE;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (listUserHighlights)");

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    if (id >= 0)
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));

    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserHighlights() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (listUserHighlights): ") + sqlite3_errmsg(db_));
        }

        Json::Value row(Json::objectValue);
        row["id"]        = static_cast<Json::Int64>(sqlite3_column_int64(stmt, 0));
        const char* sel  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* lab  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* col  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const long long upd = sqlite3_column_int64(stmt, 4);
        const bool hasDel = (sqlite3_column_type(stmt, 5) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 5) : 0;

        row["selection"] = sel ? sel : "";
        if (lab) row["label"]   = lab;
        if (col) row["colour"]  = col;
        row["updatedAt"] = static_cast<Json::Int64>(upd);
        if (del != 0)
            row["deletedAt"]   = static_cast<Json::Int64>(del);

        rowsOut.append(row);
    }

    sqlite3_finalize(stmt);
}

void Database::listUserNotes(const std::string& username, const std::string& fileId, const int& id, Json::Value& rowsOut) {
    static const char* SQL_ALL = "SELECT id, locator, content, updated_at, deleted_at "
                                 "FROM user_notes WHERE username=?1 AND file_id=?2 ORDER BY id ASC";
    static const char* SQL_ONE = "SELECT id, locator, content, updated_at, deleted_at "
                                 "FROM user_notes WHERE username=?1 AND file_id=?2 AND id=?3";

    const char* SQL = (id<0) ? SQL_ALL : SQL_ONE;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (listUserHighlights)");

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    if (id >= 0)
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));

    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserNotes() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (listUserNotes): ") + sqlite3_errmsg(db_));
        }

        Json::Value row(Json::objectValue);
        row["id"]        = static_cast<Json::Int64>(sqlite3_column_int64(stmt, 0));
        const char* loc  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* txt  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const long long upd = sqlite3_column_int64(stmt, 3);
        const bool hasDel = (sqlite3_column_type(stmt, 4) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 4) : 0;

        row["location"] = loc ? loc : "";
        if (txt) row["content"]   = txt;
        row["updatedAt"] = static_cast<Json::Int64>(upd);
        if (del != 0)
            row["deletedAt"]   = static_cast<Json::Int64>(del);

        rowsOut.append(row);
    }

    sqlite3_finalize(stmt);
}

/////////////////////////////////////////////////////////////
// POST /getSince
//
static void computePagingNextSince(long long sinceIn,
                                   const std::vector<long long>& pageTs,
                                   bool hitExtra,
                                   long long& nextSinceOut) {
    if (hitExtra) {
        // nextSince = timestamp of the (limit+1)-th row (not returned)
        nextSinceOut = pageTs.back();
    } else if (!pageTs.empty()) {
        // nextSince = max ts we returned
        nextSinceOut = pageTs.back();
    } else {
        // no rows; keep the input since
        nextSinceOut = sinceIn;
    }
}

void Database::listUserBooksSince(const std::string& username, long long since, int limit,
                                  Json::Value& rowsOut, long long& nextSinceOut) {
    const int fetch = limit + 1;
    static const char* SQL =
        "SELECT file_id, progress, updated_at, deleted_at, "
        "       COALESCE(deleted_at, updated_at) AS ts "
        "FROM user_books "
        "WHERE username = ?1 AND COALESCE(deleted_at, updated_at) >= ?2 "
        "ORDER BY ts ASC, file_id ASC "
        "LIMIT ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (scanUserBooksSince)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(since));
    sqlite3_bind_int  (stmt, 3, fetch);

    std::vector<long long> tsSeen;
    int count = 0;
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserBooksSince() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (scanUserBooksSince): ")
                                     + sqlite3_errmsg(db_));
        }

        const char* fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* prog   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const long long upd = sqlite3_column_int64(stmt, 2);
        const bool hasDel   = (sqlite3_column_type(stmt, 3) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 3) : 0;
        const long long ts  = sqlite3_column_int64(stmt, 4);

        tsSeen.push_back(ts);
        if (count < limit) {
            Json::Value row(Json::objectValue);
            row["fileId"]    = fileId ? fileId : "";
            row["progress"]  = prog ? prog : "";
            row["updatedAt"] = static_cast<Json::Int64>(ts);
            if (hasDel)
                row["deletedAt"] = static_cast<Json::Int64>(del);
            rowsOut.append(row);
        }
        ++count;
    }
    sqlite3_finalize(stmt);

    computePagingNextSince(since, tsSeen, count > limit, nextSinceOut);
}

void Database::listUserBookmarksSince(const std::string& username, long long since, int limit,
                                      Json::Value& rowsOut, long long& nextSinceOut) {
    const int fetch = limit + 1;
    static const char* SQL =
        "SELECT file_id, id, locator, label, updated_at, deleted_at, "
        "       COALESCE(deleted_at, updated_at) AS ts "
        "FROM user_bookmarks "
        "WHERE username = ?1 AND COALESCE(deleted_at, updated_at) >= ?2 "
        "ORDER BY ts ASC, file_id ASC, id ASC "
        "LIMIT ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (scanUserBookmarksSince)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(since));
    sqlite3_bind_int  (stmt, 3, fetch);

    std::vector<long long> tsSeen;
    int count = 0;
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserBookmarksSince() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (scanUserBookmarksSince): ")
                                     + sqlite3_errmsg(db_));
        }

        const char* fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        long long id       = sqlite3_column_int64(stmt, 1);
        const char* loc    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* lab    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const long long upd = sqlite3_column_int64(stmt, 4);
        const bool hasDel   = (sqlite3_column_type(stmt, 5) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 5) : 0;
        const long long ts  = sqlite3_column_int64(stmt, 6);

        tsSeen.push_back(ts);
        if (count < limit) {
            Json::Value row(Json::objectValue);
            row["fileId"]    = fileId ? fileId : "";
            row["id"]        = static_cast<Json::Int64>(id);
            row["locator"]   = loc ? loc : "";
            if (lab) row["label"] = lab;
            row["updatedAt"] = static_cast<Json::Int64>(ts);
            if (hasDel)
                row["deletedAt"] = static_cast<Json::Int64>(del);
            rowsOut.append(row);
        }
        ++count;
    }
    sqlite3_finalize(stmt);

    computePagingNextSince(since, tsSeen, count > limit, nextSinceOut);
}

void Database::listUserHighlightsSince(const std::string& username, long long since, int limit,
                                       Json::Value& rowsOut, long long& nextSinceOut) {
    const int fetch = limit + 1;
    static const char* SQL =
        "SELECT file_id, id, selection, label, colour, updated_at, deleted_at, "
        "       COALESCE(deleted_at, updated_at) AS ts "
        "FROM user_highlights "
        "WHERE username = ?1 AND COALESCE(deleted_at, updated_at) >= ?2 "
        "ORDER BY ts ASC, file_id ASC, id ASC "
        "LIMIT ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (scanUserHighlightsSince)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(since));
    sqlite3_bind_int  (stmt, 3, limit + 1);

    std::vector<long long> tsSeen;
    int count = 0;
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserHighlightsSince() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (scanUserHighlightsSince): ")
                                     + sqlite3_errmsg(db_));
        }

        const char* fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        long long id       = sqlite3_column_int64(stmt, 1);
        const char* sel    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* lab    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* col    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const long long upd = sqlite3_column_int64(stmt, 5);
        const bool hasDel   = (sqlite3_column_type(stmt, 6) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 6) : 0;
        const long long ts  = sqlite3_column_int64(stmt, 7);

        tsSeen.push_back(ts);
        if (count < limit) {
            Json::Value row(Json::objectValue);
            row["fileId"]    = fileId ? fileId : "";
            row["id"]        = static_cast<Json::Int64>(id);
            row["selection"] = sel ? sel : "";
            if (lab) row["label"]  = lab;
            if (col) row["colour"] = col;
            row["updatedAt"] = static_cast<Json::Int64>(ts);
            if (hasDel)
                row["deletedAt"] = static_cast<Json::Int64>(del);
            rowsOut.append(row);
        }
        ++count;
    }
    sqlite3_finalize(stmt);

    computePagingNextSince(since, tsSeen, count > limit, nextSinceOut);
}

void Database::listUserNotesSince(const std::string& username, long long since, int limit,
                                      Json::Value& rowsOut, long long& nextSinceOut) {
    const int fetch = limit + 1;
    static const char* SQL =
        "SELECT file_id, id, locator, content, updated_at, deleted_at, "
        "       COALESCE(deleted_at, updated_at) AS ts "
        "FROM user_notes "
        "WHERE username = ?1 AND COALESCE(deleted_at, updated_at) >= ?2 "
        "ORDER BY ts ASC, file_id ASC, id ASC "
        "LIMIT ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (listUserNotesSince)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(since));
    sqlite3_bind_int  (stmt, 3, fetch);

    std::vector<long long> tsSeen;
    int count = 0;
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            syslog(SYSLOG_ERR,"listUserNotesSince() rc=%d %s", rc, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("sqlite step failed (scanUserNotesSince): ")
                                     + sqlite3_errmsg(db_));
        }

        const char* fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        long long id       = sqlite3_column_int64(stmt, 1);
        const char* loc    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* txt    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const long long upd = sqlite3_column_int64(stmt, 4);
        const bool hasDel   = (sqlite3_column_type(stmt, 5) != SQLITE_NULL);
        const long long del = hasDel ? sqlite3_column_int64(stmt, 5) : 0;
        const long long ts  = sqlite3_column_int64(stmt, 6);

        tsSeen.push_back(ts);
        if (count < limit) {
            Json::Value row(Json::objectValue);
            row["fileId"]    = fileId ? fileId : "";
            row["id"]        = static_cast<Json::Int64>(id);
            row["locator"]   = loc ? loc : "";
            if (txt) row["content"] = txt;
            row["updatedAt"] = static_cast<Json::Int64>(ts);
            if (hasDel)
                row["deletedAt"] = static_cast<Json::Int64>(del);
            rowsOut.append(row);
        }
        ++count;
    }
    sqlite3_finalize(stmt);

    computePagingNextSince(since, tsSeen, count > limit, nextSinceOut);
}

/////////////////////////////////////////////////////////////
// POST /update
//     note: in these insertUser*() funcs, set deleted_at = NULL when resurrect==true
void Database::insertUserBook(const std::string& username, const std::string& fileId,
                              const std::string& progress, bool resurrect, long long tnow) {
    static const char* SQL = R"SQL(
        INSERT INTO user_books (username, file_id, progress, updated_at, deleted_at)
        VALUES (?1, ?2, ?3, ?4, NULL)
        ON CONFLICT(username, file_id) DO UPDATE SET
            progress   = excluded.progress,
            updated_at = excluded.updated_at,
            deleted_at = CASE WHEN ?5 THEN NULL ELSE user_books.deleted_at END
    )SQL";

    sqlite3_stmt* stmt=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (insertUserBook)");
    sqlite3_bind_text (stmt, 1, username.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, fileId.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, progress.c_str(),-1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, tnow);
    sqlite3_bind_int (stmt, 5, resurrect ? 1 : 0);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"insertUserBook() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw std::runtime_error(std::string("sqlite step failed (insertUserBook): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::insertUserBookmark(const std::string& username, const std::string& fileId, long long id,
                                  const std::string& locator, const std::string& label,
                                  bool resurrect, long long tnow) {
    static const char* SQL = R"SQL(
        INSERT INTO user_bookmarks (username, file_id, id, locator, label, updated_at, deleted_at)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, NULL)
        ON CONFLICT(username, file_id, id) DO UPDATE SET
            locator    = excluded.locator,
            label      = excluded.label,
            updated_at = excluded.updated_at,
            deleted_at = CASE WHEN ?7 THEN NULL ELSE user_bookmarks.deleted_at END
    )SQL";
    sqlite3_stmt* stmt=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (insertUserBookmark)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));
    sqlite3_bind_text (stmt, 4, locator.c_str(),  -1, SQLITE_TRANSIENT);
    if (!label.empty()) 
        sqlite3_bind_text(stmt, 5, label.c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 5);
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(tnow));
    sqlite3_bind_int (stmt, 7, resurrect ? 1 : 0);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"insertUserBookmark() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw std::runtime_error(std::string("sqlite step failed (insertUserBookmark): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::insertUserHighlight(const std::string& username, const std::string& fileId, long long id,
                                   const std::string& selection, const std::string& label, const std::string& colour,
                                   bool resurrect, long long tnow) {
    static const char* SQL = R"SQL(
        INSERT INTO user_highlights (username, file_id, id, selection, label, colour, updated_at, deleted_at)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, NULL)
        ON CONFLICT(username, file_id, id) DO UPDATE SET
            selection  = excluded.selection,
            label      = excluded.label,
            colour     = excluded.colour,
            updated_at = excluded.updated_at,
            deleted_at = CASE WHEN ?8 THEN NULL ELSE user_highlights.deleted_at END
    )SQL";

    sqlite3_stmt* stmt=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (insertUserHighlight)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));
    sqlite3_bind_text (stmt, 4, selection.c_str(), -1, SQLITE_TRANSIENT);
    if (!label.empty())
        sqlite3_bind_text(stmt, 5, label.c_str(),  -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 5);
    if (!colour.empty())
        sqlite3_bind_text(stmt, 6, colour.c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 6);
    sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(tnow));
    sqlite3_bind_int (stmt, 8, resurrect ? 1 : 0);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"insertUserHighlight() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw std::runtime_error(std::string("sqlite step failed (insertUserHighlight): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::insertUserNote(const std::string& username, const std::string& fileId, long long id,
                                  const std::string& locator, const std::string& content,
                                  bool resurrect, long long tnow) {
    static const char* SQL = R"SQL(
        INSERT INTO user_notes (username, file_id, id, locator, content, updated_at, deleted_at)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, NULL)
        ON CONFLICT(username, file_id, id) DO UPDATE SET
            locator    = excluded.locator,
            content    = excluded.content,
            updated_at = excluded.updated_at,
            deleted_at = CASE WHEN ?7 THEN NULL ELSE user_notes.deleted_at END
    )SQL";
    sqlite3_stmt* stmt=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (insertUserNote)");
    sqlite3_bind_text (stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, fileId.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));
    sqlite3_bind_text (stmt, 4, locator.c_str(),  -1, SQLITE_TRANSIENT);
    if (!content.empty())
        sqlite3_bind_text(stmt, 5, content.c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 5);
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(tnow));
    sqlite3_bind_int (stmt, 7, resurrect ? 1 : 0);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"insertUserNote() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw std::runtime_error(std::string("sqlite step failed (insertUserNote): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

/////////////////////////////////////////////////////////////
// POST /delete
void Database::softDeleteUserBook(const std::string& user, const std::string& fileId, long long tm) {
    static const char* SQL =
        "UPDATE user_books SET deleted_at=?3, updated_at=?3 WHERE username=?1 AND file_id=?2";

    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserBook)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(tm));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteUserBook() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserBook): ") + sqlite3_errmsg(db_));
    }

    sqlite3_finalize(s);
}

void Database::softDeleteUserBookmark(const std::string& user, const std::string& fileId, long long id, long long tnow) {
    static const char* SQL =
        "UPDATE user_bookmarks SET deleted_at=?4, updated_at=?4 WHERE username=?1 AND file_id=?2 AND id=?3";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserBookmark)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(id));
    sqlite3_bind_int64(s, 4, static_cast<sqlite3_int64>(tnow));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteUserBookmark() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserBookmark): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}

void Database::softDeleteUserHighlight(const std::string& user, const std::string& fileId, long long id, long long tnow) {
    static const char* SQL =
        "UPDATE user_highlights SET deleted_at=?4, updated_at=?4 WHERE username=?1 AND file_id=?2 AND id=?3";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserHighlight)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(id));
    sqlite3_bind_int64(s, 4, static_cast<sqlite3_int64>(tnow));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteUserHighlight() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserHighlight): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}

void Database::softDeleteUserNote(const std::string& user, const std::string& fileId, long long id, long long tnow) {
    static const char* SQL =
        "UPDATE user_notes SET deleted_at=?4, updated_at=?4 WHERE username=?1 AND file_id=?2 AND id=?3";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserNote)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(id));
    sqlite3_bind_int64(s, 4, static_cast<sqlite3_int64>(tnow));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteUserNote() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserNote): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}

void Database::softDeleteUserBookmarkAll(const std::string& user, const std::string& fileId, long long tnow) {
    static const char* SQL =
        "UPDATE user_bookmarks SET deleted_at=?3, updated_at=?3 WHERE username=?1 AND file_id=?2";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserBookmarkAll)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(tnow));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteUserBookmarkAll() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserBookmarkAll): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}

void Database::softDeleteUserHighlightAll(const std::string& user, const std::string& fileId, long long tnow) {
    static const char* SQL =
        "UPDATE user_highlights SET deleted_at=?3, updated_at=?3 WHERE username=?1 AND file_id=?2";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserHighlightAll)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(tnow));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteUserHighlightAll() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserHighlightAll): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}

void Database::softDeleteUserNoteAll(const std::string& user, const std::string& fileId, long long tnow) {
    static const char* SQL =
        "UPDATE user_notes SET deleted_at=?3, updated_at=?3 WHERE username=?1 AND file_id=?2";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (softDeleteUserNoteAll)");
    sqlite3_bind_text (s, 1, user.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(tnow));
    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"softDeleteuserNote() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (softDeleteUserNoteAll): ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}


/////////////////////////////////////////////////////////////
// GET /book
//
// returns: empty string if there was an error
//          name by which the client knows this book
std::string Database::getBookForDownload(const std::string& fileId,
                                  std::string& locationOut,
                                  long long&   filesizeOut,
                                  std::string& sha256Out) {
    static const char* SQL =
        "SELECT location, filesize, sha256, filename FROM books WHERE file_id=?1 LIMIT 1";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (getBookForDownload)");

    sqlite3_bind_text(s, 1, fileId.c_str(), -1, SQLITE_TRANSIENT);

    std::string clientFileName;
    int rc = sqlite3_step(s);
    if (rc == SQLITE_ROW) {
        const char* loc = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        const long long sz = sqlite3_column_int64(s, 1);
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
        const char* fn = reinterpret_cast<const char*>(sqlite3_column_text(s, 3));
        if (loc && hash) {
            locationOut = loc;
            filesizeOut = sz;
            sha256Out   = hash;
            clientFileName = fn;
        }
    } else if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"getBookForDownload() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (getBookForDownload): ")
                                 + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
    return clientFileName;
}

/////////////////////////////////////////////////////////////
// POST /uploadBook
void Database::insertBookRecord(const std::string& fileId,
                                const std::string& sha256,
                                long long filesize,
                                const std::string& location,
                                const std::string& clientFileName,
                                long long updatedAt) {
    static const char* SQL =
        "INSERT INTO books(file_id, sha256, filesize, location, filename, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6)";
    sqlite3_stmt* s=nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare failed (insertBookRecord)");
    sqlite3_bind_text (s, 1, fileId.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 2, sha256.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(filesize));
    sqlite3_bind_text (s, 4, location.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 5, clientFileName.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(updatedAt));

    int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        syslog(SYSLOG_ERR,"insertBookRecord() rc=%d %s", rc, sqlite3_errmsg(db_));
        sqlite3_finalize(s);
        throw std::runtime_error(std::string("sqlite step failed (insertBookRecord): ")
                                 + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(s);
}
