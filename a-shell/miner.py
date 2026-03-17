#!/usr/bin/env python3
"""
WebCoin Miner v4.0 - CHUẨN WEB
- Copy chính xác cách tính hash từ script.js
- ĐÃ SỬA LỖI LINE 121
"""

import os
import sys
import time
import json
import hashlib
import threading
import multiprocessing
from datetime import datetime
import configparser
import requests
from colorama import init, Fore, Back, Style

try:
    import psutil
    HAS_PSUTIL = True
except:
    HAS_PSUTIL = False

init(autoreset=True)

VERSION = "4.0"
SERVER_URL = "https://webcoin-1n9d.onrender.com/api"
DATA_DIR = "WebCoin-Miner"
SETTINGS_FILE = "config.ini"
SOC_TIMEOUT = 10

running = True
total_hashes = 0
blocks_mined = 0
total_reward = 0
start_time = time.time()
stats_lock = threading.Lock()
CPU_CORES = multiprocessing.cpu_count()
auth_cookie = None

cached_info = None
last_info_time = 0
INFO_CACHE_TIME = 2

def now():
    return datetime.now()

def get_prefix(val):
    if val >= 1_000_000:
        return f"{val/1_000_000:.2f} MH/s"
    elif val >= 1_000:
        return f"{val/1_000:.2f} kH/s"
    else:
        return f"{val:.2f} H/s"

def print_color(text, color=Fore.WHITE, bright=False):
    style = Style.BRIGHT if bright else ""
    print(f"{color}{style}{text}{Style.RESET_ALL}")

# ============== API ==============
def login(wallet, password):
    try:
        data = {"displayAddress": wallet, "password": password}
        resp = requests.post(f"{SERVER_URL}/login", json=data, timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            result = resp.json()
            if "error" not in result:
                cookie = resp.headers.get("set-cookie", "")
                return cookie.split(";")[0] if cookie else True
        return None
    except:
        return None

def get_network_info(force=False):
    global cached_info, last_info_time
    now_time = time.time()
    if not force and cached_info and now_time - last_info_time < INFO_CACHE_TIME:
        return cached_info
    try:
        resp = requests.get(f"{SERVER_URL}/info", timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            cached_info = resp.json()
            last_info_time = now_time
            return cached_info
    except:
        pass
    return cached_info

def submit_block(height, nonce, hash_value, prev_hash, reward, wallet, cookie):
    public_key = wallet[2:]
    
    transactions = [{
        "from": None,
        "to": public_key,
        "amount": reward,
        "timestamp": int(time.time() * 1000),
        "signature": None
    }]
    
    data = {
        "height": height,
        "transactions": transactions,
        "previousHash": prev_hash,
        "timestamp": int(time.time() * 1000),
        "nonce": nonce,
        "hash": hash_value,
        "minerAddress": wallet
    }
    headers = {"Content-Type": "application/json"}
    if cookie:
        headers["Cookie"] = cookie
    try:
        resp = requests.post(f"{SERVER_URL}/blocks/submit", json=data, headers=headers, timeout=SOC_TIMEOUT)
        return resp.status_code == 200
    except:
        return False

# ============== HASH CHUẨN THEO WEB ==============
def calculate_block_hash(height, prev_hash, timestamp, transactions, nonce):
    """
    Copy chính xác từ script.js:
    const txString = block.transactions.map(tx => JSON.stringify(tx)).join('');
    CryptoJS.SHA1(block.height + block.previousHash + block.timestamp + txString + block.nonce).toString();
    """
    # Tạo txString giống web: JSON.stringify từng transaction rồi join
    tx_string = ''.join([json.dumps(tx, separators=(',', ':')) for tx in transactions])
    
    # Tạo data để hash
    data = f"{height}{prev_hash}{timestamp}{tx_string}{nonce}"
    
    # Tính SHA1
    return hashlib.sha1(data.encode()).hexdigest()

# ============== MINING THREAD ==============
def mining_thread(thread_id, wallet, difficulty_override, target_cpu_percent, cookie):
    global total_hashes, blocks_mined, total_reward, running

    print_color(f"🧵 Thread {thread_id} started", Fore.CYAN)
    batch_size = 5000
    nonce_base = thread_id * 5000000

    while running:
        try:
            info = get_network_info(thread_id == 0)
            if not info or not info.get('latestBlock'):
                time.sleep(0.1)
                continue

            latest = info['latestBlock']
            network_diff = info.get('difficulty', 3)
            reward = info.get('reward', 48)

            difficulty = difficulty_override if difficulty_override else network_diff
            target = "0" * difficulty

            timestamp = int(time.time() * 1000)
            
            public_key = wallet[2:]
            transactions = [{
                "from": None,
                "to": public_key,
                "amount": reward,
                "timestamp": timestamp,
                "signature": None
            }]
            
            height = latest['height'] + 1
            prev_hash = latest['hash']

            start_local = time.time()
            local_hashes = 0
            
            for batch_start in range(nonce_base, nonce_base + 10000000, batch_size):
                if not running:
                    break

                batch_end = min(batch_start + batch_size, nonce_base + 10000000)
                
                for nonce in range(batch_start, batch_end):
                    hash_value = calculate_block_hash(height, prev_hash, timestamp, transactions, nonce)
                    local_hashes += 1
                    
                    if hash_value.startswith(target):
                        elapsed = time.time() - start_local
                        speed = local_hashes / elapsed if elapsed > 0 else 0
                        
                        print_color(f"\n🎯 Thread {thread_id}: Found nonce {nonce} in {elapsed:.1f}s ({speed/1000:.1f} kH/s)", Fore.GREEN)
                        print_color(f"   Hash: {hash_value}", Fore.CYAN)

                        if thread_id == 0 and cookie:
                            if submit_block(height, nonce, hash_value, prev_hash, reward, wallet, cookie):
                                with stats_lock:
                                    blocks_mined += 1
                                    total_reward += reward
                                print_color(f"✅ Block #{height} ACCEPTED! +{reward} WBC", Fore.GREEN)
                            else:
                                print_color(f"❌ Block #{height} REJECTED!", Fore.RED)
                        
                        # Sau khi tìm thấy nonce, thoát vòng lặp để lấy block mới
                        break
                    
                with stats_lock:
                    total_hashes += (batch_end - batch_start)
                
                if target_cpu_percent < 100:
                    time.sleep(0.0005)

        except Exception as e:
            print_color(f"Thread {thread_id} error: {e}", Fore.RED)
            time.sleep(1)

# ============== STATS THREAD ==============
def stats_thread():
    global running
    last_hashes = 0
    last_time = time.time()

    while running:
        time.sleep(5)
        with stats_lock:
            current_hashes = total_hashes
            current_blocks = blocks_mined
            current_reward = total_reward

        now = time.time()
        elapsed_total = now - start_time
        speed = (current_hashes - last_hashes) / (now - last_time) if (now - last_time) > 0 else 0

        cpu_info = ""
        if HAS_PSUTIL:
            cpu_percent = psutil.cpu_percent()
            cpu_info = f" | CPU: {cpu_percent:.1f}%"

        print_color(f"\n📊 STATS [{int(elapsed_total/60)}m {int(elapsed_total%60)}s]", Fore.MAGENTA, bright=True)
        print_color(f"   Total hashes: {current_hashes:,}", Fore.CYAN)
        print_color(f"   Speed: {speed/1000:.2f} kH/s{cpu_info}", Fore.CYAN)
        print_color(f"   Blocks mined: {current_blocks}", Fore.GREEN)
        print_color(f"   Total reward: {current_reward} WBC", Fore.GREEN)

        last_hashes = current_hashes
        last_time = now

# ============== CẤU HÌNH ==============
def save_config(wallet, password, threads, cpu_percent, difficulty):
    config = configparser.ConfigParser()
    config["WebCoin"] = {
        "wallet": wallet,
        "password": password,
        "threads": str(threads),
        "cpu_percent": str(cpu_percent),
        "difficulty": str(difficulty) if difficulty else "auto"
    }
    if not os.path.exists(DATA_DIR):
        os.makedirs(DATA_DIR)
    with open(os.path.join(DATA_DIR, SETTINGS_FILE), "w") as f:
        config.write(f)
    print_color(f"✅ Config saved", Fore.GREEN)

def load_config():
    config_path = os.path.join(DATA_DIR, SETTINGS_FILE)
    if not os.path.exists(config_path):
        return None
    config = configparser.ConfigParser()
    config.read(config_path)
    if "WebCoin" not in config:
        return None
    data = config["WebCoin"]
    diff = data.get("difficulty")
    if diff == "auto":
        diff = None
    elif diff:
        diff = int(diff)
    return {
        "wallet": data.get("wallet", ""),
        "password": data.get("password", ""),
        "threads": int(data.get("threads", CPU_CORES)),
        "cpu_percent": int(data.get("cpu_percent", 100)),
        "difficulty": diff
    }

# ============== GIAO DIỆN ==============
def input_wallet():
    addr = input(f"{Fore.YELLOW}Địa chỉ ví (W_...): {Style.RESET_ALL}").strip()
    if not addr.startswith('W_'):
        addr = 'W_' + addr
    return addr

def input_password():
    pwd = input(f"{Fore.YELLOW}Mật khẩu ví: {Style.RESET_ALL}").strip()
    return pwd

def input_threads():
    val = input(f"{Fore.YELLOW}Số luồng (1-{CPU_CORES*2}) [{CPU_CORES}]: {Style.RESET_ALL}").strip()
    return int(val) if val else CPU_CORES

def input_cpu_percent():
    val = input(f"{Fore.YELLOW}% CPU (10-100) [100]: {Style.RESET_ALL}").strip()
    return int(val) if val else 100

def input_difficulty():
    print_color("\n⚙️ Độ khó:", Fore.CYAN)
    print(" 1 - Thấp (2 số 0)")
    print(" 2 - Trung bình (3 số 0)")
    print(" 3 - Cao (4 số 0)")
    print(" 4 - Tự động (theo mạng)")
    choice = input(f"{Fore.YELLOW}Chọn (1-4) [4]: {Style.RESET_ALL}").strip()
    return None if choice in ['', '4'] else (int(choice) + 1)

# ============== MAIN ==============
def main():
    global running, start_time, auth_cookie

    print_color("\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER v4.0 - CHUẨN WEB", Fore.MAGENTA, bright=True)
    print_color("="*60, Fore.MAGENTA, bright=True)

    config = load_config()
    if config and input(f"{Fore.YELLOW}📁 Dùng config cũ? (y/n): ").lower() != 'n':
        wallet = config['wallet']
        password = config['password']
        threads = config['threads']
        cpu_percent = config['cpu_percent']
        difficulty_override = config['difficulty']
    else:
        print_color("\n📝 NHẬP THÔNG TIN MỚI", Fore.YELLOW)
        wallet = input_wallet()
        password = input_password()
        threads = input_threads()
        cpu_percent = input_cpu_percent()
        difficulty_override = input_difficulty()
        save_config(wallet, password, threads, cpu_percent, difficulty_override)

    print_color(f"\n🔐 Đang đăng nhập...", Fore.YELLOW)
    cookie = login(wallet, password)
    if not cookie:
        print_color(f"❌ Đăng nhập thất bại!", Fore.RED)
        return
    auth_cookie = cookie
    print_color(f"✅ Đăng nhập thành công!", Fore.GREEN)

    info = get_network_info(force=True)
    if info:
        print_color(f"\n📡 Mạng: độ khó {info.get('difficulty', '?')}, block #{info.get('latestBlock', {}).get('height', '?')}", Fore.CYAN)

    print_color(f"\n🚀 Bắt đầu đào {threads} luồng, {cpu_percent}% CPU", Fore.GREEN)
    print_color("="*60, Fore.MAGENTA)

    start_time = time.time()

    for i in range(threads):
        t = threading.Thread(target=mining_thread, args=(i, wallet, difficulty_override, cpu_percent, cookie), daemon=True)
        t.start()

    stats = threading.Thread(target=stats_thread, daemon=True)
    stats.start()

    try:
        while running:
            time.sleep(1)
    except KeyboardInterrupt:
        print_color(f"\n\n🛑 Stopping...", Fore.YELLOW)
        running = False

if __name__ == "__main__":
    main()
