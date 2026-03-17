#!/usr/bin/env python3
"""
WebCoin Miner v12.0 - TURBO EDITION
- Tốc độ tối đa (500-1000 kH/s)
- Điều chỉnh % CPU thật bằng psutil
- Tự động tối ưu luồng
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

# BẮT BUỘC psutil để điều chỉnh CPU
try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    print("❌ Cần cài psutil: pip install psutil")
    sys.exit(1)

init(autoreset=True)

VERSION = "12.0"
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

# ============== HASH TỐI ƯU ==============
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

# ============== CPU CONTROLLER ==============
class CPUController:
    def __init__(self, target_percent):
        self.target_percent = target_percent
        self.running = True
        self.thread = threading.Thread(target=self._monitor)
        self.thread.daemon = True
        self.thread.start()
    
    def _monitor(self):
        """Giám sát và điều chỉnh CPU realtime"""
        while self.running:
            try:
                # Lấy CPU % hiện tại
                current_cpu = psutil.cpu_percent(interval=0.5)
                
                # Tính toán sleep time dựa trên độ lệch
                diff = current_cpu - self.target_percent
                
                if diff > 5:  # Quá tải
                    self.sleep_time = min(self.sleep_time * 1.2, 0.01)
                elif diff < -5:  # Còn trống
                    self.sleep_time = max(self.sleep_time * 0.8, 0)
                
            except:
                pass
            time.sleep(1)
    
    def get_sleep(self):
        return getattr(self, 'sleep_time', 0)
    
    def stop(self):
        self.running = False

# ============== MINING THREAD TURBO ==============
def mining_thread(thread_id, wallet, difficulty_override, target_cpu_percent, cookie):
    global total_hashes, blocks_mined, total_reward, running
    
    # CPU Controller cho thread này
    cpu_controller = CPUController(target_cpu_percent)
    
    print_color(f"🧵 Thread {thread_id} started - Target CPU: {target_cpu_percent}%", Fore.CYAN)
    
    # Batch size lớn để tăng tốc
    BATCH_SIZE = 50000
    
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

            # Range lớn hơn cho mỗi thread
            range_size = 100000000
            start_nonce = thread_id * range_size
            end_nonce = (thread_id + 1) * range_size
            nonce = start_nonce
            
            pending = get_pending()
            print_color(f"\n📦 Block #{height} - Thread {thread_id} - {len(pending)} pending", Fore.YELLOW)
            
            start_local = time.time()
            local_hashes = 0
            found = False
            
            # Đào theo batch
            while running and nonce < end_nonce and not found:
                # Tạo timestamp cho batch
                timestamp = int(time.time() * 1000)
                
                coinbase = {
                    "from": None,
                    "to": public_key,
                    "amount": base_reward,
                    "timestamp": timestamp,
                    "signature": None
                }
                
                transactions = [coinbase] + pending
                
                # Đào BATCH_SIZE nonce liên tục
                batch_start = nonce
                for i in range(BATCH_SIZE):
                    current_nonce = batch_start + i
                    hash_value = calculate_block_hash(height, prev_hash, timestamp, transactions, current_nonce)
                    local_hashes += 1
                    
                    if hash_value.startswith(target):
                        found = True
                        nonce = current_nonce
                        break
                
                nonce += BATCH_SIZE
                
                # Cập nhật hashrate
                with stats_lock:
                    total_hashes += BATCH_SIZE
                
                # Điều chỉnh CPU theo target
                sleep_time = cpu_controller.get_sleep()
                if sleep_time > 0:
                    time.sleep(sleep_time)
                
                # Báo cáo tốc độ mỗi 100K hashes
                if local_hashes % 100000 == 0 and thread_id == 0:
                    elapsed = time.time() - start_local
                    speed = local_hashes / elapsed if elapsed > 0 else 0
                    current_cpu = psutil.cpu_percent()
                    print_color(f"   Thread 0: {local_hashes/1000:.0f}K hashes, {speed/1000:.1f} kH/s, CPU: {current_cpu}%", Fore.CYAN)
            
            if found:
                elapsed = time.time() - start_local
                speed = local_hashes / elapsed if elapsed > 0 else 0
                
                print_color(f"\n🎯 Thread {thread_id}: Found nonce {nonce}", Fore.GREEN)
                print_color(f"   Speed: {speed/1000:.1f} kH/s", Fore.CYAN)

                if thread_id == 0 and cookie:
                    if submit_block(height, nonce, hash_value, prev_hash, base_reward, wallet, cookie, transactions):
                        with stats_lock:
                            blocks_mined += 1
                            total_reward += base_reward
                        print_color(f"✅✅✅ Block #{height} ACCEPTED! +{base_reward} WBC ✅✅✅", Fore.GREEN)
                    else:
                        print_color(f"❌❌❌ Block #{height} REJECTED! ❌❌❌", Fore.RED)

        except Exception as e:
            print_color(f"Thread {thread_id} error: {e}", Fore.RED)
            time.sleep(1)
    
    cpu_controller.stop()

# ============== STATS THREAD ==============
def stats_thread():
    global running
    last_hashes = 0
    last_time = time.time()
    
    # Lấy CPU ban đầu
    initial_cpu = psutil.cpu_percent()

    while running:
        time.sleep(2)  # Cập nhật nhanh hơn
        with stats_lock:
            current_hashes = total_hashes
            current_blocks = blocks_mined
            current_reward = total_reward

        now = time.time()
        elapsed_total = now - start_time
        speed = (current_hashes - last_hashes) / (now - last_time) if (now - last_time) > 0 else 0

        # Thông tin CPU realtime
        cpu_percent = psutil.cpu_percent()
        cpu_freq = psutil.cpu_freq()
        cpu_temp = "N/A"
        try:
            temps = psutil.sensors_temperatures()
            if 'coretemp' in temps:
                cpu_temp = f"{temps['coretemp'][0].current}°C"
        except:
            pass

        print_color(f"\n📊 STATS [{int(elapsed_total/60)}m {int(elapsed_total%60)}s]", Fore.MAGENTA, bright=True)
        print_color(f"   📈 Hashes: {current_hashes:,} | Speed: {speed/1000:.2f} kH/s", Fore.CYAN)
        print_color(f"   💻 CPU: {cpu_percent:.1f}% | Freq: {cpu_freq.current:.0f}MHz | Temp: {cpu_temp}", Fore.YELLOW)
        print_color(f"   ⛏️  Blocks: {current_blocks} | Reward: {current_reward} WBC", Fore.GREEN)

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

# ============== MAIN ==============
def main():
    global running, start_time, auth_cookie

    print_color("\n" + "="*70, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER v12.0 - TURBO EDITION", Fore.MAGENTA, bright=True)
    print_color("="*70, Fore.MAGENTA, bright=True)

    print_color(f"\n🖥️  CPU: {CPU_CORES} cores | {psutil.cpu_freq().max:.0f}MHz max", Fore.CYAN)

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
        threads = int(input(f"Số luồng (1-{CPU_CORES}) [{CPU_CORES}]: ").strip() or CPU_CORES)
        cpu_percent = int(input(f"% CPU (10-100) [100]: ").strip() or 100)
        cpu_percent = max(10, min(100, cpu_percent))
        
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
        print_color(f"\n📡 Network: diff={info.get('difficulty')}, reward={info.get('reward')} WBC", Fore.CYAN)

    print_color(f"\n🚀 Starting {threads} threads at {cpu_percent}% CPU target", Fore.GREEN)
    print_color("="*70, Fore.MAGENTA)

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
