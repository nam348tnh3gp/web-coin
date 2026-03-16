#!/usr/bin/env python3
"""
WebCoin Miner - Simple Version
- Nhập địa chỉ ví (W_...)
- Nhập mật khẩu thường (tự động tạo private key)
- Chọn số luồng CPU
- Chọn % CPU sử dụng
- Lưu cấu hình tự động
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

# ============== THƯ VIỆN KHÔNG BẮT BUỘC ==============
HAS_REQUESTS = False
HAS_PSUTIL = False
HAS_COLORAMA = False
HAS_SERIAL = False
HAS_PRESENCE = False

try:
    import requests
    HAS_REQUESTS = True
except:
    pass

try:
    import psutil
    HAS_PSUTIL = True
except:
    pass

try:
    from colorama import init, Fore, Back, Style
    init(autoreset=True)
    HAS_COLORAMA = True
except:
    # Tự tạo màu nếu không có colorama
    class Fore:
        RED = '\033[91m'
        GREEN = '\033[92m'
        YELLOW = '\033[93m'
        BLUE = '\033[94m'
        MAGENTA = '\033[95m'
        CYAN = '\033[96m'
        RESET = '\033[0m'
    class Style:
        BRIGHT = '\033[1m'
        RESET_ALL = '\033[0m'
    Back = None

# ============== CẤU HÌNH ==============
DEFAULT_WALLET = "W_042389ad1649d3e9566c95761a64e48322b5c85b589398557bb6ee6af006f6c1a9c3657648bfefc42b0c2c945f4f05a3d306ee6473e8e8b02e12b86be41dc45437"
DEFAULT_PASSWORD = "12345678Nn"
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
thread_stats = {}
stats_lock = threading.Lock()

# Thông tin CPU
CPU_CORES = multiprocessing.cpu_count()
CPU_NAME = f"CPU ({CPU_CORES} cores)"

# Discord RPC
RPC = None
if HAS_PRESENCE:
    try:
        RPC = Presence(123456789012345678)
        RPC.connect()
    except:
        pass

# ============== HÀM MÀU SẮC ==============
def print_color(text, color=Fore.RESET, bright=False):
    prefix = Style.BRIGHT if bright and HAS_COLORAMA else ""
    print(f"{color}{prefix}{text}{Fore.RESET}")

# ============== HTTP CLIENT ==============
def http_request(method, path, data=None):
    """HTTP request dùng socket (không cần requests)"""
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
    except Exception as e:
        return None

# ============== HÀM TẠO PRIVATE KEY ==============
def generate_private_key(password):
    """Tạo private key 64 hex từ mật khẩu"""
    return hashlib.sha256(password.encode()).hexdigest()

# ============== API FUNCTIONS ==============
def login(wallet, password):
    """Đăng nhập vào WebCoin"""
    data = {"displayAddress": wallet, "password": password}
    
    if HAS_REQUESTS:
        try:
            response = requests.post(f"{BASE_URL}/login", json=data, timeout=10)
            if response.status_code == 200:
                return response.json()
            return {"error": f"HTTP {response.status_code}"}
        except Exception as e:
            return {"error": str(e)}
    else:
        return http_request("POST", "/api/login", data)

def get_network_info():
    """Lấy thông tin mạng"""
    if HAS_REQUESTS:
        try:
            response = requests.get(f"{BASE_URL}/info", timeout=10)
            if response.status_code == 200:
                return response.json()
            return None
        except:
            return None
    else:
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
    
    if HAS_REQUESTS:
        try:
            response = requests.post(f"{BASE_URL}/blocks/submit", json=data, timeout=10)
            return response.status_code == 200
        except:
            return False
    else:
        result = http_request("POST", "/api/blocks/submit", data)
        return result is not None

# ============== MINING FUNCTIONS ==============
def sha1_hash(data):
    return hashlib.sha1(data.encode()).hexdigest()

def calculate_block_hash(height, prev_hash, timestamp, tx_string, nonce):
    data = f"{height}{prev_hash}{timestamp}{tx_string}{nonce}"
    return sha1_hash(data)

def get_hashrate():
    elapsed = time.time() - start_time
    return (total_hashes / elapsed) / 1000 if elapsed > 0 else 0

# ============== QUẢN LÝ CẤU HÌNH ==============
def save_config(wallet, password, threads, cpu_percent, difficulty):
    config = configparser.ConfigParser()
    config["WebCoin"] = {
        "wallet": wallet,
        "password": password,
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
        "password": data.get("password", DEFAULT_PASSWORD),
        "threads": int(data.get("threads", DEFAULT_THREADS)),
        "cpu_percent": int(data.get("cpu_percent", DEFAULT_CPU_PERCENT)),
        "difficulty": diff
    }

def validate_wallet(address):
    """Kiểm tra địa chỉ ví hợp lệ"""
    if not address:
        return False, "Địa chỉ trống"
    if not address.startswith('W_'):
        return False, "Địa chỉ phải bắt đầu bằng W_"
    
    hex_part = address[2:]
    if len(hex_part) != 64:
        return False, f"Phần hex phải 64 ký tự (bạn có {len(hex_part)})"
    
    try:
        int(hex_part, 16)
        return True, "OK"
    except:
        return False, "Địa chỉ chứa ký tự không hợp lệ"

# ============== MINING THREAD ==============
def mining_thread(thread_id, wallet, password, difficulty_override, target_cpu_percent):
    global total_hashes, blocks_mined, total_reward, running
    
    # Tạo private key từ password
    private_key = generate_private_key(password)
    delay_time = (100 - target_cpu_percent) / 1000
    last_cpu_check = time.time()
    
    print_color(f"🧵 Thread {thread_id} started", Fore.CYAN)
    
    while running:
        try:
            # Điều chỉnh CPU nếu có psutil
            if thread_id == 0 and HAS_PSUTIL and time.time() - last_cpu_check > 5:
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
                    hashrate = attempts / elapsed if elapsed > 0 else 0
                    
                    print_color(f"\n🎯 Thread {thread_id}: Found nonce {nonce} in {elapsed:.2f}s ({hashrate:.0f} H/s)", Fore.GREEN)
                    
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
        
        print_color(f"\n📊 STATS [{int(elapsed_total/60)}m {int(elapsed_total%60)}s]", Fore.MAGENTA)
        print_color(f"   Total hashes: {current_hashes:,}", Fore.CYAN)
        print_color(f"   Speed: {speed/1000:.2f} kH/s", Fore.CYAN)
        print_color(f"   Blocks mined: {current_blocks}", Fore.GREEN)
        print_color(f"   Total reward: {current_reward} WBC", Fore.GREEN)
        
        if HAS_PSUTIL:
            try:
                cpu_percent = psutil.cpu_percent()
                memory = psutil.virtual_memory()
                print_color(f"   CPU: {cpu_percent}% | RAM: {memory.percent}%", Fore.YELLOW)
            except:
                pass
        
        last_hashes = current_hashes
        last_time = now

# ============== ARDUINO SUPPORT ==============
def list_arduino_ports():
    if not HAS_SERIAL:
        return []
    ports = []
    for port in serial.tools.list_ports.comports():
        if 'arduino' in port.description.lower() or 'usb' in port.description.lower():
            ports.append(port.device)
    return ports

def arduino_thread(port, wallet, password):
    if not HAS_SERIAL:
        return
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        print_color(f"✅ Arduino connected on {port}", Fore.GREEN)
        
        while running:
            line = ser.readline().decode().strip()
            if line:
                print_color(f"[Arduino] {line}", Fore.MAGENTA)
            time.sleep(0.1)
    except Exception as e:
        print_color(f"Arduino error: {e}", Fore.RED)

# ============== MAIN ==============
def main():
    global running, start_time
    
    print_color("\n" + "="*60, Fore.MAGENTA, bright=True)
    print_color(" WEBCCOIN MINER - SIMPLE EDITION", Fore.MAGENTA, bright=True)
    print_color("="*60, Fore.MAGENTA, bright=True)
    
    print_color(f"📊 System Info:", Fore.CYAN)
    print_color(f"   🖥️  CPU: {CPU_NAME}", Fore.CYAN)
    
    # Đọc config cũ
    config = load_config()
    
    if config and input(f"{Fore.YELLOW}📁 Load previous config? (y/n): {Fore.RESET}").lower() == 'y':
        wallet = config["wallet"]
        password = config["password"]
        threads = config["threads"]
        cpu_percent = config["cpu_percent"]
        difficulty_override = config["difficulty"]
        print_color(f"✅ Loaded config", Fore.GREEN)
    else:
        print_color(f"\n📝 CONFIGURATION", Fore.YELLOW)
        
        # Nhập địa chỉ ví
        while True:
            wallet = input(f"Wallet address (W_...) [Enter for default]: ").strip()
            if not wallet:
                wallet = DEFAULT_WALLET
                print_color(f"   Using default wallet", Fore.CYAN)
                break
            valid, msg = validate_wallet(wallet)
            if valid:
                break
            print_color(f"❌ {msg}", Fore.RED)
        
        # Nhập mật khẩu (không cần hex)
        password = input(f"Wallet password [Enter for default]: ").strip()
        if not password:
            password = DEFAULT_PASSWORD
            print_color(f"   Using default password", Fore.CYAN)
        
        # Số threads
        thread_input = input(f"Threads (1-{CPU_CORES*2}) [default: {CPU_CORES}]: ").strip()
        threads = int(thread_input) if thread_input else CPU_CORES
        threads = max(1, min(threads, CPU_CORES * 2))
        
        # % CPU
        cpu_input = input(f"CPU % (10-100) [default: 100]: ").strip()
        cpu_percent = int(cpu_input) if cpu_input else 100
        cpu_percent = max(10, min(cpu_percent, 100))
        
        # Độ khó
        diff_input = input(f"Difficulty override (1-10, Enter for auto): ").strip()
        difficulty_override = int(diff_input) if diff_input else None
        
        # Lưu config
        save_config(wallet, password, threads, cpu_percent, difficulty_override)
    
    # Đăng nhập
    print_color(f"\n🔐 Logging in...", Fore.YELLOW)
    login_result = login(wallet, password)
    
    if not login_result or "error" in login_result:
        print_color(f"❌ Login failed! Check wallet and password", Fore.RED)
        return
    
    print_color(f"✅ Login successful!", Fore.GREEN)
    
    # Thông tin mạng
    info = get_network_info()
    if info:
        print_color(f"\n📡 Network info:", Fore.CYAN)
        print_color(f"   Difficulty: {info.get('difficulty', '?')}", Fore.CYAN)
        print_color(f"   Block height: {info.get('latestBlock', {}).get('height', '?')}", Fore.CYAN)
        print_color(f"   Reward: {info.get('reward', '?')} WBC", Fore.CYAN)
    
    # Discord
    if HAS_PRESENCE and RPC:
        threading.Thread(target=update_discord, daemon=True).start()
        print_color(f"✅ Discord Rich Presence enabled", Fore.GREEN)
    
    # Arduino
    if HAS_SERIAL:
        arduino_ports = list_arduino_ports()
        if arduino_ports:
            print_color(f"✅ Found Arduino on {arduino_ports[0]}", Fore.GREEN)
            threading.Thread(target=arduino_thread, args=(arduino_ports[0], wallet, password), daemon=True).start()
    
    # Bắt đầu đào
    print_color(f"\n🚀 Starting {threads} threads at {cpu_percent}% CPU", Fore.GREEN)
    print_color("="*60, Fore.MAGENTA)
    
    start_time = time.time()
    
    for i in range(threads):
        t = threading.Thread(target=mining_thread, args=(i, wallet, password, difficulty_override, cpu_percent), daemon=True)
        t.start()
    
    stats = threading.Thread(target=stats_thread, daemon=True)
    stats.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print_color(f"\n\n🛑 Stopping miner...", Fore.YELLOW)
        running = False
        
        elapsed = time.time() - start_time
        print_color(f"\n" + "="*60, Fore.MAGENTA, bright=True)
        print_color(" FINAL STATISTICS", Fore.MAGENTA, bright=True)
        print_color("="*60, Fore.MAGENTA, bright=True)
        print_color(f"⏱️  Runtime: {int(elapsed/60)}m {int(elapsed%60)}s", Fore.CYAN)
        print_color(f"🔢 Total hashes: {total_hashes:,}", Fore.CYAN)
        print_color(f"⚡ Avg speed: {(total_hashes/elapsed)/1000:.2f} kH/s", Fore.CYAN)
        print_color(f"⛏️  Blocks mined: {blocks_mined}", Fore.GREEN)
        print_color(f"💰 Total reward: {total_reward} WBC", Fore.GREEN)
        print_color("="*60, Fore.MAGENTA)

if __name__ == "__main__":
    main()
