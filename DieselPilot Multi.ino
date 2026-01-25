/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                  DIESEL PILOT MULTI-HEATER EDITION v2.0
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * Erweiterte Version für bis zu 3 Dieselheizungen
 * 
 * Features:
 * - 3 Heizungen gleichzeitig steuern
 * - Round-Robin RF Polling
 * - Multi-Screen OLED Display
 * - Web GUI für alle Heizungen
 * - MQTT Integration (Home Assistant ready)
 * - Fehlercode-Dekodierung für jede Heizung
 * - NVS Speicher pro Heizung
 * 
 * Hardware:
 * - ESP32
 * - CC1101 @ 433.937 MHz
 * - SH1106 OLED (I2C)
 * 
 * Basiert auf: DieselPilot v1.2 
 * Erweitert für Multi-Heater Support
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ArduinoOTA.h>

// ═══════════════════════════════════════════════════════════════════════════
// MULTI-HEATER CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define NUM_HEATERS 3           // Anzahl der Heizungen
#define RF_POLL_INTERVAL 3000   // ms zwischen Status-Abfragen
#define HEATER_TIMEOUT 30000    // Offline nach 30 Sekunden

// ═══════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIG
// ═══════════════════════════════════════════════════════════════════════════

// CC1101 Pins
#define PIN_SCK   18
#define PIN_MISO  19
#define PIN_MOSI  23
#define PIN_SS    5
#define PIN_GDO2  4

// I2C Pins (OLED)
#define PIN_SDA   21
#define PIN_SCL   22

// OLED Configuration
#define USE_OLED  true

// Commands
#define CMD_WAKEUP 0x23
#define CMD_MODE   0x24
#define CMD_POWER  0x2B
#define CMD_UP     0x3C
#define CMD_DOWN   0x3E

// Heater States
#define STATE_OFF            0
#define STATE_STARTUP        1
#define STATE_WARMING        2
#define STATE_WARMING_WAIT   3
#define STATE_PRE_RUN        4
#define STATE_RUNNING        5
#define STATE_SHUTDOWN       6
#define STATE_SHUTTING_DOWN  7
#define STATE_COOLING        8

// Error Codes
#define ERR_NONE           0x00
#define ERR_ON             0x01
#define ERR_UNDERVOLTAGE   0x02
#define ERR_OVERVOLTAGE    0x03
#define ERR_SPARK_PLUG     0x04
#define ERR_OIL_PUMP       0x05
#define ERR_OVERHEAT       0x06
#define ERR_MOTOR          0x07
#define ERR_DISCONNECT     0x08
#define ERR_EXTINGUISHED   0x09
#define ERR_SENSOR         0x0A
#define ERR_IGNITION       0x0B
#define ERR_STANDBY        0x0C

// ═══════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════

struct HeaterData {
    // Configuration
    char name[32];
    uint32_t address;
    bool isPaired;
    bool enabled;
    uint8_t packetSeq;
    
    // Status
    uint8_t state;
    uint8_t power;
    uint16_t voltage;
    int8_t ambientTemp;
    uint8_t caseTemp;
    int8_t setpoint;
    uint8_t pumpFreq;
    bool autoMode;
    int16_t rssi;
    uint8_t errorCode;
    unsigned long lastUpdate;
    
    // MQTT
    char mqttPrefix[64];
};

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

WebServer server(80);
Preferences prefs;
WiFiClient espClient;
PubSubClient mqtt(espClient);
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

String version = "2.0-MULTI";

// Multi-Heater Data
HeaterData heaters[NUM_HEATERS];
uint8_t currentPollHeater = 0;
uint8_t displayScreen = 0;  // 0=overview, 1-3=detail
unsigned long lastRFPoll = 0;
unsigned long lastDisplayRotate = 0;

// WiFi
String apSSID = "DieselPilot-Multi";
String apPassword = "12345678";
String staSSID = "";
String staPassword = "";
bool useAP = true;

// MQTT
String deviceName = "DieselPilot-Multi";
String mqttServer = "";
int mqttPort = 1883;
String mqttTopic = "diesel";
String mqttUser = "";
String mqttPassword = "";
bool mqttAuthEnabled = false;
bool mqttEnabled = false;

// OTA
String otaPassword = "dieselpilot";
bool otaEnabled = true;

// Display
String displayLine1 = "Diesel Pilot";
String displayLine2 = "Multi-Heater";
String displayLine3 = "Initialisieren...";
String displayLine4 = "";

// Timing
unsigned long lastMQTTRetry = 0;
const unsigned long mqttRetryInterval = 30000;

// ═══════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

const char* getErrorName(uint8_t code) {
    switch(code) {
        case ERR_NONE:         return "OK";
        case ERR_ON:           return "NORMAL";
        case ERR_UNDERVOLTAGE: return "UNTERSPANNUNG";
        case ERR_OVERVOLTAGE:  return "UEBERSPANNUNG";
        case ERR_SPARK_PLUG:   return "GLUEHKERZE";
        case ERR_OIL_PUMP:     return "OEL PUMPE";
        case ERR_OVERHEAT:     return "UEBERHITZUNG";
        case ERR_MOTOR:        return "MOTOR";
        case ERR_DISCONNECT:   return "DISCONNECT";
        case ERR_EXTINGUISHED: return "FLAMME AUS";
        case ERR_SENSOR:       return "SENSOR";
        case ERR_IGNITION:     return "ZUENDUNG";
        case ERR_STANDBY:      return "STANDBY";
        default:               return "UNBEKANNT";
    }
}

const char* getStateName(uint8_t state) {
    switch(state) {
        case STATE_OFF: return "OFF";
        case STATE_STARTUP: return "STARTEN";
        case STATE_WARMING: return "AUFWAERMEN";
        case STATE_WARMING_WAIT: return "WARM WARTEN";
        case STATE_PRE_RUN: return "PRE-RUN";
        case STATE_RUNNING: return "RUNNING";
        case STATE_SHUTDOWN: return "HERUNTERFAHREN";
        case STATE_SHUTTING_DOWN: return "SHUTTING";
        case STATE_COOLING: return "COOLING";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CC1101 LOW-LEVEL FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void cc1101_writeReg(uint8_t addr, uint8_t val) {
    digitalWrite(PIN_SS, LOW);
    while(digitalRead(PIN_MISO));
    SPI.transfer(addr);
    SPI.transfer(val);
    digitalWrite(PIN_SS, HIGH);
}

void cc1101_writeBurst(uint8_t addr, uint8_t len, uint8_t* bytes) {
    digitalWrite(PIN_SS, LOW);
    while(digitalRead(PIN_MISO));
    SPI.transfer(addr);
    for(int i = 0; i < len; i++) {
        SPI.transfer(bytes[i]);
    }
    digitalWrite(PIN_SS, HIGH);
}

void cc1101_strobe(uint8_t addr) {
    digitalWrite(PIN_SS, LOW);
    while(digitalRead(PIN_MISO));
    SPI.transfer(addr);
    digitalWrite(PIN_SS, HIGH);
}

uint8_t cc1101_readReg(uint8_t addr) {
    digitalWrite(PIN_SS, LOW);
    while(digitalRead(PIN_MISO));
    SPI.transfer(addr);
    uint8_t val = SPI.transfer(0xFF);
    digitalWrite(PIN_SS, HIGH);
    return val;
}

// ═══════════════════════════════════════════════════════════════════════════
// CRC-16/MODBUS
// ═══════════════════════════════════════════════════════════════════════════

uint16_t crc16_modbus(uint8_t* buf, int len) {
    uint16_t crc = 0xFFFF;
    for(int pos = 0; pos < len; pos++) {
        crc ^= (uint8_t)buf[pos];
        for(int i = 8; i != 0; i--) {
            if((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ═══════════════════════════════════════════════════════════════════════════
// CC1101 INIT
// ═══════════════════════════════════════════════════════════════════════════

void cc1101_init() {
    cc1101_strobe(0x30); // SRES
    delay(100);
    
    cc1101_writeReg(0x00, 0x07);
    cc1101_writeReg(0x02, 0x06);
    cc1101_writeReg(0x03, 0x47);
    cc1101_writeReg(0x07, 0x04);
    cc1101_writeReg(0x08, 0x05);
    cc1101_writeReg(0x0A, 0x00);
    cc1101_writeReg(0x0B, 0x06);
    cc1101_writeReg(0x0C, 0x00);
    
    // 433.937 MHz
    cc1101_writeReg(0x0D, 0x10);
    cc1101_writeReg(0x0E, 0xB0);
    cc1101_writeReg(0x0F, 0x8A);
    
    cc1101_writeReg(0x10, 0xF8);
    cc1101_writeReg(0x11, 0x93);
    cc1101_writeReg(0x12, 0x13);
    cc1101_writeReg(0x13, 0x22);
    cc1101_writeReg(0x14, 0xF8);
    cc1101_writeReg(0x15, 0x26);
    cc1101_writeReg(0x17, 0x30);
    cc1101_writeReg(0x18, 0x18);
    cc1101_writeReg(0x19, 0x16);
    cc1101_writeReg(0x1A, 0x6C);
    cc1101_writeReg(0x1B, 0x03);
    cc1101_writeReg(0x1C, 0x40);
    cc1101_writeReg(0x1D, 0x91);
    cc1101_writeReg(0x20, 0xFB);
    cc1101_writeReg(0x21, 0x56);
    cc1101_writeReg(0x22, 0x17);
    cc1101_writeReg(0x23, 0xE9);
    cc1101_writeReg(0x24, 0x2A);
    cc1101_writeReg(0x25, 0x00);
    cc1101_writeReg(0x26, 0x1F);
    cc1101_writeReg(0x2C, 0x81);
    cc1101_writeReg(0x2D, 0x35);
    cc1101_writeReg(0x2E, 0x09);
    cc1101_writeReg(0x09, 0x00);
    cc1101_writeReg(0x04, 0x7E);
    cc1101_writeReg(0x05, 0x3C);
    
    uint8_t paTable[8] = {0x00, 0x12, 0x0E, 0x34, 0x60, 0xC5, 0xC1, 0xC0};
    cc1101_writeBurst(0x7E, 8, paTable);
    
    cc1101_strobe(0x31);
    cc1101_strobe(0x36);
    cc1101_strobe(0x3B);
    cc1101_strobe(0x36);
    cc1101_strobe(0x3A);
    delay(136);
    
    Serial.println("✅ CC1101 initialized");
}

// ═══════════════════════════════════════════════════════════════════════════
// TX/RX FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void txFlush() {
    cc1101_strobe(0x36);
    cc1101_strobe(0x3B);
    delay(16);
}

void txBurst(uint8_t len, uint8_t* bytes) {
    txFlush();
    cc1101_writeBurst(0x7F, len, bytes);
    cc1101_strobe(0x35);
}

void sendCommand(uint8_t heaterIdx, uint8_t cmd) {
    if(heaterIdx >= NUM_HEATERS || !heaters[heaterIdx].isPaired) return;
    
    uint8_t buf[10];
    buf[0] = 9;
    buf[1] = cmd;
    buf[2] = (heaters[heaterIdx].address >> 24) & 0xFF;
    buf[3] = (heaters[heaterIdx].address >> 16) & 0xFF;
    buf[4] = (heaters[heaterIdx].address >> 8) & 0xFF;
    buf[5] = heaters[heaterIdx].address & 0xFF;
    buf[6] = heaters[heaterIdx].packetSeq++;
    buf[9] = 0;
    
    uint16_t crc = crc16_modbus(buf, 7);
    buf[7] = (crc >> 8) & 0xFF;
    buf[8] = crc & 0xFF;
    
    for(int i = 0; i < 10; i++) {
        txBurst(10, buf);
        unsigned long t = millis();
        while(cc1101_readReg(0xF5) != 0x01) {
            delay(1);
            if(millis() - t > 100) return;
        }
    }
    
    Serial.printf("[HEATER %d] Command sent: 0x%02X\n", heaterIdx + 1, cmd);
}

void rxFlush() {
    cc1101_strobe(0x36);
    cc1101_readReg(0xBF);
    cc1101_strobe(0x3A);
    delay(16);
}

void rxEnable() {
    cc1101_strobe(0x34);
}

bool receivePacket(uint8_t* bytes, uint16_t timeout) {
    unsigned long t = millis();
    uint8_t rxLen;
    
    rxFlush();
    rxEnable();
    
    while(1) {
        yield();
        if(millis() - t > timeout) return false;
        
        while(!digitalRead(PIN_GDO2)) {
            yield();
            if(millis() - t > timeout) return false;
        }
        
        delay(5);
        rxLen = cc1101_readReg(0xFB);
        
        if(rxLen >= 23 && rxLen <= 26) break;
        
        rxFlush();
        rxEnable();
    }
    
    for(int i = 0; i < rxLen; i++) {
        bytes[i] = cc1101_readReg(0xBF);
    }
    
    rxFlush();
    
    uint16_t crc = crc16_modbus(bytes, 21);
    uint16_t rxCrc = (bytes[21] << 8) | bytes[22];
    
    return (crc == rxCrc);
}

// ═══════════════════════════════════════════════════════════════════════════
// MULTI-HEATER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void updateHeaterStatus(uint8_t heaterIdx) {
    if(heaterIdx >= NUM_HEATERS || !heaters[heaterIdx].isPaired || !heaters[heaterIdx].enabled) {
        return;
    }
    
    sendCommand(heaterIdx, CMD_WAKEUP);
    
    uint8_t buf[32];
    if(receivePacket(buf, 2000)) {
        uint32_t addr = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) | 
                        ((uint32_t)buf[4] << 8) | buf[5];
        
        if(addr == heaters[heaterIdx].address) {
            heaters[heaterIdx].state = buf[6];
            heaters[heaterIdx].power = buf[7];
            heaters[heaterIdx].errorCode = buf[7];
            heaters[heaterIdx].voltage = buf[9];
            heaters[heaterIdx].ambientTemp = (int8_t)buf[10];
            heaters[heaterIdx].caseTemp = buf[12];
            heaters[heaterIdx].setpoint = (int8_t)buf[13];
            heaters[heaterIdx].autoMode = (buf[14] == 0x32);
            heaters[heaterIdx].pumpFreq = buf[15];
            heaters[heaterIdx].rssi = (buf[23] - (buf[23] >= 128 ? 256 : 0)) / 2 - 74;
            heaters[heaterIdx].lastUpdate = millis();
            
            Serial.printf("[HEATER %d] Status OK - State: %s, Temp: %d°C\n", 
                         heaterIdx + 1, 
                         getStateName(heaters[heaterIdx].state),
                         heaters[heaterIdx].ambientTemp);
        }
    } else {
        Serial.printf("[HEATER %d] No response\n", heaterIdx + 1);
    }
}

void handleRFCommunication() {
    if(millis() - lastRFPoll >= RF_POLL_INTERVAL) {
        updateHeaterStatus(currentPollHeater);
        currentPollHeater = (currentPollHeater + 1) % NUM_HEATERS;
        lastRFPoll = millis();
    }
}

void monitorHeaters() {
    unsigned long now = millis();
    for(int i = 0; i < NUM_HEATERS; i++) {
        if(!heaters[i].isPaired || !heaters[i].enabled) continue;
        
        if(now - heaters[i].lastUpdate > HEATER_TIMEOUT) {
            if(heaters[i].lastUpdate > 0) {
                Serial.printf("[HEATER %d] OFFLINE!\n", i + 1);
            }
        }
    }
}

uint32_t findHeater(uint16_t timeout) {
    Serial.println("🔍 Searching for heater...");
    displayLine2 = "Pairing...";
    updateDisplay();
    
    uint8_t buf[32];
    if(receivePacket(buf, timeout)) {
        uint32_t addr = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) | 
                        ((uint32_t)buf[4] << 8) | buf[5];
        return addr;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// CONFIG STORAGE
// ═══════════════════════════════════════════════════════════════════════════

void loadHeaterConfig(uint8_t idx) {
    char key[32];
    
    sprintf(key, "h%d_addr", idx);
    heaters[idx].address = prefs.getUInt(key, 0);
    
    sprintf(key, "h%d_name", idx);
    String defaultName = "Heater " + String(idx + 1);
    prefs.getString(key, heaters[idx].name, sizeof(heaters[idx].name));
    if(strlen(heaters[idx].name) == 0) {
        defaultName.toCharArray(heaters[idx].name, sizeof(heaters[idx].name));
    }
    
    sprintf(key, "h%d_paired", idx);
    heaters[idx].isPaired = prefs.getBool(key, false);
    
    sprintf(key, "h%d_enabled", idx);
    heaters[idx].enabled = prefs.getBool(key, true);
    
    sprintf(heaters[idx].mqttPrefix, "%s/heater%d", mqttTopic.c_str(), idx + 1);
    
    heaters[idx].packetSeq = 0;
    heaters[idx].state = 0;
    heaters[idx].power = 0;
    heaters[idx].voltage = 0;
    heaters[idx].ambientTemp = 0;
    heaters[idx].caseTemp = 0;
    heaters[idx].setpoint = 0;
    heaters[idx].pumpFreq = 0;
    heaters[idx].autoMode = false;
    heaters[idx].rssi = 0;
    heaters[idx].errorCode = 0;
    heaters[idx].lastUpdate = 0;
}

void saveHeaterConfig(uint8_t idx) {
    char key[32];
    
    sprintf(key, "h%d_addr", idx);
    prefs.putUInt(key, heaters[idx].address);
    
    sprintf(key, "h%d_name", idx);
    prefs.putString(key, heaters[idx].name);
    
    sprintf(key, "h%d_paired", idx);
    prefs.putBool(key, heaters[idx].isPaired);
    
    sprintf(key, "h%d_enabled", idx);
    prefs.putBool(key, heaters[idx].enabled);
    
    Serial.printf("✅ Saved config for Heater %d\n", idx + 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void updateDisplay() {
#if USE_OLED
    display.clearBuffer();
    
    if(displayScreen == 0) {
        // Overview Screen
        display.drawFrame(0, 0, 128, 12);
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(4, 9, "DIESEL PILOT MULTI");
        
        // IP Address
        String ip = useAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        display.setFont(u8g2_font_5x8_tf);
        display.drawStr(2, 22, ip.c_str());
        
        // Heaters
        display.setFont(u8g2_font_6x10_tf);
        for(int i = 0; i < NUM_HEATERS; i++) {
            int y = 34 + (i * 10);
            char line[32];
            
            if(heaters[i].isPaired && heaters[i].enabled) {
                bool online = (millis() - heaters[i].lastUpdate < HEATER_TIMEOUT);
                sprintf(line, "%d.%-7s %s %2d%c", 
                       i + 1,
                       heaters[i].name,
                       heaters[i].state == STATE_RUNNING ? "ON " : "OFF",
                       heaters[i].ambientTemp,
                       online ? ' ' : '!');
            } else {
                sprintf(line, "%d.%-7s ---", i + 1, heaters[i].name);
            }
            
            display.drawStr(2, y, line);
        }
        
    } else {
        // Detail Screen
        uint8_t idx = displayScreen - 1;
        if(idx >= NUM_HEATERS) return;
        
        display.setFont(u8g2_font_7x13B_tf);
        char header[32];
        sprintf(header, "[%d] %s", idx + 1, heaters[idx].name);
        display.drawStr(2, 12, header);
        
        if(!heaters[idx].isPaired) {
            display.setFont(u8g2_font_6x10_tf);
            display.drawStr(2, 30, "NOT PAIRED");
            display.sendBuffer();
            return;
        }
        
        bool online = (millis() - heaters[idx].lastUpdate < HEATER_TIMEOUT);
        
        display.setFont(u8g2_font_6x10_tf);
        sprintf(header, "State: %s", online ? getStateName(heaters[idx].state) : "OFFLINE");
        display.drawStr(2, 26, header);
        
        if(heaters[idx].errorCode > 1 && heaters[idx].errorCode != 12) {
            display.drawStr(2, 38, getErrorName(heaters[idx].errorCode));
        } else {
            sprintf(header, "Temp: %d->%d C", heaters[idx].ambientTemp, heaters[idx].setpoint);
            display.drawStr(2, 38, header);
        }
        
        sprintf(header, "Pump: %.1f Hz", heaters[idx].pumpFreq / 10.0);
        display.drawStr(2, 50, header);
        
        sprintf(header, "%.1fV  %s", heaters[idx].voltage / 10.0, 
                heaters[idx].autoMode ? "AUTO" : "MAN");
        display.drawStr(2, 62, header);
    }
    
    display.sendBuffer();
#endif
}

void rotateDisplay() {
    if(millis() - lastDisplayRotate > 5000) {
        displayScreen = (displayScreen + 1) % (NUM_HEATERS + 1);
        lastDisplayRotate = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MQTT FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for(int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    String topicStr = String(topic);
    
    // Find which heater
    for(int i = 0; i < NUM_HEATERS; i++) {
        String prefix = String(heaters[i].mqttPrefix);
        if(topicStr.startsWith(prefix)) {
            if(topicStr.endsWith("/cmd/power")) {
                sendCommand(i, CMD_POWER);
            } else if(topicStr.endsWith("/cmd/up")) {
                sendCommand(i, CMD_UP);
            } else if(topicStr.endsWith("/cmd/down")) {
                sendCommand(i, CMD_DOWN);
            } else if(topicStr.endsWith("/cmd/mode")) {
                sendCommand(i, CMD_MODE);
            }
            break;
        }
    }
}

void connectMQTT() {
    if(!mqttEnabled || mqttServer.length() == 0) return;
    
    if(mqtt.connected()) {
        mqtt.disconnect();
        delay(100);
    }
    
    mqtt.setServer(mqttServer.c_str(), mqttPort);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(512);
    
    Serial.print("MQTT connecting... ");
    
    bool connected = false;
    if(mqttAuthEnabled) {
        connected = mqtt.connect(deviceName.c_str(), mqttUser.c_str(), mqttPassword.c_str());
    } else {
        connected = mqtt.connect(deviceName.c_str());
    }
    
    if(connected) {
        Serial.println("✅ Connected!");
        
        // Subscribe to all heater topics
        for(int i = 0; i < NUM_HEATERS; i++) {
            if(!heaters[i].enabled) continue;
            String topic = String(heaters[i].mqttPrefix) + "/cmd/#";
            mqtt.subscribe(topic.c_str());
        }
    } else {
        Serial.printf("❌ Failed (State: %d)\n", mqtt.state());
    }
}

void publishMQTT() {
    if(!mqtt.connected()) return;
    
    for(int i = 0; i < NUM_HEATERS; i++) {
        if(!heaters[i].enabled || heaters[i].lastUpdate == 0) continue;
        
        char topic[128];
        String value;
        
        sprintf(topic, "%s/state", heaters[i].mqttPrefix);
        mqtt.publish(topic, getStateName(heaters[i].state));
        
        sprintf(topic, "%s/voltage", heaters[i].mqttPrefix);
        value = String(heaters[i].voltage / 10.0, 1);
        mqtt.publish(topic, value.c_str());
        
        sprintf(topic, "%s/ambient", heaters[i].mqttPrefix);
        mqtt.publish(topic, String(heaters[i].ambientTemp).c_str());
        
        sprintf(topic, "%s/case", heaters[i].mqttPrefix);
        mqtt.publish(topic, String(heaters[i].caseTemp).c_str());
        
        sprintf(topic, "%s/setpoint", heaters[i].mqttPrefix);
        mqtt.publish(topic, String(heaters[i].setpoint).c_str());
        
        sprintf(topic, "%s/pump", heaters[i].mqttPrefix);
        value = String(heaters[i].pumpFreq / 10.0, 1);
        mqtt.publish(topic, value.c_str());
        
        sprintf(topic, "%s/mode", heaters[i].mqttPrefix);
        mqtt.publish(topic, heaters[i].autoMode ? "AUTO" : "MANUAL");
        
        sprintf(topic, "%s/error", heaters[i].mqttPrefix);
        mqtt.publish(topic, getErrorName(heaters[i].errorCode));
        
        sprintf(topic, "%s/availability", heaters[i].mqttPrefix);
        bool online = (millis() - heaters[i].lastUpdate < HEATER_TIMEOUT);
        mqtt.publish(topic, online ? "online" : "offline");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// OTA FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setupOTA() {
    if(!otaEnabled) return;
    
    ArduinoOTA.setHostname(deviceName.c_str());
    ArduinoOTA.setPassword(otaPassword.c_str());
    
    ArduinoOTA.onStart([]() {
        displayLine1 = "OTA UPDATE";
        displayLine2 = "Updating...";
        displayLine3 = "NICHT";
        displayLine4 = "AUSSCHALTEN!";
        updateDisplay();
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int percent = (progress / (total / 100));
        displayLine2 = "Progress: " + String(percent) + "%";
        updateDisplay();
    });
    
    ArduinoOTA.begin();
    Serial.println("✅ OTA enabled");
}

// ═══════════════════════════════════════════════════════════════════════════
// WEB SERVER HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

void handleRoot() {
    // Die HTML-Seite ist zu groß für einen String - wird in Teilen gesendet
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    
    // Teil 1: HTML Header und CSS
    server.sendContent(R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Diesel Pilot Multi</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            background: #0a0a0a;
            color: #e0e0e0;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        h1 { 
            color: #ff6b00; 
            margin-bottom: 30px;
            text-align: center;
            text-shadow: 0 0 10px rgba(255, 107, 0, 0.5);
        }
        
        .tabs {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            border-bottom: 2px solid #333;
        }
        .tab {
            background: #1a1a1a;
            border: 1px solid #333;
            border-bottom: none;
            padding: 12px 24px;
            cursor: pointer;
            border-radius: 8px 8px 0 0;
            transition: all 0.3s;
        }
        .tab:hover { background: #252525; }
        .tab.active {
            background: #ff6b00;
            color: white;
            border-color: #ff6b00;
        }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        
        .card {
            background: #1a1a1a;
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
            border: 1px solid #333;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
        .card h2 {
            color: #ff6b00;
            margin-bottom: 15px;
            font-size: 1.3em;
        }
        
        .heater-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        
        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        .status-item {
            background: #0f0f0f;
            padding: 15px;
            border-radius: 8px;
            border: 1px solid #2a2a2a;
        }
        .status-label {
            color: #888;
            font-size: 0.85em;
            margin-bottom: 5px;
        }
        .status-value {
            color: #fff;
            font-size: 1.5em;
            font-weight: bold;
        }
        
        .state-running { color: #4CAF50; }
        .state-off { color: #f44336; }
        .state-other { color: #ff9800; }
        .error-ok { color: #4CAF50; }
        .error-warning { color: #ff9800; }
        .error-critical { color: #f44336; }
        
        .btn {
            background: #ff6b00;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 1em;
            margin: 5px;
            transition: all 0.3s;
        }
        .btn:hover {
            background: #ff8533;
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(255, 107, 0, 0.3);
        }
        .btn:active { transform: translateY(0); }
        .btn-group {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-top: 15px;
        }
        
        input, select {
            background: #0f0f0f;
            border: 1px solid #333;
            color: #e0e0e0;
            padding: 10px;
            border-radius: 6px;
            width: 100%;
            margin: 5px 0;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #ff6b00;
        }
        
        .form-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin: 10px 0;
        }
        
        .info-box {
            margin-top:15px; 
            padding:10px; 
            background:#0f0f0f; 
            border-radius:6px; 
            border:1px solid #2a2a2a; 
            font-size:0.9em; 
            color:#888;
        }
        .info-box strong { color:#ff6b00; }
        
        .offline {
            opacity: 0.6;
            background: rgba(100, 100, 100, 0.1);
        }
        
        @media (max-width: 768px) {
            .heater-grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔥 DIESEL PILOT MULTI 🔥</h1>
        
        <div class="tabs">
            <div class="tab active" onclick="switchTab('dashboard')">📊 Dashboard</div>
            <div class="tab" onclick="switchTab('config')">⚙️ Konfiguration</div>
        </div>
)rawliteral");

    // Teil 2: Dashboard Tab
    server.sendContent(R"rawliteral(
        <div id="dashboard" class="tab-content active">
            <div class="heater-grid" id="heaters"></div>
        </div>
)rawliteral");

    // Teil 3: Config Tab
    server.sendContent(R"rawliteral(
        <div id="config" class="tab-content">
            
            <!-- HEATER PAIRING -->
            <div class="card">
                <h2>🔗 Heizungen Pairen</h2>
                <div class="form-row">
                    <div>
                        <label>Heizung auswählen:</label>
                        <select id="pairHeater">
                            <option value="0">Heizung 1</option>
                            <option value="1">Heizung 2</option>
                            <option value="2">Heizung 3</option>
                        </select>
                    </div>
                    <div>
                        <button class="btn" onclick="autoPair()" style="width:100%">🔍 AUTO PAIRING</button>
                    </div>
                </div>
                <div class="form-row">
                    <input type="text" id="manualAddr" placeholder="0xCA00445B">
                    <button class="btn" onclick="manualPair()">✏️ MANUELL PAIREN</button>
                </div>
                <div class="info-box">
                    <strong>📡 Auto Pairing:</strong> Wähle Heizung, klicke Auto Pairing, halte dann Pairing-Taste an der Heizung 5-10 Sekunden.<br>
                    <strong>✏️ Manuell:</strong> Adresse eingeben (z.B. mit RTL-SDR gefunden).
                </div>
            </div>
            
            <!-- HEATER NAMES -->
            <div class="card">
                <h2>✏️ Heizungs-Namen</h2>
                <input type="text" id="name0" placeholder="Name für Heizung 1 (z.B. Wohnzimmer)">
                <input type="text" id="name1" placeholder="Name für Heizung 2 (z.B. Werkstatt)">
                <input type="text" id="name2" placeholder="Name für Heizung 3 (z.B. Garage)">
                <button class="btn" onclick="saveNames()">💾 Namen speichern</button>
                <div class="info-box">
                    Namen werden im Display und Web-Interface angezeigt.
                </div>
            </div>
            
            <!-- WIFI CONFIG -->
            <div class="card">
                <h2>📡 WiFi Konfiguration</h2>
                <input type="text" id="deviceName" placeholder="Hostname (z.B. DieselPilot-Garage)">
                <input type="text" id="wifiSSID" placeholder="WLAN SSID">
                <input type="password" id="wifiPass" placeholder="WLAN Passwort">
                <button class="btn" onclick="saveWiFi()">💾 WiFi speichern & Neustart</button>
                <div class="info-box">
                    <strong>📛 Hostname:</strong> Name des Geräts im Netzwerk und als MQTT Client ID.<br>
                    <strong>ℹ️ Hinweis:</strong> Nach dem Speichern erfolgt Neustart und Verbindung zum WLAN.
                </div>
            </div>
            
            <!-- MQTT CONFIG -->
            <div class="card">
                <h2>📨 MQTT Konfiguration</h2>
                <input type="text" id="mqttServer" placeholder="MQTT Broker IP (z.B. 192.168.1.100)">
                <div class="form-row">
                    <input type="number" id="mqttPort" placeholder="Port" value="1883">
                    <input type="text" id="mqttTopic" placeholder="Basis-Topic (z.B. diesel)">
                </div>
                <input type="text" id="mqttUser" placeholder="Username (optional)">
                <input type="password" id="mqttPass" placeholder="Passwort (optional)">
                <button class="btn" onclick="saveMQTT()">💾 MQTT speichern</button>
                <div class="info-box">
                    <strong>🏠 Home Assistant:</strong> Topics werden automatisch erstellt:<br>
                    <code>diesel/heater1/state</code>, <code>diesel/heater2/state</code>, etc.<br>
                    Jede Heizung erhält eigene Command-Topics: <code>diesel/heater1/cmd/power</code>
                </div>
            </div>
            
            <!-- OTA CONFIG -->
            <div class="card">
                <h2>🔄 OTA Update Konfiguration</h2>
                <input type="password" id="otaPassword" placeholder="OTA Passwort (default: dieselpilot)">
                <button class="btn" onclick="saveOTA()">💾 OTA speichern</button>
                <div class="info-box">
                    <strong>📡 OTA Updates:</strong> Kabelloses Update über Arduino IDE.<br>
                    <strong>Port:</strong> 3232 | <strong>Hostname:</strong> siehe System Info<br>
                    <strong>⚠️ Sicherheit:</strong> Ändern Sie das Standard-Passwort!
                </div>
            </div>
            
            <!-- SYSTEM INFO -->
            <div class="card">
                <h2>ℹ️ System Info</h2>
                <div id="systemInfo" style="font-family: monospace; font-size: 0.9em; line-height: 1.6;">
                    Lade...
                </div>
                <div style="margin-top: 20px;">
                    <button class="btn" onclick="reboot()" style="background: #ff9800;">🔄 Neustart</button>
                    <button class="btn" onclick="factoryReset()" style="background: #d32f2f;">⚠️ Factory Reset</button>
                </div>
                <div class="info-box">
                    <strong>🔄 Neustart:</strong> Gerät neu starten ohne Datenverlust.<br>
                    <strong>⚠️ Factory Reset:</strong> Löscht ALLE Einstellungen (WiFi, MQTT, Pairing)!
                </div>
            </div>
            
        </div>
)rawliteral");

    // Teil 4: JavaScript
    server.sendContent(R"rawliteral(
    </div>
    
    <script>
        function switchTab(tab) {
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.getElementById(tab).classList.add('active');
            event.target.classList.add('active');
            if(tab === 'config') updateSystemInfo();
        }
        
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(d => {
                    let html = '';
                    d.heaters.forEach((h, i) => {
                        let online = h.lastUpdate > 0 && ((Date.now() - h.lastUpdate) < 30000);
                        let stateClass = h.state === 'RUNNING' ? 'state-running' : 
                                        h.state === 'OFF' ? 'state-off' : 'state-other';
                        let errorClass = h.errorCode === 0 ? 'error-ok' : 
                                        h.errorCode <= 1 ? 'error-warning' : 'error-critical';
                        
                        html += `<div class="card ${online ? '' : 'offline'}">
                            <h2>${i+1}. ${h.name}</h2>`;
                        
                        if(h.paired) {
                            html += `<div class="status-grid">
                                <div class="status-item">
                                    <div class="status-label">Status</div>
                                    <div class="status-value ${stateClass}">${h.state}</div>
                                </div>
                                <div class="status-item">
                                    <div class="status-label">Fehler</div>
                                    <div class="status-value ${errorClass}">${h.errorName || 'OK'}</div>
                                </div>
                                <div class="status-item">
                                    <div class="status-label">Temperatur</div>
                                    <div class="status-value">${h.ambient}°C</div>
                                </div>
                                <div class="status-item">
                                    <div class="status-label">Sollwert</div>
                                    <div class="status-value">${h.setpoint}°C</div>
                                </div>
                                <div class="status-item">
                                    <div class="status-label">Spannung</div>
                                    <div class="status-value">${h.voltage}V</div>
                                </div>
                                <div class="status-item">
                                    <div class="status-label">Pumpe</div>
                                    <div class="status-value">${h.pump}Hz</div>
                                </div>
                            </div>
                            <div class="btn-group">
                                <button class="btn" onclick="cmd(${i}, 'power')">⚡ POWER</button>
                                <button class="btn" onclick="cmd(${i}, 'up')">⬆️ HOCH</button>
                                <button class="btn" onclick="cmd(${i}, 'down')">⬇️ RUNTER</button>
                                <button class="btn" onclick="cmd(${i}, 'mode')">🔄 MODUS</button>
                            </div>`;
                        } else {
                            html += `<p style="text-align:center;padding:20px;opacity:0.7;">
                                Nicht gepairt. Gehe zu Konfiguration → Pairing.
                            </p>`;
                        }
                        
                        html += '</div>';
                    });
                    document.getElementById('heaters').innerHTML = html;
                })
                .catch(e => console.error('Status update failed:', e));
        }
        
        function updateSystemInfo() {
            fetch('/api/info')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('systemInfo').innerHTML = `
                        <div>Hostname: <span style="color:#ff6b00">${d.hostname}</span></div>
                        <div>WiFi Mode: ${d.wifiMode}</div>
                        <div>IP: ${d.ip}</div>
                        <div>MQTT: ${d.mqtt}</div>
                        <div>OTA: ${d.ota}</div>
                        <div>Uptime: ${d.uptime}</div>
                    `;
                })
                .catch(e => console.error('Info update failed:', e));
        }
        
        function cmd(h, c) {
            fetch(`/api/cmd?h=${h}&c=${c}`)
                .then(() => setTimeout(updateStatus, 500))
                .catch(e => alert('Befehl fehlgeschlagen!'));
        }
        
        function autoPair() {
            let h = document.getElementById('pairHeater').value;
            if(confirm(`Auto-Pairing für Heizung ${parseInt(h)+1} starten?\n\nHalte jetzt die Pairing-Taste an der Heizung!`)) {
                fetch(`/api/pair?h=${h}&mode=auto`)
                    .then(r => r.text())
                    .then(msg => {
                        alert(msg);
                        setTimeout(updateStatus, 1000);
                    });
            }
        }
        
        function manualPair() {
            let h = document.getElementById('pairHeater').value;
            let addr = document.getElementById('manualAddr').value;
            if(addr.length < 8) {
                alert('Bitte gültige Adresse eingeben (z.B. 0xCA00445B)');
                return;
            }
            fetch(`/api/pair?h=${h}&mode=manual&addr=${encodeURIComponent(addr)}`)
                .then(r => r.text())
                .then(msg => {
                    alert(msg);
                    setTimeout(updateStatus, 1000);
                });
        }
        
        function saveNames() {
            let names = [
                document.getElementById('name0').value,
                document.getElementById('name1').value,
                document.getElementById('name2').value
            ];
            fetch('/api/names?n0=' + encodeURIComponent(names[0]) + 
                  '&n1=' + encodeURIComponent(names[1]) + 
                  '&n2=' + encodeURIComponent(names[2]))
                .then(r => r.text())
                .then(msg => {
                    alert(msg);
                    setTimeout(updateStatus, 1000);
                });
        }
        
        function saveWiFi() {
            let deviceName = document.getElementById('deviceName').value;
            let ssid = document.getElementById('wifiSSID').value;
            let pass = document.getElementById('wifiPass').value;
            if(confirm('WiFi-Konfiguration speichern und neu starten?')) {
                fetch('/api/wifi?deviceName=' + encodeURIComponent(deviceName) + 
                      '&ssid=' + encodeURIComponent(ssid) + 
                      '&pass=' + encodeURIComponent(pass))
                    .then(r => r.text())
                    .then(msg => {
                        alert(msg + '\n\nGerät startet neu...');
                        setTimeout(() => location.reload(), 2000);
                    });
            }
        }
        
        function saveMQTT() {
            let server = document.getElementById('mqttServer').value;
            let port = document.getElementById('mqttPort').value;
            let topic = document.getElementById('mqttTopic').value;
            let user = document.getElementById('mqttUser').value;
            let pass = document.getElementById('mqttPass').value;
            
            fetch('/api/mqtt?server=' + encodeURIComponent(server) + 
                  '&port=' + port + 
                  '&topic=' + encodeURIComponent(topic) + 
                  '&user=' + encodeURIComponent(user) + 
                  '&pass=' + encodeURIComponent(pass))
                .then(r => r.text())
                .then(alert);
        }
        
        function saveOTA() {
            let pass = document.getElementById('otaPassword').value;
            fetch('/api/ota?pass=' + encodeURIComponent(pass))
                .then(r => r.text())
                .then(alert);
        }
        
        function reboot() {
            if(confirm('Gerät jetzt neu starten?')) {
                fetch('/api/reboot')
                    .then(() => alert('Neustart läuft... Warte 10 Sekunden und aktualisiere die Seite.'));
            }
        }
        
        function factoryReset() {
            if(confirm('⚠️ FACTORY RESET\n\nAlle Einstellungen werden gelöscht!\n\nWirklich fortfahren?')) {
                if(confirm('⚠️ LETZTE WARNUNG!\n\nAlle WiFi, MQTT, OTA und Pairing-Daten gehen verloren!\n\nFortsetzten?')) {
                    fetch('/api/factory')
                        .then(() => alert('Factory Reset abgeschlossen!\n\nGerät startet im AP-Modus.\n\nSSID: DieselPilot-Multi\nPasswort: 12345678'));
                }
            }
        }
        
        // Auto-refresh
        setInterval(updateStatus, 3000);
        updateStatus();
    </script>
</body>
</html>
)rawliteral");
    
    server.sendContent("");
}

void handleAPI_Status() {
    String json = "{\"heaters\":[";
    
    for(int i = 0; i < NUM_HEATERS; i++) {
        if(i > 0) json += ",";
        json += "{";
        json += "\"name\":\"" + String(heaters[i].name) + "\",";
        json += "\"paired\":" + String(heaters[i].isPaired ? "true" : "false") + ",";
        json += "\"state\":\"" + String(getStateName(heaters[i].state)) + "\",";
        json += "\"ambient\":" + String(heaters[i].ambientTemp) + ",";
        json += "\"setpoint\":" + String(heaters[i].setpoint) + ",";
        json += "\"voltage\":" + String(heaters[i].voltage / 10.0, 1) + ",";
        json += "\"pump\":" + String(heaters[i].pumpFreq / 10.0, 1) + ",";
        json += "\"errorCode\":" + String(heaters[i].errorCode) + ",";
        json += "\"errorName\":\"" + String(getErrorName(heaters[i].errorCode)) + "\",";
        json += "\"lastUpdate\":" + String(heaters[i].lastUpdate);
        json += "}";
    }
    
    json += "]}";
    server.send(200, "application/json", json);
}

void handleAPI_Info() {
    String json = "{";
    json += "\"hostname\":\"" + deviceName + "\",";
    json += "\"wifiMode\":\"" + String(useAP ? "AP" : "STA") + "\",";
    json += "\"ip\":\"" + (useAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
    json += "\"mqtt\":\"" + String(mqttEnabled && mqtt.connected() ? "Connected" : "Disconnected") + "\",";
    json += "\"ota\":\"" + String(otaEnabled ? "Enabled" : "Disabled") + "\",";
    json += "\"uptime\":\"" + String(millis() / 1000 / 60) + " min\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleAPI_Names() {
    for(int i = 0; i < NUM_HEATERS; i++) {
        char argName[8];
        sprintf(argName, "n%d", i);
        if(server.hasArg(argName)) {
            String name = server.arg(argName);
            if(name.length() > 0) {
                name.toCharArray(heaters[i].name, sizeof(heaters[i].name));
                saveHeaterConfig(i);
            }
        }
    }
    server.send(200, "text/plain", "Namen gespeichert!");
}

void handleAPI_WiFi() {
    deviceName = server.arg("deviceName");
    if(deviceName.length() == 0) deviceName = "DieselPilot-Multi";
    staSSID = server.arg("ssid");
    staPassword = server.arg("pass");
    
    prefs.putString("deviceName", deviceName);
    prefs.putString("staSSID", staSSID);
    prefs.putString("staPass", staPassword);
    
    server.send(200, "text/plain", "WiFi gespeichert! Neustart...");
    delay(1000);
    ESP.restart();
}

void handleAPI_MQTT() {
    mqttServer = server.arg("server");
    mqttPort = server.arg("port").toInt();
    mqttTopic = server.arg("topic");
    mqttUser = server.arg("user");
    mqttPassword = server.arg("pass");
    mqttAuthEnabled = (mqttUser.length() > 0);
    mqttEnabled = (mqttServer.length() > 0);
    
    prefs.putString("mqttServer", mqttServer);
    prefs.putInt("mqttPort", mqttPort);
    prefs.putString("mqttTopic", mqttTopic);
    prefs.putBool("mqttAuthEn", mqttAuthEnabled);
    prefs.putString("mqttUser", mqttUser);
    prefs.putString("mqttPass", mqttPassword);
    prefs.putBool("mqttEnabled", mqttEnabled);
    
    // Update MQTT prefixes
    for(int i = 0; i < NUM_HEATERS; i++) {
        sprintf(heaters[i].mqttPrefix, "%s/heater%d", mqttTopic.c_str(), i + 1);
    }
    
    server.send(200, "text/plain", "MQTT gespeichert!");
    
    if(mqttEnabled) {
        connectMQTT();
    }
}

void handleAPI_OTA() {
    String newPass = server.arg("pass");
    if(newPass.length() > 0) {
        otaPassword = newPass;
        prefs.putString("otaPass", otaPassword);
    }
    server.send(200, "text/plain", "OTA Passwort gespeichert!");
}

void handleAPI_Reboot() {
    server.send(200, "text/plain", "Neustart...");
    delay(500);
    ESP.restart();
}

void handleAPI_Factory() {
    server.send(200, "text/plain", "Werkseinstellungen...");
    Serial.println("\n⚠️ Werkseinstellungen");
    prefs.clear();
    delay(2000);
    ESP.restart();
}

void handleAPI_Command() {
    int heaterIdx = server.arg("h").toInt();
    String cmd = server.arg("c");
    
    if(heaterIdx < 0 || heaterIdx >= NUM_HEATERS) {
        server.send(400, "text/plain", "Invalid heater");
        return;
    }
    
    if(cmd == "power") sendCommand(heaterIdx, CMD_POWER);
    else if(cmd == "up") sendCommand(heaterIdx, CMD_UP);
    else if(cmd == "down") sendCommand(heaterIdx, CMD_DOWN);
    else if(cmd == "mode") sendCommand(heaterIdx, CMD_MODE);
    
    server.send(200, "text/plain", "OK");
}

void handleAPI_Pair() {
    int heaterIdx = server.arg("h").toInt();
    String mode = server.arg("mode");
    
    if(heaterIdx < 0 || heaterIdx >= NUM_HEATERS) {
        server.send(400, "text/plain", "Invalid heater");
        return;
    }
    
    if(mode == "auto") {
        uint32_t addr = findHeater(60000);
        if(addr != 0) {
            heaters[heaterIdx].address = addr;
            heaters[heaterIdx].isPaired = true;
            saveHeaterConfig(heaterIdx);
            server.send(200, "text/plain", "Paired: 0x" + String(addr, HEX));
        } else {
            server.send(200, "text/plain", "Pairing failed!");
        }
    } else if(mode == "manual") {
        String addrStr = server.arg("addr");
        heaters[heaterIdx].address = strtoul(addrStr.c_str(), NULL, 0);
        heaters[heaterIdx].isPaired = true;
        saveHeaterConfig(heaterIdx);
        server.send(200, "text/plain", "Paired!");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n═══════════════════════════════════════");
    Serial.println("  DIESEL PILOT MULTI-HEATER v" + version);
    Serial.println("═══════════════════════════════════════\n");
    
#if USE_OLED
    Wire.begin(PIN_SDA, PIN_SCL);
    display.begin();
    display.setContrast(255);
    displayLine1 = "Diesel Pilot";
    displayLine2 = "Multi-Heater";
    displayLine3 = "v" + version;
    displayLine4 = "Initialisiere...";
    updateDisplay();
    Serial.println("✅ OLED initialisiert");
    delay(2000);
#endif
    
    // Load configuration
    prefs.begin("diesel", false);
    deviceName = prefs.getString("deviceName", "DieselPilot-Multi");
    staSSID = prefs.getString("staSSID", "");
    staPassword = prefs.getString("staPass", "");
    mqttServer = prefs.getString("mqttServer", "");
    mqttPort = prefs.getInt("mqttPort", 1883);
    mqttTopic = prefs.getString("mqttTopic", "diesel");
    mqttAuthEnabled = prefs.getBool("mqttAuthEn", false);
    mqttUser = prefs.getString("mqttUser", "");
    mqttPassword = prefs.getString("mqttPass", "");
    mqttEnabled = prefs.getBool("mqttEnabled", false);
    otaEnabled = prefs.getBool("otaEnabled", true);
    otaPassword = prefs.getString("otaPass", "dieselpilot");
    
    // Load heater configurations
    Serial.println("\nLade Konfigurationen:");
    for(int i = 0; i < NUM_HEATERS; i++) {
        loadHeaterConfig(i);
        Serial.printf("  Heater %d: %s (0x%08X) %s\n", 
                     i + 1, 
                     heaters[i].name, 
                     heaters[i].address,
                     heaters[i].isPaired ? "PAIRED" : "NOT PAIRED");
    }
    
    // Initialize CC1101
    pinMode(PIN_SCK, OUTPUT);
    pinMode(PIN_MOSI, OUTPUT);
    pinMode(PIN_MISO, INPUT);
    pinMode(PIN_SS, OUTPUT);
    pinMode(PIN_GDO2, INPUT);
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
    cc1101_init();
    
    // WiFi setup
    WiFi.setHostname(deviceName.c_str());
    
    if(staSSID.length() > 0) {
        Serial.println("\nVerbinde mit Wlan: " + staSSID);
        WiFi.begin(staSSID.c_str(), staPassword.c_str());
        int attempts = 0;
        while(WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if(WiFi.status() == WL_CONNECTED) {
            useAP = false;
            Serial.println("\n✅ Wlan verbunden!");
            Serial.println("IP: " + WiFi.localIP().toString());
        } else {
            Serial.println("\n❌ WLAN Fehler, starte AP");
            useAP = true;
        }
    }
    
    if(useAP) {
        WiFi.softAP(apSSID.c_str(), apPassword.c_str());
        Serial.println("✅ AP gestartet");
        Serial.println("SSID: " + apSSID);
        Serial.println("IP: " + WiFi.softAPIP().toString());
    }
    
    // OTA setup
    if(!useAP && otaEnabled) {
        setupOTA();
    }
    
    // MQTT setup
    if(mqttEnabled) {
        connectMQTT();
    }
    
    // Web server
    server.on("/", handleRoot);
    server.on("/api/status", handleAPI_Status);
    server.on("/api/info", handleAPI_Info);
    server.on("/api/cmd", handleAPI_Command);
    server.on("/api/pair", handleAPI_Pair);
    server.on("/api/names", handleAPI_Names);
    server.on("/api/wifi", handleAPI_WiFi);
    server.on("/api/mqtt", handleAPI_MQTT);
    server.on("/api/ota", handleAPI_OTA);
    server.on("/api/reboot", handleAPI_Reboot);
    server.on("/api/factory", handleAPI_Factory);
    server.begin();
    
    Serial.println("\n✅ Web Server gestartet");
    Serial.println("Fertig!\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    server.handleClient();
    
    if(otaEnabled) {
        ArduinoOTA.handle();
    }
    
    // RF Communication (Round-Robin)
    handleRFCommunication();
    
    // Monitor heaters
    monitorHeaters();
    
    // MQTT
    if(mqttEnabled && !mqtt.connected()) {
        if(millis() - lastMQTTRetry > mqttRetryInterval) {
            lastMQTTRetry = millis();
            connectMQTT();
        }
    }
    if(mqttEnabled && mqtt.connected()) {
        mqtt.loop();
        static unsigned long lastMQTTPublish = 0;
        if(millis() - lastMQTTPublish > 5000) {
            publishMQTT();
            lastMQTTPublish = millis();
        }
    }
    
    // Display
    static unsigned long lastDisplay = 0;
    if(millis() - lastDisplay > 500) {
        updateDisplay();
        rotateDisplay();
        lastDisplay = millis();
    }
    
    yield();
}