#!/usr/bin/env node
const fetch = require('node-fetch');
const CryptoJS = require('crypto-js');
const EC = require('elliptic').ec;
const readline = require('readline');

const ec = new EC('secp256k1');
const BASE_URL = 'https://webcoin-1n9d.onrender.com/api';

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

function question(query) {
    return new Promise(resolve => {
        rl.question(query, resolve);
    });
}

function calculateBlockHash(block) {
    const txString = block.transactions.map(tx => JSON.stringify(tx)).join('');
    return CryptoJS.SHA1(block.height + block.previousHash + block.timestamp + txString + block.nonce).toString();
}

async function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function getWalletInfo() {
    console.log('\n=== WebCoin Terminal Miner ===\n');
    
    const displayAddr = await question('Nhập địa chỉ ví (W_...): ');
    if (!displayAddr || !displayAddr.startsWith('W_')) {
        console.error('❌ Địa chỉ ví không hợp lệ! Phải bắt đầu bằng W_');
        rl.close();
        process.exit(1);
    }
    
    const privateKey = await question('Nhập private key (hex): ');
    if (!privateKey || privateKey.length !== 64) {
        console.error('❌ Private key không hợp lệ! Phải là 64 ký tự hex');
        rl.close();
        process.exit(1);
    }
    
    console.log('\nĐộ khó hiện tại của mạng sẽ được lấy từ server.');
    console.log('Bạn có thể đặt độ khó thấp hơn để đào nhanh hơn, nhưng block có thể bị từ chối.');
    const customDifficulty = await question('Nhập độ khó muốn đào (1-10, để trống để dùng độ khó mạng): ');
    
    let difficultyOverride = null;
    if (customDifficulty.trim() !== '') {
        difficultyOverride = parseInt(customDifficulty);
        if (isNaN(difficultyOverride) || difficultyOverride < 1 || difficultyOverride > 10) {
            console.error('❌ Độ khó không hợp lệ! Phải từ 1 đến 10');
            rl.close();
            process.exit(1);
        }
    }
    
    return { displayAddr, privateKey, difficultyOverride };
}

async function mineLoop(minerDisplayAddress, privateKey, difficultyOverride) {
    console.log(`\n🚀 Miner started for address ${minerDisplayAddress}`);
    if (difficultyOverride) {
        console.log(`⚙️ Sẽ đào với độ khó ${difficultyOverride} (có thể bị từ chối nếu thấp hơn mạng)`);
    } else {
        console.log(`⚙️ Sẽ đào với độ khó của mạng`);
    }
    console.log('──────────────────────────────\n');
    
    while (true) {
        try {
            const infoRes = await fetch(BASE_URL + '/info');
            if (!infoRes.ok) {
                console.error('❌ Failed to fetch info, retrying in 10s...');
                await sleep(10000);
                continue;
            }
            const info = await infoRes.json();
            if (!info.latestBlock) {
                console.log('⏳ No blockchain yet, waiting...');
                await sleep(5000);
                continue;
            }

            const latest = info.latestBlock;
            const networkDifficulty = info.difficulty;
            const reward = info.reward;
            
            const miningDifficulty = difficultyOverride || networkDifficulty;
            
            if (miningDifficulty < networkDifficulty) {
                console.log(`⚠️  CẢNH BÁO: Độ khó đặt (${miningDifficulty}) thấp hơn mạng (${networkDifficulty})`);
                console.log(`   Block có thể bị từ chối!`);
            }

            const pendingRes = await fetch(BASE_URL + '/pending');
            const pending = await pendingRes.json();

            const key = ec.keyFromPrivate(privateKey);
            const publicKey = key.getPublic('hex');

            const coinbase = {
                from: null,
                to: publicKey,
                amount: reward,
                timestamp: Date.now(),
                signature: null
            };

            const transactions = [coinbase, ...pending.map(p => ({
                from: p.from,
                to: p.to,
                amount: p.amount,
                timestamp: p.timestamp,
                signature: p.signature
            }))];

            const newBlock = {
                height: latest.height + 1,
                transactions,
                previousHash: latest.hash,
                timestamp: Date.now(),
                nonce: 0
            };

            const target = '0'.repeat(miningDifficulty);
            let hash = calculateBlockHash(newBlock);
            let attempts = 0;
            const start = Date.now();

            console.log(`⛏️  Mining block #${newBlock.height} | Độ khó đặt: ${miningDifficulty} | Mạng: ${networkDifficulty} | Thưởng: ${reward} WebCoin`);

            while (!hash.startsWith(target)) {
                newBlock.nonce++;
                hash = calculateBlockHash(newBlock);
                attempts++;
                
                if (attempts % 100000 === 0) {
                    const elapsed = (Date.now() - start) / 1000;
                    const speed = (attempts / elapsed).toFixed(0);
                    const hashPreview = hash.substring(0, 20) + '...';
                    console.log(`⏳ Attempts: ${attempts} | Speed: ${speed} H/s | Hash: ${hashPreview}`);
                }
            }

            const elapsed = ((Date.now() - start) / 1000).toFixed(2);
            console.log(`✅ Found nonce ${newBlock.nonce} in ${elapsed}s`);
            console.log(`🔗 Hash: ${hash}`);

            const submitData = {
                height: newBlock.height,
                transactions,
                previousHash: newBlock.previousHash,
                timestamp: newBlock.timestamp,
                nonce: newBlock.nonce,
                hash,
                minerAddress: minerDisplayAddress
            };

            const submitRes = await fetch(BASE_URL + '/blocks/submit', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(submitData),
                credentials: 'include'
            });
            
            const result = await submitRes.json();
            if (submitRes.ok) {
                console.log(`✅ Block accepted! Received ${reward} WebCoin`);
            } else {
                console.log(`❌ Error: ${result.error}`);
                
                if (result.error.includes('difficulty') && miningDifficulty < networkDifficulty) {
                    console.log(`💡 Gợi ý: Thử đặt độ khó >= ${networkDifficulty}`);
                }
            }

            console.log('──────────────────────────────\n');
            await sleep(1000);
            
        } catch (err) {
            console.error('❌ Miner error:', err.message);
            await sleep(10000);
        }
    }
}

async function main() {
    console.log('✅ Node version:', process.version);
    console.log('✅ Connected to:', BASE_URL);
    
    if (process.argv.length >= 4) {
        const displayAddr = process.argv[2];
        const privKey = process.argv[3];
        let difficultyOverride = null;
        
        if (process.argv[4]) {
            difficultyOverride = parseInt(process.argv[4]);
            if (isNaN(difficultyOverride) || difficultyOverride < 1 || difficultyOverride > 10) {
                console.error('❌ Độ khó không hợp lệ! Phải từ 1 đến 10');
                process.exit(1);
            }
        }
        
        console.log('Sử dụng tham số dòng lệnh...');
        await mineLoop(displayAddr, privKey, difficultyOverride);
    } else {
        const { displayAddr, privateKey, difficultyOverride } = await getWalletInfo();
        rl.close();
        await mineLoop(displayAddr, privateKey, difficultyOverride);
    }
}

main().catch(err => {
    console.error('Fatal error:', err);
    rl.close();
    process.exit(1);
});