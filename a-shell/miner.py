#!/usr/bin/env python3
"""
WebCoin Miner PRO - Dùng thư viện chuẩn
- colorama: màu sắc đẹp
- requests: HTTP dễ dàng
- psutil: giám sát CPU
- KHÔNG CẦN MẬT KHẨU
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

# ============== THƯ VIỆN ==============
try:
    import requests
    from requests.adapters import HTTPAdapter
    from urllib3.util.retry import Retry
    HAS_REQUESTS = True
except:
    HAS_REQUESTS = False
    print("⚠️ requests not installed. Run: pip install requests")

try:
    import psutil
    HAS_PSUTIL = True
except:
    HAS_PSUTIL = False
    print("⚠️ psutil not installed. Run: pip install psutil")

try:
    from colorama import init, Fore, Back, Style
    init(autoreset=True)
    HAS_COLORAMA = True
except:
    HAS_COLORAMA = False
    # Tự tạo màu nếu không có colorama
    class Fore:
        RED = '\033[91m'; GREEN = '\033[92m'; YELLOW = '\033[93m'
        BLUE = '\033[94m'; MAGENTA = '\033[95m'; CYAN = '\033[96m'
        RESET = '\033[0m'
    class Style:
        BRIGHT = '\033[1m'
        RESET_ALL = '\033[0m'

def print_color(text, color=Fore.RESET, bright=False):
    prefix = Style.BRIGHT if bright and HAS_COLORAMA else ""
    print(f"{color}{prefix}{text}{Fore.RESET}")

# ============== CẤU HÌNH ==============
DEFAULT_WALLET = "W_042389ad1649d3e9566c95761a64e48322b5c85b589398557bb6ee6af006f6c1a9c3657648bfefc42b0c2c945f4f05a3d306ee6473e8e8b02e12b86be41dc45437"
DEFAULT_THREADS = multiprocessing.cpu_count()
DEFAULT_CPU_PERCENT = 100
DEFAULT_DIFFICULTY = None

CONFIG_FILE = "webcoin_config.ini"
BASE_URL = "https://webcoin-1n9d.onrender.com/api"

# ============== BIẾN TOÀN CỤC ==============
running = True
total_hashes = 0
blocks_mined = 0
total_reward = 0
start_time = time.time()
stats_lock = threading.Lock()
CPU_CORES = multiprocessing.cpu_count()

# ============== SESSION HTTP (dùng requests) ==============
if HAS_REQUESTS:
    session = requests.Session()
    retries = Retry(total=3, backoff_factor=1, status_forcelist=[502, 503, 504])
    session.mount('https://', HTTPAdapter(max_retries=retries))
    session.headers.update({'Content-Type': 'application/json'})

def http_request(method, path, data=None):
    """HTTP request dùng requests (nếu có) hoặc socket"""
    if HAS_REQUESTS:
        try:
            url = f"{BASE_URL}{path}"
            if method == "POST":
                response = session.post(url, json=data, timeout=10)
            else:
                response = session.get(url, timeout=10)
            
            if response.status_code == 200:
                return response.json()
            return None
        except:
            return None
    else:
        # Fallback dùng socket
        import socket
        try:
            host = "webcoin-1n9d.onrender.com"
            port = 443
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(10)
            s.connect((host, port))
            
            if method == "POST" and data:
                json_data = json.dumps(data)
                request = f"POST {path} HTTP/1.1\r\nHost: {host}\r\nContent-Type: application/json\r\nContent-Length: {len(json_data)}\r\nConnection: close\r\n\r\n{json_data}"
            else:
                request = f"GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n"
            
            s.send(request.encode())
            response = b""
            while True:
                part = s.recv(4096)
                if not part:
                    break
                response += part
            s.close()
            
            parts = response.decode('utf-8', errors='ignore').split('\r\n\r\n', 1)
            if len(parts) < 2:
                return None
            return json.loads(parts[1])
        except:
            return None

def get_network_info():
    """Lấy thông tin mạng"""
    return http_request("GET", "/api/info")

def submit_block(height, nonce, hash_value, prev_hash, reward, wallet):
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
    result = http_request("POST", "/api/blocks/submit", data)
    return result is not None

def sha1_hash(data):
    return hashlib.sha1(data.encode()).hexdigest()

def calculate_block_hash(height, prev_hash, timestamp, tx_string, nonce):
    return sha1_hash(f"{height}{prev_hash}{timestamp}{tx_string}{nonce}")

# ============== QUẢN LÝ CẤU HÌNH ==============
def save_config(wallet, threads, cpu_percent, difficulty):
    config = configparser.ConfigParser()
    config["WebCoin"] = {
        "wallet": wallet,
        "threads": str(threads),
        "cpu_percent": str(cpu_percent),
        "difficulty": str(difficulty) if difficulty else "auto"
    }
    with open(CONFIG_FILE, "w") as f:
        config.write(f)
    print_color(f"✅ Config saved", Fore.GREEN)

def load_config():
    if not os.path.exists(CONFIG_FILE):
        return None
    config = configparser.ConfigParser()
    config.read(CONFIG_FILE)
    if "WebCoin" not in config:
        return None
    data = config["WebCoin"]
    diff = data.get("difficulty")
    if diff == "auto":
        diff = None
    elif diff:
        diff = int(diff)
    return {
        "wallet": data.get("wallet", DEFAULT_WALLET),
        "threads": int(data.get("threads", DEFAULT_THREADS)),
        "cpu_percent": int(data.get("cpu_percent", DEFAULT_CPU_PERCENT)),
        "difficulty": diff
    }

# ============== MINING THREAD ==============
def mining_thread(thread_id, wallet, difficulty_override, target_cpu_percent):
    global total_hashes, blocks_mined, total_reward, running
    
    delay_time = (100 - target_cpu_percent) / 1000
    last_cpu_check = time.time()
    
    print_color(f"🧵 Thread {thread_id} started", Fore.CYAN)
    
    while running:
        try:
            # Điều chỉnh CPU dùng psutil
            if HAS_PSUTIL and thread_id == 0 and time.time() - last_cpu_check > 5:
                try:
                    cpu_percent = psutil.cpu_percent(interval=0.5)
                    if cpu_percent > target_cpu_percent + 10:
                        delay_time = min(delay_time + 0.001, 0.01)
                    elif cpu_percent < target_cpu_percent - 10 and delay_time > 0:
                        delay_time = max(delay_time - 0.001, 0)
                except:
                    pass
                last_cpu_check = time.time()
            
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
                            wallet
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
        
        # Thông tin CPU/RAM từ psutil
        cpu_info = ""
        if HAS_PSUTIL:
            try:
                cpu_percent = psutil.cpu_percent()
                memory = psutil.virtual_memory()
                cpu_info = f" | CPU: {cpu_percent}% | RAM: {memory.percent}%"
            except:
                pass
        
        print_color(f"\n📊 STATS [{int(elapsed_total/60)}m {int(elapsed_total%60)}s]", Fore.MAGENTA, bright=True)
        print_color(f"   Total hashes: {current_hashes:,}", Fore.CYAN)
        print_color(f"   Speed: {speed/1000:.2f} kH/s", Fore.CYAN)
        print_color(f"   Blocks mined: {current_blocks}", Fore.GREEN)
        print_color(f"   Total reward: {current_reward} WBC", Fore.GREEN)
        if cpu_info:
            print_color(f"   {cpu_info}", Fore.YELLOW)
        
        last_hashes = current_hashes
        last_time = now

# ============== MAIN ==============
def main():
    global running, start_time
    
    print_color("\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER PRO", Fore.MAGENTA, bright=True)
    print_color("="*60, Fore.MAGENTA, bright=True)
    
    print_color(f"\n📊 System:", Fore.CYAN)
    print_color(f"   CPU cores: {CPU_CORES}", Fore.CYAN)
    print_color(f"   Requests: {'✅' if HAS_REQUESTS else '❌'} psutil: {'✅' if HAS_PSUTIL else '❌'}", Fore.CYAN)
    
    # Đọc config cũ
    config = load_config()
    
    if config and input(f"{Fore.YELLOW}📁 Dùng config cũ? (y/n): {Fore.RESET}").lower() == 'y':
        wallet = config["wallet"]
        threads = config["threads"]
        cpu_percent = config["cpu_percent"]
        difficulty_override = config["difficulty"]
        print_color(f"✅ Đã dùng config cũ", Fore.GREEN)
    else:
        print_color(f"\n📝 NHẬP THÔNG TIN", Fore.YELLOW)
        
        wallet = input(f"Địa chỉ ví (có hoặc không W_): ").strip()
        if not wallet:
            wallet = DEFAULT_WALLET
            print_color(f"   Dùng ví mặc định", Fore.CYAN)
        elif not wallet.startswith('W_'):
            wallet = 'W_' + wallet
            print_color(f"   Đã thêm W_: {wallet[:20]}...", Fore.CYAN)
        
        thread_input = input(f"Số luồng CPU (1-{CPU_CORES*2}) [mặc định {CPU_CORES}]: ").strip()
        threads = int(thread_input) if thread_input else CPU_CORES
        threads = max(1, min(threads, CPU_CORES * 2))
        
        cpu_input = input(f"% CPU sử dụng (10-100) [mặc định 100]: ").strip()
        cpu_percent = int(cpu_input) if cpu_input else 100
        cpu_percent = max(10, min(cpu_percent, 100))
        
        diff_input = input(f"Độ khó (1-10, Enter để auto): ").strip()
        difficulty_override = int(diff_input) if diff_input else None
        
        save_config(wallet, threads, cpu_percent, difficulty_override)
    
    # Thông tin mạng
    print_color(f"\n📡 Kết nối...", Fore.YELLOW)
    info = get_network_info()
    if info:
        print_color(f"   Độ khó: {info.get('difficulty', '?')}", Fore.CYAN)
        print_color(f"   Block: {info.get('latestBlock', {}).get('height', '?')}", Fore.CYAN)
        print_color(f"   Thưởng: {info.get('reward', '?')} WBC", Fore.CYAN)
    else:
        print_color(f"⚠️ Không lấy được thông tin mạng", Fore.YELLOW)
    
    # Bắt đầu đào
    print_color(f"\n🚀 Bắt đầu đào với {threads} luồng, {cpu_percent}% CPU", Fore.GREEN)
    print_color("="*60, Fore.MAGENTA)
    
    start_time = time.time()
    
    for i in range(threads):
        t = threading.Thread(target=mining_thread, args=(i, wallet, difficulty_override, cpu_percent), daemon=True)
        t.start()
    
    stats = threading.Thread(target=stats_thread, daemon=True)
    stats.start()
    
    try:
        while True:
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
