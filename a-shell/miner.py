#!/usr/bin/env python3
"""
WebCoin Official PC Miner 1.0
CPU Mining Edition - KHÔNG CẦN MẬT KHẨU
- Chỉ cần địa chỉ ví
- Chọn số threads
- Chọn độ khó
- Lưu cấu hình
"""

import sys
import os
import json
import time
import socket
import hashlib
import threading
import requests
from datetime import datetime
from multiprocessing import Process, Manager, cpu_count
from configparser import ConfigParser
from signal import SIGINT, signal
from colorama import init, Fore, Back, Style

# Khởi tạo colorama
init(autoreset=True)

# ============== CẤU HÌNH ==============
VERSION = "1.0"
SERVER_URL = "https://webcoin-1n9d.onrender.com/api"
DATA_DIR = f"WebCoin-PC-Miner-{VERSION}"
SETTINGS_FILE = "/config.cfg"
SOC_TIMEOUT = 10
REPORT_TIME = 300

debug_mode = False
running = True

# ============== TIỆN ÍCH ==============
def now():
    return datetime.now()

def handler(signal_received, frame):
    """Xử lý Ctrl+C"""
    global running
    print(f"\n{Fore.YELLOW}⛔ Shutting down...{Style.RESET_ALL}")
    running = False
    sys.exit(0)

signal(SIGINT, handler)

def debug_print(msg):
    """In debug nếu được bật"""
    if debug_mode:
        print(f"{Fore.WHITE}{Style.DIM}[DEBUG] {msg}{Style.RESET_ALL}")

def get_prefix(val):
    """Định dạng hashrate (H/s, kH/s, MH/s)"""
    if val >= 1_000_000:
        return f"{val/1_000_000:.2f} MH/s"
    elif val >= 1_000:
        return f"{val/1_000:.2f} kH/s"
    else:
        return f"{val:.2f} H/s"

def title(title_str):
    """Đổi tiêu đề console"""
    if os.name == 'nt':
        os.system(f'title {title_str}')
    else:
        print(f'\33]0;{title_str}\a', end='')
        sys.stdout.flush()

def clear_screen():
    """Xóa màn hình console"""
    os.system('cls' if os.name == 'nt' else 'clear')

# ============== GIAO DIỆN NHẬP LIỆU ==============
def print_banner():
    """In banner đẹp"""
    clear_screen()
    print(f"{Fore.CYAN}{Style.BRIGHT}")
    print("╔══════════════════════════════════════════════════════════╗")
    print("║                    WEBCCOIN PC MINER                    ║")
    print("║                      Version 1.0                        ║")
    print("║                  CPU Mining Edition                     ║")
    print("║              KHÔNG CẦN MẬT KHẨU - CHỈ CẦN VÍ            ║")
    print("╚══════════════════════════════════════════════════════════╝")
    print(f"{Style.RESET_ALL}\n")

def input_wallet_address():
    """Nhập địa chỉ ví"""
    while True:
        print(f"\n{Fore.CYAN}📝 WALLET ADDRESS{Style.RESET_ALL}")
        print("Your wallet address starts with 'W_' followed by your public key")
        
        addr = input(f"\n{Fore.YELLOW}Enter wallet address: {Style.BRIGHT}").strip()
        
        if not addr:
            print(f"{Fore.RED}❌ Wallet address cannot be empty!{Style.RESET_ALL}")
            continue
            
        if not addr.startswith('W_'):
            print(f"{Fore.YELLOW}⚠️ Address doesn't start with 'W_'. Adding it automatically...{Style.RESET_ALL}")
            addr = 'W_' + addr
        
        # Kiểm tra sơ bộ
        if len(addr) < 10:
            print(f"{Fore.RED}❌ Address seems too short. Please check!{Style.RESET_ALL}")
            continue
        
        return addr

def input_difficulty():
    """Nhập độ khó"""
    print(f"\n{Fore.CYAN}⚙️ DIFFICULTY LEVEL{Style.RESET_ALL}")
    print(" 1 - Low    (Faster mining, lower reward - 2 zeros)")
    print(" 2 - Medium (Balanced - 3 zeros)")
    print(" 3 - High   (Slower, higher reward - 4 zeros)")
    print(" 4 - Auto   (Follow network difficulty)")
    
    while True:
        choice = input(f"{Fore.YELLOW}Choose (1-4) [default: 4]: {Style.BRIGHT}").strip()
        
        if not choice:
            return "NET"
        
        if choice == "1":
            return "LOW"
        elif choice == "2":
            return "MEDIUM"
        elif choice == "3":
            return "HIGH"
        elif choice == "4":
            return "NET"
        else:
            print(f"{Fore.RED}❌ Invalid choice! Please enter 1-4.{Style.RESET_ALL}")

def input_threads():
    """Nhập số threads"""
    max_threads = cpu_count()
    print(f"\n{Fore.CYAN}🧵 NUMBER OF THREADS{Style.RESET_ALL}")
    print(f"Your CPU has {max_threads} cores available")
    
    while True:
        choice = input(f"{Fore.YELLOW}Threads (1-{max_threads}) [default: {max_threads}]: {Style.BRIGHT}").strip()
        
        if not choice:
            return max_threads
        
        try:
            threads = int(choice)
            if 1 <= threads <= max_threads:
                return threads
            else:
                print(f"{Fore.RED}❌ Please enter a number between 1 and {max_threads}{Style.RESET_ALL}")
        except ValueError:
            print(f"{Fore.RED}❌ Invalid number!{Style.RESET_ALL}")

def input_identifier():
    """Nhập tên rig"""
    print(f"\n{Fore.CYAN}🏷️ RIG IDENTIFIER (Optional){Style.RESET_ALL}")
    print("Give this miner a name to identify it in logs")
    
    ident = input(f"{Fore.YELLOW}Rig name: {Style.BRIGHT}").strip()
    return ident if ident else "None"

def toggle_debug(current):
    """Bật/tắt debug mode"""
    print(f"\n{Fore.CYAN}🐛 DEBUG MODE{Style.RESET_ALL}")
    print(f"Current: {'ON' if current else 'OFF'}")
    
    choice = input(f"{Fore.YELLOW}Enable debug? (y/N): {Style.BRIGHT}").strip().lower()
    return choice == 'y'

def save_config(config_data):
    """Lưu cấu hình vào file"""
    if not os.path.exists(DATA_DIR):
        os.makedirs(DATA_DIR)
    
    config = ConfigParser()
    config["WebCoin"] = config_data
    
    with open(DATA_DIR + SETTINGS_FILE, "w") as f:
        config.write(f)
    
    print(f"{Fore.GREEN}✅ Configuration saved to {DATA_DIR + SETTINGS_FILE}{Style.RESET_ALL}")

def load_config():
    """Đọc cấu hình từ file hoặc tạo mới"""
    config_path = DATA_DIR + SETTINGS_FILE
    
    if os.path.exists(config_path):
        config = ConfigParser()
        config.read(config_path)
        
        print(f"{Fore.GREEN}✅ Loaded existing configuration{Style.RESET_ALL}")
        return config["WebCoin"]
    
    return None

def interactive_setup():
    """Thiết lập tương tác - KHÔNG CẦN MẬT KHẨU"""
    print_banner()
    
    # Thử đọc config cũ
    existing = load_config()
    config_data = {}
    
    if existing:
        print(f"{Fore.CYAN}Found existing configuration:{Style.RESET_ALL}")
        print(f"  Wallet: {existing.get('username', 'N/A')[:20]}...")
        print(f"  Difficulty: {existing.get('start_diff', 'N/A')}")
        print(f"  Threads: {existing.get('threads', 'N/A')}")
        
        use_existing = input(f"\n{Fore.YELLOW}Use existing config? (Y/n): {Style.BRIGHT}").strip().lower()
        if use_existing != 'n':
            return existing
    
    # Nếu không có config cũ hoặc không dùng, tạo mới
    print(f"\n{Fore.GREEN}{Style.BRIGHT}🔧 LET'S SET UP YOUR MINER{Style.RESET_ALL}")
    print("You'll need your wallet address.")
    print("If you don't have a wallet yet, create one at: https://webcoin-1n9d.onrender.com\n")
    
    input(f"{Fore.CYAN}Press Enter to continue...{Style.RESET_ALL}")
    
    # Nhập từng mục (BỎ PHẦN PASSWORD)
    config_data["username"] = input_wallet_address()
    config_data["start_diff"] = input_difficulty()
    config_data["threads"] = str(input_threads())
    config_data["identifier"] = input_identifier()
    config_data["debug"] = str(toggle_debug(False)).lower()
    config_data["soc_timeout"] = str(SOC_TIMEOUT)
    config_data["report_sec"] = str(REPORT_TIME)
    
    # Hiển thị tổng kết
    print_banner()
    print(f"{Fore.GREEN}{Style.BRIGHT}📋 CONFIGURATION SUMMARY{Style.RESET_ALL}")
    print("═" * 50)
    print(f"  Wallet:     {config_data['username'][:30]}...")
    print(f"  Difficulty: {config_data['start_diff']}")
    print(f"  Threads:    {config_data['threads']}")
    print(f"  Rig ID:     {config_data['identifier']}")
    print(f"  Debug:      {config_data['debug']}")
    print("═" * 50)
    
    # Xác nhận
    confirm = input(f"\n{Fore.YELLOW}Save this configuration and start mining? (Y/n): {Style.BRIGHT}").strip().lower()
    if confirm == 'n':
        print(f"{Fore.RED}Setup cancelled.{Style.RESET_ALL}")
        sys.exit(0)
    
    # Lưu config
    save_config(config_data)
    
    return config_data

# ============== GIAO TIẾP MẠNG ==============
class WebCoinClient:
    @staticmethod
    def fetch_pool():
        """Lấy thông tin pool (dùng server chính)"""
        return ("webcoin-1n9d.onrender.com", 443)
    
    @staticmethod
    def connect(pool):
        """Kết nối socket"""
        import ssl
        sock = socket.socket()
        sock.settimeout(SOC_TIMEOUT)
        
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        
        ssl_sock = context.wrap_socket(sock, server_hostname=pool[0])
        ssl_sock.connect(pool)
        return ssl_sock
    
    @staticmethod
    def send(sock, msg):
        sock.sendall(f"{msg}\n".encode())
    
    @staticmethod
    def recv(sock, limit=4096):
        return sock.recv(limit).decode().strip()

# ============== THUẬT TOÁN ĐÀO ==============
class Algorithms:
    @staticmethod
    def WEBCOIN_S1(last_h, target_prefix, diff, intensity):
        """
        Thuật toán đào WebCoin (SHA-1 based)
        Tìm hash bắt đầu bằng '0' * diff
        """
        start_time = time.time_ns()
        
        base_hash = hashlib.sha1(last_h.encode()).copy()
        max_nonce = 100000 * diff
        
        for nonce in range(max_nonce):
            temp_h = base_hash.copy()
            temp_h.update(str(nonce).encode())
            result = temp_h.hexdigest()
            
            if intensity > 0 and nonce % 5000 == 0:
                time.sleep(intensity / 1000)
            
            if result.startswith(target_prefix):
                elapsed = time.time_ns() - start_time
                hashrate = 1e9 * nonce / elapsed if elapsed > 0 else 0
                return [nonce, hashrate, result]
        
        return [0, 0, ""]

# ============== THREAD ĐÀO ==============
def mining_thread(thread_id, user_settings, share_stats, pool, print_queue):
    """Thread đào chính - KHÔNG CẦN LOGIN"""
    username = user_settings["username"]
    start_diff = user_settings["start_diff"]
    identifier = user_settings["identifier"]
    
    debug_print(f"Thread {thread_id} started")
    
    while running:
        try:
            # Xác định độ khó
            if start_diff == "LOW":
                diff = 2
            elif start_diff == "MEDIUM":
                diff = 3
            elif start_diff == "HIGH":
                diff = 4
            else:  # NET
                try:
                    response = requests.get(f"{SERVER_URL}/info", timeout=5)
                    if response.status_code == 200:
                        info = response.json()
                        diff = info.get("difficulty", 3)
                    else:
                        diff = 3
                except:
                    diff = 3
            
            # Tạo last_h từ username và timestamp
            last_h = hashlib.sha1(f"{username}{time.time()}{thread_id}".encode()).hexdigest()
            
            # Target prefix: '0' * diff
            target_prefix = "0" * diff
            
            # Đào
            intensity = 0
            result = Algorithms.WEBCOIN_S1(last_h, target_prefix, diff, intensity)
            
            if result[0] > 0:
                # Tìm thấy nonce
                share_stats["accepted"] += 1
                
                # Format output
                time_str = now().strftime("%H:%M:%S")
                
                share_msg = (
                    f"{Fore.WHITE}{time_str} "
                    f"{Fore.WHITE}{Style.BRIGHT}{Back.YELLOW} cpu{thread_id} {Style.RESET_ALL}"
                    f"{Fore.GREEN} ✅ Found "
                    f"nonce:{result[0]} "
                    f"hash:{result[2][:10]}... "
                    f"speed:{get_prefix(result[1])} "
                    f"diff:{diff}"
                )
                print_queue.append(share_msg)
            else:
                share_stats["rejected"] += 1
            
            # Báo cáo định kỳ
            if thread_id == 0 and share_stats["accepted"] % 10 == 0 and share_stats["accepted"] > 0:
                print_queue.append(
                    f"{Fore.CYAN}📊 Thread 0: {share_stats['accepted']} solutions found{Style.RESET_ALL}"
                )
                
        except Exception as e:
            debug_print(f"Thread {thread_id} error: {e}")
            time.sleep(5)

# ============== XỬ LÝ IN ẤN ==============
def print_queue_handler(print_queue):
    """Xử lý hàng đợi in ấn"""
    while running:
        if print_queue:
            print(print_queue.pop(0))
        time.sleep(0.01)

# ============== MAIN ==============
if __name__ == "__main__":
    # Thiết lập tương tác (KHÔNG CẦN MẬT KHẨU)
    user_settings = interactive_setup()
    
    # Cập nhật debug mode
    debug_mode = user_settings.get("debug") == "true"
    
    # Tiêu đề console
    title(f"WebCoin Miner v{VERSION} - {user_settings['username'][:20]}...")
    
    # In banner bắt đầu
    print_banner()
    print(f"{Fore.GREEN}{Style.BRIGHT}🚀 STARTING MINER (NO PASSWORD REQUIRED){Style.RESET_ALL}")
    print("═" * 50)
    print(f"  Wallet:     {user_settings['username'][:30]}...")
    print(f"  Difficulty: {user_settings['start_diff']}")
    print(f"  Threads:    {user_settings['threads']}")
    print(f"  Rig ID:     {user_settings['identifier']}")
    print("═" * 50)
    print(f"{Fore.YELLOW}Press Ctrl+C to stop mining{Style.RESET_ALL}\n")
    
    # Pool
    pool = WebCoinClient.fetch_pool()
    
    # Shared variables
    manager = Manager()
    share_stats = manager.dict()
    share_stats["accepted"] = 0
    share_stats["rejected"] = 0
    print_queue = manager.list()
    
    # Thread in ấn
    threading.Thread(target=print_queue_handler, args=(print_queue,), daemon=True).start()
    
    # Khởi tạo threads
    threads = []
    num_threads = int(user_settings["threads"])
    
    for i in range(num_threads):
        t = threading.Thread(
            target=mining_thread,
            args=(i, user_settings, share_stats, pool, print_queue),
            daemon=True
        )
        t.start()
        threads.append(t)
    
    # Vòng lặp chính - hiển thị thống kê
    last_report = time.time()
    last_accepted = 0
    
    try:
        while running:
            time.sleep(5)
            
            if share_stats["accepted"] > 0 or share_stats["rejected"] > 0:
                total = share_stats["accepted"] + share_stats["rejected"]
                rate = share_stats["accepted"] / total * 100 if total > 0 else 0
                
                sys.stdout.write(
                    f"\r{Fore.CYAN}📊 Found: {share_stats['accepted']} | "
                    f"Failed: {share_stats['rejected']} | "
                    f"Success rate: {rate:.1f}%{Style.RESET_ALL}"
                )
                sys.stdout.flush()
            
    except KeyboardInterrupt:
        print(f"\n\n{Fore.YELLOW}⛔ Stopping miner...{Style.RESET_ALL}")
        running = False
    
    # Thống kê cuối cùng
    print(f"\n\n{Fore.GREEN}{Style.BRIGHT}📊 FINAL STATISTICS{Style.RESET_ALL}")
    print("═" * 50)
    print(f"  Total solutions found: {share_stats['accepted']}")
    print(f"  Total failed attempts: {share_stats['rejected']}")
    if share_stats['accepted'] + share_stats['rejected'] > 0:
        rate = share_stats['accepted'] / (share_stats['accepted'] + share_stats['rejected']) * 100
        print(f"  Success rate: {rate:.1f}%")
    print("═" * 50)
    print(f"{Fore.GREEN}👋 Goodbye!{Style.RESET_ALL}\n")
