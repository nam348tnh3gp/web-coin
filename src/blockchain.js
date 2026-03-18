const crypto = require('crypto');
const EC = require('elliptic').ec;
const ec = new EC('secp256k1');
const db = require('./db');
const config = require('./config');

// ============== HMAC MANAGER ==============
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
            console.log('🔄 HMAC salt rotated');
        }
    }

    sign(data, customSalt = null) {
        this.rotateSaltIfNeeded();
        const salt = customSalt || this.currentSalt;
        const hmac = crypto.createHmac('sha256', this.secretKey + salt);
        
        const dataString = typeof data === 'string' ? data : JSON.stringify(data);
        hmac.update(dataString);
        
        return {
            signature: hmac.digest('hex'),
            salt: salt,
            timestamp: Date.now()
        };
    }

    verify(data, signature, salt, maxAge = this.maxAge) {
        const hmac = crypto.createHmac('sha256', this.secretKey + salt);
        const dataString = typeof data === 'string' ? data : JSON.stringify(data);
        hmac.update(dataString);
        
        const calculated = hmac.digest('hex');
        return calculated === signature;
    }

    generateAPIKey(userId) {
        const salt = this.generateSalt();
        const data = {
            userId,
            created: Date.now(),
            nonce: crypto.randomBytes(8).toString('hex')
        };
        
        const { signature } = this.sign(data, salt);
        
        return {
            apiKey: Buffer.from(JSON.stringify(data)).toString('base64'),
            signature,
            salt
        };
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
        } catch (e) {
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
        tx.salt = json.salt || hmacManager.generateSalt();
        tx.hmac = json.hmac || null;
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
        this.miningSalt = null; // Salt cho PoW
        this.hash = this.calculateHash();
    }

    calculateHash() {
        const txString = this.transactions.map(tx => JSON.stringify(tx.toJSON())).join('');
        const miningData = this.miningSalt ? 
            this.height + this.previousHash + this.timestamp + txString + this.nonce + this.blockSalt + this.miningSalt :
            this.height + this.previousHash + this.timestamp + txString + this.nonce + this.blockSalt;
        
        return crypto.createHash('sha1')
            .update(miningData)
            .digest('hex');
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

    calculateMerkleRoot() {
        if (this.transactions.length === 0) return crypto.createHash('sha256').digest('hex');
        
        let hashes = this.transactions.map(tx => tx.hash());
        
        while (hashes.length > 1) {
            if (hashes.length % 2 !== 0) {
                hashes.push(hashes[hashes.length - 1]);
            }
            
            const newHashes = [];
            for (let i = 0; i < hashes.length; i += 2) {
                const combined = hashes[i] + hashes[i + 1];
                newHashes.push(crypto.createHash('sha256').update(combined).digest('hex'));
            }
            hashes = newHashes;
        }
        
        return hashes[0];
    }

    mine(difficulty) {
        const target = '0'.repeat(difficulty);
        const startTime = Date.now();
        let hashCount = 0;
        
        // Tạo mining salt mới cho mỗi lần mine
        this.miningSalt = crypto.randomBytes(4).toString('hex');
        
        while (this.hash.substring(0, difficulty) !== target) {
            this.nonce++;
            this.hash = this.calculateHash();
            hashCount++;
            
            // Báo cáo tiến độ mỗi 100k hash
            if (hashCount % 100000 === 0) {
                const elapsed = (Date.now() - startTime) / 1000;
                const hashrate = hashCount / elapsed;
                console.log(`⛏️  Mining... Nonce: ${this.nonce}, Hashrate: ${(hashrate/1000).toFixed(2)} kH/s`);
            }
        }
        
        this.generateBlockHMAC();
        
        const mineTime = (Date.now() - startTime) / 1000;
        const finalHashrate = hashCount / mineTime;
        
        console.log(`✅ Block #${this.height} mined in ${mineTime.toFixed(2)}s`);
        console.log(`   • Nonce: ${this.nonce}`);
        console.log(`   • Hash: ${this.hash.substring(0, 20)}...`);
        console.log(`   • Hashrate: ${(finalHashrate/1000).toFixed(2)} kH/s`);
        console.log(`   • HMAC: ${this.blockHMAC.substring(0, 16)}...`);
        
        return this;
    }

    hasValidTransactions() {
        for (let i = 1; i < this.transactions.length; i++) {
            if (!this.transactions[i].isValid()) return false;
        }
        return true;
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
        block.blockSalt = json.blockSalt || hmacManager.generateSalt(config.BLOCK_SALT_LENGTH);
        block.blockHMAC = json.blockHMAC || null;
        block.miningSalt = json.miningSalt || null;
        return block;
    }
}

class BlockchainController {
    static calculateReward(difficulty) {
        const maxDifficulty = 10;
        const minReward = config.MIN_REWARD;
        const maxReward = config.MAX_REWARD;
        
        let reward = minReward + (difficulty * (maxReward - minReward) / maxDifficulty);
        reward = Math.round(reward);
        reward = Math.min(maxReward, Math.max(minReward, reward));
        
        return reward;
    }

    static getLatestBlock() {
        const row = db.prepare('SELECT * FROM blocks ORDER BY height DESC LIMIT 1').get();
        if (!row) return null;
        return Block.fromJSON({
            height: row.height,
            hash: row.hash,
            previousHash: row.previous_hash,
            timestamp: row.timestamp,
            nonce: row.nonce,
            transactions: JSON.parse(row.transactions)
        });
    }

    static getBlockByHeight(height) {
        const row = db.prepare('SELECT * FROM blocks WHERE height = ?').get(height);
        if (!row) return null;
        return Block.fromJSON({
            height: row.height,
            hash: row.hash,
            previousHash: row.previous_hash,
            timestamp: row.timestamp,
            nonce: row.nonce,
            transactions: JSON.parse(row.transactions)
        });
    }

    static getAllBlocks(limit = null, offset = null) {
        let sql = 'SELECT * FROM blocks ORDER BY height ASC';
        if (limit) sql += ` LIMIT ${limit}`;
        if (offset) sql += ` OFFSET ${offset}`;
        const rows = db.prepare(sql).all();
        return rows.map(row => Block.fromJSON({
            height: row.height,
            hash: row.hash,
            previousHash: row.previous_hash,
            timestamp: row.timestamp,
            nonce: row.nonce,
            transactions: JSON.parse(row.transactions)
        }));
    }

    static getBalance(address) {
        const row = db.prepare('SELECT balance FROM balances WHERE address = ?').get(address);
        return row ? row.balance : 0;
    }

    static addBlock(block) {
        if (!block.verifyBlockHMAC()) {
            throw new Error('Invalid block HMAC signature');
        }
        
        const addBlockTx = db.transaction((block) => {
            const stmt = db.prepare(`
                INSERT INTO blocks (
                    height, hash, previous_hash, timestamp, nonce, 
                    transactions, block_salt, block_hmac, mining_salt, merkle_root
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            `);
            
            stmt.run(
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

            for (const tx of block.transactions) {
                if (tx.from === null) {
                    db.prepare(`
                        INSERT INTO balances (address, balance) VALUES (?, ?)
                        ON CONFLICT(address) DO UPDATE SET balance = balance + excluded.balance
                    `).run(tx.to, tx.amount);
                } else {
                    db.prepare(`
                        UPDATE balances SET balance = balance - ? WHERE address = ?
                    `).run(tx.amount, tx.from);
                    
                    db.prepare(`
                        INSERT INTO balances (address, balance) VALUES (?, ?)
                        ON CONFLICT(address) DO UPDATE SET balance = balance + ?
                    `).run(tx.to, tx.amount, tx.amount);
                }
            }
        });

        addBlockTx(block);
    }

    static addToMempool(tx) {
        if (!tx.verifyHMAC()) {
            throw new Error('Invalid transaction HMAC');
        }
        
        const stmt = db.prepare(`
            INSERT OR IGNORE INTO mempool (
                tx_hash, from_addr, to_addr, amount, timestamp, 
                signature, salt, hmac
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        `);
        
        stmt.run(
            tx.hash(), 
            tx.from, 
            tx.to, 
            tx.amount, 
            tx.timestamp, 
            tx.signature,
            tx.salt,
            tx.hmac
        );
    }

    static getMempool(limit = config.MAX_PENDING_TRANSACTIONS) {
        const rows = db.prepare('SELECT * FROM mempool ORDER BY received_at ASC LIMIT ?').all(limit);
        return rows.map(row => ({
            from: row.from_addr,
            to: row.to_addr,
            amount: row.amount,
            timestamp: row.timestamp,
            signature: row.signature,
            tx_hash: row.tx_hash,
            salt: row.salt,
            hmac: row.hmac
        }));
    }

    static removeFromMempool(txHashes) {
        if (txHashes.length === 0) return;
        const placeholders = txHashes.map(() => '?').join(',');
        db.prepare(`DELETE FROM mempool WHERE tx_hash IN (${placeholders})`).run(...txHashes);
    }

    static adjustDifficulty() {
        const latest = BlockchainController.getLatestBlock();
        if (!latest || latest.height < config.DIFFICULTY_ADJUSTMENT_INTERVAL) {
            return config.INITIAL_DIFFICULTY;
        }
        
        const prevBlock = BlockchainController.getBlockByHeight(
            latest.height - config.DIFFICULTY_ADJUSTMENT_INTERVAL
        );
        
        const timeDiff = latest.timestamp - prevBlock.timestamp;
        const expectedTime = config.BLOCK_TIME_TARGET * config.DIFFICULTY_ADJUSTMENT_INTERVAL;
        
        let newDifficulty = config.INITIAL_DIFFICULTY;
        
        if (timeDiff < expectedTime * 0.5) {
            newDifficulty = Math.min(10, config.INITIAL_DIFFICULTY + 1);
        } else if (timeDiff > expectedTime * 1.5) {
            newDifficulty = Math.max(1, config.INITIAL_DIFFICULTY - 1);
        }
        
        return newDifficulty;
    }

    static generateMinerAPIKey(userId) {
        return hmacManager.generateAPIKey(userId);
    }

    static verifyMinerAPIKey(apiKey, signature, salt) {
        try {
            const data = JSON.parse(Buffer.from(apiKey, 'base64').toString());
            
            if (Date.now() - data.created > config.API_KEY_EXPIRY) {
                return false;
            }
            
            return hmacManager.verify(data, signature, salt);
        } catch (e) {
            return false;
        }
    }

    static validatePoW(block, difficulty) {
        const target = '0'.repeat(difficulty);
        return block.hash.startsWith(target);
    }

    static getNetworkHashrate() {
        const blocks = BlockchainController.getAllBlocks(100);
        if (blocks.length < 2) return 0;
        
        const latestBlock = blocks[blocks.length - 1];
        const oldestBlock = blocks[0];
        const timeSpan = (latestBlock.timestamp - oldestBlock.timestamp) / 1000; // seconds
        const totalHashes = blocks.reduce((sum, block) => sum + block.nonce, 0);
        
        return totalHashes / timeSpan;
    }
}

function initDatabase() {
    try {
        db.exec(`
            ALTER TABLE mempool ADD COLUMN salt TEXT;
            ALTER TABLE mempool ADD COLUMN hmac TEXT;
            ALTER TABLE blocks ADD COLUMN block_salt TEXT;
            ALTER TABLE blocks ADD COLUMN block_hmac TEXT;
            ALTER TABLE blocks ADD COLUMN mining_salt TEXT;
            ALTER TABLE blocks ADD COLUMN merkle_root TEXT;
        `);
        console.log('✅ Database schema updated with HMAC/Salt columns');
    } catch (e) {
        // Columns may already exist
        console.log('ℹ️ Database schema already up to date');
    }
}

initDatabase();

module.exports = { 
    Transaction, 
    Block, 
    BlockchainController, 
    ec,
    hmacManager 
};
