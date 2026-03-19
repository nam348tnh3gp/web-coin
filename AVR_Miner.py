#!/usr/bin/env python3
"""
WebCoin Official AVR Miner 2.0
SHA1 + HMAC + Salt Support
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
import hashlib
import hmac
import secrets
from datetime import datetime
from collections import deque
from configparser import ConfigParser
from signal import SIGINT, signal
from colorama import init, Fore, Back, Style

init(autoreset=True)

VERSION = "2.0"
SERVER_URL = "https://webcoin-1n9d.onrender.com/api"
WALLET_ADDRESS = "W_0488269778421338855a2442815747a0b151c3389f60f27bf70b868578c5ab481ec5eef7a43773bcdda58e153d27721116885441c4bffa9fe36dffd8dc07722ccb"
WALLET_PASSWORD = "12345678Nn"
DIFFICULTY = 3
BAUDRATE = 115200
TIMEOUT = 10
CONFIG_DIR = "WebCoin-Data"

shares = [0, 0]
hashrate_list = []
ping_list = deque(maxlen=25)
print_queue = []
running = True
config = ConfigParser()
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

def debug_print(msg):
    if debug_mode:
        print(f"{Fore.WHITE}{Style.DIM}[DEBUG] {msg}{Style.RESET_ALL}")

def save_config():
    if not os.path.exists(CONFIG_DIR):
        os.makedirs(CONFIG_DIR)
    
    with open(f"{CONFIG_DIR}/config.cfg", "w") as f:
        config.write(f)
    print(f"{Fore.GREEN}✅ Config saved{Style.RESET_ALL}")

def load_config():
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
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]

def login():
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
    try:
        headers = {"Cookie": auth_cookie} if auth_cookie else {}
        response = requests.get(f"{SERVER_URL}/info", headers=headers, timeout=5)
        
        if response.status_code == 200:
            return response.json()
    except:
        pass
    return None

def get_pending():
    try:
        headers = {"Cookie": auth_cookie} if auth_cookie else {}
        response = requests.get(f"{SERVER_URL}/pending", headers=headers, timeout=5)
        
        if response.status_code == 200:
            return response.json()
    except:
        pass
    return []

def generate_salt(length=16):
    return secrets.token_hex(length)

def calculate_hmac(data, key, salt):
    message = json.dumps(data) if isinstance(data, dict) else str(data)
    hmac_obj = hmac.new(
        (key + salt).encode('utf-8'),
        message.encode('utf-8'),
        hashlib.sha256
    )
    return hmac_obj.hexdigest()

def submit_result(height, nonce, hash_value, prev_hash, reward, 
                  block_hmac, worker_salt, mining_salt, block_salt):
    try:
        public_key = username[2:] if username.startswith("W_") else username
        
        coinbase_salt = generate_salt(16)
        coinbase_hmac = calculate_hmac(
            f"{public_key}{reward}{int(time.time()*1000)}{coinbase_salt}",
            public_key,
            coinbase_salt
        )
        
        data = {
            "height": height,
            "transactions": [{
                "from": None,
                "to": public_key,
                "amount": reward,
                "timestamp": int(time.time() * 1000),
                "signature": None,
                "salt": coinbase_salt,
                "hmac": coinbase_hmac
            }],
            "previousHash": prev_hash,
            "timestamp": int(time.time() * 1000),
            "nonce": nonce,
            "hash": hash_value,
            "minerAddress": username,
            "blockHMAC": block_hmac,
            "workerSalt": worker_salt,
            "miningSalt": mining_salt,
            "blockSalt": block_salt
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
        else:
            debug_print(f"Submit failed: {response.status_code} - {response.text}")
    except Exception as e:
        debug_print(f"Submit error: {e}")
    
    return False

def print_header():
    print(f"{Fore.MAGENTA}{Style.BRIGHT}")
    print("╔══════════════════════════════════════╗")
    print("║     WEBCCOIN AVR MINER v2.0         ║")
    print("║    (SHA1 + HMAC + Salt)             ║")
    print("╚══════════════════════════════════════╝")
    print(f"{Style.RESET_ALL}")

def print_config():
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
    while running:
        if print_queue:
            print(print_queue.pop(0))
        time.sleep(0.01)

def test_board(ser, thread_id):
    try:
        test_cmd = "TEST,10\n"
        ser.write(test_cmd.encode())
        
        result = ser.read_until(b'\n').decode().strip().split(',')
        
        if len(result) >= 2:
            num_res = int(result[0])
            exec_time = int(result[1]) / 1_000_000
            hashrate = num_res / exec_time
            
            print_status(f"Board {thread_id} hashrate: {get_prefix(hashrate)}", "success")
            return hashrate
    except Exception as e:
        print_status(f"Board test failed: {e}", "error")
    
    return 100

def mine_arduino(port_name, thread_id, identifier):
    global shares, hashrate_list, running
    
    try:
        ser = serial.Serial(
            port_name,
            baudrate=BAUDRATE,
            timeout=TIMEOUT
        )
        time.sleep(2)
        
        print_status(f"Connected to Arduino on {port_name}", "success")
    except Exception as e:
        print_status(f"Cannot connect to {port_name}: {e}", "error")
        return
    
    board_hashrate = test_board(ser, thread_id)
    hashrate_list.append(board_hashrate)
    
    local_accepted = 0
    local_rejected = 0
    
    while running:
        try:
            info = get_network_info()
            if not info:
                time.sleep(5)
                continue
            
            latest = info["latestBlock"]
            height = latest["height"] + 1
            reward = info["reward"]
            prev_hash = latest["hash"]
            network_diff = info["difficulty"]
            
            pending = get_pending()
            
            pending_tx_str = json.dumps(pending) if pending else "[]"
            
            mining_salt = generate_salt(8)
            block_salt = generate_salt(8)
            
            job_data = f"JOB,{height},{prev_hash},{int(time.time()*1000)},{network_diff},{reward},{pending_tx_str},{mining_salt},{block_salt}\n"
            
            ser.write(job_data.encode())
            
            result = ser.read_until(b'\n').decode().strip().split(',')
            
            if len(result) >= 8:
                nonce = int(result[0])
                job_time = int(result[1]) / 1_000_000
                hash_value = result[2]
                block_hmac = result[3]
                worker_salt = result[4]
                received_mining_salt = result[5]
                received_block_salt = result[6]
                
                hashrate = 1000000 / job_time
                
                success = submit_result(
                    height, nonce, hash_value, prev_hash, reward,
                    block_hmac, worker_salt, received_mining_salt, received_block_salt
                )
                
                if success:
                    shares[0] += 1
                    local_accepted += 1
                else:
                    shares[1] += 1
                    local_rejected += 1
                
                hashrate_list[thread_id] = hashrate
                total_hashrate = sum(hashrate_list)
                
                ping = 50 + (thread_id * 10)
                
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
                
                if success and debug_mode:
                    print_status(f"Block HMAC: {block_hmac[:16]}...", "info")
            
        except serial.SerialException as e:
            print_status(f"Serial error on {port_name}: {e}", "error")
            break
        except Exception as e:
            debug_print(f"Mining error: {e}")
            time.sleep(1)
    
    ser.close()
    print_status(f"Disconnected from {port_name}", "warning")

def signal_handler(sig, frame):
    global running
    print_status("Shutting down...", "warning")
    running = False
    sys.exit(0)

def main():
    global running
    
    signal(SIGINT, signal_handler)
    
    print_header()
    
    load_config()
    print_config()
    
    if not login():
        return
    
    ports = get_available_ports()
    if not ports:
        print_status("No serial ports found!", "error")
        return
    
    print_status(f"Found ports: {', '.join(ports)}", "info")
    
    selected_port = None
    if port == "auto":
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
    
    threading.Thread(target=print_queue_handler, daemon=True).start()
    
    print_status(f"Starting miner on {selected_port}", "success")
    
    mining_thread = threading.Thread(
        target=mine_arduino,
        args=(selected_port, 0, identifier)
    )
    mining_thread.start()
    
    while running:
        time.sleep(1)

if __name__ == "__main__":
    main()
