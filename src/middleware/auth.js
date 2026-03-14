const jwt = require('jsonwebtoken');
const config = require('../config');

function authenticateToken(req, res, next) {
    const token = req.cookies?.token;
    if (!token) return res.status(401).json({ error: 'Unauthorized' });

    jwt.verify(token, config.JWT_SECRET, (err, user) => {
        if (err) return res.status(403).json({ error: 'Invalid token' });
        req.user = user; // { publicKey, displayAddress }
        next();
    });
}

module.exports = { authenticateToken };