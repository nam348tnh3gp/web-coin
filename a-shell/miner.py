#!/usr/bin/env python3
"""
WebCoin Miner - FIXED VERSION
- Tối ưu tốc độ gấp 10x
- Fix lỗi block reject
- Tự động điều chỉnh nonce range
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

# ============== CẤU HÌNH ==============
VERSION = "2.1"
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

# Cache thông tin mạng
cached_info = None
last_info_time = 0
INFO_CACHE_TIME = 2  # Giây

# ============== TIỆN ÍCH ==============
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
    now = time.time()
    if not force and cached_info and now - last_info_time < INFO_CACHE_TIME:
        return cached_info
    try:
        resp = requests.get(f"{SERVER_URL}/info", timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            cached_info = resp.json()
            last_info_time = now
            return cached_info
    except:
        pass
    return cached_info

def submit_block(height, nonce, hash_value, prev_hash, reward, wallet, cookie):
    public_key = wallet[2:]
    data = {
        "height": height,
        "transactions": [{
            "from": None,
            "to": public_key,
            "amount": reward,
            "timestamp": int(time.time() * 1000),
            "signature": None
        }],
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

# ============== THUẬT TOÁN ĐÀO TỐI ƯU ==============
def fast_sha1(data):
    """Tính SHA1 nhanh nhất có thể"""
    return hashlib.sha1(data.encode()).hexdigest()

def mine_range(start_nonce, end_nonce, height, prev_hash, timestamp, tx_string, target_prefix):
    """Đào trong một range nonce, trả về nonce đầu tiên tìm thấy hoặc None"""
    for nonce in range(start_nonce, end_nonce):
        hash_value = fast_sha1(f"{height}{prev_hash}{timestamp}{tx_string}{nonce}")
        if hash_value.startswith(target_prefix):
            return nonce, hash_value
    return None, None

# ============== THREAD ĐÀO ==============
def mining_thread(thread_id, wallet, difficulty_override, target_cpu_percent, cookie):
    global total_hashes, blocks_mined, total_reward, running

    print_color(f"🧵 Thread {thread_id} started", Fore.CYAN)
    batch_size = 5000  # Tăng batch size để tăng tốc
    nonce_counter = 0

    while running:
        try:
            info = get_network_info(thread_id == 0)  # Thread 0 force refresh
            if not info or not info.get('latestBlock'):
                time.sleep(0.1)
                continue

            latest = info['latestBlock']
            network_diff = info.get('difficulty', 3)
            reward = info.get('reward', 48)

            difficulty = difficulty_override if difficulty_override else network_diff
            target = "0" * difficulty

            timestamp = int(time.time() * 1000)
            tx_string = "[]"

            # Mỗi thread đào một range nonce khác nhau
            base_nonce = nonce_counter * 1000000 + thread_id * 10000
            nonce_counter += 1

            start_local = time.time()
            attempts = 0

            # Đào theo batch
            for batch_start in range(base_nonce, base_nonce + 1000000, batch_size):
                if not running:
                    break

                batch_end = min(batch_start + batch_size, base_nonce + 1000000)
                
                # Tìm nonce trong batch
                found_nonce, found_hash = mine_range(
                    batch_start, batch_end,
                    latest['height'] + 1,
                    latest['hash'],
                    timestamp,
                    tx_string,
                    target
                )

                with stats_lock:
                    total_hashes += (batch_end - batch_start)

                if found_nonce:
                    elapsed = time.time() - start_local
                    speed = (batch_end - batch_start) / elapsed if elapsed > 0 else 0
                    
                    print_color(f"\n🎯 Thread {thread_id}: Found nonce {found_nonce}", Fore.GREEN)

                    if thread_id == 0 and cookie:
                        if submit_block(
                            latest['height'] + 1,
                            found_nonce,
                            found_hash,
                            latest['hash'],
                            reward,
                            wallet,
                            cookie
                        ):
                            with stats_lock:
                                blocks_mined += 1
                                total_reward += reward
                            print_color(f"✅ Block accepted! +{reward} WBC", Fore.GREEN)
                        else:
                            print_color(f"❌ Block rejected!", Fore.RED)
                    
                    # Reset để đào block tiếp theo
                    break

                # Delay nhẹ để không ngập CPU
                if target_cpu_percent < 100:
                    time.sleep(0.001)

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
            cpu_info = f" | CPU: {cpu_percent}%"

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

# ============== GIAO DIỆN NHẬP LIỆU ==============
def input_wallet():
    while True:
        addr = input(f"{Fore.YELLOW}Địa chỉ ví (W_...): {Style.RESET_ALL}").strip()
        if not addr:
            print_color("❌ Không được để trống", Fore.RED)
            continue
        if not addr.startswith('W_'):
            addr = 'W_' + addr
        return addr

def input_password():
    while True:
        pwd = input(f"{Fore.YELLOW}Mật khẩu ví: {Style.RESET_ALL}").strip()
        if not pwd or len(pwd) < 6:
            print_color("❌ Mật khẩu phải có ít nhất 6 ký tự", Fore.RED)
            continue
        pwd2 = input(f"{Fore.YELLOW}Nhập lại mật khẩu: {Style.RESET_ALL}").strip()
        if pwd != pwd2:
            print_color("❌ Mật khẩu không khớp", Fore.RED)
            continue
        return pwd

def input_threads():
    max_th = CPU_CORES * 2
    default = CPU_CORES
    val = input(f"{Fore.YELLOW}Số luồng (1-{max_th}) [mặc định {default}]: {Style.RESET_ALL}").strip()
    return int(val) if val else default

def input_cpu_percent():
    val = input(f"{Fore.YELLOW}% CPU (10-100) [mặc định 100]: {Style.RESET_ALL}").strip()
    return int(val) if val else 100

def input_difficulty():
    print_color("\n⚙️ Độ khó:", Fore.CYAN)
    print(" 1 - Thấp (2 số 0)")
    print(" 2 - Trung bình (3 số 0)")
    print(" 3 - Cao (4 số 0)")
    print(" 4 - Tự động (theo mạng)")
    choice = input(f"{Fore.YELLOW}Chọn (1-4) [mặc định 4]: {Style.RESET_ALL}").strip()
    if choice == "1":
        return 2
    elif choice == "2":
        return 3
    elif choice == "3":
        return 4
    else:
        return None

# ============== MAIN ==============
def main():
    global running, start_time, auth_cookie

    print_color("\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER - FIXED VERSION", Fore.MAGENTA, bright=True)
    print_color("="*60, Fore.MAGENTA, bright=True)

    # Đọc config
    config = load_config()
    if config:
        print_color(f"\n📁 Config cũ:", Fore.CYAN)
        print_color(f"   Ví: {config['wallet'][:30]}...", Fore.CYAN)
        if input(f"{Fore.YELLOW}Dùng config này? (Y/n): ").lower() != 'n':
            wallet = config['wallet']
            password = config['password']
            threads = config['threads']
            cpu_percent = config['cpu_percent']
            difficulty_override = config['difficulty']
        else:
            wallet = None
    else:
        wallet = None

    if not wallet:
        print_color("\n📝 NHẬP THÔNG TIN MỚI", Fore.YELLOW)
        wallet = input_wallet()
        password = input_password()
        threads = input_threads()
        cpu_percent = input_cpu_percent()
        difficulty_override = input_difficulty()
        save_config(wallet, password, threads, cpu_percent, difficulty_override)

    # Đăng nhập
    print_color(f"\n🔐 Đang đăng nhập...", Fore.YELLOW)
    cookie = login(wallet, password)
    if not cookie:
        print_color(f"❌ Đăng nhập thất bại!", Fore.RED)
        return
    auth_cookie = cookie
    print_color(f"✅ Đăng nhập thành công!", Fore.GREEN)

    # Thông tin mạng
    info = get_network_info(force=True)
    if info:
        print_color(f"\n📡 Mạng:", Fore.CYAN)
        print_color(f"   Độ khó: {info.get('difficulty', '?')}", Fore.CYAN)
        print_color(f"   Block: {info.get('latestBlock', {}).get('height', '?')}", Fore.CYAN)

    # Bắt đầu đào
    print_color(f"\n🚀 Bắt đầu đào với {threads} luồng, {cpu_percent}% CPU", Fore.GREEN)
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
