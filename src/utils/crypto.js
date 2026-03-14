const crypto = require('crypto');

// Mã hóa private key với AES-256-CBC (dùng Base64)
function encryptPrivateKey(privateKey, password) {
    const iv = crypto.randomBytes(16);
    const salt = crypto.randomBytes(64);
    const key = crypto.pbkdf2Sync(password, salt, 100000, 32, 'sha256');
    const cipher = crypto.createCipheriv('aes-256-cbc', key, iv);
    const encrypted = Buffer.concat([cipher.update(privateKey, 'utf8'), cipher.final()]);
    return {
        encrypted: encrypted.toString('base64'),
        iv: iv.toString('base64'),
        salt: salt.toString('base64')
    };
}

module.exports = { encryptPrivateKey };