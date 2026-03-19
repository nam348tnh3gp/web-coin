const crypto = require('crypto');
const express = require('express');
const helmet = require('helmet');
const cors = require('cors');
const cookieParser = require('cookie-parser');
const bcrypt = require('bcrypt');
const jwt = require('jsonwebtoken');
const { v4: uuidv4 } = require('uuid');
const { Transaction, Block, BlockchainController, ec, hmacManager } = require('./blockchain');
const db = require('./db');
const config = require('./config');
const { authenticateToken } = require('./middleware/auth');
const { validate, registerSchema, loginSchema, transactionSchema, blockSchema } = require('./middleware/validate');
const { encryptPrivateKey } = require('./utils/crypto');
const logger = require('./utils/logger');

const app = express();

app.use(helmet({
    contentSecurityPolicy: {
        directives: {
            defaultSrc: ["'self'"],
            scriptSrc: ["'self'", "'unsafe-inline'", 'cdnjs.cloudflare.com'],
            styleSrc: ["'self'", "'unsafe-inline'"],
        }
    }
}));

const corsOptions = {
    origin: function (origin, callback) {
        if (!origin) return callback(null, true);
        if (config.NODE_ENV === 'production') {
            return callback(null, true);
        }
        if (config.CORS_ORIGIN.includes(origin)) {
            callback(null, true);
        } else {
            callback(new Error('Not allowed by CORS'));
        }
    },
    credentials: true,
    optionsSuccessStatus: 200
};
app.use(cors(corsOptions));

app.use(express.json({ limit: '1mb' }));
app.use(express.urlencoded({ extended: true, limit: '1mb' }));
app.use(cookieParser(config.SESSION_SECRET));

if (config.TRUST_PROXY) {
    app.set('trust proxy', 1);
}

app.use(express.static('public'));

function formatDisplayAddress(publicKey) {
    return 'W_' + publicKey;
}

function parseDisplayAddress(displayAddr) {
    if (displayAddr && displayAddr.startsWith('W_')) {
        return displayAddr.substring(2);
    }
    return displayAddr;
}

app.get('/api/info', (req, res) => {
    try {
        const latest = BlockchainController.getLatestBlock();
        const pendingCount = db.prepare('SELECT COUNT(*) as count FROM mempool').get().count;
        const difficulty = BlockchainController.adjustDifficulty();
        const reward = BlockchainController.calculateReward(difficulty);
        const networkHashrate = BlockchainController.getNetworkHashrate();
        
        res.json({
            latestBlock: latest ? latest.toJSON() : null,
            difficulty,
            pendingCount,
            reward,
            minReward: config.MIN_REWARD,
            maxReward: config.MAX_REWARD,
            blockTimeTarget: config.BLOCK_TIME_TARGET,
            networkHashrate: Math.round(networkHashrate / 1000)
        });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/blocks', (req, res) => {
    try {
        let limit = parseInt(req.query.limit) || 20;
        let offset = parseInt(req.query.offset) || 0;
        if (limit > 100) limit = 100;
        const blocks = BlockchainController.getAllBlocks(limit, offset).map(b => b.toJSON());
        res.json(blocks);
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/block/:height', (req, res) => {
    try {
        const height = parseInt(req.params.height);
        const block = BlockchainController.getBlockByHeight(height);
        if (!block) {
            return res.status(404).json({ error: 'Block not found' });
        }
        res.json(block.toJSON());
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/balance/:address', (req, res) => {
    try {
        const address = req.params.address;
        const publicKey = parseDisplayAddress(address);
        const balance = BlockchainController.getBalance(publicKey);
        res.json({ address, balance });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/pending', (req, res) => {
    try {
        const pending = BlockchainController.getMempool();
        res.json(pending);
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/history/:address', (req, res) => {
    try {
        const address = req.params.address;
        const publicKey = parseDisplayAddress(address);
        
        const blocks = BlockchainController.getAllBlocks();
        const history = [];
        
        for (const block of blocks) {
            for (const tx of block.transactions) {
                const txJson = tx.toJSON();
                
                if (txJson.from === publicKey || txJson.to === publicKey) {
                    history.push({
                        hash: tx.hash(),
                        from: txJson.from ? formatDisplayAddress(txJson.from) : 'COINBASE',
                        to: formatDisplayAddress(txJson.to),
                        amount: txJson.amount,
                        timestamp: txJson.timestamp,
                        blockHeight: block.height,
                        type: txJson.from === null ? 'mine' : (txJson.from === publicKey ? 'send' : 'receive'),
                        status: 'confirmed',
                        salt: txJson.salt,
                        hmac: txJson.hmac
                    });
                }
            }
        }
        
        history.sort((a, b) => b.timestamp - a.timestamp);
        res.json({ address, history });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.post('/api/register', validate(registerSchema), async (req, res) => {
    try {
        const { password } = req.body;
        const key = ec.genKeyPair();
        const publicKey = key.getPublic('hex');
        const privateKey = key.getPrivate('hex');

        const encrypted = encryptPrivateKey(privateKey, password);
        const encryptedJson = JSON.stringify(encrypted);

        const passwordHash = await bcrypt.hash(password, config.BCRYPT_ROUNDS);
        const displayAddress = formatDisplayAddress(publicKey);

        const stmt = db.prepare(`
            INSERT INTO users (public_key, encrypted_private_key, password_hash, display_address)
            VALUES (?, ?, ?, ?)
        `);
        stmt.run(publicKey, encryptedJson, passwordHash, displayAddress);

        logger.info(`New wallet registered: ${displayAddress}`);
        res.json({
            message: 'Wallet created successfully',
            publicKey,
            displayAddress,
            encryptedPrivateKey: encryptedJson
        });
    } catch (err) {
        if (err.code === 'SQLITE_CONSTRAINT') {
            res.status(400).json({ error: 'Public key already exists' });
        } else {
            logger.error(err);
            res.status(500).json({ error: 'Internal server error' });
        }
    }
});

app.post('/api/login', validate(loginSchema), async (req, res) => {
    try {
        const { displayAddress, password } = req.body;
        const publicKey = parseDisplayAddress(displayAddress);
        const user = db.prepare('SELECT * FROM users WHERE public_key = ?').get(publicKey);
        if (!user) {
            return res.status(404).json({ error: 'Wallet not found' });
        }

        const match = await bcrypt.compare(password, user.password_hash);
        if (!match) {
            return res.status(401).json({ error: 'Invalid password' });
        }

        const token = jwt.sign(
            { publicKey: user.public_key, displayAddress: user.display_address },
            config.JWT_SECRET,
            { expiresIn: config.JWT_EXPIRY }
        );

        res.cookie('token', token, {
            httpOnly: true,
            secure: config.NODE_ENV === 'production',
            sameSite: 'strict',
            maxAge: 7 * 24 * 60 * 60 * 1000
        });

        res.json({
            message: 'Login successful',
            publicKey: user.public_key,
            displayAddress: user.display_address,
            encryptedPrivateKey: user.encrypted_private_key
        });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.post('/api/logout', (req, res) => {
    res.clearCookie('token');
    res.json({ message: 'Logged out' });
});

app.post('/api/transactions', authenticateToken, validate(transactionSchema), (req, res) => {
    try {
        const { from, to, amount, timestamp, signature, salt, hmac } = req.body;
        const fromPub = parseDisplayAddress(from);
        const toPub = parseDisplayAddress(to);

        if (fromPub !== req.user.publicKey) {
            return res.status(403).json({ error: 'Cannot send from another address' });
        }

        const tx = new Transaction(fromPub, toPub, amount, timestamp);
        tx.signature = signature;
        if (salt) tx.salt = salt;
        if (hmac) tx.hmac = hmac;

        if (!tx.isValid()) {
            return res.status(400).json({ error: 'Invalid signature or HMAC' });
        }

        const balance = BlockchainController.getBalance(fromPub);
        if (balance < amount) {
            return res.status(400).json({ error: 'Insufficient balance' });
        }

        BlockchainController.addToMempool(tx);
        res.json({ 
            message: 'Transaction added to mempool', 
            txHash: tx.hash(),
            salt: tx.salt,
            hmac: tx.hmac
        });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.post('/api/blocks/submit', authenticateToken, validate(blockSchema), (req, res) => {
    try {
        const { 
            height, transactions, previousHash, timestamp, nonce, 
            hash, minerAddress, blockHMAC, workerSalt, miningSalt, blockSalt
        } = req.body;

        const minerPub = parseDisplayAddress(minerAddress);
        if (minerPub !== req.user.publicKey) {
            return res.status(403).json({ error: 'Miner address mismatch' });
        }

        const latest = BlockchainController.getLatestBlock();
        if (!latest) {
            return res.status(500).json({ error: 'No blockchain' });
        }

        if (height !== latest.height + 1) {
            return res.status(400).json({ error: 'Invalid height' });
        }
        if (previousHash !== latest.hash) {
            return res.status(400).json({ error: 'Previous hash mismatch' });
        }

        const txObjects = transactions.map(t => Transaction.fromJSON(t));
        const block = new Block(height, txObjects, previousHash, timestamp, nonce);
        
        if (miningSalt) block.miningSalt = miningSalt;
        if (blockSalt) block.blockSalt = blockSalt;
        
        if (block.calculateHash() !== hash) {
            console.log('=== DEBUG HASH SERVER ===');
            console.log('Height:', height);
            console.log('PreviousHash:', previousHash);
            console.log('Timestamp:', timestamp);
            console.log('Nonce:', nonce);
            console.log('MiningSalt:', miningSalt);
            console.log('BlockSalt:', blockSalt);
            console.log('Client hash:', hash);
            console.log('Server hash:', block.calculateHash());
            console.log('========================');
            return res.status(400).json({ error: 'Hash calculation mismatch' });
        }

        if (blockHMAC && workerSalt) {
            const blockData = {
                height,
                hash,
                previousHash,
                nonce
            };
            if (!hmacManager.verify(blockData, blockHMAC, workerSalt)) {
                return res.status(400).json({ error: 'Invalid block HMAC signature' });
            }
            block.blockHMAC = blockHMAC;
        }

        const difficulty = BlockchainController.adjustDifficulty();
        if (!hash.startsWith('0'.repeat(difficulty))) {
            return res.status(400).json({ error: 'Hash does not meet difficulty requirement' });
        }

        if (txObjects.length === 0 || txObjects[0].from !== null) {
            return res.status(400).json({ error: 'First transaction must be coinbase' });
        }
        
        const expectedReward = BlockchainController.calculateReward(difficulty);
        if (txObjects[0].amount !== expectedReward) {
            return res.status(400).json({ 
                error: `Invalid mining reward. Expected ${expectedReward} for difficulty ${difficulty}, got ${txObjects[0].amount}` 
            });
        }
        
        if (txObjects[0].to !== minerPub) {
            return res.status(400).json({ error: 'Coinbase recipient mismatch' });
        }

        for (let i = 1; i < txObjects.length; i++) {
            if (!txObjects[i].isValid()) {
                return res.status(400).json({ error: `Invalid transaction at index ${i}` });
            }
        }

        const checkTx = db.transaction(() => {
            const balances = new Map();
            const balanceRows = db.prepare('SELECT address, balance FROM balances').all();
            for (const row of balanceRows) {
                balances.set(row.address, row.balance);
            }

            for (let i = 1; i < txObjects.length; i++) {
                const tx = txObjects[i];
                const senderBalance = balances.get(tx.from) || 0;
                if (senderBalance < tx.amount) {
                    throw new Error(`Insufficient balance for transaction ${i}`);
                }
                balances.set(tx.from, senderBalance - tx.amount);
                balances.set(tx.to, (balances.get(tx.to) || 0) + tx.amount);
            }

            BlockchainController.addBlock(block);
            const txHashes = txObjects.slice(1).map(tx => tx.hash());
            BlockchainController.removeFromMempool(txHashes);
        });

        checkTx();
        
        logger.info(`Block #${height} accepted from miner ${minerAddress}`);
        res.json({ 
            message: 'Block accepted', 
            block: { height, hash },
            blockHMAC: block.blockHMAC
        });
    } catch (err) {
        logger.error(err);
        res.status(400).json({ error: err.message });
    }
});

app.post('/api/apikey/generate', authenticateToken, (req, res) => {
    try {
        const apiKey = BlockchainController.generateMinerAPIKey(req.user.publicKey);
        res.json(apiKey);
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.post('/api/apikey/verify', (req, res) => {
    try {
        const { apiKey, signature, salt } = req.body;
        const isValid = BlockchainController.verifyMinerAPIKey(apiKey, signature, salt);
        res.json({ valid: isValid });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/hmac-secret', authenticateToken, (req, res) => {
    try {
        logger.info(`User ${req.user.displayAddress} requested HMAC secret`);
        res.json({ 
            secret: config.HMAC_SECRET,
            message: 'HMAC secret retrieved successfully'
        });
    } catch (err) {
        logger.error('Error getting HMAC secret:', err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/hmac-secret-public', (req, res) => {
    try {
        if (config.NODE_ENV === 'production') {
            return res.status(403).json({ error: 'Forbidden in production' });
        }
        res.json({ 
            secret: config.HMAC_SECRET,
            message: 'HMAC secret retrieved successfully (public)'
        });
    } catch (err) {
        logger.error('Error getting HMAC secret:', err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/stats', (req, res) => {
    try {
        const blocks = BlockchainController.getAllBlocks(100);
        const totalBlocks = db.prepare('SELECT COUNT(*) as count FROM blocks').get().count;
        const totalTransactions = db.prepare('SELECT COUNT(*) as count FROM mempool').get().count;
        const totalWallets = db.prepare('SELECT COUNT(*) as count FROM users').get().count;
        
        const difficulty = BlockchainController.adjustDifficulty();
        const networkHashrate = BlockchainController.getNetworkHashrate();
        
        let avgBlockTime = 0;
        if (blocks.length > 1) {
            const firstBlock = blocks[0];
            const lastBlock = blocks[blocks.length - 1];
            const timeSpan = (lastBlock.timestamp - firstBlock.timestamp) / 1000;
            avgBlockTime = timeSpan / (blocks.length - 1);
        }
        
        res.json({
            totalBlocks,
            totalTransactions,
            totalWallets,
            currentDifficulty: difficulty,
            networkHashrate: Math.round(networkHashrate / 1000),
            averageBlockTime: Math.round(avgBlockTime),
            lastBlock: blocks[blocks.length - 1]?.toJSON()
        });
    } catch (err) {
        logger.error(err);
        res.status(500).json({ error: 'Internal server error' });
    }
});

(function initGenesis() {
    const latest = BlockchainController.getLatestBlock();
    if (!latest) {
        const genesis = new Block(0, [], "0", Date.now(), 0);
        genesis.miningSalt = crypto.randomBytes(4).toString('hex');
        genesis.hash = genesis.calculateHash();
        genesis.generateBlockHMAC();
        BlockchainController.addBlock(genesis);
        logger.info('Genesis block created with HMAC');
        logger.info(`Genesis hash: ${genesis.hash}`);
        logger.info(`Genesis HMAC: ${genesis.blockHMAC}`);
    }
})();

app.get('/health', (req, res) => res.send('OK'));

app.use((err, req, res, next) => {
    logger.error(err.stack);
    res.status(500).json({ error: 'Something went wrong!' });
});

const PORT = config.PORT;
app.listen(PORT, () => {
    logger.info(`🚀 WebCoin server running on http://localhost:${PORT} in ${config.NODE_ENV} mode`);
    logger.info(`📊 HMAC Security: Enabled`);
    logger.info(`🔐 Salt rotation: ${config.HMAC_SALT_ROTATION_INTERVAL / 3600000}h`);
    logger.info(`⛏️  PoW Algorithm: SHA-1 with mining salt`);
});
