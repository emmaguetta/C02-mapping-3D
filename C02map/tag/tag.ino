#include <SPI.h>
#include <DW1000Ranging.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
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

const unsigned long DATA_INTERVAL  = 10000;    // 10s
const unsigned long LOG_INTERVAL   = 120000;   // 2min
const unsigned long CLEAR_INTERVAL = 900000;   // 15min
const int MAX_LOGS = 10;

#define BLE_SERVICE_UUID        "service-uid"
#define BLE_CHAR_CMD_UUID       "char-cmd-uid"
#define BLE_CHAR_LOGS_UUID      "char-logs-uid"

struct LogEntry {
  unsigned long timestamp;
  String json;
};

struct MyLink *uwb_data;

LogEntry logBuffer[MAX_LOGS];
int logIndex = 0;

unsigned long lastDataTime = 0;
unsigned long lastLogTime = 0;
unsigned long lastClearTime = 0;

BLEServer* pServer = nullptr;
BLECharacteristic* pCmdCharacteristic = nullptr;
BLECharacteristic* pLogsCharacteristic = nullptr;
bool deviceConnected = false;

String cmd = "";

void clearLogs() {
  for (int i = 0; i < MAX_LOGS; i++) {
    logBuffer[i].timestamp = 0;
    logBuffer[i].json = "";
  }
  logIndex = 0;

  File root = SPIFFS.open("/");
  if (!root) return;

  File file = root.openNextFile();
  while(file){
    String fname = file.name();
    file.close();
    if (SPIFFS.exists(fname)) SPIFFS.remove(fname);
    file = root.openNextFile();
  }

  Serial.println("Logs cleared");
}

void addLog(String json) {
  logBuffer[logIndex].timestamp = millis();
  logBuffer[logIndex].json = json;

  String filename = "/log_" + String(logIndex) + ".json";
  File file = SPIFFS.open(filename, FILE_WRITE);

  if (file) {
    file.printf("{\"T\":%lu,\"entries\":%s}\n", millis(), json.c_str());
    file.close();
  } else {
    Serial.printf("Failed to open %s for write\n", filename.c_str());
  }

  logIndex = (logIndex + 1) % MAX_LOGS;
  Serial.printf("Added log at index %d\n", logIndex);
}

String buildUWBLogJSON() {
  String json;
  make_link_json(uwb_data, &json);
  Serial.println(json);
  return json;
}

void sendLogsOverBLE() {
  if (!pLogsCharacteristic) return;

  File root = SPIFFS.open("/");
  if (!root) return;

  File file = root.openNextFile();
  while(file) {
    const size_t CHUNK = 200;
    while (file.available()) {
      uint8_t buffer[CHUNK];
      size_t toRead = file.read(buffer, CHUNK);
      if (toRead > 0) {
        pLogsCharacteristic->setValue(buffer, toRead);
        pLogsCharacteristic->notify();
        delay(10);
      } else break;
    }
    std::string nl = "\n";
    pLogsCharacteristic->setValue((uint8_t*)nl.data(), nl.size());
    pLogsCharacteristic->notify();
    delay(10);

    file = root.openNextFile();
  }
  Serial.println("Finished sending logs.");
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE client connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("BLE client disconnected");
    pServer->getAdvertising()->start();
  }
};

class CmdCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    for (size_t i = 0; i < value.length(); ++i) {
      char c = value[i];
      if (c == '\n') {
        cmd.trim();
        Serial.printf("CMD received: '%s'\n", cmd.c_str());
        if (cmd == "GET_LOGS") sendLogsOverBLE();
        else if (cmd == "CLEAR_LOGS") clearLogs();
        else Serial.println("Unknown command");
        cmd = "";
      } else if (c != '\r') {
        cmd += c;
      }
    }
  }
};

void newRange() {
  DW1000Device* device = DW1000Ranging.getDistantDevice();
  if (!device) return;

  fresh_link(uwb_data, device->getShortAddress(), device->getRange(), device->getRXPower());
}

void newDevice(DW1000Device *device) {
  add_link(uwb_data, device->getShortAddress());
}

void inactiveDevice(DW1000Device *device) {
  delete_link(uwb_data, device->getShortAddress());
}

void setupBLE() {
  BLEDevice::init("UWB_Tag");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

  pCmdCharacteristic = pService->createCharacteristic(
                        BLE_CHAR_CMD_UUID,
                        BLECharacteristic::PROPERTY_WRITE
                      );
  pCmdCharacteristic->setCallbacks(new CmdCharCallbacks());

  pLogsCharacteristic = pService->createCharacteristic(
                         BLE_CHAR_LOGS_UUID,
                         BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
                       );
  pLogsCharacteristic->addDescriptor(new BLE2902()); 
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pServer->getAdvertising()->start();

  Serial.println("BLE advertising started");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed!");
  else Serial.println("SPIFFS mounted");

  setupBLE();

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, DW_CS, PIN_IRQ);
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  DW1000Ranging.startAsTag(
      "7D:00:22:EA:82:60:3B:9C",
      DW1000.MODE_SHORTDATA_FAST_LOWPOWER
  );

  uwb_data = init_link();
  clearLogs();
  lastDataTime = lastLogTime = lastClearTime = millis();

  Serial.println("Setup complete");
}

void loop() {
  DW1000Ranging.loop();
  yield();

  unsigned long now = millis();
  if (uwb_data && now - lastDataTime >= DATA_INTERVAL) {
    String tmp;
    make_link_json(uwb_data, &tmp);
    lastDataTime = now;
  }
  if (uwb_data && now - lastLogTime >= LOG_INTERVAL) {
    addLog(buildUWBLogJSON());
    lastLogTime = now;
  }
  if (now - lastClearTime >= CLEAR_INTERVAL) {
    clearLogs();
    lastClearTime = now;
  }

  delay(20);
}



