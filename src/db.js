const Database = require('better-sqlite3');
const config = require('./config');

const db = new Database(config.DATABASE_PATH);

// Tối ưu hiệu năng và độ tin cậy
db.pragma('foreign_keys = ON');
db.pragma('journal_mode = WAL');
db.pragma('synchronous = NORMAL');

// Tạo bảng
db.exec(`
    CREATE TABLE IF NOT EXISTS blocks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        height INTEGER UNIQUE NOT NULL,
        hash TEXT UNIQUE NOT NULL,
        previous_hash TEXT NOT NULL,
        timestamp INTEGER NOT NULL,
        nonce INTEGER NOT NULL,
        transactions TEXT NOT NULL,
        created_at INTEGER DEFAULT (strftime('%s','now'))
    );
    CREATE INDEX IF NOT EXISTS idx_blocks_height ON blocks(height);
    CREATE INDEX IF NOT EXISTS idx_blocks_hash ON blocks(hash);

    CREATE TABLE IF NOT EXISTS mempool (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        tx_hash TEXT UNIQUE NOT NULL,
        from_addr TEXT,
        to_addr TEXT NOT NULL,
        amount INTEGER NOT NULL,
        timestamp INTEGER NOT NULL,
        signature TEXT NOT NULL,
        received_at INTEGER DEFAULT (strftime('%s','now'))
    );
    CREATE INDEX IF NOT EXISTS idx_mempool_time ON mempool(received_at);

    CREATE TABLE IF NOT EXISTS balances (
        address TEXT PRIMARY KEY,
        balance INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        public_key TEXT UNIQUE NOT NULL,
        encrypted_private_key TEXT NOT NULL,  -- JSON: {encrypted, iv, salt, tag}
        password_hash TEXT NOT NULL,
        display_address TEXT UNIQUE NOT NULL,
        created_at INTEGER DEFAULT (strftime('%s','now'))
    );
    CREATE INDEX IF NOT EXISTS idx_users_display ON users(display_address);
`);

module.exports = db;