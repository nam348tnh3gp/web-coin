#!/usr/bin/env python3
"""
WebCoin Official AVR Miner 1.0
Based on Duino-Coin AVR Miner structure
https://webcoin-1n9d.onrender.com
"""

import sys
import os
import json
import time
import socket
import threading
import serial
import serial.tools.list_ports
import requests
from datetime import datetime
from collections import deque
from configparser import ConfigParser
from signal import SIGINT, signal
from colorama import init, Fore, Back, Style

# Khởi tạo colorama
init(autoreset=True)

# ============== CẤU HÌNH ==============
VERSION = "1.0"
SERVER_URL = "https://webcoin-1n9d.onrender.com/api"
WALLET_ADDRESS = "W_0488269778421338855a2442815747a0b151c3389f60f27bf70b868578c5ab481ec5eef7a43773bcdda58e153d27721116885441c4bffa9fe36dffd8dc07722ccb"
WALLET_PASSWORD = "12345678Nn"
DIFFICULTY = 3
BAUDRATE = 115200
TIMEOUT = 10
CONFIG_DIR = "WebCoin-Data"

# ============== BIẾN TOÀN CỤC ==============
shares = [0, 0]  # [accepted, rejected]
hashrate_list = []
ping_list = deque(maxlen=25)
print_queue = []
running = True
config = ConfigParser()
auth_cookie = None

# ============== HÀM TIỆN ÍCH ==============
def now():
    return datetime.now()

def get_prefix(val):
    """Định dạng hashrate (H/s, kH/s, MH/s)"""
    if val >= 1_000_000:
        return f"{val/1_000_000:.2f} MH/s"
    elif val >= 1_000:
        return f"{val/1_000:.2f} kH/s"
    else:
        return f"{val:.2f} H/s"

def debug_print(msg):
    """In debug nếu được bật"""
    if debug_mode:
        print(f"{Fore.WHITE}{Style.DIM}[DEBUG] {msg}{Style.RESET_ALL}")

def save_config():
    """Lưu cấu hình"""
    if not os.path.exists(CONFIG_DIR):
        os.makedirs(CONFIG_DIR)
    
    with open(f"{CONFIG_DIR}/config.cfg", "w") as f:
        config.write(f)
    print(f"{Fore.GREEN}✅ Config saved{Style.RESET_ALL}")

def load_config():
    """Đọc cấu hình"""
    global username, password, difficulty, port, debug_mode, identifier
    
    config.read(f"{CONFIG_DIR}/config.cfg")
    
    try:
        username = config["WebCoin"]["username"]
        password = config["WebCoin"]["password"]
        difficulty = int(config["WebCoin"]["difficulty"])
        port = config["WebCoin"]["port"]
        identifier = config["WebCoin"]["identifier"]
        debug_mode = config["WebCoin"].getboolean("debug")
    except:
        # Tạo config mới nếu chưa có
        username = WALLET_ADDRESS
        password = WALLET_PASSWORD
        difficulty = DIFFICULTY
        identifier = "ArduinoMiner"
        debug_mode = False
        
        config["WebCoin"] = {
            "username": username,
            "password": password,
            "difficulty": difficulty,
            "port": "auto",
            "identifier": identifier,
            "debug": "False"
        }
        save_config()

def get_available_ports():
    """Lấy danh sách cổng COM khả dụng"""
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]

def login():
    """Đăng nhập vào WebCoin server"""
    global auth_cookie
    
    try:
        data = {
            "displayAddress": username,
            "password": password
        }
        
        response = requests.post(
            f"{SERVER_URL}/login",
            json=data,
            timeout=10
        )
        
        if response.status_code == 200:
            result = response.json()
            if "error" not in result:
                # Lưu cookie
                auth_cookie = response.headers.get("set-cookie", "").split(";")[0]
                print(f"{Fore.GREEN}✅ Login successful!{Style.RESET_ALL}")
                return True
            else:
                print(f"{Fore.RED}❌ Login failed: {result['error']}{Style.RESET_ALL}")
        else:
            print(f"{Fore.RED}❌ HTTP {response.status_code}{Style.RESET_ALL}")
    except Exception as e:
        print(f"{Fore.RED}❌ Connection error: {e}{Style.RESET_ALL}")
    
    return False

def get_network_info():
    """Lấy thông tin mạng"""
    try:
        headers = {"Cookie": auth_cookie} if auth_cookie else {}
        response = requests.get(f"{SERVER_URL}/info", headers=headers, timeout=5)
        
        if response.status_code == 200:
            return response.json()
    except:
        pass
    return None

def get_pending():
    """Lấy pending transactions"""
    try:
        headers = {"Cookie": auth_cookie} if auth_cookie else {}
        response = requests.get(f"{SERVER_URL}/pending", headers=headers, timeout=5)
        
        if response.status_code == 200:
            return response.json()
    except:
        pass
    return []

def submit_result(height, nonce, hash_value, prev_hash, reward):
    """Gửi kết quả đào lên server"""
    try:
        # Lấy public key từ username
        public_key = username[2:] if username.startswith("W_") else username
        
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
            "minerAddress": username
        }
        
        headers = {
            "Content-Type": "application/json",
            "Cookie": auth_cookie
        }
        
        response = requests.post(
            f"{SERVER_URL}/blocks/submit",
            json=data,
            headers=headers,
            timeout=10
        )
        
        if response.status_code == 200:
            result = response.json()
            return "error" not in result
    except Exception as e:
        debug_print(f"Submit error: {e}")
    
    return False

# ============== GIAO DIỆN ==============
def print_header():
    """In banner khởi động"""
    print(f"{Fore.MAGENTA}{Style.BRIGHT}")
    print("╔══════════════════════════════════════╗")
    print("║        WEBCCOIN AVR MINER v1.0       ║")
    print("║      Arduino WebCoin Miner           ║")
    print("╚══════════════════════════════════════╝")
    print(f"{Style.RESET_ALL}")

def print_config():
    """In thông tin cấu hình"""
    print(f"{Fore.CYAN}📋 Configuration:{Style.RESET_ALL}")
    print(f"  • Wallet: {username[:20]}...")
    print(f"  • Difficulty: {difficulty}")
    print(f"  • Identifier: {identifier}")
    print(f"  • Debug mode: {'ON' if debug_mode else 'OFF'}")
    if port == "auto":
        print(f"  • Port: Auto-detect")
    else:
        print(f"  • Port: {port}")
    print()

def print_share(thread_id, accepted, rejected, hashrate, total_hashrate, job_time, diff, ping):
    """In kết quả share"""
    percent = int(accepted / (accepted + rejected) * 100) if accepted + rejected > 0 else 0
    
    print_queue.append(
        f"{Fore.RESET}{now().strftime('%H:%M:%S')} "
        f"{Fore.WHITE}{Back.MAGENTA}{Style.BRIGHT} AVR{thread_id} {Style.RESET_ALL}"
        f"{Fore.GREEN if accepted > rejected else Fore.YELLOW} ⛏ "
        f"{accepted}/{accepted + rejected} ({percent}%) "
        f"∙ {job_time:.1f}s ∙ {get_prefix(hashrate)} "
        f"({get_prefix(total_hashrate)} total) "
        f"∙ diff {diff} ∙ ping {ping}ms{Style.RESET_ALL}"
    )

def print_status(message, type="info"):
    """In thông báo hệ thống"""
    colors = {
        "info": Fore.BLUE,
        "success": Fore.GREEN,
        "error": Fore.RED,
        "warning": Fore.YELLOW
    }
    print_queue.append(
        f"{Fore.RESET}{now().strftime('%H:%M:%S')} "
        f"{Fore.WHITE}{Back.GREEN}{Style.BRIGHT} SYS {Style.RESET_ALL}"
        f" {colors.get(type, Fore.WHITE)}{message}{Style.RESET_ALL}"
    )

def print_queue_handler():
    """Xử lý hàng đợi in ấn (tránh xung đột thread)"""
    while running:
        if print_queue:
            print(print_queue.pop(0))
        time.sleep(0.01)

# ============== XỬ LÝ ARDUINO ==============
def test_board(ser, thread_id):
    """Test board để xác định hashrate"""
    try:
        # Gửi lệnh test
        test_cmd = "TEST,10\n"
        ser.write(test_cmd.encode())
        
        # Đọc kết quả
        result = ser.read_until(b'\n').decode().strip().split(',')
        
        if len(result) >= 2:
            num_res = int(result[0])
            exec_time = int(result[1]) / 1_000_000  # microsecond to second
            hashrate = num_res / exec_time
            
            print_status(f"Board {thread_id} hashrate: {get_prefix(hashrate)}", "success")
            return hashrate
    except Exception as e:
        print_status(f"Board test failed: {e}", "error")
    
    return 100  # Giá trị mặc định

def mine_arduino(port_name, thread_id, identifier):
    """Thread đào chính cho mỗi Arduino"""
    global shares, hashrate_list, running
    
    # Kết nối đến Arduino
    try:
        ser = serial.Serial(
            port_name,
            baudrate=BAUDRATE,
            timeout=TIMEOUT
        )
        time.sleep(2)  # Đợi board reset
        
        print_status(f"Connected to Arduino on {port_name}", "success")
    except Exception as e:
        print_status(f"Cannot connect to {port_name}: {e}", "error")
        return
    
    # Test board
    board_hashrate = test_board(ser, thread_id)
    hashrate_list.append(board_hashrate)
    
    # Vòng lặp đào chính
    local_accepted = 0
    local_rejected = 0
    
    while running:
        try:
            # Lấy thông tin mạng
            info = get_network_info()
            if not info:
                time.sleep(5)
                continue
            
            latest = info["latestBlock"]
            height = latest["height"] + 1
            reward = info["reward"]
            prev_hash = latest["hash"]
            network_diff = info["difficulty"]
            
            # Lấy pending transactions
            pending = get_pending()
            
            # Tạo job cho Arduino
            job_data = f"JOB,{height},{prev_hash},{int(time.time()*1000)},{network_diff},{reward}\n"
            
            # Gửi job
            ser.write(job_data.encode())
            
            # Đọc kết quả
            result = ser.read_until(b'\n').decode().strip().split(',')
            
            if len(result) >= 3:
                nonce = int(result[0])
                job_time = int(result[1]) / 1_000_000
                hash_value = result[2]
                
                # Tính hashrate
                hashrate = 1000000 / job_time  # Ước lượng
                
                # Gửi kết quả lên server
                success = submit_result(height, nonce, hash_value, prev_hash, reward)
                
                # Cập nhật thống kê
                if success:
                    shares[0] += 1
                    local_accepted += 1
                else:
                    shares[1] += 1
                    local_rejected += 1
                
                # Cập nhật hashrate
                hashrate_list[thread_id] = hashrate
                total_hashrate = sum(hashrate_list)
                
                # Tính ping (giả lập)
                ping = 50 + (thread_id * 10)
                
                # In kết quả
                print_share(
                    thread_id,
                    shares[0],
                    shares[1],
                    hashrate,
                    total_hashrate,
                    job_time,
                    network_diff,
                    ping
                )
            
        except serial.SerialException as e:
            print_status(f"Serial error on {port_name}: {e}", "error")
            break
        except Exception as e:
            debug_print(f"Mining error: {e}")
            time.sleep(1)
    
    ser.close()
    print_status(f"Disconnected from {port_name}", "warning")

# ============== MAIN ==============
def signal_handler(sig, frame):
    """Xử lý Ctrl+C"""
    global running
    print_status("Shutting down...", "warning")
    running = False
    sys.exit(0)

def main():
    global running
    
    # Bắt tín hiệu Ctrl+C
    signal(SIGINT, signal_handler)
    
    # In banner
    print_header()
    
    # Tải cấu hình
    load_config()
    print_config()
    
    # Đăng nhập
    if not login():
        return
    
    # Tìm cổng Arduino
    ports = get_available_ports()
    if not ports:
        print_status("No serial ports found!", "error")
        return
    
    print_status(f"Found ports: {', '.join(ports)}", "info")
    
    # Chọn cổng
    selected_port = None
    if port == "auto":
        # Tự động chọn cổng có Arduino
        for p in ports:
            try:
                ser = serial.Serial(p, baudrate=BAUDRATE, timeout=2)
                ser.write(b"PING\n")
                response = ser.read_until(b'\n')
                if b"PONG" in response:
                    selected_port = p
                    ser.close()
                    break
                ser.close()
            except:
                pass
        
        if not selected_port:
            print_status("No Arduino found, using first port", "warning")
            selected_port = ports[0]
    else:
        if port in ports:
            selected_port = port
        else:
            print_status(f"Port {port} not found", "error")
            return
    
    # Khởi tạo thread in ấn
    threading.Thread(target=print_queue_handler, daemon=True).start()
    
    # Bắt đầu đào
    print_status(f"Starting miner on {selected_port}", "success")
    
    mining_thread = threading.Thread(
        target=mine_arduino,
        args=(selected_port, 0, identifier)
    )
    mining_thread.start()
    
    # Vòng lặp chính
    while running:
        time.sleep(1)

if __name__ == "__main__":
    main()
