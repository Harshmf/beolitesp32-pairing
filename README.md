# Beolit 20 Auto Stereo Link (ESP32-S3)

[![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3-red.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

An automated True Wireless Stereo (TWS) controller for **Bang & Olufsen Beolit 20** speakers running on an ESP32-S3.

Eliminates the need to open the official Bang & Olufsen mobile app to establish or switch stereo pairing. Just power on the ESP32 (e.g., plugged into USB), and it will automatically discover your speakers, pair them in True Wireless Stereo, and cleanly disconnect so your phone or computer has uninterrupted Bluetooth audio.

---

## Key Features

- **Automated True Wireless Stereo (TWS):** Connects to both Beolit 20 speakers over BLE, sends the proprietary B&O GATT handshake commands, and locks them into stereo.
- **Intelligent Pre-Check (Zero Interruption):** Checks if the speakers are already paired before attempting any pairing command. If already in stereo, it skips the reset sequence completely—so active music playback is **never cut off**.
- **Hardware Mode Switcher (BOOT Button):** Press the onboard `BOOT` button (GPIO 0) at any time to toggle between:
  - 🔷 **Stereo L/R:** Master = Left channel, Slave = Right channel
  - 🟣 **Stereo R/L:** Master = Right channel, Slave = Left channel (Inverted)
  - 🟡 **Synced:** Dual-Mono / Party mode (both speakers play full synchronized audio)
- **Clean Disconnect Architecture:** Once pairing or mode changes are confirmed, the ESP32 gracefully disconnects its BLE GATT connection. Your phone or Mac/PC retains 100% of the Bluetooth audio bandwidth with zero latency penalty.
- **Logarithmic RGB LED Status:** Uses a physiological perceptual gamma curve ($\gamma = 2.6$) with rate-limited 33 FPS hardware RMT driving. Delivers calm, organic breathing without blinding brightness or strobe glitches.

---

## State Machine Architecture

```
[ Power On / USB Connected ]
              │
              ▼
      [ STATE_BOOT_SCAN ]
              │ (Master Discovered)
              ▼
      [ STATE_CHECK_ONCE ] ──(Already Paired: 0x04)──► [ STATE_SHOW_SUCCESS ]
              │ (Unpaired: 0x00)                               │ (Fade Green 3s)
              ▼                                                ▼
   [ STATE_SCAN_FOR_SLAVE ]                           [ STATE_STANDBY_IDLE ]
              │ (Slave Discovered)                             ▲ (Quiet / Dark)
              ▼                                                │
  [ STATE_PAIR_CONNECT_SLAVE ]                                 │
              │                                                │
              ▼                                                │
    [ STATE_PAIR_CONFIGURE ]                                   │
              │                                                │
              ▼                                                │
  [ STATE_PAIR_SEND_COMMANDS ]                                 │
              │                                                │
              ▼                                                │
  [ STATE_PAIR_WAIT_CONFIRM ]                                  │
              │ (Roles Confirmed)                              │
              ▼                                                │
   [ STATE_PAIR_APPLY_MODE ]                                   │
              │                                                │
              └────────────────────────────────────────────────┘
```

---

## Hardware Requirements

- **Microcontroller:** Any **ESP32-S3** development board (e.g., ESP32-S3-DevKitC-1, Waveshare ESP32-S3 Zero, SuperMini S3, YD-ESP32-S3).
- **Speakers:** Two Bang & Olufsen Beolit 20 speakers.
- **Connections:** No external wiring required! Uses onboard BLE, onboard WS2812 RGB LED (GPIO 48 / 38 / 21), and the onboard `BOOT` push-button (GPIO 0).

---

## Quick Start

### 1. Locate Speaker BLE MAC Addresses
You need the Bluetooth Low Energy MAC addresses of your two Beolit 20 speakers. You can find them using free BLE scanner apps:
- **Android:** [nRF Connect for Mobile](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp) or [LightBlue](https://play.google.com/store/apps/details?id=com.punchthrough.lightblueexplorer)
- **iOS:** [nRF Connect](https://apps.apple.com/app/nrf-connect-for-mobile/id1054362403) or [LightBlue](https://apps.apple.com/app/lightblue/id557428110)

Search for devices advertising service UUID `0xFE89` or named `Beolit 20`. Note down both 6-byte hexadecimal MAC addresses.

### 2. Configure the Sketch

> [!IMPORTANT]
> **You MUST modify `SPEAKER_MASTER_MAC` and `SPEAKER_SLAVE_MAC` in `config.h` before flashing!**
> The MAC addresses shipped in `config.h` are randomly scrambled placeholders. The ESP32 will only locate and connect to your Beolit 20 speakers after you insert their actual BLE addresses.

Open `beolit20_esp32_pairing/config.h` and replace the placeholder MAC addresses with your actual speaker MAC addresses:
```c
// Speaker 1: Master Speaker (Default Left channel in Stereo mode)
#define SPEAKER_MASTER_MAC      "34:81:f4:5a:21:c0"  // <-- Put your Master Speaker BLE MAC here

// Speaker 2: Slave Speaker (Default Right channel in Stereo mode)
#define SPEAKER_SLAVE_MAC       "34:81:f4:9b:4e:82"  // <-- Put your Slave Speaker BLE MAC here
```

If your ESP32-S3 board has its onboard WS2812 RGB LED on a different pin, adjust `WS2812_PIN_1`:
- Espressif ESP32-S3 DevKitC-1: `48` (default)
- YD-ESP32-S3: `48` or `38`
- Waveshare ESP32-S3 Zero / SuperMini: `21`

---

## Building and Flashing

### Option A: Arduino IDE
1. Install [Arduino IDE](https://www.arduino.cc/en/software) (version 2.0+).
2. Add ESP32 board support via Boards Manager: `esp32` by Espressif (version 3.x+).
3. Install required libraries from Library Manager:
   - **`NimBLE-Arduino`** (by h2zero, version 2.x+)
   - **`Adafruit NeoPixel`** (by Adafruit, version 1.12+)
4. Select board: **ESP32S3 Dev Module** (or your specific S3 board).
5. Open `beolit20_esp32_pairing/beolit20_esp32_pairing.ino` and click **Upload**.

### Option B: PlatformIO
```bash
# Clone the repository
git clone https://github.com/YOUR_USERNAME/beolit20-esp32-stereo.git
cd beolit20-esp32-stereo

# Build and upload
pio run --target upload

# View serial monitor
pio device monitor
```

### Option C: Arduino CLI
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 beolit20_esp32_pairing
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3 beolit20_esp32_pairing
```

---

## LED Status Reference

The onboard WS2812 RGB LED uses a perceptual logarithmic breathing curve ($\gamma = 2.6$) to provide soft, non-intrusive status feedback:

| State | LED Pattern | Description |
|---|---|---|
| **Scanning** | 🔵 **Breathing Blue** (2.4s cycle) | Searching for Master speaker. |
| **Checking / Connecting** | 🟠 **Breathing Amber** (1.4s cycle) | Checking TWS state once. |
| **Pairing** | 🟣 **Breathing Purple** (800ms cycle) | Handshaking & assigning roles. |
| **Success / Already Paired** | 🟢 **Fade In ➔ Hold ➔ Fade Out Green** (3s) | Confirmed stereo link; entering standby. |
| **Standby / Idle** | ⚫ **OFF (Dark)** | Completely dark. Full Bluetooth bandwidth free for phone. |
| **BOOT Button Pulse** | ⚡ **Dual Smooth Pulse** | Visual feedback indicating active mode: <br>• 🔷 **Cyan:** Stereo L/R<br>• 🟣 **Magenta:** Stereo R/L<br>• 🟡 **Amber:** Synced Dual-Mono |

---

## Protocol Specification

For full reverse-engineering details, Wireshark captures, ATT packet opcodes, and GATT service layout, see [docs/PROTOCOL.md](docs/PROTOCOL.md).

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
