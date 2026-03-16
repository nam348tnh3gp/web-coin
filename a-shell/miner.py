#!/usr/bin/env python3
"""
WebCoin Pro Miner - Full features with all libraries
- py-cpuinfo: Lấy thông tin CPU chi tiết
- colorama: Màu sắc terminal đẹp
- requests: HTTP requests đơn giản
- pyserial: Giao tiếp Arduino (tùy chọn)
- pypresence: Discord Rich Presence
- psutil: Giám sát CPU/RAM realtime
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

# ============== THƯ VIỆN BẮT BUỘC ==============
try:
    import requests
    import psutil
    import cpuinfo
    from colorama import init, Fore, Back, Style
    init(autoreset=True)
except ImportError as e:
    print(f"❌ Missing library: {e}")
    print("Please install: pip install py-cpuinfo colorama requests psutil")
    sys.exit(1)

# ============== THƯ VIỆN TÙY CHỌN ==============
try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except:
    HAS_SERIAL = False
    print("⚠️ pyserial not installed - Arduino support disabled")

try:
    from pypresence import Presence
    HAS_PRESENCE = True
except:
    HAS_PRESENCE = False
    print("⚠️ pypresence not installed - Discord Rich Presence disabled")

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

# Discord RPC
RPC = None
if HAS_PRESENCE:
    try:
        RPC = Presence(123456789012345678)  # Replace with your Discord App ID
        RPC.connect()
    except:
        pass

# ============== LẤY THÔNG TIN CPU ==============
try:
    cpu_info = cpuinfo.get_cpu_info()
    CPU_NAME = cpu_info.get('brand_raw', 'Unknown CPU')
    CPU_CORES = cpu_info.get('count', multiprocessing.cpu_count())
except:
    CPU_NAME = "Unknown CPU"
    CPU_CORES = multiprocessing.cpu_count()

# ============== QUẢN LÝ CẤU HÌNH ==============
def save_config(wallet, password, threads, cpu_percent, difficulty):
    """Lưu cấu hình vào file"""
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
    
    print(f"{Fore.GREEN}✅ Config saved to {CONFIG_FILE}{Style.RESET_ALL}")

def load_config():
    """Đọc cấu hình từ file"""
    if not os.path.exists(CONFIG_FILE):
        return None
    
    config = configparser.ConfigParser()
    config.read(CONFIG_FILE)
    
    if "WebCoin" not in config:
        return None
    
    data = config["WebCoin"]
    difficulty = data.get("difficulty")
    if difficulty == "auto":
        difficulty = None
    elif difficulty:
        difficulty = int(difficulty)
    
    return {
        "wallet": data.get("wallet", DEFAULT_WALLET),
        "password": data.get("password", DEFAULT_PASSWORD),
        "threads": int(data.get("threads", DEFAULT_THREADS)),
        "cpu_percent": int(data.get("cpu_percent", DEFAULT_CPU_PERCENT)),
        "difficulty": difficulty
    }

# ============== VALIDATION ==============
def validate_wallet(address):
    """Kiểm tra địa chỉ ví hợp lệ"""
    if not address or not address.startswith('W_'):
        return False
    hex_part = address[2:]
    if len(hex_part) != 64:
        return False
    try:
        int(hex_part, 16)
        return True
    except:
        return False

def generate_private_key(password):
    """Tạo private key từ mật khẩu"""
    return hashlib.sha256(password.encode()).hexdigest()

# ============== API FUNCTIONS ==============
def login(wallet, password):
    """Đăng nhập vào WebCoin"""
    try:
        data = {
            "displayAddress": wallet,
            "password": password
        }
        response = requests.post(f"{BASE_URL}/login", json=data, timeout=10)
        if response.status_code == 200:
            return response.json()
        return {"error": f"HTTP {response.status_code}"}
    except Exception as e:
        return {"error": str(e)}

def get_network_info():
    """Lấy thông tin mạng"""
    try:
        response = requests.get(f"{BASE_URL}/info", timeout=10)
        if response.status_code == 200:
            return response.json()
        return None
    except:
        return None

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
    try:
        response = requests.post(f"{BASE_URL}/blocks/submit", json=data, timeout=10)
        return response.status_code == 200
    except:
        return False

# ============== DISCORD RICH PRESENCE ==============
def update_discord():
    """Cập nhật Discord Rich Presence"""
    global RPC, running
    while running and RPC:
        try:
            elapsed = time.time() - start_time
            hours = int(elapsed // 3600)
            minutes = int((elapsed % 3600) // 60)
            
            with stats_lock:
                hashes = total_hashes
                blocks = blocks_mined
                reward = total_reward
            
            RPC.update(
                details=f"Mining WebCoin | {blocks} blocks",
                state=f"{hashes:,} hashes | {reward} WBC",
                large_image="webcoin",
                large_text=f"Hashrate: {get_hashrate():.1f} kH/s",
                start=start_time,
                buttons=[
                    {"label": "WebCoin", "url": "https://webcoin-1n9d.onrender.com"},
                    {"label": "Join Discord", "url": "https://discord.gg/webcoin"}
                ]
            )
        except:
            pass
        time.sleep(15)

# ============== MINING FUNCTIONS ==============
def sha1_hash(data):
    """Tính SHA1 nhanh"""
    return hashlib.sha1(data.encode()).hexdigest()

def calculate_block_hash(height, prev_hash, timestamp, tx_string, nonce):
    """Tính hash của block"""
    data = f"{height}{prev_hash}{timestamp}{tx_string}{nonce}"
    return sha1_hash(data)

def get_hashrate():
    """Tính hashrate hiện tại"""
    elapsed = time.time() - start_time
    if elapsed > 0:
        return (total_hashes / elapsed) / 1000
    return 0

# ============== MINING THREAD ==============
def mining_thread(thread_id, wallet, password, difficulty_override, target_cpu_percent):
    global total_hashes, blocks_mined, total_reward, running
    
    private_key = generate_private_key(password)
    local_hashes = 0
    delay_time = (100 - target_cpu_percent) / 1000
    last_cpu_check = time.time()
    
    print(f"{Fore.CYAN}🧵 Thread {thread_id} started{Style.RESET_ALL}")
    
    while running:
        try:
            # Kiểm tra CPU usage mỗi 5 giây (nếu có psutil)
            if thread_id == 0 and time.time() - last_cpu_check > 5:
                try:
                    cpu_percent = psutil.cpu_percent(interval=0.5)
                    if cpu_percent > target_cpu_percent + 10:
                        delay_time = min(delay_time + 0.001, 0.01)
                    elif cpu_percent < target_cpu_percent - 10 and delay_time > 0:
                        delay_time = max(delay_time - 0.001, 0)
                except:
                    pass
                last_cpu_check = time.time()
            
            # Lấy thông tin mạng
            info = get_network_info()
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
                
                local_hashes += 1
                with stats_lock:
                    total_hashes += 1
                
                attempts += 1
                
                if delay_time > 0 and attempts % 100 == 0:
                    time.sleep(delay_time * 100)
                
                if hash_value.startswith(target):
                    elapsed = time.time() - start_local
                    hashrate = attempts / elapsed if elapsed > 0 else 0
                    
                    print(f"\n{Fore.GREEN}🎯 Thread {thread_id}: Found nonce {nonce} in {elapsed:.2f}s ({hashrate:.0f} H/s){Style.RESET_ALL}")
                    print(f"{Fore.CYAN}   Hash: {hash_value[:20]}...{Style.RESET_ALL}")
                    
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
                            print(f"{Fore.GREEN}✅ Block accepted! +{reward} WBC{Style.RESET_ALL}")
                        else:
                            print(f"{Fore.RED}❌ Block rejected!{Style.RESET_ALL}")
                    
                    break
                
                nonce += 1
                
                if attempts % 10000 == 0 and thread_id == 0:
                    elapsed = time.time() - start_local
                    speed = attempts / elapsed if elapsed > 0 else 0
                    print(f"{Fore.YELLOW}   Thread 0: {attempts} hashes, {speed:.0f} H/s{Style.RESET_ALL}")
            
        except Exception as e:
            print(f"{Fore.RED}Thread {thread_id} error: {e}{Style.RESET_ALL}")
            time.sleep(1)

# ============== STATS THREAD ==============
def stats_thread():
    global running
    last_hashes = 0
    last_time = time.time()
    
    while running:
        time.sleep(2)
        
        with stats_lock:
            current_hashes = total_hashes
            current_blocks = blocks_mined
            current_reward = total_reward
        
        now = time.time()
        elapsed_total = now - start_time
        elapsed = now - last_time
        
        if elapsed > 0:
            speed = (current_hashes - last_hashes) / elapsed
            
            print(f"\n{Fore.MAGENTA}{Style.BRIGHT}📊 STATISTICS{Style.RESET_ALL}")
            print(f"{Fore.CYAN}   ⏱️  Runtime: {int(elapsed_total/60)}m {int(elapsed_total%60)}s{Style.RESET_ALL}")
            print(f"{Fore.CYAN}   🔢 Total hashes: {current_hashes:,}{Style.RESET_ALL}")
            print(f"{Fore.CYAN}   ⚡ Current speed: {speed/1000:.2f} kH/s{Style.RESET_ALL}")
            print(f"{Fore.CYAN}   ⚡ Avg speed: {(current_hashes/elapsed_total)/1000:.2f} kH/s{Style.RESET_ALL}")
            print(f"{Fore.GREEN}   ⛏️  Blocks mined: {current_blocks}{Style.RESET_ALL}")
            print(f"{Fore.GREEN}   💰 Total reward: {current_reward} WBC{Style.RESET_ALL}")
            
            # Thông tin hệ thống
            try:
                cpu_percent = psutil.cpu_percent()
                memory = psutil.virtual_memory()
                print(f"{Fore.YELLOW}   🖥️  CPU: {cpu_percent}% | RAM: {memory.percent}%{Style.RESET_ALL}")
            except:
                pass
        
        last_hashes = current_hashes
        last_time = now

# ============== ARDUINO SUPPORT ==============
def list_arduino_ports():
    """Liệt kê các cổng Arduino"""
    if not HAS_SERIAL:
        return []
    ports = []
    for port in serial.tools.list_ports.comports():
        if 'arduino' in port.description.lower() or 'usb' in port.description.lower():
            ports.append(port.device)
    return ports

def arduino_thread(port, wallet, password):
    """Thread giao tiếp với Arduino"""
    if not HAS_SERIAL:
        return
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        print(f"{Fore.GREEN}✅ Arduino connected on {port}{Style.RESET_ALL}")
        
        while running:
            # Đọc dữ liệu từ Arduino
            line = ser.readline().decode().strip()
            if line:
                print(f"{Fore.MAGENTA}[Arduino] {line}{Style.RESET_ALL}")
            time.sleep(0.1)
    except Exception as e:
        print(f"{Fore.RED}Arduino error: {e}{Style.RESET_ALL}")

# ============== MAIN ==============
def main():
    global running, start_time
    
    print(f"\n{Fore.MAGENTA}{Style.BRIGHT}" + "="*60)
    print(" WEBCCOIN PRO MINER v2.0")
    print("="*60 + f"{Style.RESET_ALL}")
    
    print(f"{Fore.CYAN}📊 System Info:{Style.RESET_ALL}")
    print(f"   🖥️  CPU: {CPU_NAME}")
    print(f"   🧠 Cores: {CPU_CORES}")
    print(f"   📦 RAM: {psutil.virtual_memory().total / (1024**3):.1f} GB")
    
    # Đọc config cũ
    config = load_config()
    
    if config and input(f"{Fore.YELLOW}📁 Load previous config? (y/n): {Style.RESET_ALL}").lower() == 'y':
        wallet = config["wallet"]
        password = config["password"]
        threads = config["threads"]
        cpu_percent = config["cpu_percent"]
        difficulty_override = config["difficulty"]
        print(f"{Fore.GREEN}✅ Loaded config: {wallet[:30]}...{Style.RESET_ALL}")
    else:
        print(f"\n{Fore.YELLOW}📝 CONFIGURATION{Style.RESET_ALL}")
        
        wallet = input(f"Wallet address (W_...) [default]: ").strip()
        if not wallet:
            wallet = DEFAULT_WALLET
            print(f"{Fore.CYAN}   Using default{Style.RESET_ALL}")
        
        if not validate_wallet(wallet):
            print(f"{Fore.RED}❌ Invalid wallet address!{Style.RESET_ALL}")
            return
        
        password = input(f"Wallet password [default]: ").strip()
        if not password:
            password = DEFAULT_PASSWORD
            print(f"{Fore.CYAN}   Using default password{Style.RESET_ALL}")
        
        thread_input = input(f"Threads (1-{CPU_CORES*2}) [default: {CPU_CORES}]: ").strip()
        threads = int(thread_input) if thread_input else CPU_CORES
        threads = max(1, min(threads, CPU_CORES * 2))
        
        cpu_input = input(f"CPU % (10-100) [default: 100]: ").strip()
        cpu_percent = int(cpu_input) if cpu_input else 100
        cpu_percent = max(10, min(cpu_percent, 100))
        
        diff_input = input(f"Difficulty override (1-10, enter for auto): ").strip()
        difficulty_override = int(diff_input) if diff_input else None
        
        save_config(wallet, password, threads, cpu_percent, difficulty_override)
    
    # Kiểm tra kết nối
    print(f"\n{Fore.YELLOW}🔐 Logging in...{Style.RESET_ALL}")
    login_result = login(wallet, password)
    
    if not login_result or "error" in login_result:
        print(f"{Fore.RED}❌ Login failed: {login_result.get('error', 'Connection error') if login_result else 'Unknown'}{Style.RESET_ALL}")
        return
    
    print(f"{Fore.GREEN}✅ Login successful!{Style.RESET_ALL}")
    if "publicKey" in login_result:
        print(f"{Fore.CYAN}   Public key: {login_result['publicKey'][:30]}...{Style.RESET_ALL}")
    
    info = get_network_info()
    if info:
        print(f"\n{Fore.CYAN}📡 Network info:{Style.RESET_ALL}")
        print(f"   Difficulty: {info.get('difficulty', '?')}")
        print(f"   Block height: {info.get('latestBlock', {}).get('height', '?')}")
        print(f"   Reward: {info.get('reward', '?')} WBC")
    
    # Khởi tạo Discord RPC
    if HAS_PRESENCE and RPC:
        threading.Thread(target=update_discord, daemon=True).start()
        print(f"{Fore.GREEN}✅ Discord Rich Presence enabled{Style.RESET_ALL}")
    
    # Tìm Arduino nếu có
    if HAS_SERIAL:
        arduino_ports = list_arduino_ports()
        if arduino_ports:
            print(f"{Fore.GREEN}✅ Found Arduino on {arduino_ports[0]}{Style.RESET_ALL}")
            threading.Thread(target=arduino_thread, args=(arduino_ports[0], wallet, password), daemon=True).start()
    
    # Bắt đầu đào
    print(f"\n{Fore.GREEN}🚀 Starting {threads} threads at {cpu_percent}% CPU{Style.RESET_ALL}")
    print(f"{Fore.MAGENTA}{Style.BRIGHT}" + "="*60 + f"{Style.RESET_ALL}")
    
    start_time = time.time()
    mining_threads = []
    for i in range(threads):
        t = threading.Thread(target=mining_thread, args=(i, wallet, password, difficulty_override, cpu_percent))
        t.daemon = True
        t.start()
        mining_threads.append(t)
    
    stats = threading.Thread(target=stats_thread)
    stats.daemon = True
    stats.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print(f"\n\n{Fore.YELLOW}🛑 Stopping miner...{Style.RESET_ALL}")
        running = False
        
        elapsed = time.time() - start_time
        print(f"\n{Fore.MAGENTA}{Style.BRIGHT}" + "="*60)
        print(" FINAL STATISTICS")
        print("="*60 + f"{Style.RESET_ALL}")
        print(f"{Fore.CYAN}⏱️  Runtime: {int(elapsed/60)}m {int(elapsed%60)}s{Style.RESET_ALL}")
        print(f"{Fore.CYAN}🔢 Total hashes: {total_hashes:,}{Style.RESET_ALL}")
        print(f"{Fore.CYAN}⚡ Avg speed: {(total_hashes/elapsed)/1000:.2f} kH/s{Style.RESET_ALL}")
        print(f"{Fore.GREEN}⛏️  Blocks mined: {blocks_mined}{Style.RESET_ALL}")
        print(f"{Fore.GREEN}💰 Total reward: {total_reward} WBC{Style.RESET_ALL}")
        print(f"{Fore.MAGENTA}{Style.BRIGHT}" + "="*60 + f"{Style.RESET_ALL}")

if __name__ == "__main__":
    main()
