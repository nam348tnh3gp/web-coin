require('dotenv').config();

module.exports = {
    NODE_ENV: process.env.NODE_ENV || 'development',
    PORT: process.env.PORT || 3000,
    DATABASE_PATH: process.env.DATABASE_PATH || './webcoin.db',

    // Blockchain
    INITIAL_DIFFICULTY: parseInt(process.env.DIFFICULTY) || 3,
    MINING_REWARD: parseInt(process.env.MINING_REWARD) || 100,
    MIN_REWARD: parseInt(process.env.MIN_REWARD) || 10,
    MAX_REWARD: parseInt(process.env.MAX_REWARD) || 200,
    BLOCK_TIME_TARGET: 60 * 1000,
    DIFFICULTY_ADJUSTMENT_INTERVAL: 10,
    MAX_PENDING_TRANSACTIONS: 1000,

    // HMAC Configuration
    HMAC_SECRET: process.env.HMAC_SECRET || 'webcoin-hmac-secret-key-2026',
    HMAC_SALT_ROTATION_INTERVAL: 24 * 60 * 60 * 1000,
    HMAC_MAX_AGE: 5 * 60 * 1000,
    API_KEY_EXPIRY: 7 * 24 * 60 * 60 * 1000,

    // Salt Configuration
    SALT_LENGTH: 16,
    BLOCK_SALT_LENGTH: 8,

    // PoW Configuration
    MIN_DIFFICULTY: 1,
    MAX_DIFFICULTY: 10,
    HASH_RATE_SAMPLES: 100,

    // Security
    JWT_SECRET: process.env.JWT_SECRET || 'your-very-strong-secret',
    JWT_EXPIRY: '7d',
    BCRYPT_ROUNDS: 12,
    SESSION_SECRET: process.env.SESSION_SECRET || 'session-secret',
    CORS_ORIGIN: process.env.CORS_ORIGIN ? process.env.CORS_ORIGIN.split(',') : ['http://localhost:3000'],
    RATE_LIMIT_WINDOW: 15 * 60 * 1000,
    RATE_LIMIT_MAX: 100,
    TRUST_PROXY: process.env.TRUST_PROXY || false,

    // Logging
    LOG_LEVEL: process.env.LOG_LEVEL || 'info',
};
