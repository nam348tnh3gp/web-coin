// Khởi tạo elliptic
const ec = new elliptic.ec('secp256k1');

let currentWallet = {
    publicKey: null,
    displayAddress: null,
    encryptedPrivateKey: null,
    privateKey: null
};

let isMining = false;
let stopButton = null;
let transactionHistory = [];
let historyFilter = 'all';
let networkDifficulty = 3; // Độ khó thật của mạng
let miningDifficulty = 3;   // Độ khó đang dùng để đào (có thể điều chỉnh)

// Hàm chuyển tab
function showTab(tabId) {
    document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
    document.querySelector(`.tab[data-tab="${tabId}"]`).classList.add('active');
    document.getElementById(tabId).classList.add('active');
    
    if (tabId === 'history' && currentWallet.displayAddress) {
        loadHistory();
    }
}

// Helper hiển thị lỗi/thành công
function showError(elementId, message) {
    const el = document.getElementById(elementId);
    if (el) {
        el.innerHTML = `<span class="error">${message}</span>`;
    } else {
        alert(message);
    }
}

function showSuccess(elementId, message) {
    const el = document.getElementById(elementId);
    if (el) {
        el.innerHTML = `<span class="success">${message}</span>`;
    }
}

// Điều chỉnh độ khó đào (từ thanh trượt)
function adjustMiningDifficulty() {
    const slider = document.getElementById('difficulty-slider');
    if (slider) {
        miningDifficulty = parseInt(slider.value);
        document.getElementById('current-difficulty-display').innerText = miningDifficulty;
        
        // Hiển thị cảnh báo nếu đang đặt độ khó thấp hơn mạng
        const warningEl = document.getElementById('difficulty-warning');
        if (warningEl) {
            if (miningDifficulty < networkDifficulty) {
                warningEl.innerHTML = '⚠️ Độ khó đang thấp hơn mạng! Block sẽ không được chấp nhận.';
                warningEl.style.color = '#dc3545';
            } else {
                warningEl.innerHTML = '✅ Độ khó phù hợp';
                warningEl.style.color = '#28a745';
            }
        }
    }
}

// Đăng ký ví
async function registerWallet() {
    const password = document.getElementById('reg-password').value;
    if (!password || password.length < 6) {
        showError('reg-result', 'Mật khẩu phải có ít nhất 6 ký tự');
        return;
    }

    try {
        const res = await fetch('/api/register', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ password })
        });
        const data = await res.json();
        if (res.ok) {
            showSuccess('reg-result', '✅ Tạo ví thành công! Địa chỉ: ' + data.displayAddress);
            document.getElementById('login-address').value = data.displayAddress;
            document.getElementById('login-password').value = password;
        } else {
            showError('reg-result', '❌ Lỗi: ' + data.error);
        }
    } catch (err) {
        showError('reg-result', '❌ Lỗi kết nối: ' + err.message);
    }
}

// Đăng nhập
async function loginWallet() {
    const displayAddress = document.getElementById('login-address').value.trim();
    const password = document.getElementById('login-password').value;

    if (!displayAddress || !password) {
        showError('login-result', 'Vui lòng nhập đầy đủ');
        return;
    }

    try {
        const res = await fetch('/api/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ displayAddress, password }),
            credentials: 'include'
        });
        const data = await res.json();
        
        if (res.ok) {
            currentWallet.publicKey = data.publicKey;
            currentWallet.displayAddress = data.displayAddress;
            currentWallet.encryptedPrivateKey = JSON.parse(data.encryptedPrivateKey);

            // Giải mã private key
            let privateKeyHex = null;
            let decryptionSuccess = false;
            let lastError = null;

            // Method 1: Base64
            try {
                const salt = CryptoJS.enc.Base64.parse(currentWallet.encryptedPrivateKey.salt);
                const iv = CryptoJS.enc.Base64.parse(currentWallet.encryptedPrivateKey.iv);
                const ciphertext = CryptoJS.enc.Base64.parse(currentWallet.encryptedPrivateKey.encrypted);

                const key = CryptoJS.PBKDF2(password, salt, {
                    keySize: 256 / 32,
                    iterations: 100000,
                    hasher: CryptoJS.algo.SHA256
                });

                const decrypted = CryptoJS.AES.decrypt(
                    { ciphertext: ciphertext },
                    key,
                    { iv: iv, mode: CryptoJS.mode.CBC, padding: CryptoJS.pad.Pkcs7 }
                );

                privateKeyHex = decrypted.toString(CryptoJS.enc.Utf8);
                
                if (privateKeyHex && /^[0-9a-f]{64}$/i.test(privateKeyHex)) {
                    decryptionSuccess = true;
                }
            } catch (e) {
                lastError = e.message;
            }

            // Method 2: Hex
            if (!decryptionSuccess) {
                try {
                    const salt = CryptoJS.enc.Hex.parse(currentWallet.encryptedPrivateKey.salt);
                    const iv = CryptoJS.enc.Hex.parse(currentWallet.encryptedPrivateKey.iv);
                    const ciphertext = CryptoJS.enc.Hex.parse(currentWallet.encryptedPrivateKey.encrypted);

                    const key = CryptoJS.PBKDF2(password, salt, {
                        keySize: 256 / 32,
                        iterations: 100000,
                        hasher: CryptoJS.algo.SHA256
                    });

                    const decrypted = CryptoJS.AES.decrypt(
                        { ciphertext: ciphertext },
                        key,
                        { iv: iv, mode: CryptoJS.mode.CBC, padding: CryptoJS.pad.Pkcs7 }
                    );

                    privateKeyHex = decrypted.toString(CryptoJS.enc.Utf8);
                    
                    if (privateKeyHex && /^[0-9a-f]{64}$/i.test(privateKeyHex)) {
                        decryptionSuccess = true;
                    }
                } catch (e) {
                    lastError = e.message;
                }
            }

            if (!decryptionSuccess) {
                throw new Error('Giải mã thất bại: ' + lastError);
            }

            currentWallet.privateKey = privateKeyHex;

            // Hiển thị thông tin ví
            document.getElementById('wallet-info').style.display = 'block';
            document.getElementById('wallet-display-address').innerText = currentWallet.displayAddress;

            refreshBalance();

            document.getElementById('send-from').value = currentWallet.displayAddress;
            document.getElementById('mine-address').value = currentWallet.displayAddress;

            showSuccess('login-result', '✅ Đăng nhập thành công!');
            document.getElementById('send-info').innerHTML = 'Sẵn sàng gửi giao dịch.';
            
            loadHistory();
        } else {
            showError('login-result', '❌ ' + data.error);
        }
    } catch (err) {
        console.error('Login error:', err);
        showError('login-result', '❌ Lỗi: ' + err.message);
    }
}

// Đăng xuất
async function logoutWallet() {
    stopMining();
    
    await fetch('/api/logout', { method: 'POST', credentials: 'include' });
    currentWallet = { publicKey: null, displayAddress: null, privateKey: null };
    document.getElementById('wallet-info').style.display = 'none';
    document.getElementById('login-result').innerHTML = '';
    document.getElementById('send-info').innerHTML = 'Vui lòng đăng nhập trước.';
    document.getElementById('history-controls').style.display = 'none';
    document.getElementById('history-info').style.display = 'block';
}

// Cập nhật số dư
async function refreshBalance() {
    if (!currentWallet.displayAddress) return;
    try {
        const res = await fetch(`/api/balance/${currentWallet.displayAddress}`);
        const data = await res.json();
        document.getElementById('wallet-balance').innerText = data.balance;
    } catch (err) {
        console.error(err);
    }
}

// Gửi giao dịch
async function sendTransaction() {
    if (!currentWallet.privateKey) {
        showError('send-result', 'Vui lòng đăng nhập trước');
        return;
    }

    const from = currentWallet.displayAddress;
    const to = document.getElementById('send-to').value.trim();
    const amount = parseInt(document.getElementById('send-amount').value);

    if (!to || !amount || amount <= 0) {
        showError('send-result', 'Vui lòng nhập địa chỉ nhận và số lượng hợp lệ');
        return;
    }

    if (!to.startsWith('W_')) {
        showError('send-result', 'Địa chỉ nhận phải bắt đầu bằng W_');
        return;
    }

    const timestamp = Date.now();
    const tx = {
        from: currentWallet.publicKey,
        to: to.substring(2),
        amount,
        timestamp
    };

    const hash = CryptoJS.SHA256(tx.from + tx.to + tx.amount + tx.timestamp).toString();
    const key = ec.keyFromPrivate(currentWallet.privateKey);
    const signature = key.sign(hash).toDER('hex');

    try {
        const res = await fetch('/api/transactions', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                from: tx.from,
                to: tx.to,
                amount: tx.amount,
                timestamp: tx.timestamp,
                signature
            }),
            credentials: 'include'
        });
        const data = await res.json();
        if (res.ok) {
            showSuccess('send-result', '✅ ' + data.message);
            refreshBalance();
            loadInfo();
            loadHistory();
        } else {
            showError('send-result', '❌ ' + data.error);
        }
    } catch (err) {
        showError('send-result', '❌ Lỗi kết nối: ' + err.message);
    }
}

// Load thông tin mạng
async function loadInfo() {
    try {
        const res = await fetch('/api/info');
        const data = await res.json();
        networkDifficulty = data.difficulty;
        document.getElementById('mine-difficulty').innerText = data.difficulty;
        document.getElementById('mine-reward').innerText = data.reward;
        document.getElementById('mine-pending').innerText = data.pendingCount;
        document.getElementById('min-reward').innerText = data.minReward;
        document.getElementById('max-reward').innerText = data.maxReward;
        
        // Cập nhật thanh trượt
        const difficultySlider = document.getElementById('difficulty-slider');
        if (difficultySlider) {
            difficultySlider.value = miningDifficulty;
            document.getElementById('current-difficulty-display').innerText = miningDifficulty;
        }
        
        return data;
    } catch (err) {
        console.error(err);
        return null;
    }
}

// Load blockchain
async function loadChain() {
    try {
        const res = await fetch('/api/blocks');
        const data = await res.json();
        document.getElementById('chain-display').innerText = JSON.stringify(data, null, 2);
    } catch (err) {
        console.error(err);
    }
}

// Load lịch sử giao dịch
async function loadHistory() {
    if (!currentWallet.displayAddress) {
        document.getElementById('history-controls').style.display = 'none';
        document.getElementById('history-info').style.display = 'block';
        document.getElementById('history-info').innerHTML = 'Vui lòng đăng nhập để xem lịch sử.';
        return;
    }

    try {
        document.getElementById('history-info').style.display = 'none';
        document.getElementById('history-controls').style.display = 'block';
        
        const historyList = document.getElementById('history-list');
        historyList.innerHTML = '<div class="history-loading">🔄 Đang tải lịch sử...</div>';
        
        const res = await fetch(`/api/history/${currentWallet.displayAddress}`);
        const data = await res.json();
        
        if (res.ok) {
            transactionHistory = data.history;
            displayHistory();
        } else {
            historyList.innerHTML = `<div class="error">❌ Lỗi: ${data.error}</div>`;
        }
    } catch (err) {
        document.getElementById('history-list').innerHTML = `<div class="error">❌ Lỗi kết nối: ${err.message}</div>`;
    }
}

// Hiển thị lịch sử theo filter
function displayHistory() {
    const historyList = document.getElementById('history-list');
    
    if (transactionHistory.length === 0) {
        historyList.innerHTML = '<div class="history-empty">📭 Chưa có giao dịch nào</div>';
        return;
    }
    
    let filteredHistory = transactionHistory;
    if (historyFilter !== 'all') {
        filteredHistory = transactionHistory.filter(tx => tx.type === historyFilter);
    }
    
    if (filteredHistory.length === 0) {
        historyList.innerHTML = `<div class="history-empty">📭 Không có giao dịch ${
            historyFilter === 'send' ? 'gửi' : 
            historyFilter === 'receive' ? 'nhận' : 
            'đào được'
        }</div>`;
        return;
    }
    
    let html = '';
    filteredHistory.forEach(tx => {
        const date = new Date(tx.timestamp).toLocaleString('vi-VN');
        const amountPrefix = tx.type === 'send' ? '-' : '+';
        
        html += `
            <div class="history-item ${tx.type}">
                <div class="history-header">
                    <span class="history-type ${tx.type}">
                        ${tx.type === 'send' ? '📤 Gửi' : 
                          tx.type === 'receive' ? '📥 Nhận' : 
                          '⛏️ Đào được'}
                    </span>
                    <span class="history-time">${date}</span>
                </div>
                
                ${tx.type === 'send' ? `
                    <div class="history-address">
                        <strong>Đến:</strong> ${tx.to}
                    </div>
                ` : tx.type === 'receive' ? `
                    <div class="history-address">
                        <strong>Từ:</strong> ${tx.from}
                    </div>
                ` : `
                    <div class="history-address">
                        <strong>Phần thưởng đào block</strong>
                    </div>
                `}
                
                <div class="history-amount ${tx.type}">
                    ${amountPrefix} ${tx.amount} WebCoin
                </div>
                
                <div class="history-footer">
                    <span class="history-block">⛓️ Block #${tx.blockHeight}</span>
                    <span class="history-tx-hash" onclick="alert('Hash giao dịch:\\n${tx.hash}')" title="Click để xem hash">
                        🔗 ${tx.hash.substring(0, 10)}...
                    </span>
                </div>
            </div>
        `;
    });
    
    historyList.innerHTML = html;
}

// Hàm dừng đào
function stopMining() {
    isMining = false;
    if (stopButton && stopButton.parentNode) {
        stopButton.remove();
        stopButton = null;
    }
}

// Đào block với độ khó tùy chỉnh
async function startMining() {
    if (!currentWallet.displayAddress) {
        showError('mining-log', 'Vui lòng đăng nhập trước');
        return;
    }

    // Nếu đang đào thì tạm dừng
    if (isMining) {
        stopMining();
        document.getElementById('mining-log').innerHTML += '\n⏸️ Đã tạm dừng đào.\n';
        document.getElementById('mine-btn').textContent = 'Bắt đầu đào';
        document.getElementById('mine-btn').style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';
        return;
    }

    const logDiv = document.getElementById('mining-log');
    logDiv.innerHTML = `🔄 Bắt đầu đào tự động với độ khó ${miningDifficulty}...\n`;
    
    // Cảnh báo nếu độ khó thấp hơn mạng
    if (miningDifficulty < networkDifficulty) {
        logDiv.innerHTML += `⚠️ CẢNH BÁO: Độ khó ${miningDifficulty} thấp hơn mạng (${networkDifficulty}). Block có thể bị từ chối!\n`;
    }
    
    isMining = true;
    document.getElementById('mine-btn').textContent = '⏸️ Tạm dừng đào';
    document.getElementById('mine-btn').style.background = '#ffc107';
    
    // Thêm nút dừng hẳn
    if (!stopButton) {
        stopButton = document.createElement('button');
        stopButton.id = 'stop-mining-btn';
        stopButton.textContent = '⏹️ Dừng hẳn';
        stopButton.className = 'danger w-auto';
        stopButton.style.marginTop = '10px';
        stopButton.onclick = () => {
            stopMining();
            logDiv.innerHTML += '\n⏹️ Đã dừng hẳn.\n';
            document.getElementById('mine-btn').textContent = 'Bắt đầu đào';
            document.getElementById('mine-btn').style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';
        };
        
        const buttonContainer = document.createElement('div');
        buttonContainer.className = 'button-container';
        buttonContainer.id = 'mining-button-container';
        
        const mineBtn = document.getElementById('mine-btn');
        mineBtn.parentNode.insertBefore(buttonContainer, mineBtn);
        buttonContainer.appendChild(mineBtn);
        buttonContainer.appendChild(stopButton);
    }

    // Vòng lặp đào
    while (isMining) {
        try {
            const info = await loadInfo();
            if (!info) {
                await new Promise(resolve => setTimeout(resolve, 5000));
                continue;
            }

            const pendingRes = await fetch('/api/pending');
            const pending = await pendingRes.json();

            const coinbase = {
                from: null,
                to: currentWallet.publicKey,
                amount: info.reward,
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

            const latest = info.latestBlock;
            const newBlock = {
                height: latest ? latest.height + 1 : 0,
                transactions,
                previousHash: latest ? latest.hash : "0",
                timestamp: Date.now(),
                nonce: 0
            };

            function calculateBlockHash(block) {
                const txString = block.transactions.map(tx => JSON.stringify(tx)).join('');
                return CryptoJS.SHA1(block.height + block.previousHash + block.timestamp + txString + block.nonce).toString();
            }

            let hash = calculateBlockHash(newBlock);
            const target = '0'.repeat(miningDifficulty); // Dùng độ khó từ thanh trượt
            let attempts = 0;
            const startTime = Date.now();

            logDiv.innerHTML += `\n⛏️ Đào block #${newBlock.height} (độ khó đặt: ${miningDifficulty}, mạng: ${networkDifficulty}, thưởng ${info.reward} WebCoin)...\n`;

            while (!hash.startsWith(target) && isMining) {
                newBlock.nonce++;
                hash = calculateBlockHash(newBlock);
                attempts++;
                
                if (attempts % 10000 === 0 && isMining) {
                    const elapsed = ((Date.now() - startTime)/1000).toFixed(1);
                    const speed = (attempts / elapsed).toFixed(0);
                    logDiv.innerHTML += `⏳ Đã thử ${attempts} nonce, tốc độ ${speed} H/s, hash hiện tại: ${hash.substring(0, 20)}...\n`;
                    logDiv.scrollTop = logDiv.scrollHeight;
                    await new Promise(resolve => setTimeout(resolve, 0));
                }
            }

            if (!isMining) break;

            const totalTime = ((Date.now() - startTime)/1000).toFixed(2);
            logDiv.innerHTML += `✅ Tìm thấy nonce: ${newBlock.nonce}\n`;
            logDiv.innerHTML += `🔗 Hash: ${hash}\n`;
            logDiv.innerHTML += `⏱️ Thời gian: ${totalTime}s\n`;

            const submitData = {
                height: newBlock.height,
                transactions: newBlock.transactions,
                previousHash: newBlock.previousHash,
                timestamp: newBlock.timestamp,
                nonce: newBlock.nonce,
                hash: hash,
                minerAddress: currentWallet.displayAddress
            };

            const submitRes = await fetch('/api/blocks/submit', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(submitData),
                credentials: 'include'
            });
            
            const result = await submitRes.json();
            if (submitRes.ok) {
                logDiv.innerHTML += `✅ Block accepted! Nhận ${info.reward} WebCoin\n`;
                refreshBalance();
                loadInfo();
                loadChain();
                loadHistory();
                
                // Nghỉ 1 giây trước khi đào block tiếp theo
                await new Promise(resolve => setTimeout(resolve, 1000));
            } else {
                logDiv.innerHTML += `❌ Lỗi: ${result.error}\n`;
                
                // Nếu lỗi vì độ khó thấp, tự động tăng độ khó lên bằng mạng
                if (result.error.includes('difficulty') && miningDifficulty < networkDifficulty) {
                    logDiv.innerHTML += `⚠️ Tự động tăng độ khó lên ${networkDifficulty} để phù hợp mạng\n`;
                    miningDifficulty = networkDifficulty;
                    document.getElementById('difficulty-slider').value = miningDifficulty;
                    document.getElementById('current-difficulty-display').innerText = miningDifficulty;
                }
                
                await new Promise(resolve => setTimeout(resolve, 5000));
            }
            
        } catch (err) {
            logDiv.innerHTML += `❌ Lỗi kết nối: ${err.message}\n`;
            await new Promise(resolve => setTimeout(resolve, 5000));
        }
    }
}

// Đào nhanh với độ khó thấp
async function startSimpleMining() {
    if (!currentWallet.displayAddress) {
        showError('mining-log', 'Vui lòng đăng nhập trước');
        return;
    }

    // Tạm thời đặt độ khó thấp
    const originalDifficulty = miningDifficulty;
    miningDifficulty = 2;
    
    const logDiv = document.getElementById('mining-log');
    logDiv.innerHTML = `⚡ Bắt đầu đào nhanh với độ khó 2...\n`;
    
    if (2 < networkDifficulty) {
        logDiv.innerHTML += `⚠️ CẢNH BÁO: Độ khó 2 thấp hơn mạng (${networkDifficulty}). Block có thể bị từ chối!\n`;
    }
    
    try {
        const info = await loadInfo();
        if (!info) return;

        const pendingRes = await fetch('/api/pending');
        const pending = await pendingRes.json();

        const coinbase = {
            from: null,
            to: currentWallet.publicKey,
            amount: info.reward,
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

        const latest = info.latestBlock;
        const newBlock = {
            height: latest ? latest.height + 1 : 0,
            transactions,
            previousHash: latest ? latest.hash : "0",
            timestamp: Date.now(),
            nonce: 0
        };

        function calculateBlockHash(block) {
            const txString = block.transactions.map(tx => JSON.stringify(tx)).join('');
            return CryptoJS.SHA1(block.height + block.previousHash + block.timestamp + txString + block.nonce).toString();
        }

        let hash = calculateBlockHash(newBlock);
        const target = '00'; // Độ khó 2
        let attempts = 0;
        const startTime = Date.now();

        while (!hash.startsWith(target)) {
            newBlock.nonce++;
            hash = calculateBlockHash(newBlock);
            attempts++;
            
            if (attempts % 1000 === 0) {
                const elapsed = ((Date.now() - startTime)/1000).toFixed(1);
                logDiv.innerHTML = `⚡ Đang thử ${attempts} nonce... (${elapsed}s)\n`;
                logDiv.scrollTop = logDiv.scrollHeight;
                await new Promise(resolve => setTimeout(resolve, 0));
            }
        }

        const totalTime = ((Date.now() - startTime)/1000).toFixed(2);
        logDiv.innerHTML += `✅ Tìm thấy nonce: ${newBlock.nonce}\n`;
        logDiv.innerHTML += `🔗 Hash: ${hash}\n`;
        logDiv.innerHTML += `⏱️ Thời gian: ${totalTime}s\n`;

        const submitData = {
            height: newBlock.height,
            transactions: newBlock.transactions,
            previousHash: newBlock.previousHash,
            timestamp: newBlock.timestamp,
            nonce: newBlock.nonce,
            hash: hash,
            minerAddress: currentWallet.displayAddress
        };

        const submitRes = await fetch('/api/blocks/submit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(submitData),
            credentials: 'include'
        });
        
        const result = await submitRes.json();
        if (submitRes.ok) {
            logDiv.innerHTML += `✅ Block accepted! Nhận ${info.reward} WebCoin\n`;
            refreshBalance();
            loadInfo();
            loadChain();
            loadHistory();
        } else {
            logDiv.innerHTML += `❌ Lỗi: ${result.error}\n`;
            
            // Nếu lỗi vì độ khó, đề xuất tăng lên
            if (result.error.includes('difficulty')) {
                logDiv.innerHTML += `💡 Gợi ý: Nên đặt độ khó >= ${networkDifficulty}\n`;
            }
        }
        
    } catch (err) {
        logDiv.innerHTML += `❌ Lỗi: ${err.message}\n`;
    } finally {
        // Khôi phục độ khó
        miningDifficulty = originalDifficulty;
        document.getElementById('difficulty-slider').value = miningDifficulty;
        document.getElementById('current-difficulty-display').innerText = miningDifficulty;
    }
}

// Gán sự kiện sau khi DOM tải
document.addEventListener('DOMContentLoaded', () => {
    // Tab switching
    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            showTab(tab.dataset.tab);
        });
    });

    // Các nút chính
    document.getElementById('register-btn')?.addEventListener('click', registerWallet);
    document.getElementById('login-btn')?.addEventListener('click', loginWallet);
    document.getElementById('logout-btn')?.addEventListener('click', logoutWallet);
    document.getElementById('refresh-balance-btn')?.addEventListener('click', refreshBalance);
    document.getElementById('send-btn')?.addEventListener('click', sendTransaction);
    document.getElementById('mine-btn')?.addEventListener('click', startMining);
    document.getElementById('refresh-chain-btn')?.addEventListener('click', loadChain);
    
    // History controls
    document.getElementById('history-filter')?.addEventListener('change', (e) => {
        historyFilter = e.target.value;
        displayHistory();
    });
    
    document.getElementById('refresh-history-btn')?.addEventListener('click', loadHistory);

    // Thanh trượt độ khó
    document.getElementById('difficulty-slider')?.addEventListener('input', adjustMiningDifficulty);

    // Khởi tạo
    loadInfo();
    loadChain();

    // Tự động refresh balance
    setInterval(() => {
        if (currentWallet.displayAddress) refreshBalance();
    }, 10000);
});