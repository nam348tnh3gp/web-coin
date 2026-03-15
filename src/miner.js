#!/usr/bin/env node
const fetch = require('node-fetch');
const CryptoJS = require('crypto-js');
const EC = require('elliptic').ec;
const readline = require('readline');
const os = require('os');

const ec = new EC('secp256k1');
const BASE_URL = 'https://webcoin-1n9d.onrender.com/api';

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

// ============== CẤU HÌNH CPU ==============
const CPU_CORES = os.cpus().length;
let miningThreads = CPU_CORES;
let cpuUsagePercent = 100;
let activeThreads = [];

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

// ============== TẠO PRIVATE KEY TỪ MẬT KHẨU ==============
function generatePrivateKeyFromPassword(password) {
    // Dùng SHA256 để tạo private key 64 ký tự hex từ mật khẩu
    return CryptoJS.SHA256(password).toString();
}

// ============== ĐĂNG NHẬP ĐỂ LẤY PUBLIC KEY ==============
async function login(displayAddr, password) {
    try {
        const response = await fetch(BASE_URL + '/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
                displayAddress: displayAddr, 
                password: password 
            }),
            credentials: 'include'
        });
        
        const data = await response.json();
        
        if (response.ok) {
            console.log('✅ Đăng nhập thành công!');
            return {
                success: true,
                publicKey: data.publicKey,
                encryptedKey: data.encryptedPrivateKey
            };
        } else {
            console.log('❌ Đăng nhập thất bại:', data.error);
            return { success: false };
        }
    } catch (err) {
        console.log('❌ Lỗi kết nối:', err.message);
        return { success: false };
    }
}

// ============== NHẬP CẤU HÌNH CPU ==============
async function getCpuConfig() {
    console.log(`\n🖥️  CPU Configuration:`);
    console.log(`   • Available cores: ${CPU_CORES}`);
    
    const threadInput = await question(`   • Number of threads to use (1-${CPU_CORES}, default: ${CPU_CORES}): `);
    if (threadInput.trim() !== '') {
        const threads = parseInt(threadInput);
        if (!isNaN(threads) && threads >= 1 && threads <= CPU_CORES) {
            miningThreads = threads;
        } else {
            console.log(`   ⚠️ Invalid input, using ${CPU_CORES} threads`);
        }
    }
    
    const percentInput = await question(`   • CPU usage percentage (1-100, default: 100): `);
    if (percentInput.trim() !== '') {
        const percent = parseInt(percentInput);
        if (!isNaN(percent) && percent >= 1 && percent <= 100) {
            cpuUsagePercent = percent;
        } else {
            console.log(`   ⚠️ Invalid input, using 100%`);
        }
    }
    
    console.log(`   ✅ Using ${miningThreads} threads at ${cpuUsagePercent}% speed\n`);
}

// ============== THREAD WORKER ==============
async function miningWorker(threadId, minerDisplayAddress, privateKey, difficultyOverride, stats) {
    const key = ec.keyFromPrivate(privateKey);
    const publicKey = key.getPublic('hex');
    
    while (stats.running) {
        try {
            let info, pending, latest, networkDifficulty, reward;
            
            if (threadId === 0) {
                const infoRes = await fetch(BASE_URL + '/info');
                if (!infoRes.ok) {
                    console.error(`Thread ${threadId}: ❌ Failed to fetch info, retrying...`);
                    await sleep(5000);
                    continue;
                }
                info = await infoRes.json();
                
                if (!info.latestBlock) {
                    console.log(`Thread ${threadId}: ⏳ No blockchain yet, waiting...`);
                    await sleep(5000);
                    continue;
                }
                
                const pendingRes = await fetch(BASE_URL + '/pending');
                pending = await pendingRes.json();
                
                stats.latestInfo = info;
                stats.pending = pending;
            } else {
                let waitCount = 0;
                while (!stats.latestInfo && waitCount < 50) {
                    await sleep(100);
                    waitCount++;
                }
                if (!stats.latestInfo) {
                    await sleep(1000);
                    continue;
                }
                info = stats.latestInfo;
                pending = stats.pending;
            }
            
            latest = info.latestBlock;
            networkDifficulty = info.difficulty;
            reward = info.reward;
            
            const miningDifficulty = difficultyOverride || networkDifficulty;
            
            if (threadId === 0 && miningDifficulty < networkDifficulty) {
                console.log(`⚠️  CẢNH BÁO: Độ khó đặt (${miningDifficulty}) thấp hơn mạng (${networkDifficulty})`);
            }

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
            
            const delayPerIteration = (100 - cpuUsagePercent) / 1000;

            if (threadId === 0) {
                console.log(`\n⛏️  Thread ${threadId}: Mining block #${newBlock.height} | Độ khó: ${miningDifficulty} | Mạng: ${networkDifficulty} | Thưởng: ${reward} WebCoin`);
            }

            while (!hash.startsWith(target) && stats.running) {
                newBlock.nonce++;
                hash = calculateBlockHash(newBlock);
                attempts++;
                stats.totalHashes[threadId] = (stats.totalHashes[threadId] || 0) + 1;
                
                if (delayPerIteration > 0 && attempts % 100 === 0) {
                    await sleep(delayPerIteration * 100);
                }
                
                if (attempts % 10000 === 0) {
                    const elapsed = (Date.now() - start) / 1000;
                    const speed = (attempts / elapsed).toFixed(0);
                    if (threadId === 0) {
                        console.log(`   Thread ${threadId}: ⏳ Attempts: ${attempts} | Speed: ${speed} H/s | Hash: ${hash.substring(0, 20)}...`);
                    }
                }
            }

            if (!stats.running) break;

            const elapsed = (Date.now() - start) / 1000;
            const speed = (attempts / elapsed).toFixed(0);
            
            console.log(`✅ Thread ${threadId}: Found nonce ${newBlock.nonce} in ${elapsed.toFixed(2)}s (${speed} H/s)`);
            
            if (threadId === 0) {
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
                    stats.blocksMined++;
                    stats.totalReward += reward;
                } else {
                    console.log(`❌ Error: ${result.error}`);
                    
                    if (result.error.includes('difficulty') && miningDifficulty < networkDifficulty) {
                        console.log(`💡 Gợi ý: Thử đặt độ khó >= ${networkDifficulty}`);
                    }
                }
            } else {
                console.log(`   Thread ${threadId}: Nonce ${newBlock.nonce} found but not submitting`);
            }

            await sleep(100);
            
        } catch (err) {
            console.error(`Thread ${threadId}: ❌ Error:`, err.message);
            await sleep(5000);
        }
    }
}

// ============== THỐNG KÊ ==============
async function statsReporter(stats) {
    const startTime = Date.now();
    
    while (stats.running) {
        await sleep(5000);
        
        const now = Date.now();
        const elapsed = (now - startTime) / 1000;
        const totalHashes = Object.values(stats.totalHashes).reduce((a, b) => a + (b || 0), 0);
        const avgSpeed = totalHashes / elapsed;
        
        const threadSpeeds = [];
        for (let i = 0; i < miningThreads; i++) {
            const threadHashes = stats.totalHashes[i] || 0;
            threadSpeeds.push(threadHashes / elapsed);
        }
        
        console.log(`\n📊 STATISTICS:`);
        console.log(`   ⏱️  Runtime: ${Math.floor(elapsed / 60)}m ${Math.floor(elapsed % 60)}s`);
        console.log(`   🔢 Total hashes: ${totalHashes.toLocaleString()}`);
        console.log(`   ⚡ Avg speed: ${(avgSpeed / 1000).toFixed(2)} kH/s`);
        console.log(`   ⛏️  Blocks mined: ${stats.blocksMined}`);
        console.log(`   💰 Total reward: ${stats.totalReward} WBC`);
        console.log(`   🧵 Thread speeds: ${threadSpeeds.map((s, i) => `${i}: ${(s/1000).toFixed(1)}kH/s`).join(' | ')}`);
        console.log(`   🔋 CPU usage: ${cpuUsagePercent}% across ${miningThreads}/${CPU_CORES} cores\n`);
    }
}

// ============== MAIN ==============
async function getWalletInfo() {
    console.log('\n=== WebCoin Terminal Miner v2.1 (Password Edition) ===\n');
    
    const displayAddr = await question('Nhập địa chỉ ví (W_...): ');
    if (!displayAddr || !displayAddr.startsWith('W_')) {
        console.error('❌ Địa chỉ ví không hợp lệ! Phải bắt đầu bằng W_');
        rl.close();
        process.exit(1);
    }
    
    const password = await question('Nhập mật khẩu ví: ');
    if (!password || password.length < 6) {
        console.error('❌ Mật khẩu phải có ít nhất 6 ký tự');
        rl.close();
        process.exit(1);
    }
    
    // Đăng nhập để kiểm tra thông tin
    console.log('\n🔐 Đang đăng nhập...');
    const loginResult = await login(displayAddr, password);
    
    if (!loginResult.success) {
        console.error('❌ Đăng nhập thất bại! Kiểm tra lại địa chỉ ví và mật khẩu.');
        rl.close();
        process.exit(1);
    }
    
    // Cấu hình CPU
    await getCpuConfig();
    
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
    
    // Tạo private key từ mật khẩu
    const privateKey = generatePrivateKeyFromPassword(password);
    console.log(`\n🔑 Private key generated: ${privateKey.substring(0, 20)}...`);
    
    return { displayAddr, privateKey, difficultyOverride };
}

async function main() {
    console.log('✅ Node version:', process.version);
    console.log('✅ CPU cores detected:', CPU_CORES);
    console.log('✅ Connected to:', BASE_URL);
    
    let displayAddr, privateKey, difficultyOverride;
    
    if (process.argv.length >= 4) {
        // Dùng tham số dòng lệnh (vẫn dùng private key hex)
        displayAddr = process.argv[2];
        privateKey = process.argv[3];
        
        if (process.argv[4]) {
            difficultyOverride = parseInt(process.argv[4]);
            if (isNaN(difficultyOverride) || difficultyOverride < 1 || difficultyOverride > 10) {
                console.error('❌ Độ khó không hợp lệ! Phải từ 1 đến 10');
                process.exit(1);
            }
        }
        
        // Cấu hình CPU mặc định
        await getCpuConfig();
        
        console.log('Sử dụng tham số dòng lệnh...');
    } else {
        const result = await getWalletInfo();
        displayAddr = result.displayAddr;
        privateKey = result.privateKey;
        difficultyOverride = result.difficultyOverride;
    }
    
    // Khởi tạo stats
    const stats = {
        running: true,
        totalHashes: {},
        latestInfo: null,
        pending: null,
        blocksMined: 0,
        totalReward: 0
    };
    
    // Xử lý Ctrl+C
    process.on('SIGINT', () => {
        console.log('\n\n🛑 Dừng miner...');
        stats.running = false;
        setTimeout(() => process.exit(0), 1000);
    });
    
    console.log(`\n🚀 Starting ${miningThreads} mining threads...`);
    
    // Khởi tạo các thread
    const threads = [];
    for (let i = 0; i < miningThreads; i++) {
        threads.push(miningWorker(i, displayAddr, privateKey, difficultyOverride, stats));
    }
    
    // Thread báo cáo thống kê
    threads.push(statsReporter(stats));
    
    // Đợi tất cả thread hoàn thành
    await Promise.all(threads);
}

main().catch(err => {
    console.error('Fatal error:', err);
    rl.close();
    process.exit(1);
});
