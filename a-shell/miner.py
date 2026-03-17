#!/usr/bin/env python3
"""
WebCoin Miner v11.0 - FINAL WORKING VERSION
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

VERSION = "11.0"
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

def get_network_info():
    try:
        resp = requests.get(f"{SERVER_URL}/info", timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            return resp.json()
    except:
        pass
    return None

def get_pending():
    try:
        resp = requests.get(f"{SERVER_URL}/pending", timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            return resp.json()
    except:
        pass
    return []

def submit_block(height, nonce, hash_value, prev_hash, reward, wallet, cookie, transactions):
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

# ============== HASH CHUẨN ==============
def json_stringify(obj):
    if obj is None:
        return "null"
    elif isinstance(obj, str):
        return f'"{obj}"'
    elif isinstance(obj, (int, float)):
        return str(obj)
    elif isinstance(obj, bool):
        return "true" if obj else "false"
    elif isinstance(obj, dict):
        items = []
        for k, v in obj.items():
            items.append(f'"{k}":{json_stringify(v)}')
        return "{" + ",".join(items) + "}"
    elif isinstance(obj, list):
        items = [json_stringify(item) for item in obj]
        return "[" + ",".join(items) + "]"
    return json.dumps(obj, separators=(',', ':'))

def calculate_block_hash(height, prev_hash, timestamp, transactions, nonce):
    tx_string = ''.join([json_stringify(tx) for tx in transactions])
    data = f"{height}{prev_hash}{timestamp}{tx_string}{nonce}"
    return hashlib.sha1(data.encode()).hexdigest()

# ============== MINING THREAD ==============
def mining_thread(thread_id, wallet, difficulty_override, target_cpu_percent, cookie):
    global total_hashes, blocks_mined, total_reward, running

    print_color(f"🧵 Thread {thread_id} started", Fore.CYAN)

    while running:
        try:
            info = get_network_info()
            if not info or not info.get('latestBlock'):
                time.sleep(0.1)
                continue

            latest = info['latestBlock']
            network_diff = info.get('difficulty', 3)
            base_reward = info.get('reward', 48)

            difficulty = difficulty_override if difficulty_override else network_diff
            target = "0" * difficulty
            
            height = latest['height'] + 1
            prev_hash = latest['hash']
            public_key = wallet[2:]

            start_nonce = thread_id * 20000000
            end_nonce = (thread_id + 1) * 20000000
            nonce = start_nonce
            
            pending = get_pending()
            print_color(f"\n📦 Block #{height} - Độ khó {difficulty} - {len(pending)} pending", Fore.YELLOW)
            
            start_local = time.time()
            local_hashes = 0
            found = False
            
            while running and nonce < end_nonce and not found:
                timestamp = int(time.time() * 1000)
                
                coinbase = {
                    "from": None,
                    "to": public_key,
                    "amount": base_reward,
                    "timestamp": timestamp,
                    "signature": None
                }
                
                transactions = [coinbase] + pending
                hash_value = calculate_block_hash(height, prev_hash, timestamp, transactions, nonce)
                local_hashes += 1
                
                if hash_value.startswith(target):
                    elapsed = time.time() - start_local
                    speed = local_hashes / elapsed if elapsed > 0 else 0
                    
                    print_color(f"\n🎯 Thread {thread_id}: Found nonce {nonce}", Fore.GREEN)
                    print_color(f"   Hash: {hash_value}", Fore.CYAN)
                    print_color(f"   Speed: {speed/1000:.1f} kH/s", Fore.CYAN)

                    if thread_id == 0 and cookie:
                        if submit_block(height, nonce, hash_value, prev_hash, base_reward, wallet, cookie, transactions):
                            with stats_lock:
                                blocks_mined += 1
                                total_reward += base_reward
                            print_color(f"✅✅✅ Block #{height} ACCEPTED! +{base_reward} WBC ✅✅✅", Fore.GREEN)
                        else:
                            print_color(f"❌❌❌ Block #{height} REJECTED! ❌❌❌", Fore.RED)
                    
                    found = True
                    break
                
                nonce += 1
                
                if nonce % 10000 == 0:
                    with stats_lock:
                        total_hashes += 10000

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

# ============== MAIN ==============
def main():
    global running, start_time, auth_cookie

    print_color("\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER v11.0 - WORKING", Fore.MAGENTA, bright=True)
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
        wallet = input("Địa chỉ ví (W_...): ").strip()
        if not wallet.startswith('W_'):
            wallet = 'W_' + wallet
        password = input("Mật khẩu ví: ").strip()
        threads = int(input(f"Số luồng (1-{CPU_CORES*2}) [{CPU_CORES}]: ").strip() or CPU_CORES)
        cpu_percent = int(input(f"% CPU (10-100) [100]: ").strip() or 100)
        print_color("\n⚙️ Độ khó:", Fore.CYAN)
        print(" 1 - Thấp (2 số 0)")
        print(" 2 - Trung bình (3 số 0)")
        print(" 3 - Cao (4 số 0)")
        print(" 4 - Tự động (theo mạng)")
        choice = input("Chọn (1-4) [4]: ").strip()
        if choice == "1":
            difficulty_override = 2
        elif choice == "2":
            difficulty_override = 3
        elif choice == "3":
            difficulty_override = 4
        else:
            difficulty_override = None
        save_config(wallet, password, threads, cpu_percent, difficulty_override)

    print_color(f"\n🔐 Đang đăng nhập...", Fore.YELLOW)
    cookie = login(wallet, password)
    if not cookie:
        print_color(f"❌ Đăng nhập thất bại!", Fore.RED)
        return
    auth_cookie = cookie
    print_color(f"✅ Đăng nhập thành công!", Fore.GREEN)

    info = get_network_info()
    if info:
        print_color(f"\n📡 Độ khó: {info.get('difficulty', '?')} | Block: {info.get('latestBlock', {}).get('height', '?')} | Thưởng: {info.get('reward', '?')} WBC", Fore.CYAN)

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

if __name__ == "__main__":
    main()
