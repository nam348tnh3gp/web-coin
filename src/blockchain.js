const crypto = require('crypto');
const EC = require('elliptic').ec;
const ec = new EC('secp256k1');
const db = require('./db');
const config = require('./config');

class Transaction {
    constructor(from, to, amount, timestamp = Date.now()) {
        this.from = from;        // public key hex (null cho coinbase)
        this.to = to;
        this.amount = amount;
        this.timestamp = timestamp;
        this.signature = null;
    }

    // Dùng SHA-256 cho giao dịch
    hash() {
        return crypto.createHash('sha256')
            .update(this.from + this.to + this.amount + this.timestamp)
            .digest('hex');
    }

    sign(privateKey) {
        const key = ec.keyFromPrivate(privateKey, 'hex');
        const sig = key.sign(this.hash());
        this.signature = sig.toDER('hex');
    }

    isValid() {
        if (this.from === null) return true; // coinbase
        if (!this.signature) return false;
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
            signature: this.signature
        };
    }

    static fromJSON(json) {
        const tx = new Transaction(json.from, json.to, json.amount, json.timestamp);
        tx.signature = json.signature;
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
        this.hash = this.calculateHash();
    }

    // Dùng SHA-1 cho block (Proof-of-Work)
    calculateHash() {
        const txString = this.transactions.map(tx => JSON.stringify(tx.toJSON())).join('');
        return crypto.createHash('sha1')
            .update(this.height + this.previousHash + this.timestamp + txString + this.nonce)
            .digest('hex');
    }

    mine(difficulty) {
        const target = '0'.repeat(difficulty);
        while (this.hash.substring(0, difficulty) !== target) {
            this.nonce++;
            this.hash = this.calculateHash();
        }
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
            transactions: this.transactions.map(tx => tx.toJSON())
        };
    }

    static fromJSON(json) {
        const txs = json.transactions.map(t => Transaction.fromJSON(t));
        const block = new Block(json.height, txs, json.previousHash, json.timestamp, json.nonce);
        block.hash = json.hash;
        return block;
    }
}

class BlockchainController {
    // Hàm tính phần thưởng dựa trên độ khó
    static calculateReward(difficulty) {
        // Công thức: reward = min_reward + (difficulty * (max_reward - min_reward) / max_difficulty)
        // Giả sử max_difficulty = 10
        const maxDifficulty = 10;
        const minReward = config.MIN_REWARD;
        const maxReward = config.MAX_REWARD;
        
        // Tính phần thưởng tỷ lệ thuận với độ khó
        let reward = minReward + (difficulty * (maxReward - minReward) / maxDifficulty);
        
        // Làm tròn và đảm bảo trong khoảng cho phép
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
        const addBlockTx = db.transaction((block) => {
            // Lưu block
            const stmt = db.prepare(`
                INSERT INTO blocks (height, hash, previous_hash, timestamp, nonce, transactions)
                VALUES (?, ?, ?, ?, ?, ?)
            `);
            stmt.run(block.height, block.hash, block.previousHash, block.timestamp, block.nonce, JSON.stringify(block.transactions.map(tx => tx.toJSON())));

            // Cập nhật balances
            for (const tx of block.transactions) {
                if (tx.from === null) {
                    // Coinbase: cộng cho người nhận
                    db.prepare(`
                        INSERT INTO balances (address, balance) VALUES (?, ?)
                        ON CONFLICT(address) DO UPDATE SET balance = balance + excluded.balance
                    `).run(tx.to, tx.amount);
                } else {
                    // Giao dịch thường: trừ người gửi, cộng người nhận
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
        const stmt = db.prepare(`
            INSERT OR IGNORE INTO mempool (tx_hash, from_addr, to_addr, amount, timestamp, signature)
            VALUES (?, ?, ?, ?, ?, ?)
        `);
        stmt.run(tx.hash(), tx.from, tx.to, tx.amount, tx.timestamp, tx.signature);
    }

    static getMempool(limit = config.MAX_PENDING_TRANSACTIONS) {
        const rows = db.prepare('SELECT * FROM mempool ORDER BY received_at ASC LIMIT ?').all(limit);
        return rows.map(row => ({
            from: row.from_addr,
            to: row.to_addr,
            amount: row.amount,
            timestamp: row.timestamp,
            signature: row.signature,
            tx_hash: row.tx_hash
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
        const prevBlock = BlockchainController.getBlockByHeight(latest.height - config.DIFFICULTY_ADJUSTMENT_INTERVAL);
        const timeDiff = latest.timestamp - prevBlock.timestamp; // ms
        const expectedTime = config.BLOCK_TIME_TARGET * config.DIFFICULTY_ADJUSTMENT_INTERVAL;
        let newDifficulty = config.INITIAL_DIFFICULTY;
        
        if (timeDiff < expectedTime * 0.5) {
            newDifficulty = config.INITIAL_DIFFICULTY + 1;
        } else if (timeDiff > expectedTime * 1.5) {
            newDifficulty = Math.max(1, config.INITIAL_DIFFICULTY - 1);
        }
        return newDifficulty;
    }
}

module.exports = { Transaction, Block, BlockchainController, ec };