#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// State Machine Definitions
enum SystemState {
    STATE_BOOT_SCAN,
    STATE_CHECK_ONCE,
    STATE_SCAN_FOR_SLAVE,
    STATE_PAIR_CONNECT_SLAVE,
    STATE_PAIR_CONFIGURE,
    STATE_PAIR_SEND_COMMANDS,
    STATE_PAIR_WAIT_CONFIRM,
    STATE_PAIR_APPLY_MODE,
    STATE_SHOW_SUCCESS,
    STATE_STANDBY_IDLE,
    STATE_SYNC_MODE
};

enum StereoMode {
    MODE_STEREO_LR = 0, // Master = Left, Slave = Right (0x04 0x01)
    MODE_STEREO_RL = 1, // Master = Right, Slave = Left (0x04 0x02)
    MODE_SYNCED    = 2  // Synced / Ambient Mode (0x04 0x00)
};

static SystemState currentState = STATE_BOOT_SCAN;
static StereoMode  currentMode  = MODE_STEREO_LR;

// Target Device Addresses (Pre-initialized with PUBLIC address type - never uninitialized)
static NimBLEAddress addrMaster(SPEAKER_MASTER_MAC, BLE_ADDR_PUBLIC);
static NimBLEAddress addrSlave(SPEAKER_SLAVE_MAC, BLE_ADDR_PUBLIC);
static bool foundMaster = false;
static bool foundSlave  = false;

// Static BLE Clients (Allocated once, reused forever)
static NimBLEClient* clientMaster = nullptr;
static NimBLEClient* clientSlave  = nullptr;

static NimBLERemoteCharacteristic* charTwsMaster     = nullptr;
static NimBLERemoteCharacteristic* charSessionMaster = nullptr;
static NimBLERemoteCharacteristic* charTwsSlave      = nullptr;
static NimBLERemoteCharacteristic* charSessionSlave  = nullptr;

// Tracking Flags & Timers
static bool masterPairedNotification = false;
static bool slavePairedNotification  = false;
static unsigned long stateTimer      = 0;

// BOOT Button Interrupt Flag (GPIO 0)
static volatile bool bootButtonPressed = false;
static unsigned long lastInterruptTime = 0;
static bool buttonPulseActive          = false;
static unsigned long buttonPulseStart  = 0;

// Onboard WS2812 RGB LED (Adafruit NeoPixel driver with hardware RMT)
static Adafruit_NeoPixel rgbLed(1, WS2812_PIN_1, NEO_GRB + NEO_KHZ800);

// Forward Declarations
void IRAM_ATTR onBootButtonISR();
void onMasterNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);
void onSlaveNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);
void cleanDisconnect();
void setStatusColor(uint8_t r, uint8_t g, uint8_t b);
void setFadingColor(uint8_t baseR, uint8_t baseG, uint8_t baseB, unsigned long periodMs);
void updateLedStatus();
void handleBootButton();
void printCurrentMode();

// ==============================================================================
// BOOT Button Hardware Interrupt (GPIO 0)
// ==============================================================================
void IRAM_ATTR onBootButtonISR() {
    unsigned long now = millis();
    if (now - lastInterruptTime > 300) { // 300ms software debounce
        bootButtonPressed = true;
        lastInterruptTime = now;
    }
}

// ==============================================================================
// LED Driver - Dedicated Adafruit NeoPixel Driver with Soft Logarithmic Curve
// ==============================================================================
void setStatusColor(uint8_t r, uint8_t g, uint8_t b) {
#if ENABLE_STATUS_LED
    static uint8_t lastR = 255, lastG = 255, lastB = 255;
    // Avoid redundant writes to prevent bus contention
    if (r == lastR && g == lastG && b == lastB) {
        return;
    }
    lastR = r; lastG = g; lastB = b;
    rgbLed.setPixelColor(0, r, g, b);
    rgbLed.show();
#endif
}

// Smooth Logarithmic Fading (Weber-Fechner / Perceptual Gamma 2.6 curve)
void setFadingColor(uint8_t baseR, uint8_t baseG, uint8_t baseB, unsigned long periodMs) {
#if ENABLE_STATUS_LED
    unsigned long now = millis();
    float phase = (float)(now % periodMs) / (float)periodMs;
    // Cosine breathing wave from 0.0 -> 1.0 -> 0.0
    float wave = 0.5f * (1.0f - cosf(phase * 2.0f * PI));
    // Logarithmic lightness perception scaling
    float logBrightness = powf(wave, 2.6f);

    uint8_t r = (uint8_t)(baseR * logBrightness);
    uint8_t g = (uint8_t)(baseG * logBrightness);
    uint8_t b = (uint8_t)(baseB * logBrightness);

    setStatusColor(r, g, b);
#endif
}

void updateLedStatus() {
#if ENABLE_STATUS_LED
    static unsigned long lastLedUpdate = 0;
    unsigned long now = millis();
    // Rate limit to 33 FPS (every 30ms) to ensure strict WS2812 latch timings
    // and eliminate any RMT timing glitches with BLE interrupts
    if (now - lastLedUpdate < 30) {
        return;
    }
    lastLedUpdate = now;

    // Handle non-blocking mode change pulse animation
    if (buttonPulseActive) {
        unsigned long elapsed = now - buttonPulseStart;
        if (elapsed < 600) {
            // 2 smooth pulses over 600ms (300ms per pulse)
            float phase = (float)(elapsed % 300) / 300.0f;
            float factor = powf(sinf(phase * PI), 2.6f);

            uint8_t mr = 0, mg = 0, mb = 0;
            if (currentMode == MODE_STEREO_LR) {
                mb = LED_BRIGHTNESS; mg = LED_BRIGHTNESS; // Cyan (L/R)
            } else if (currentMode == MODE_STEREO_RL) {
                mr = LED_BRIGHTNESS; mb = LED_BRIGHTNESS; // Magenta (R/L)
            } else {
                mr = LED_BRIGHTNESS; mg = LED_BRIGHTNESS / 2; // Amber (Synced)
            }

            setStatusColor((uint8_t)(mr * factor), (uint8_t)(mg * factor), (uint8_t)(mb * factor));
            return;
        } else {
            buttonPulseActive = false;
            setStatusColor(0, 0, 0);
        }
    }

    switch (currentState) {
        case STATE_BOOT_SCAN:
        case STATE_SCAN_FOR_SLAVE:
            // Smooth Logarithmic Breathing Blue (2.4s cycle)
            setFadingColor(0, 0, LED_BRIGHTNESS, 2400);
            break;

        case STATE_CHECK_ONCE:
        case STATE_PAIR_CONNECT_SLAVE:
        case STATE_PAIR_CONFIGURE:
        case STATE_SYNC_MODE:
            // Smooth Logarithmic Breathing Amber (1.4s cycle)
            setFadingColor(LED_BRIGHTNESS, LED_BRIGHTNESS / 3, 0, 1400);
            break;

        case STATE_PAIR_SEND_COMMANDS:
        case STATE_PAIR_WAIT_CONFIRM:
            // Smooth Logarithmic Breathing Purple (800ms cycle)
            setFadingColor(LED_BRIGHTNESS, 0, LED_BRIGHTNESS, 800);
            break;

        case STATE_SHOW_SUCCESS: {
            // Smooth Logarithmic Fade In -> Hold -> Fade Out to OFF over 3 seconds
            unsigned long elapsed = millis() - stateTimer;
            if (elapsed < 3000) {
                float factor;
                if (elapsed < 800) {
                    float x = (float)elapsed / 800.0f;
                    factor = powf(x, 2.6f);
                } else if (elapsed < 2200) {
                    factor = 1.0f;
                } else {
                    float x = (float)(3000 - elapsed) / 800.0f;
                    factor = powf(x, 2.6f);
                }
                uint8_t g = (uint8_t)(LED_BRIGHTNESS * factor);
                setStatusColor(0, g, 0);
            } else {
                setStatusColor(0, 0, 0);
                currentState = STATE_STANDBY_IDLE;
            }
            break;
        }

        case STATE_STANDBY_IDLE:
            // In standby: LED is completely dark. Zero flashing!
            setStatusColor(0, 0, 0);
            break;

        default:
            break;
    }
#endif
}

// ==============================================================================
// BOOT Button Press Handler (Fully Non-blocking)
// ==============================================================================
void handleBootButton() {
    if (!bootButtonPressed) return;
    bootButtonPressed = false;

    // Cycle through: Stereo L/R -> Stereo R/L -> Synced
    currentMode = (StereoMode)((currentMode + 1) % 3);
    printCurrentMode();

    // Trigger smooth non-blocking visual pulse in updateLedStatus()
    buttonPulseActive = true;
    buttonPulseStart  = millis();
    currentState      = STATE_SYNC_MODE;
}

void printCurrentMode() {
    Serial.println("\n=======================================================");
    switch (currentMode) {
        case MODE_STEREO_LR:
            Serial.println("  [MODE] STEREO (Left / Right):");
            Serial.println("         Speaker 1 (Master) = LEFT channel");
            Serial.println("         Speaker 2 (Slave)  = RIGHT channel");
            break;
        case MODE_STEREO_RL:
            Serial.println("  [MODE] STEREO (Right / Left - Inverted):");
            Serial.println("         Speaker 1 (Master) = RIGHT channel");
            Serial.println("         Speaker 2 (Slave)  = LEFT channel");
            break;
        case MODE_SYNCED:
            Serial.println("  [MODE] SYNCED (Ambient / Dual Sync):");
            Serial.println("         Both speakers playing synchronized dual-mono");
            break;
    }
    Serial.println("=======================================================");
}

// ==============================================================================
// Advertised Device Callbacks
// ==============================================================================
class ScanCallbacks : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice* advertisedDevice) override {
        NimBLEAddress addr = advertisedDevice->getAddress();
        std::string addrStr = addr.toString();

        if (strcasecmp(addrStr.c_str(), SPEAKER_MASTER_MAC) == 0) {
            foundMaster = true;
        } else if (strcasecmp(addrStr.c_str(), SPEAKER_SLAVE_MAC) == 0) {
            foundSlave = true;
        }
    }
};

// ==============================================================================
// Notification Callbacks
// ==============================================================================
void onMasterNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("[NOTIFY][Master] ");
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    Serial.println();

    if (length > 0 && pData[0] == STATUS_TWS_PAIRED_M) {
        masterPairedNotification = true;
        Serial.println("[STATUS] >>> Master Confirmed Paired! (0x04) <<<");
    }
}

void onSlaveNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("[NOTIFY][Slave]  ");
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    Serial.println();

    if (length > 0 && pData[0] == STATUS_TWS_PAIRED_S) {
        slavePairedNotification = true;
        Serial.println("[STATUS] >>> Slave Confirmed Paired! (0x05) <<<");
    }
}

// ==============================================================================
// Setup & Main Loop
// ==============================================================================
void setup() {
    Serial.begin(115200);

    // Initialize BOOT button on GPIO 0 with pullup & hardware interrupt
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON_PIN), onBootButtonISR, FALLING);

#if ENABLE_STATUS_LED
    rgbLed.begin();
#endif
    // Initial state: starts completely dark
    setStatusColor(0, 0, 0);

    Serial.println("\n=======================================================");
    Serial.println("  ESP32-S3 Bang & Olufsen Beolit 20 Stereo Auto-Pairer ");
    Serial.println("=======================================================");
    Serial.printf("Master Speaker MAC : %s\n", SPEAKER_MASTER_MAC);
    Serial.printf("Slave Speaker MAC  : %s\n", SPEAKER_SLAVE_MAC);
    Serial.printf("B&O Service UUID   : %s\n", BO_SERVICE_UUID);
    Serial.printf("TWS Char UUID      : %s\n", BO_CHAR_UUID_TWS);
    Serial.println("BOOT Button (GPIO 0): Press anytime to cycle modes");
    printCurrentMode();

    NimBLEDevice::init("ESP32-BOGattPairer");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum TX power

    clientMaster = NimBLEDevice::createClient();
    clientSlave  = NimBLEDevice::createClient();

    clientMaster->setConnectTimeout(3000); // 3s connect timeout
    clientSlave->setConnectTimeout(3000);

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new ScanCallbacks(), false);
    pScan->setInterval(BLE_SCAN_INTERVAL_MS);
    pScan->setWindow(BLE_SCAN_WINDOW_MS);
    pScan->setActiveScan(true);

    Serial.println("[BLE] Starting BLE Scan for Beolit 20 speakers...");
    pScan->start(0, false);

    currentState = STATE_BOOT_SCAN;
    stateTimer = millis();
}

void loop() {
    yield();
    handleBootButton();
    updateLedStatus();

    switch (currentState) {
        case STATE_BOOT_SCAN: {
            // As soon as Master is discovered, check state ONCE
            if (foundMaster) {
                Serial.println("\n[SCAN] Master speaker found! Connecting to check state ONCE...");
                NimBLEDevice::getScan()->stop();
                delay(50);
                currentState = STATE_CHECK_ONCE;
                stateTimer = millis();
            } else {
                if (millis() - stateTimer > (SCAN_TIMEOUT_SEC * 1000UL)) {
                    Serial.println("[SCAN] Searching for Master speaker...");
                    stateTimer = millis();
                }
                delay(15);
            }
            break;
        }

        case STATE_CHECK_ONCE: {
            Serial.println("[CHECK] Connecting to Master Speaker...");
            if (!clientMaster->connect(addrMaster)) {
                Serial.println("[ERROR] Failed to connect to Master. Retrying scan...");
                foundMaster = false;
                currentState = STATE_BOOT_SCAN;
                NimBLEDevice::getScan()->start(0, false);
                break;
            }

            Serial.println("[CHECK] Master connected! Discovering B&O service...");
            NimBLERemoteService* srvMaster = clientMaster->getService(NimBLEUUID(BO_SERVICE_UUID));
            if (!srvMaster) {
                Serial.println("[ERROR] Service not found on Master. Disconnecting...");
                cleanDisconnect();
                currentState = STATE_BOOT_SCAN;
                NimBLEDevice::getScan()->start(0, false);
                break;
            }

            charTwsMaster = srvMaster->getCharacteristic(NimBLEUUID(BO_CHAR_UUID_TWS));
            charSessionMaster = srvMaster->getCharacteristic(NimBLEUUID(BO_CHAR_UUID_SESSION));

            if (!charTwsMaster) {
                Serial.println("[ERROR] TWS Characteristic not found on Master. Disconnecting...");
                cleanDisconnect();
                currentState = STATE_BOOT_SCAN;
                NimBLEDevice::getScan()->start(0, false);
                break;
            }

            // Read the TWS State ONCE
            Serial.println("[CHECK] Reading TWS state from Master...");
            NimBLEAttValue val = charTwsMaster->readValue();

            if (val.size() > 0) {
                uint8_t stateByte = val.data()[0];
                Serial.printf("[CHECK] Master TWS State = 0x%02X\n", stateByte);

                // If State is 0x04 (Master Paired) or 0x05 (Paired)
                if (stateByte == STATUS_TWS_PAIRED_M || stateByte == STATUS_TWS_PAIRED_S) {
                    Serial.println("\n=======================================================");
                    Serial.println("  >>> SPEAKERS ARE ALREADY PAIRED IN STEREO! <<<       ");
                    Serial.println("  State 0x04 verified. No pairing sequence needed.     ");
                    Serial.println("  Music playback & phone link remain 100% untouched!   ");
                    Serial.println("  Entering Standby.                                    ");
                    Serial.println("=======================================================");

                    clientMaster->disconnect();

                    // Smooth logarithmic Green fade in -> hold -> fade out, then standby
                    currentState = STATE_SHOW_SUCCESS;
                    stateTimer = millis();
                    break;
                } else {
                    Serial.printf("[CHECK] Speakers NOT paired (State: 0x%02X). Needs pairing!\n", stateByte);
                }
            } else {
                Serial.println("[WARN] Empty response from TWS characteristic. Will pair.");
            }

            if (foundSlave) {
                currentState = STATE_PAIR_CONNECT_SLAVE;
            } else {
                Serial.println("[PAIR] Scanning for Slave Speaker...");
                currentState = STATE_SCAN_FOR_SLAVE;
                stateTimer = millis();
                NimBLEDevice::getScan()->start(0, false);
            }
            break;
        }

        case STATE_SCAN_FOR_SLAVE: {
            if (foundSlave) {
                Serial.println("[SCAN] Slave speaker found! Connecting Slave...");
                NimBLEDevice::getScan()->stop();
                delay(50);
                currentState = STATE_PAIR_CONNECT_SLAVE;
            } else {
                if (millis() - stateTimer > (SCAN_TIMEOUT_SEC * 1000UL)) {
                    Serial.println("[WARN] Timeout waiting for Slave. Resetting...");
                    cleanDisconnect();
                    currentState = STATE_BOOT_SCAN;
                    foundMaster = false;
                    NimBLEDevice::getScan()->start(0, false);
                }
                delay(15);
            }
            break;
        }

        case STATE_PAIR_CONNECT_SLAVE: {
            Serial.println("[PAIR] Connecting to Slave Speaker...");
            if (!clientSlave->connect(addrSlave)) {
                Serial.println("[ERROR] Failed to connect to Slave. Resetting...");
                cleanDisconnect();
                currentState = STATE_BOOT_SCAN;
                foundMaster = false;
                foundSlave  = false;
                NimBLEDevice::getScan()->start(0, false);
                break;
            }

            Serial.println("[PAIR] Slave connected! Discovering services on Slave...");
            NimBLERemoteService* srvSlave = clientSlave->getService(NimBLEUUID(BO_SERVICE_UUID));
            if (!srvSlave) {
                Serial.println("[ERROR] Service not found on Slave. Resetting...");
                cleanDisconnect();
                currentState = STATE_BOOT_SCAN;
                NimBLEDevice::getScan()->start(0, false);
                break;
            }

            charTwsSlave = srvSlave->getCharacteristic(NimBLEUUID(BO_CHAR_UUID_TWS));
            charSessionSlave = srvSlave->getCharacteristic(NimBLEUUID(BO_CHAR_UUID_SESSION));

            if (!charTwsSlave) {
                Serial.println("[ERROR] TWS Characteristic not found on Slave. Resetting...");
                cleanDisconnect();
                currentState = STATE_BOOT_SCAN;
                NimBLEDevice::getScan()->start(0, false);
                break;
            }

            currentState = STATE_PAIR_CONFIGURE;
            break;
        }

        case STATE_PAIR_CONFIGURE: {
            Serial.println("[GATT] Subscribing to TWS notifications on Master and Slave...");
            charTwsMaster->subscribe(true, onMasterNotify, true);
            charTwsSlave->subscribe(true, onSlaveNotify, true);

            if (charSessionMaster && charSessionMaster->canWrite()) {
                uint8_t unlockVal = 0x01;
                charSessionMaster->writeValue(&unlockVal, 1, true);
            }
            if (charSessionSlave && charSessionSlave->canWrite()) {
                uint8_t unlockVal = 0x01;
                charSessionSlave->writeValue(&unlockVal, 1, true);
            }

            delay(CMD_SETTLE_DELAY_MS);
            currentState = STATE_PAIR_SEND_COMMANDS;
            break;
        }

        case STATE_PAIR_SEND_COMMANDS: {
            Serial.println("\n[PAIR] Step 1: Sending TWS Reset (0x00)...");
            uint8_t resetCmd = CMD_TWS_RESET;
            if (charTwsMaster->canWrite()) charTwsMaster->writeValue(&resetCmd, 1, true);
            if (charTwsSlave->canWrite())  charTwsSlave->writeValue(&resetCmd, 1, true);

            delay(CMD_SETTLE_DELAY_MS);

            Serial.println("[PAIR] Step 2: Assigning Roles (Master=0x01, Slave=0x02)...");
            uint8_t masterRole = CMD_TWS_ROLE_MASTER; // 0x01
            uint8_t slaveRole  = CMD_TWS_ROLE_SLAVE;  // 0x02

            if (charTwsMaster->canWrite()) charTwsMaster->writeValue(&masterRole, 1, true);
            if (charTwsSlave->canWrite())  charTwsSlave->writeValue(&slaveRole, 1, true);

            Serial.println("[PAIR] Roles assigned. Waiting for pairing confirmation...");
            masterPairedNotification = false;
            slavePairedNotification  = false;
            stateTimer = millis();
            currentState = STATE_PAIR_WAIT_CONFIRM;
            break;
        }

        case STATE_PAIR_WAIT_CONFIRM: {
            if (masterPairedNotification && slavePairedNotification) {
                Serial.println("\n=======================================================");
                Serial.println("  SUCCESS! Both Beolit 20 Speakers Paired in Stereo!   ");
                Serial.println("=======================================================");
                currentState = STATE_PAIR_APPLY_MODE;
            } else {
                if (millis() - stateTimer > PAIR_TIMEOUT_MS) {
                    Serial.println("\n[WARN] Pairing timeout reached (25s). Proceeding.");
                    currentState = STATE_PAIR_APPLY_MODE;
                }
                delay(15);
            }
            break;
        }

        case STATE_PAIR_APPLY_MODE: {
            uint8_t modeCode = (currentMode == MODE_STEREO_LR) ? MODE_CODE_STEREO_LR :
                               (currentMode == MODE_STEREO_RL) ? MODE_CODE_STEREO_RL :
                                                                MODE_CODE_SYNCED;

            Serial.printf("[MODE] Writing mode [0x04, 0x%02X] to Master...\n", modeCode);
            uint8_t modePayload[2] = { 0x04, modeCode };

            if (charTwsMaster && charTwsMaster->canWrite()) {
                charTwsMaster->writeValue(modePayload, 2, true);
            }

            delay(CMD_SETTLE_DELAY_MS);
            cleanDisconnect();
            currentState = STATE_SHOW_SUCCESS;
            stateTimer = millis();
            break;
        }

        case STATE_SHOW_SUCCESS: {
            cleanDisconnect();
            // The logarithmic fade in -> hold -> fade out is handled smoothly in updateLedStatus()
            delay(15);
            break;
        }

        case STATE_STANDBY_IDLE: {
            // Completely dark & quiet. Waiting for BOOT button interrupt.
            delay(50);
            break;
        }

        case STATE_SYNC_MODE: {
            // Wait for smooth visual pulse feedback to finish before BLE connect
            if (buttonPulseActive) {
                delay(10);
                break;
            }

            // Non-blocking mode update triggered by BOOT button
            Serial.println("[BUTTON] Connecting to Master to apply new mode...");
            if (clientMaster->connect(addrMaster)) {
                NimBLERemoteService* srv = clientMaster->getService(NimBLEUUID(BO_SERVICE_UUID));
                if (srv) {
                    NimBLERemoteCharacteristic* chTws = srv->getCharacteristic(NimBLEUUID(BO_CHAR_UUID_TWS));
                    if (chTws && chTws->canWrite()) {
                        uint8_t modeCode = (currentMode == MODE_STEREO_LR) ? MODE_CODE_STEREO_LR :
                                           (currentMode == MODE_STEREO_RL) ? MODE_CODE_STEREO_RL :
                                                                            MODE_CODE_SYNCED;
                        uint8_t payload[2] = { 0x04, modeCode };
                        chTws->writeValue(payload, 2, true);
                        Serial.printf("[BUTTON] Successfully wrote mode [0x04, 0x%02X] to Master!\n", modeCode);
                    }
                }
                clientMaster->disconnect();
            } else {
                Serial.println("[BUTTON] Master speaker not reachable right now.");
            }

            // Immediately back to standby with LED OFF
            setStatusColor(0, 0, 0);
            currentState = STATE_STANDBY_IDLE;
            break;
        }

        default:
            break;
    }
}

// ==============================================================================
// Clean Disconnect Helper
// ==============================================================================
void cleanDisconnect() {
    if (clientMaster && clientMaster->isConnected()) {
        clientMaster->disconnect();
    }
    if (clientSlave && clientSlave->isConnected()) {
        clientSlave->disconnect();
    }
}
