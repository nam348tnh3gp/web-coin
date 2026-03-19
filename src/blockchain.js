const crypto = require('crypto');
const EC = require('elliptic').ec;
const stringify = require('json-stable-stringify');
const ec = new EC('secp256k1');
const db = require('./db');
const config = require('./config');

class HMACManager {
    constructor() {
        this.secretKey = config.HMAC_SECRET;
        this.saltRotationInterval = config.HMAC_SALT_ROTATION_INTERVAL;
        this.maxAge = config.HMAC_MAX_AGE;
        this.saltLength = config.SALT_LENGTH;
        this.blockSaltLength = config.BLOCK_SALT_LENGTH;
        this.currentSalt = crypto.randomBytes(32).toString('hex');
        this.lastSaltRotation = Date.now();
    }

    generateSalt(length = this.saltLength) {
        return crypto.randomBytes(length).toString('hex');
    }

    rotateSaltIfNeeded() {
        if (Date.now() - this.lastSaltRotation > this.saltRotationInterval) {
            this.currentSalt = crypto.randomBytes(32).toString('hex');
            this.lastSaltRotation = Date.now();
        }
    }

    sign(data, customSalt = null) {
        this.rotateSaltIfNeeded();
        const salt = customSalt || this.currentSalt;
        const hmac = crypto.createHmac('sha256', this.secretKey + salt);
        const dataString = typeof data === 'string' ? data : stringify(data);
        hmac.update(dataString);
        return {
            signature: hmac.digest('hex'),
            salt: salt,
            timestamp: Date.now()
        };
    }

    verify(data, signature, salt) {
        const hmac = crypto.createHmac('sha256', this.secretKey + salt);
        const dataString = typeof data === 'string' ? data : stringify(data);
        hmac.update(dataString);
        return hmac.digest('hex') === signature;
    }
}

const hmacManager = new HMACManager();

class Transaction {
    constructor(from, to, amount, timestamp = Date.now()) {
        this.from = from;
        this.to = to;
        this.amount = amount;
        this.timestamp = timestamp;
        this.signature = null;
        this.salt = hmacManager.generateSalt();
        this.hmac = null;
    }

    hash() {
        return crypto.createHash('sha256')
            .update(this.from + this.to + this.amount + this.timestamp + this.salt)
            .digest('hex');
    }

    generateHMAC() {
        const txData = {
            from: this.from,
            to: this.to,
            amount: this.amount,
            timestamp: this.timestamp,
            hash: this.hash()
        };
        const { signature } = hmacManager.sign(txData, this.salt);
        this.hmac = signature;
        return signature;
    }

    verifyHMAC() {
        if (!this.hmac) return false;
        const txData = {
            from: this.from,
            to: this.to,
            amount: this.amount,
            timestamp: this.timestamp,
            hash: this.hash()
        };
        return hmacManager.verify(txData, this.hmac, this.salt);
    }

    sign(privateKey) {
        const key = ec.keyFromPrivate(privateKey, 'hex');
        const sig = key.sign(this.hash());
        this.signature = sig.toDER('hex');
        this.generateHMAC();
    }

    isValid() {
        if (this.from === null) return true;
        if (!this.signature) return false;
        if (!this.verifyHMAC()) return false;
        try {
            const key = ec.keyFromPublic(this.from, 'hex');
            return key.verify(this.hash(), this.signature);
        } catch {
            return false;
        }
    }

    toJSON() {
        return {
            from: this.from,
            to: this.to,
            amount: this.amount,
            timestamp: this.timestamp,
            signature: this.signature,
            salt: this.salt,
            hmac: this.hmac
        };
    }

    static fromJSON(json) {
        const tx = new Transaction(json.from, json.to, json.amount, json.timestamp);
        tx.signature = json.signature;
        tx.salt = json.salt;
        tx.hmac = json.hmac;
        return tx;
    }
}

class Block {
    constructor(height, transactions, previousHash, timestamp = Date.now(), nonce = 0) {
        this.height = height;
        this.timestamp = timestamp;
        this.transactions = transactions;
        this.previousHash = previousHash;
        this.nonce = nonce;
        this.blockSalt = hmacManager.generateSalt(config.BLOCK_SALT_LENGTH);
        this.blockHMAC = null;
        this.miningSalt = null;
        this.hash = this.calculateHash();
    }

    calculateHash() {
        const txString = this.transactions.map(tx => stringify(tx.toJSON())).join('');
        let data = this.height + this.previousHash + this.timestamp + txString + this.nonce;
        if (this.miningSalt) data += this.miningSalt;
        if (this.blockSalt) data += this.blockSalt;
        return crypto.createHash('sha1').update(data).digest('hex');
    }

    calculateMerkleRoot() {
        if (this.transactions.length === 0) return crypto.createHash('sha256').digest('hex');
        let hashes = this.transactions.map(tx => tx.hash());
        while (hashes.length > 1) {
            if (hashes.length % 2 !== 0) hashes.push(hashes[hashes.length - 1]);
            const newHashes = [];
            for (let i = 0; i < hashes.length; i += 2) {
                newHashes.push(crypto.createHash('sha256').update(hashes[i] + hashes[i + 1]).digest('hex'));
            }
            hashes = newHashes;
        }
        return hashes[0];
    }

    generateBlockHMAC() {
        const blockData = {
            height: this.height,
            hash: this.hash,
            previousHash: this.previousHash,
            timestamp: this.timestamp,
            nonce: this.nonce,
            txCount: this.transactions.length,
            merkleRoot: this.calculateMerkleRoot()
        };
        const { signature } = hmacManager.sign(blockData, this.blockSalt);
        this.blockHMAC = signature;
        return signature;
    }

    verifyBlockHMAC() {
        if (!this.blockHMAC) return false;
        const blockData = {
            height: this.height,
            hash: this.hash,
            previousHash: this.previousHash,
            timestamp: this.timestamp,
            nonce: this.nonce,
            txCount: this.transactions.length,
            merkleRoot: this.calculateMerkleRoot()
        };
        return hmacManager.verify(blockData, this.blockHMAC, this.blockSalt);
    }

    mine(difficulty) {
        const target = '0'.repeat(difficulty);
        this.miningSalt = crypto.randomBytes(4).toString('hex');
        while (!this.hash.startsWith(target)) {
            this.nonce++;
            this.hash = this.calculateHash();
        }
        this.generateBlockHMAC();
        return this;
    }

    toJSON() {
        return {
            height: this.height,
            hash: this.hash,
            previousHash: this.previousHash,
            timestamp: this.timestamp,
            nonce: this.nonce,
            blockSalt: this.blockSalt,
            blockHMAC: this.blockHMAC,
            miningSalt: this.miningSalt,
            transactions: this.transactions.map(tx => tx.toJSON()),
            merkleRoot: this.calculateMerkleRoot()
        };
    }

    static fromJSON(json) {
        const txs = json.transactions.map(t => Transaction.fromJSON(t));
        const block = new Block(json.height, txs, json.previousHash, json.timestamp, json.nonce);
        block.hash = json.hash;
        block.blockSalt = json.blockSalt;
        block.blockHMAC = json.blockHMAC;
        block.miningSalt = json.miningSalt;
        return block;
    }
}

class BlockchainController {
    static addBlock(block) {
        if (!block.verifyBlockHMAC()) {
            throw new Error('Invalid block HMAC signature');
        }

        db.prepare(`
            INSERT INTO blocks (
                height, hash, previous_hash, timestamp, nonce,
                transactions, block_salt, block_hmac, mining_salt, merkle_root
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `).run(
            block.height,
            block.hash,
            block.previousHash,
            block.timestamp,
            block.nonce,
            JSON.stringify(block.transactions.map(tx => tx.toJSON())),
            block.blockSalt,
            block.blockHMAC,
            block.miningSalt,
            block.calculateMerkleRoot()
        );
    }
}

module.exports = { Transaction, Block, BlockchainController, ec, hmacManager };
