const rateLimit = require('express-rate-limit');
const RedisStore = require('rate-limit-redis');
const Redis = require('ioredis');
const config = require('../config');

let store;
if (process.env.REDIS_URL) {
    const redis = new Redis(process.env.REDIS_URL);
    store = new RedisStore({
        client: redis,
        prefix: 'rl:'
    });
}

const limiter = rateLimit({
    windowMs: config.RATE_LIMIT_WINDOW,
    max: config.RATE_LIMIT_MAX,
    standardHeaders: true,
    legacyHeaders: false,
    store,
    skip: (req) => req.path === '/health'
});

module.exports = limiter;