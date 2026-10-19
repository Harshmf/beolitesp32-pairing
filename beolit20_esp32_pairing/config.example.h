#pragma once

#include <Arduino.h>

// ==============================================================================
// Bang & Olufsen Beolit 20 Speaker Configuration
// Replace with your speakers' BLE MAC addresses
// (Use nRF Connect or LightBlue mobile apps to locate them)
// ==============================================================================

// Speaker 1: Primary / Master Speaker (Default Left channel in Stereo mode)
#define SPEAKER_MASTER_MAC      "AA:BB:CC:DD:EE:01"

// Speaker 2: Secondary / Slave Speaker (Default Right channel in Stereo mode)
#define SPEAKER_SLAVE_MAC       "AA:BB:CC:DD:EE:02"

// ==============================================================================
// B&O BLE GATT Services and Characteristics
// (Discovered via Bluetooth HCI snoop reverse-engineering)
// ==============================================================================

// Primary B&O 16-bit Service UUID
#define BO_SERVICE_UUID         "fe89"

// TWS Control & Status Characteristic
#define BO_CHAR_UUID_TWS        "d5b5e4c2-d2a7-4eec-a2d0-6225033a4caf"

// Session / Control Unlock Characteristic
#define BO_CHAR_UUID_SESSION    "3ba91c2e-8b08-4c27-9d4e-4936a793fcfb"

// ==============================================================================
// Protocol Commands & Status Values
// ==============================================================================
#define CMD_TWS_RESET           0x00   // Resets/prepares TWS state
#define CMD_TWS_ROLE_MASTER     0x01   // Assigns Master role
#define CMD_TWS_ROLE_SLAVE      0x02   // Assigns Slave role

// Stereo & Sync Modes (Written as 2 bytes: [0x04, mode_code] to Master)
#define MODE_CODE_STEREO_LR     0x01   // Master = Left, Slave = Right
#define MODE_CODE_STEREO_RL     0x02   // Master = Right, Slave = Left (Inverted)
#define MODE_CODE_SYNCED        0x00   // Both Synced (Dual-Mono / Party mode)

#define STATUS_TWS_PAIRED_M     0x04   // Master paired successfully notification
#define STATUS_TWS_PAIRED_S     0x05   // Slave paired successfully notification

// ==============================================================================
// Hardware Inputs & Onboard LED Configuration
// ==============================================================================
#define BOOT_BUTTON_PIN         0      // ESP32-S3 BOOT button (active LOW)

#define ENABLE_STATUS_LED       true
#define LED_BRIGHTNESS          20     // Soft, comfortable brightness (0-255)

// Pin definitions across ESP32-S3 board variants
#define WS2812_PIN_1            48     // Espressif ESP32-S3 DevKitC-1
#define WS2812_PIN_2            38     // YD-ESP32-S3 / clone DevKits
#define WS2812_PIN_3            21     // Waveshare ESP32-S3 Zero, SuperMini

// ==============================================================================
// Timing & Operational Settings
// ==============================================================================
#define BLE_SCAN_INTERVAL_MS    100    // BLE scan interval
#define BLE_SCAN_WINDOW_MS       99    // BLE scan window
#define SCAN_TIMEOUT_SEC         30    // Scan cycle timeout
#define PAIR_TIMEOUT_MS       25000    // Max time to wait for pairing confirmation
#define CMD_SETTLE_DELAY_MS     250    // Delay between GATT writes
