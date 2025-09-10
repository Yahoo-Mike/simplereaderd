// add_user.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <sodium.h>

#define DB_PATH "/var/lib/simplereader/app.db"

static int ensure_schema(sqlite3 *db) {
    const char *ddl =
        "CREATE TABLE IF NOT EXISTS users ("
        "  username   TEXT PRIMARY KEY,"
        "  pwd_hash   TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL"
        ");";
    char *err = NULL;
    int rc = sqlite3_exec(db, ddl, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "schema init failed: %s\n", err ? err : "unknown");
        sqlite3_free(err);
    }
    return rc;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <username> <password>\n", argv[0]);
        return 1;
    }
    const char *username = argv[1];
    const char *password = argv[2];

    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium init failed\n");
        return 1;
    }

    // Open DB
    sqlite3 *db = NULL;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite open failed: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return 1;
    }

    if (ensure_schema(db) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    // Hash password (Argon2id, interactive params)
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash, password, strlen(password),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        fprintf(stderr, "crypto_pwhash_str failed (OOM?)\n");
        sqlite3_close(db);
        return 1;
    }

    // Insert or replace user
    const char *sql =
        "INSERT OR REPLACE INTO users(username, pwd_hash, created_at) "
        "VALUES(?, ?, strftime('%s','now'));";

    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "prepare failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "insert failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_finalize(st);
    sqlite3_close(db);

    printf("User '%s' added/updated in %s\n", username, DB_PATH);
    return 0;
}
