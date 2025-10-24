#include <SPI.h>
#include <DW1000Ranging.h>
#include <BluetoothSerial.h>
#include <FS.h>
#include <SPIFFS.h>
#include "link.h"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS 4

BluetoothSerial SerialBT;

struct MyLink *uwb_data;
const int MAX_LOGS = 10;
struct LogEntry { unsigned long timestamp; String json; };
LogEntry logBuffer[MAX_LOGS];
int logIndex = 0;

const unsigned long DATA_INTERVAL = 10000;
const unsigned long LOG_INTERVAL = 120000;
const unsigned long CLEAR_INTERVAL = 900000;

unsigned long lastDataTime = 0;
unsigned long lastLogTime = 0;
unsigned long lastClearTime = 0;

void clearLogs() {
    for (int i = 0; i < MAX_LOGS; i++) {
        logBuffer[i].timestamp = 0;
        logBuffer[i].json = "";
    }
    logIndex = 0;
}

void addLog(String json) {
    logBuffer[logIndex] = { millis(), json };
    String filename = "/log_" + String(logIndex) + ".json";
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (file) {
        file.println("{\"T\":" + String(millis()) + ",\"entries\":" + json + "}");
        file.close();
    } else {
        File oldest = SPIFFS.open("/log_0.json", FILE_WRITE);
        if (oldest) {
            oldest.println("{\"T\":" + String(millis()) + ",\"entries\":" + json + "}");
            oldest.close();
        }
    }
    logIndex = (logIndex + 1) % MAX_LOGS;
    Serial.printf("Added log at index %d\n", logIndex);
}

void newRange() {
    DW1000Device* device = DW1000Ranging.getDistantDevice();
    if (!device) return;
    fresh_link(uwb_data, device->getShortAddress(), device->getRange(), device->getRXPower());

    if(SerialBT.hasClient()){
        Serial.println("Client connected!");
    }
    /*Serial.print("from: ");
    Serial.print(device->getShortAddress(), HEX);
    Serial.print("\tRange: ");
    Serial.print(device->getRange());
    Serial.print(" m\tRX power: ");
    Serial.print(device->getRXPower());
    Serial.println(" dBm");*/
}

void newDevice(DW1000Device *device) { add_link(uwb_data, device->getShortAddress()); }
void inactiveDevice(DW1000Device *device) { delete_link(uwb_data, device->getShortAddress()); }

String buildUWBLogJSON() {
    String all_json = "";
    make_link_json(uwb_data, &all_json);
    return all_json;
}

void sendLogs() {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file){
        while(file.available()){
            SerialBT.write(file.read());
        }
        SerialBT.println();
        file = root.openNextFile();
    }
}

void setup() {
    Serial.begin(115200);
    SPIFFS.begin(true);
    SerialBT.end();
    SerialBT.begin("UWB_Tag");
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    DW1000Ranging.initCommunication(PIN_RST, DW_CS, PIN_IRQ);
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);
    DW1000Ranging.startAsTag("7D:00:22:EA:82:60:3B:9C", DW1000.MODE_SHORTDATA_FAST_LOWPOWER);
    uwb_data = init_link();
    clearLogs();
    lastDataTime = lastLogTime = lastClearTime = millis();
}

void loop() {
    DW1000Ranging.loop();
    unsigned long now = millis();
    if (uwb_data != nullptr) {
        if (now - lastDataTime >= DATA_INTERVAL) {
            String tmp;
            make_link_json(uwb_data, &tmp);
            lastDataTime = now;
        }
        if (now - lastLogTime >= LOG_INTERVAL) {
            String uwb_json = buildUWBLogJSON();
            addLog(uwb_json);
            lastLogTime = now;
        }
    }
    if (now - lastClearTime >= CLEAR_INTERVAL) {
        clearLogs();
        lastClearTime = now;
    }
    while (SerialBT.available()) {
        char c = SerialBT.read();
        if(c == '\n'){
            cmd.trim();
            if (cmd == "GET_LOGS") sendLogs();
            else if (cmd == "CLEAR_LOGS") clearLogs();
            cmd = "";
        } else {
            cmd += c;
        }
    }
    SerialBT.println("send");
}


