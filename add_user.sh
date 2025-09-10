#!/bin/bash
# Usage: ./add_user.sh username password

DB_PATH="/var/lib/simplereader/app.db"

if [ $# -ne 2 ]; then
  echo "Usage: $0 username password"
  exit 1
fi

USER="$1"
PASS="$2"

# Generate Argon2id hash using libsodium's CLI wrapper
# (crypto_pwhash_str output format, same as C API)
HASH=$(echo -n "$PASS" | sodium-pwhash \
  --opslimit=interactive \
  --memlimit=interactive \
  --algorithm=argon2id13)

if [ -z "$HASH" ]; then
  echo "Error: could not generate hash (is libsodium-utils installed?)"
  exit 1
fi

# Insert into SQLite
sqlite3 "$DB_PATH" <<EOF
INSERT OR REPLACE INTO users (username, pwd_hash, created_at)
VALUES ('$USER', '$HASH', strftime('%s','now'));
EOF

echo "User '$USER' inserted/updated in $DB_PATH"
