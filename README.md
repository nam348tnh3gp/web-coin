╔══════════════════════════════════════════════════════════════════════════════╗
║                         WEBCCOIN ARDUINO MINER                               ║
║                Turn your Arduino into a WebCoin mining rig!                  ║
║                    Ultra-lightweight SHA-1 firmware                          ║
║               Optimized for AVR microcontrollers                             ║
╚══════════════════════════════════════════════════════════════════════════════╝


┌──────────────────────────────────────────────────────────────────────────────┐
│                           PROJECT LINKS                                       │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│   🌐 LIVE DEMO: https://webcoin-1n9d.onrender.com                            │
│                                                                               │
│   📦 MAIN REPO: https://github.com/nam348tnh3gp/web-coin                     │
│                                                                               │
│   🐍 PYTHON MINER: https://github.com/nam348tnh3gp/web-coin/blob/main/AVR_Miner.py │
│                                                                               │
│   📟 THIS FIRMWARE: WebCoin-Arduino-Miner                                    │
│                                                                               │
└──────────────────────────────────────────────────────────────────────────────┘


████████████████████████████████  OVERVIEW  ███████████████████████████████████

WebCoin Arduino Miner is a specialized firmware for Arduino boards that enables:

✅ Mine WebCoin directly on AVR microcontrollers
✅ Communicate with Python AVR Miner via serial connection
✅ Automatically receive jobs and submit results
✅ Memory and performance optimized for Arduino

This project is part of the complete WebCoin ecosystem, working together with:

🌐 WebCoin Live Demo: https://webcoin-1n9d.onrender.com
   - Create wallets
   - Check balances
   - View blockchain
   - Monitor mining activity

🖥️ WebCoin Server (Node.js) - Backend API & blockchain
🐍 WebCoin AVR Miner (Python) - Serial communication manager
📟 This Firmware (Arduino) - Actual mining hardware


████████████████████████  KEY FEATURES  ███████████████████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  ⛏️  MINING ENGINE                                                            │
├──────────────────────────────────────────────────────────────────────────────┤
│  • Pure C++ SHA-1 implementation (no external libraries)                     │
│  • Flexible difficulty adjustment based on server requirements               │
│  • Real-time hash rate calculation every second                              │
│  • Automatic watchdog to prevent hanging on long jobs                        │
│  • 1,000,000 nonce limit to prevent overflow                                 │
│  • Compatible with WebCoin's Proof-of-Work algorithm                         │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  📟  LED STATUS INDICATOR                                                     │
├──────────────────────────────────────────────────────────────────────────────┤
│  • 🔴 Slow blink (1Hz): Idle, waiting for job from Python                    │
│  • 🟢 Fast blink: Mining in progress (faster = better)                       │
│  • 🔵 Solid on: Serial connection established                                │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  📊  PERFORMANCE (estimated)                                                  │
├──────────────────────────────────────────────────────────────────────────────┤
│  Board         Flash    RAM    Hash Rate (H/s)   Power @5V                   │
│  ─────────────────────────────────────────────────────────────────────────── │
│  Arduino Uno   32 KB    2 KB      2 - 5 H/s       ~50 mA                     │
│  Arduino Nano  32 KB    2 KB      2 - 5 H/s       ~40 mA                     │
│  Arduino Mega  256 KB   8 KB      5 - 10 H/s      ~100 mA                    │
│  Arduino Pro   32 KB    2 KB      2 - 5 H/s       ~20 mA (3.3V)              │
│  Arduino Leonardo 32 KB  2.5 KB   2 - 5 H/s       ~40 mA                     │
└──────────────────────────────────────────────────────────────────────────────┘


████████████████████████  SERIAL PROTOCOL  ████████████████████████████████████

▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬
Technical Specifications:
- Baud rate: 115200
- Data bits: 8
- Stop bits: 1
- Parity: None
- Encoding: ASCII, terminated with \n
▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬

┌──────────────────────────────────────────────────────────────────────────────┐
│  📌  PING COMMAND - Connection test                                          │
├──────────────────────────────────────────────────────────────────────────────┤
│  PC → Arduino:  PING\n                                                        │
│  Arduino → PC:  PONG\n                                                        │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  📌  TEST COMMAND - Hash rate measurement                                    │
├──────────────────────────────────────────────────────────────────────────────┤
│  PC → Arduino:  TEST,<iterations>\n                                          │
│                 Example: TEST,100\n                                          │
│                                                                               │
│  Arduino → PC:  <hash_count>,<time_us>\n                                     │
│                 Example: 100,1234567\n                                       │
│                 → 100 hashes in 1.234567 seconds                             │
│                 → Hash rate ≈ 81 H/s                                         │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  📌  JOB COMMAND - Mining task                                               │
├──────────────────────────────────────────────────────────────────────────────┤
│  PC → Arduino:                                                                │
│    JOB,<height>,<prev_hash>,<timestamp>,<diff>,<reward>\n                    │
│                                                                               │
│  Example:                                                                     │
│    JOB,123,0123456789abcdef0123456789abcdef01234567,1741872000000,3,48\n     │
│    │     │    │                              │           │   │   │           │
│    │     │    │                              │           │   │   └── reward  │
│    │     │    │                              │           │   └────── diff    │
│    │     │    │                              │           └────────── timestamp│
│    │     │    │                              └──────────────── prev_hash     │
│    │     │    └───────────────────────────────────────── height              │
│    │     └───────────────────────────────────────────────── JOB command      │
│    └─────────────────────────────────────────────────────── prefix           │
│                                                                               │
│  Arduino → PC (when nonce found):                                            │
│    <nonce>,<time_us>,<hash_hex>\n                                            │
│    Example: 12345,2345678,000abc123def4567890fedcba9876543210fedcba\n        │
│                                                                               │
│  Arduino → PC (if job cancelled):                                            │
│    JOB_STOPPED\n                                                              │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  📌  STOP COMMAND - Cancel current job                                       │
├──────────────────────────────────────────────────────────────────────────────┤
│  PC → Arduino:  STOP\n                                                        │
│  Arduino → PC:  JOB_STOPPED\n                                                │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  BLOCK STRUCTURE & ALGORITHM  ██████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  📦  DATA STRUCTURE (52 bytes)                                               │
├──────────────────────────────────────────────────────────────────────────────┤
│  Offset  | Size | Field        | Description                                 │
│  ─────────────────────────────────────────────────────────────────────────── │
│  0       | 4    | height       | Block height (big-endian)                   │
│  4       | 20   | prev_hash    | Previous block hash                         │
│  24      | 4    | timestamp    | Timestamp (milliseconds, lower 4 bytes)     │
│  28      | 20   | zero_hash    | 20 bytes zero (simulated transaction hash)  │
│  48      | 4    | nonce        | Nonce (big-endian)                          │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🔐  SHA-1 ALGORITHM (AVR-optimized)                                         │
├──────────────────────────────────────────────────────────────────────────────┤
│  • Processes data in 64-byte blocks                                          │
│  • Uses 32-bit operations (efficient on AVR)                                 │
│  • 80 rounds per block                                                       │
│  • 20-byte (160-bit) output                                                  │
│                                                                               │
│  Hash function:                                                               │
│  SHA1( height (4B) + prev_hash (20B) + timestamp (4B) +                      │
│        zero_hash (20B) + nonce (4B) )                                        │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  ✅  DIFFICULTY CHECK                                                         │
├──────────────────────────────────────────────────────────────────────────────┤
│  bool checkDifficulty(uint8_t* hash, uint32_t diff) {                        │
│      for (uint32_t i = 0; i < diff; i++) {                                   │
│          if (hash[i] != 0) return false;                                     │
│      }                                                                        │
│      return true;                                                             │
│  }                                                                            │
│                                                                               │
│  Examples:                                                                    │
│  - diff = 3: hash must start with 00 00 00                                   │
│  - diff = 4: hash must start with 00 00 00 00                                │
│  - diff = 5: hash must start with 00 00 00 00 00                             │
└──────────────────────────────────────────────────────────────────────────────┘


████████████████████████  INSTALLATION GUIDE  █████████████████████████████████

▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬
REQUIREMENTS:
- Arduino board (Uno, Nano, Mega, Pro, Leonardo, etc.)
- USB cable
- PlatformIO (recommended) or Arduino IDE
- Python 3.8+ with pyserial
- WebCoin Python AVR Miner (AVR_Miner.py)
- WebCoin Server (running at https://webcoin-1n9d.onrender.com)
▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬

┌──────────────────────────────────────────────────────────────────────────────┐
│  📦  METHOD 1: PLATFORMIO (Recommended)                                      │
├──────────────────────────────────────────────────────────────────────────────┤
│  Step 1: Install PlatformIO CLI                                              │
│  ─────────────────────────────────────────────────────────────────────────── │
│  pip install platformio                                                       │
│                                                                               │
│  Step 2: Clone repository                                                     │
│  ─────────────────────────────────────────────────────────────────────────── │
│  git clone https://github.com/nam348tnh3gp/web-coin.git                      │
│  cd web-coin                                                                  │
│  # The Arduino firmware is in the root directory as miner.ino                │
│                                                                               │
│  Step 3: Upload firmware to your board                                       │
│  ─────────────────────────────────────────────────────────────────────────── │
│  # For Arduino Uno                                                            │
│  pio run -e uno -t upload                                                     │
│                                                                               │
│  # For Arduino Nano                                                           │
│  pio run -e nano -t upload                                                    │
│                                                                               │
│  # For Arduino Mega                                                           │
│  pio run -e mega -t upload                                                    │
│                                                                               │
│  Step 4: Monitor serial (verify)                                             │
│  ─────────────────────────────────────────────────────────────────────────── │
│  pio device monitor -b 115200                                                 │
│  # If you see "READY" -> Firmware is running                                 │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🛠️  METHOD 2: ARDUINO IDE                                                   │
├──────────────────────────────────────────────────────────────────────────────┤
│  Step 1: Open Arduino IDE                                                     │
│  ─────────────────────────────────────────────────────────────────────────── │
│  • File → Open → Select miner.ino                                            │
│                                                                               │
│  Step 2: Select Board                                                         │
│  ─────────────────────────────────────────────────────────────────────────── │
│  • Tools → Board → Arduino AVR Boards → Choose your board                    │
│    (Uno/Nano/Mega/Leonardo/etc.)                                             │
│                                                                               │
│  Step 3: Select Port                                                          │
│  ─────────────────────────────────────────────────────────────────────────── │
│  • Tools → Port → Select your Arduino's COM port                             │
│                                                                               │
│  Step 4: Upload                                                               │
│  ─────────────────────────────────────────────────────────────────────────── │
│  • Click Upload button (→) or Sketch → Upload                                │
│  • Wait for "Done uploading" message                                         │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🔧  PLATFORMIO CONFIGURATION (platformio.ini)                               │
├──────────────────────────────────────────────────────────────────────────────┤
│  [env:uno]                                                                    │
│  platform = atmelavr                                                          │
│  board = uno                                                                  │
│  framework = arduino                                                          │
│  monitor_speed = 115200                                                       │
│                                                                               │
│  [env:nano]                                                                   │
│  platform = atmelavr                                                          │
│  board = nano                                                                 │
│  framework = arduino                                                          │
│  monitor_speed = 115200                                                       │
│                                                                               │
│  [env:mega]                                                                   │
│  platform = atmelavr                                                          │
│  board = megaatmega2560                                                       │
│  framework = arduino                                                          │
│  monitor_speed = 115200                                                       │
│                                                                               │
│  [env:leonardo]                                                               │
│  platform = atmelavr                                                          │
│  board = leonardo                                                             │
│  framework = arduino                                                          │
│  monitor_speed = 115200                                                       │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  CONNECTING TO WEBCCOIN ECOSYSTEM  █████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  🐍  STEP 1: INSTALL PYTHON DEPENDENCIES                                     │
├──────────────────────────────────────────────────────────────────────────────┤
│  # From the WebCoin project root                                             │
│  pip install -r requirements.txt                                             │
│                                                                               │
│  # Required packages:                                                         │
│  # - requests (API communication)                                            │
│  # - colorama (colored console output)                                       │
│  # - pyserial (Arduino communication)                                        │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🚀  STEP 2: RUN THE AVR MINER                                               │
├──────────────────────────────────────────────────────────────────────────────┤
│  python AVR_Miner.py                                                          │
│                                                                               │
│  The program will automatically:                                             │
│  • Scan for available serial ports                                           │
│  • Detect your Arduino (looks for "PONG" response)                           │
│  • Test the board to determine hash rate                                     │
│  • Connect to WebCoin server at https://webcoin-1n9d.onrender.com           │
│  • Login with your wallet credentials                                        │
│  • Start receiving mining jobs                                               │
│  • Send jobs to Arduino and receive results                                  │
│  • Display real-time statistics                                              │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🌐  STEP 3: MONITOR VIA WEB INTERFACE                                       │
├──────────────────────────────────────────────────────────────────────────────┤
│  Open your browser and go to:                                                │
│  ─────────────────────────────────────────────────────────────────────────── │
│  🔗 https://webcoin-1n9d.onrender.com                                        │
│                                                                               │
│  From there you can:                                                          │
│  • Create a new wallet (if you don't have one)                               │
│  • Login with your wallet address and password                               │
│  • Check your balance in real-time                                           │
│  • View transaction history                                                   │
│  • See the latest blocks on the blockchain                                   │
│  • Monitor mining activity                                                    │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🔄  COMPLETE WORKFLOW                                                        │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│   🌐 WebCoin Server                      🐍 Python AVR Miner                 │
│   https://webcoin-1n9d.onrender.com  ←→  AVR_Miner.py                        │
│         ↓                                      ↓                              │
│    [REST API]                              [Serial]                          │
│         ↓                                      ↓                              │
│   • Latest block                          📟 Arduino Board                   │
│   • Difficulty                            • Receives job                     │
│   • Pending transactions ───────────────→ • Mines with SHA-1                 │
│   • Mining reward                          • Returns result                  │
│         ↑                                      ↓                              │
│    [Submit Block]                         [Send nonce + hash]                │
│         ↓                                      ↓                              │
│   Balance updated                         Statistics displayed               │
│                                                                               │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  TROUBLESHOOTING  █████████████████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  ❌  SERIAL CONNECTION FAILS                                                  │
├──────────────────────────────────────────────────────────────────────────────┤
│  Symptoms:                                                                    │
│  • Python miner shows "No Arduino found"                                     │
│  • No "PONG" response                                                        │
│                                                                               │
│  Solutions:                                                                   │
│  ✓ Check baud rate (must be 115200)                                          │
│  ✓ Verify correct port in Python script                                      │
│  ✓ Reset Arduino (press reset button)                                        │
│  ✓ Check USB cable connection                                                 │
│  ✓ Try different USB port                                                     │
│  ✓ Re-upload firmware                                                         │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🐢  LOW HASH RATE                                                            │
├──────────────────────────────────────────────────────────────────────────────┤
│  Symptoms:                                                                    │
│  • Hash rate below 1 H/s                                                     │
│  • Very slow mining                                                          │
│                                                                               │
│  Solutions:                                                                   │
│  ✓ Close other programs using serial port                                    │
│  ✓ Use external power for power-hungry boards (Mega)                         │
│  ✓ Reduce temperature (AVR throttles when hot)                               │
│  ✓ Check for serial interference (long cables)                               │
│  ✓ Consider using Mega for better performance                                │
│  ✓ Overclock (advanced - change crystal)                                     │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🔄  RANDOM RESETS                                                            │
├──────────────────────────────────────────────────────────────────────────────┤
│  Symptoms:                                                                    │
│  • Arduino reboots during mining                                             │
│  • Serial connection drops                                                   │
│                                                                               │
│  Solutions:                                                                   │
│  ✓ Add capacitor (100µF) across 5V and GND                                   │
│  ✓ Check USB cable quality (use shielded cable)                              │
│  ✓ Reduce clock speed if overclocked                                         │
│  ✓ Use external power supply (7-12V)                                         │
│  ✓ Check for power-hungry components on board                                │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  ⚠️  NO RESPONSE FROM BOARD                                                   │
├──────────────────────────────────────────────────────────────────────────────┤
│  Symptoms:                                                                    │
│  • Nothing appears in serial monitor                                         │
│  • LED not blinking                                                          │
│                                                                               │
│  Solutions:                                                                   │
│  ✓ Re-upload firmware                                                        │
│  ✓ Press reset button                                                         │
│  ✓ Check if LED is blinking (power OK)                                       │
│  ✓ Try different USB cable                                                    │
│  ✓ Test with simple Blink sketch first                                       │
│  ✓ Check if board is damaged                                                  │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🔑  AUTHENTICATION FAILED                                                    │
├──────────────────────────────────────────────────────────────────────────────┤
│  Symptoms:                                                                    │
│  • Python miner shows "Login failed"                                         │
│  • Cannot connect to server                                                   │
│                                                                               │
│  Solutions:                                                                   │
│  ✓ Verify wallet address (must start with W_)                                │
│  ✓ Check password (case sensitive)                                           │
│  ✓ Create new wallet at https://webcoin-1n9d.onrender.com                   │
│  ✓ Check internet connection                                                  │
│  ✓ Verify server is online (open in browser)                                 │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  OPTIMIZATION TIPS  ████████████████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  🚀  INCREASE HASH RATE                                                      │
├──────────────────────────────────────────────────────────────────────────────┤
│  Hardware upgrades:                                                          │
│  • Use Arduino Mega (2x faster than Uno)                                     │
│  • Overclock by changing crystal (16MHz → 20MHz)                             │
│  • Use 3.3V boards at higher clock (Pro, Pro Mini)                          │
│                                                                               │
│  Software optimizations:                                                     │
│  • Disable debug code (remove Serial.print statements)                       │
│  • Use -O3 compiler optimization                                             │
│  • Reduce nonce limit (if you find solutions quickly)                        │
│  • Use PlatformIO with release flags                                         │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  🔋  REDUCE POWER CONSUMPTION                                                │
├──────────────────────────────────────────────────────────────────────────────┤
│  For battery-powered operation:                                              │
│  • Lower clock speed (if you don't need max performance)                     │
│  • Disable LED by commenting digitalWrite calls                              │
│  • Use sleep mode between jobs (not implemented yet)                         │
│  • Use 3.3V boards instead of 5V                                             │
│  • Arduino Pro Mini at 8MHz/3.3V uses ~4mA                                   │
│  • Remove power LED (cut trace or desolder)                                  │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  API REFERENCE  ████████████████████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  🌐  WEBCCOIN SERVER API                                                      │
├──────────────────────────────────────────────────────────────────────────────┤
│  Base URL: https://webcoin-1n9d.onrender.com/api                             │
│                                                                               │
│  Public Endpoints:                                                            │
│  ─────────────────────────────────────────────────────────────────────────── │
│  GET  /info              → Network info (latest block, difficulty, reward)  │
│  GET  /blocks            → List of blocks (paginated)                        │
│  GET  /balance/:address  → Wallet balance                                    │
│  GET  /pending           → Pending transactions                              │
│  GET  /history/:address  → Transaction history                               │
│                                                                               │
│  Authentication Endpoints:                                                    │
│  ─────────────────────────────────────────────────────────────────────────── │
│  POST /register          → Create new wallet                                 │
│  POST /login             → Login to wallet                                   │
│  POST /logout            → Logout                                            │
│                                                                               │
│  Protected Endpoints (require login):                                         │
│  ─────────────────────────────────────────────────────────────────────────── │
│  POST /transactions      → Create new transaction                            │
│  POST /blocks/submit     → Submit mined block                                │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  MEMORY USAGE  █████████████████████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│  📊  ARDUINO UNO / NANO                                                      │
├──────────────────────────────────────────────────────────────────────────────┤
│  • Flash (code):    ~8 KB   (25% of 32 KB)                                   │
│  • RAM (data):      ~500 B  (25% of 2 KB)                                    │
│  • Stack:           ~200 B                                                    │
│  • Free memory:     ~1.3 KB                                                   │
│                                                                               │
│  📊  ARDUINO MEGA                                                             │
├──────────────────────────────────────────────────────────────────────────────┤
│  • Flash (code):    ~8 KB   (3% of 256 KB)                                   │
│  • RAM (data):      ~500 B  (6% of 8 KB)                                     │
│  • Stack:           ~1 KB                                                     │
│  • Free memory:     ~6.5 KB                                                   │
└──────────────────────────────────────────────────────────────────────────────┘


██████████████████████  LICENSE  ██████████████████████████████████████████████

This project is licensed under the MIT License - see the LICENSE file for details.

Copyright (c) 2026 WebCoin Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


██████████████████████  ACKNOWLEDGMENTS  ██████████████████████████████████████

- Based on Duino-Coin AVR miner structure
- SHA-1 implementation inspired by Arduino SHA library
- Thanks to the Arduino and PlatformIO communities
- Special thanks to all WebCoin contributors


██████████████████████  CONTACT & SUPPORT  ████████████████████████████████████

┌──────────────────────────────────────────────────────────────────────────────┐
│                                                                               │
│   🌐 LIVE DEMO:  https://webcoin-1n9d.onrender.com                           │
│                                                                               │
│   📦 MAIN REPO:  https://github.com/nam348tnh3gp/web-coin                    │
│                                                                               │
│   🐛 ISSUES:      https://github.com/nam348tnh3gp/web-coin/issues            │
│                                                                               │
│   💬 DISCORD:     https://discord.gg/webcoin                                 │
│                                                                               │
│   📧 EMAIL:       webcoin@example.com                                        │
│                                                                               │
└──────────────────────────────────────────────────────────────────────────────┘


┌──────────────────────────────────────────────────────────────────────────────┐
│                                    ⚡                                         │
│                         Made with ⚡ by the WebCoin Team                      │
│                                    ⚡                                         │
│                         ⚙️  Mine on!  💰  Get Rewards!  🔗  Decentralized!   │
│                                    ⚡                                         │
└──────────────────────────────────────────────────────────────────────────────┘


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
WEBCCOIN ARDUINO MINER v1.0 - Last updated: March 2026
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
