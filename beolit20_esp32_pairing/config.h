#pragma once

#include <Arduino.h>

// ==============================================================================
// Bang & Olufsen Beolit 20 Speaker Configuration
// 
// IMPORTANT: YOU MUST MODIFY THESE MAC ADDRESSES BEFORE FLASHING!
// Replace these placeholder MACs with your two Beolit 20 speakers' actual BLE MACs.
// Use BLE scanner apps (e.g. nRF Connect or LightBlue) to locate your speakers.
// ==============================================================================

// Speaker 1: Master Speaker (Default Left channel in Stereo mode)
#define SPEAKER_MASTER_MAC      "34:81:f4:5a:21:c0"  // <-- REPLACE WITH YOUR MASTER SPEAKER MAC

// Speaker 2: Slave Speaker (Default Right channel in Stereo mode)
#define SPEAKER_SLAVE_MAC       "34:81:f4:9b:4e:82"  // <-- REPLACE WITH YOUR SLAVE SPEAKER MAC

// ==============================================================================
// B&O BLE GATT Services and Characteristics
// ==============================================================================

// Primary B&O Service (Assigned 16-bit UUID 0xFE89)
#define BO_SERVICE_UUID         "fe89"

// TWS Control & Status Characteristic (Handle 0x0027, CCCD Handle 0x0028)
// Little-endian wire bytes: af 4c 3a 03 25 62 d0 a2 ec 4e a7 d2 c2 e4 b5 d5
#define BO_CHAR_UUID_TWS        "d5b5e4c2-d2a7-4eec-a2d0-6225033a4caf"

// Session / Control Enable Characteristic (Handle 0x002D)
// Little-endian wire bytes: fb fc 93 a7 36 49 4e 9d 27 4c 08 8b 2e 1c a9 3b
#define BO_CHAR_UUID_SESSION    "3ba91c2e-8b08-4c27-9d4e-4936a793fcfb"

// ==============================================================================
// Protocol Commands & Status Values
// ==============================================================================
#define CMD_TWS_RESET           0x00   // Resets/prepares TWS state
#define CMD_TWS_ROLE_MASTER     0x01   // Assigns Master role
#define CMD_TWS_ROLE_SLAVE      0x02   // Assigns Slave role

// Stereo & Sync Modes (Written as 2 bytes: 0x04 [mode_code] to Master)
#define MODE_CODE_STEREO_LR     0x01   // Master = Left, Slave = Right
#define MODE_CODE_STEREO_RL     0x02   // Master = Right, Slave = Left (Swapped)
#define MODE_CODE_SYNCED        0x00   // Both Synced / Ambient / Party Mode

#define STATUS_TWS_PAIRED_M     0x04   // Master paired successfully notification
#define STATUS_TWS_PAIRED_S     0x05   // Slave paired successfully notification

// ==============================================================================
// Hardware Inputs & Onboard LED Configuration
// ==============================================================================
#define BOOT_BUTTON_PIN         0      // ESP32-S3 BOOT button (active LOW)

#define ENABLE_STATUS_LED       true
#define LED_BRIGHTNESS          20     // Soft, comfortable brightness (0-255)

// Pin definitions across all ESP32-S3 board variants
#define WS2812_PIN_1            48     // Official Espressif ESP32-S3 DevKitC-1
#define WS2812_PIN_2            38     // Many YD-ESP32-S3 and DevKit clones
#define WS2812_PIN_3            21     // Waveshare ESP32-S3 Zero, SuperMini, StampS3

#define SINGLE_LED_PIN_1        2      // Common single LED
#define SINGLE_LED_PIN_2        8      // SuperMini single LED

// ==============================================================================
// Timing & Operational Settings
// ==============================================================================
#define BLE_SCAN_INTERVAL_MS    100    // BLE scan interval
#define BLE_SCAN_WINDOW_MS       99    // BLE scan window
#define SCAN_TIMEOUT_SEC         30    // Scan cycle timeout before clearing stale entries

#define PAIR_TIMEOUT_MS       25000    // Max time to wait for pairing confirmation
#define CMD_SETTLE_DELAY_MS     250    // Delay between GATT writes
