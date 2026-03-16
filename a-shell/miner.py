#!/usr/bin/env python3
"""
WebCoin Miner - Có mật khẩu thường (không hex)
- Nhập địa chỉ ví + mật khẩu thường
- Tự động đăng nhập
- Chọn số luồng CPU, % CPU
- Lưu cấu hình
- Đào và gửi block
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
import getpass

init(autoreset=True)

# ============== CẤU HÌNH ==============
VERSION = "1.0"
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
auth_cookie = None  # Lưu cookie sau khi đăng nhập

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
    """Đăng nhập và lấy cookie"""
    try:
        data = {"displayAddress": wallet, "password": password}
        resp = requests.post(f"{SERVER_URL}/login", json=data, timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            result = resp.json()
            if "error" not in result:
                # Lấy cookie từ header
                cookie = resp.headers.get("set-cookie", "")
                if cookie:
                    return cookie.split(";")[0]
                return True
        return None
    except Exception as e:
        print_color(f"❌ Lỗi đăng nhập: {e}", Fore.RED)
        return None

def get_network_info():
    """Lấy thông tin mạng"""
    try:
        resp = requests.get(f"{SERVER_URL}/info", timeout=SOC_TIMEOUT)
        if resp.status_code == 200:
            return resp.json()
    except:
        pass
    return None

def submit_block(height, nonce, hash_value, prev_hash, reward, wallet, cookie):
    """Gửi block lên server"""
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

# ============== THUẬT TOÁN ĐÀO ==============
def sha1_hash(data):
    return hashlib.sha1(data.encode()).hexdigest()

def calculate_block_hash(height, prev_hash, timestamp, tx_string, nonce):
    return sha1_hash(f"{height}{prev_hash}{timestamp}{tx_string}{nonce}")

# ============== THREAD ĐÀO ==============
def mining_thread(thread_id, wallet, difficulty_override, target_cpu_percent, cookie):
    global total_hashes, blocks_mined, total_reward, running

    delay_time = (100 - target_cpu_percent) / 1000
    print_color(f"🧵 Thread {thread_id} started", Fore.CYAN)

    while running:
        try:
            info = get_network_info()
            if not info or not info.get('latestBlock'):
                time.sleep(0.5)
                continue

            latest = info['latestBlock']
            network_diff = info.get('difficulty', 3)
            reward = info.get('reward', 48)

            difficulty = difficulty_override if difficulty_override else network_diff
            target = "0" * difficulty

            timestamp = int(time.time() * 1000)
            tx_string = "[]"
            nonce = 0
            start_local = time.time()
            attempts = 0

            while running:
                hash_value = calculate_block_hash(
                    latest['height'] + 1,
                    latest['hash'],
                    timestamp,
                    tx_string,
                    nonce
                )

                with stats_lock:
                    total_hashes += 1

                attempts += 1

                if delay_time > 0 and attempts % 100 == 0:
                    time.sleep(delay_time * 100)

                if hash_value.startswith(target):
                    elapsed = time.time() - start_local
                    print_color(f"\n🎯 Thread {thread_id}: Found nonce {nonce} in {elapsed:.2f}s", Fore.GREEN)

                    if thread_id == 0:
                        if submit_block(
                            latest['height'] + 1,
                            nonce,
                            hash_value,
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
                    break

                nonce += 1

                if thread_id == 0 and attempts % 10000 == 0:
                    speed = attempts / (time.time() - start_local)
                    print_color(f"   Thread 0: {attempts} hashes, {speed:.0f} H/s", Fore.YELLOW)

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

        print_color(f"\n📊 STATS [{int(elapsed_total/60)}m {int(elapsed_total%60)}s]", Fore.MAGENTA, bright=True)
        print_color(f"   Total hashes: {current_hashes:,}", Fore.CYAN)
        print_color(f"   Speed: {speed/1000:.2f} kH/s", Fore.CYAN)
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
            print_color(f"⚠️ Đã thêm W_: {addr}", Fore.YELLOW)
        if len(addr) < 10:
            print_color("❌ Địa chỉ quá ngắn", Fore.RED)
            continue
        return addr

def input_password():
    while True:
        pwd = getpass.getpass(f"{Fore.YELLOW}Mật khẩu ví: {Style.RESET_ALL}")
        if not pwd:
            print_color("❌ Mật khẩu không được để trống", Fore.RED)
            continue
        if len(pwd) < 6:
            print_color("❌ Mật khẩu phải có ít nhất 6 ký tự", Fore.RED)
            continue
        pwd2 = getpass.getpass(f"{Fore.YELLOW}Nhập lại mật khẩu: {Style.RESET_ALL}")
        if pwd != pwd2:
            print_color("❌ Mật khẩu không khớp", Fore.RED)
            continue
        return pwd

def input_threads():
    max_th = CPU_CORES * 2
    default = CPU_CORES
    while True:
        val = input(f"{Fore.YELLOW}Số luồng (1-{max_th}) [mặc định {default}]: {Style.RESET_ALL}").strip()
        if not val:
            return default
        try:
            t = int(val)
            if 1 <= t <= max_th:
                return t
            else:
                print_color(f"❌ Nhập từ 1 đến {max_th}", Fore.RED)
        except:
            print_color("❌ Không hợp lệ", Fore.RED)

def input_cpu_percent():
    while True:
        val = input(f"{Fore.YELLOW}% CPU (10-100) [mặc định 100]: {Style.RESET_ALL}").strip()
        if not val:
            return 100
        try:
            p = int(val)
            if 10 <= p <= 100:
                return p
            else:
                print_color("❌ Nhập từ 10 đến 100", Fore.RED)
        except:
            print_color("❌ Không hợp lệ", Fore.RED)

def input_difficulty():
    print_color("\n⚙️ Độ khó:", Fore.CYAN)
    print(" 1 - Thấp (2 số 0, thưởng thấp)")
    print(" 2 - Trung bình (3 số 0)")
    print(" 3 - Cao (4 số 0, thưởng cao)")
    print(" 4 - Tự động (theo mạng)")
    while True:
        choice = input(f"{Fore.YELLOW}Chọn (1-4) [mặc định 4]: {Style.RESET_ALL}").strip()
        if not choice:
            return None  # auto
        if choice == "1":
            return 2
        elif choice == "2":
            return 3
        elif choice == "3":
            return 4
        elif choice == "4":
            return None
        else:
            print_color("❌ Chọn 1-4", Fore.RED)

# ============== MAIN ==============
def main():
    global running, start_time, auth_cookie

    print_color("\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER - CÓ MẬT KHẨU", Fore.MAGENTA, bright=True)
    print_color("="*60, Fore.MAGENTA, bright=True)

    # Đọc config cũ
    config = load_config()
    wallet = None
    password = None
    threads = CPU_CORES
    cpu_percent = 100
    difficulty_override = None

    if config:
        print_color(f"\n📁 Config cũ:", Fore.CYAN)
        print_color(f"   Ví: {config['wallet'][:30]}...", Fore.CYAN)
        print_color(f"   Threads: {config['threads']}", Fore.CYAN)
        print_color(f"   CPU: {config['cpu_percent']}%", Fore.CYAN)
        if input(f"{Fore.YELLOW}Dùng config này? (Y/n): ").lower() != 'n':
            wallet = config['wallet']
            password = config['password']
            threads = config['threads']
            cpu_percent = config['cpu_percent']
            difficulty_override = config['difficulty']

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
        print_color(f"❌ Đăng nhập thất bại! Kiểm tra lại ví và mật khẩu.", Fore.RED)
        return
    auth_cookie = cookie
    print_color(f"✅ Đăng nhập thành công!", Fore.GREEN)

    # Lấy thông tin mạng
    info = get_network_info()
    if info:
        print_color(f"\n📡 Mạng:", Fore.CYAN)
        print_color(f"   Độ khó: {info.get('difficulty', '?')}", Fore.CYAN)
        print_color(f"   Block cao nhất: {info.get('latestBlock', {}).get('height', '?')}", Fore.CYAN)
        print_color(f"   Phần thưởng: {info.get('reward', '?')} WBC", Fore.CYAN)

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
        print_color(f"\n\n🛑 Đang dừng...", Fore.YELLOW)
        running = False

    elapsed = time.time() - start_time
    print_color(f"\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" KẾT QUẢ CUỐI CÙNG", Fore.MAGENTA, bright=True)
    print_color("="*60, Fore.MAGENTA, bright=True)
    print_color(f"⏱️  Thời gian: {int(elapsed/60)}m {int(elapsed%60)}s", Fore.CYAN)
    print_color(f"🔢 Tổng hash: {total_hashes:,}", Fore.CYAN)
    print_color(f"⚡ Tốc độ TB: {(total_hashes/elapsed)/1000:.2f} kH/s", Fore.CYAN)
    print_color(f"⛏️  Blocks đào được: {blocks_mined}", Fore.GREEN)
    print_color(f"💰 Tổng thưởng: {total_reward} WBC", Fore.GREEN)
    print_color("="*60, Fore.MAGENTA)

if __name__ == "__main__":
    main()
