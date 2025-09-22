#ifndef SIMPLEREADER_DATABASE_H
#define SIMPLEREADER_DATABASE_H

#include <string>

#include <sqlite3.h>
#include <json/value.h>

class Database {
    public:
        struct RowState {
            bool      exists    = false;   // row exists AND deleted_at IS NULL
            bool      deleted   = false;   // tombstone present
            long long updatedAt = 0;       // valid when exists==true
            long long deletedAt = 0;       // valid when deleted==true
        };

        static Database& get();     // singleton instance
        sqlite3* handle() { return db_; }

        void open(const std::string& path);
        void close(void);

        // helpers
        bool bookExists(const std::string& fileId);

        // user_books lookup
        RowState select_userBooks_byUserAndFileId (const std::string& username, const std::string& fileId);

        // highlights / bookmarks (composite key: username/fileId)
        RowState select_byUserFileAndItemId(const std::string& table,
                                            const std::string& username,
                                            const std::string& fileId,
                                            long long itemId);        

        // check for book by sha256+filesize in "books"
        std::string lookupFileIdByHashSize(const std::string& sha256, long long filesize);

        // Books: append 0 or 1 row: {fileId, progress, updatedAt, deleted}
        void listUserBook(const std::string& username, const std::string& fileId, Json::Value& rowsOut);

        // Bookmarks: append rows: {fileId, id, locator, label, updatedAt, deleted}
        void listUserBookmarks(const std::string& username, const std::string& fileId, const int& id, Json::Value& rowsOut);

        // Highlights: append rows: {fileId, id, selection, label, colour, updatedAt, deleted}
        void listUserHighlights(const std::string& username, const std::string& fileId, const int& id, Json::Value& rowsOut);

        // for /getSince
        void listUserBooksSince(const std::string& username, long long since, int limit,
                                Json::Value& rowsOut, long long& nextSinceOut);
        void listUserBookmarksSince(const std::string& username, long long since, int limit,
                                    Json::Value& rowsOut, long long& nextSinceOut);
        void listUserHighlightsSince(const std::string& username, long long since, int limit,
                                    Json::Value& rowsOut, long long& nextSinceOut);
                                    
        // for /update
        void insertUserBook(const std::string& user, const std::string& fileId,
                            const std::string& progress, bool resurrect, long long nowMs);

        void insertUserBookmark(const std::string& user, const std::string& fileId, long long id,
                                const std::string& locator, const std::string& label,
                                bool resurrect, long long nowMs);

        void insertUserHighlight(const std::string& user, const std::string& fileId, long long id,
                                const std::string& selection, const std::string& label, const std::string& colour,
                                bool resurrect, long long nowMs);

        // for /delete
        // Database.h
        void softDeleteUserBook(const std::string& user, const std::string& fileId, long long nowMs);
        void softDeleteUserBookmark(const std::string& user, const std::string& fileId, long long id, long long nowMs);
        void softDeleteUserHighlight(const std::string& user, const std::string& fileId, long long id, long long nowMs);
        void softDeleteUserBookmarkAll(const std::string& user, const std::string& fileId, long long nowMs);
        void softDeleteUserHighlightAll(const std::string& user, const std::string& fileId, long long nowMs);

        // for GET /book
        std::string getBookForDownload(const std::string& fileId, std::string& locationOut,
                                long long& filesizeOut, std::string& sha256Out);

        // for /uploadBook
        void insertBookRecord(const std::string& fileId, const std::string& sha256, long long filesize,
                              const std::string& location, const std::string& clientFileName, long long updatedAt);

    private:
        sqlite3* db_ = nullptr;

        // restrict construction/destruction/copy/equality
        Database() = default;
        ~Database() = default;
        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;

        void initSchema(void);  // build the db schema, if it doesn't exist

};

#endif