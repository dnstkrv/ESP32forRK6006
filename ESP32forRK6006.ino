#include <ArduinoJson.h>
#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ModbusMaster.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

const char* ssid = "admin";
const char* password = "00000000";
const char* serverName = "http://192.168.0.103/esp-data.php";

WebSocketsServer webSocket = WebSocketsServer(81);
AsyncWebServer server(80);

byte buf[] = {0x71, 0x75, 0x65, 0x72, 0x79, 0x64, 0x0d, 0x0a};
bool swState_connect_rk, swState_batteryRecovery, swState_discharge, flagToMillis, output_status = 0;
int v_out, i_out, batteryVoltageSet, batteryCapacity, dischargeVoltageSet, amp_hour, voltageInput = 0;
float voltageSql, voltageInputSql, currentSql, capacitySql = 0;
int8_t connectionNumber = 0;
unsigned long messageInterval = 500;
unsigned long messageIntervalSql = 60000;
uint32_t recoveryStartTime, recoveryRunningTime, recoveryStep1Time,
    recoveryStep2Time = 0;
String apiKeyValue = "tPmAT5Ab3j7F9";

ModbusMaster node;

typedef struct ModbusRegister {
  uint16_t address;
  const char* description;
};

ModbusRegister temp_sys_reg = {0x0005, "Temperature in Celsius, ro"};
ModbusRegister v_set_reg = {0x0008, "Set voltage, rw"};
ModbusRegister i_set_reg = {0x0009, "Set current, rw"};
ModbusRegister v_out_reg = {0x000A, "Output voltage, ro"};
ModbusRegister i_out_reg = {0x000B, "Output current, ro"};
ModbusRegister watt_reg = {0x000D, "Power, ro"};
ModbusRegister v_input_reg = {0x000E, "Input voltage, ro"};
ModbusRegister lock_reg = {0x000F, "Lock status, rw"};
ModbusRegister output_status_reg = {0x0012, "Output control, rw"};
ModbusRegister backlight_reg = {0x0048, "Backlight setting, rw"};
ModbusRegister temp_probe_reg = {0x0023, "Temperature probe in Celsius, ro"};
ModbusRegister amp_hour_reg = {0x0026, "Amper per hour, ro"};
ModbusRegister watt_hour_reg = {0x0028, "Watt per hour, ro"};

void setup() {
  Serial.begin(115200);
  node.begin(1, Serial);
  if (!LittleFS.begin()) {
    // Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  connectToWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  
    server.on("/graph.html", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/graph.html", "text/html");
  });

  server.on("/index.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.css", "text/css");
  });

  AsyncElegantOTA.begin(&server);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

unsigned long lastUpdate = millis() + messageInterval;
unsigned long lastUpdateSql = millis() + messageIntervalSql;

void loop() {
  webSocket.loop();
if (swState_discharge){
  discharge();
}

  if (swState_batteryRecovery) {
    //batteryRecovery();
  
  uint8_t result_out;
  result_out = node.readHoldingRegisters(0, 72);
  output_status = node.getResponseBuffer(18);
      if ((lastUpdateSql + messageIntervalSql < millis()) && (output_status)) {
      WiFiClient client;
      HTTPClient http;
      http.begin(client, serverName);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      voltageSql = node.getResponseBuffer(10);
      voltageInputSql = node.getResponseBuffer(14);
      currentSql = node.getResponseBuffer(11);
      capacitySql = node.getResponseBuffer(39);
      String httpRequestData = "api_key=" + apiKeyValue + "&voltage=" + voltageSql
                          + "&voltage_input=" + voltageInputSql + "&current=" + currentSql + "&capacity=" + capacitySql + "";
      http.POST(httpRequestData);
      http.end();
      lastUpdateSql = millis();
     }}
  readRegisters();
}

void webSocketEvent(uint8_t client_num, WStype_t type, uint8_t* payload,
                    size_t length) {
  switch (type) {
    case WStype_TEXT: {
      DynamicJsonDocument doc(200);
      deserializeJson(doc, payload);
      const char* type = doc["type"];
      int value = doc["value"];

      if (strcmp(type, "voltage") == 0) {
        node.writeSingleRegister(v_set_reg.address, value);
      }

      if (strcmp(type, "current") == 0) {
        node.writeSingleRegister(i_set_reg.address, value);
      }

      if (strcmp(type, "connect_rk") == 0) {
        if (value == 1) {
          swState_connect_rk = 1;
          connectRK6006();
        } else {
          swState_connect_rk = 0;
          disconnectRF6006();
        }
      }

      if (strcmp(type, "output") == 0) {
        if (value == 1) {
          node.writeSingleRegister(output_status_reg.address, 1);
        } else {
          node.writeSingleRegister(output_status_reg.address, 0);
        }
      }

      if (strcmp(type, "batteryVoltageSet") == 0) {
        batteryVoltageSet = value;
      }
      if (strcmp(type, "swState_discharge") == 0) {
        swState_discharge = value;
      }
      if (strcmp(type, "batteryCapacity") == 0) {
        batteryCapacity = value;
      }
      if (strcmp(type, "dischargeVoltageSet") == 0) {
        dischargeVoltageSet = value;
      }
      if (strcmp(type, "swState_batteryRecovery") == 0) {
        swState_batteryRecovery = value;
        if (value == 0) {
          recoveryStartTime = 0;
          flagToMillis = 0;
          recoveryRunningTime = 0;
          recoveryStep1Time = 0;
          recoveryStep2Time = 0;
        }
      }

      break;
    }
    case WStype_CONNECTED:
      connectionNumber = webSocket.connectedClients();
      break;
    case WStype_DISCONNECTED:
      connectionNumber = webSocket.connectedClients();
      break;
  }
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  // while (WiFi.status() != WL_CONNECTED) {
  // delay(1000);
  //  Serial.println("Connecting to WiFi...");
  //}
  // Serial.println("Connected to WiFi");
  // Serial.print("IP Address: ");
  // Serial.println(WiFi.localIP());
}

void connectRK6006() {
  // Serial.println("Connect to RK6006");
  Serial.write(buf, sizeof(buf));
}

void readRegisters() {
  uint8_t result;
  result = 0;

  if (swState_connect_rk && connectionNumber > 0 &&
      lastUpdate + messageInterval < millis()) {
    const size_t capacity = 1024;
    DynamicJsonDocument doc(capacity);
    result = node.readHoldingRegisters(0, 72);
    doc["temp_sys"] = node.getResponseBuffer(5);
    doc["v_out"] = node.getResponseBuffer(10);
    doc["i_out"] = node.getResponseBuffer(11);
    doc["watt"] = node.getResponseBuffer(13);
    doc["v_input"] = node.getResponseBuffer(14);
    doc["lock"] = node.getResponseBuffer(15);
    doc["output_status"] = node.getResponseBuffer(18);
    doc["backlight"] = node.getResponseBuffer(72);
    doc["temp_probe"] = node.getResponseBuffer(35);
    doc["amp_hour"] = node.getResponseBuffer(39);
    doc["watt_hour"] = node.getResponseBuffer(41);
    doc["v_set"] = node.getResponseBuffer(8);
    doc["i_set"] = node.getResponseBuffer(9);
    doc["charge_time"] = recoveryRunningTime;
    String buf;
    serializeJson(doc, buf);
    webSocket.broadcastTXT(buf);
    lastUpdate = millis();
  }
}

void disconnectRF6006() {
  // Serial.println("Disconnect from RK6006");
  node.writeSingleRegister(0x0012, 0);
  node.writeSingleRegister(0x000F, 0);
}

void batteryRecovery() {
  int batteryVoltageSetStep2 = 1620; //16,2 V
  bool step1 = 1;
  if (!flagToMillis) {
    flagToMillis = !flagToMillis;
    recoveryStartTime = millis();
    node.writeSingleRegister(v_set_reg.address, batteryVoltageSet);
    node.writeSingleRegister(i_set_reg.address,
                             (batteryCapacity * 0.01 * 1000));
    delay(100);
    node.writeSingleRegister(output_status_reg.address, 1);
  }
  recoveryRunningTime = millis() - recoveryStartTime;

  if ((batteryVoltageSet > (v_out - 0.02)) &&
      (i_out < (batteryCapacity * 0.001)) && (step1)) {
    step1 = !step1;
    node.writeSingleRegister(v_set_reg.address, batteryVoltageSetStep2);
    node.writeSingleRegister(i_set_reg.address,
                             (batteryCapacity * 0.02 * 1000));
    recoveryStep1Time = recoveryStartTime - recoveryRunningTime;
    recoveryStep2Time = recoveryRunningTime;
  }
  if ((recoveryRunningTime - recoveryStep2Time) > 28800000) {
    node.writeSingleRegister(output_status_reg.address, 0);
    swState_batteryRecovery = 0;
    step1 = !step1;
    flagToMillis = !flagToMillis;
  }
}

void discharge() {
  uint8_t result_discharge;
    result_discharge = node.readHoldingRegisters(0, 72);
    voltageInput = node.getResponseBuffer(14);
      if (voltageInput < dischargeVoltageSet) {
        node.writeSingleRegister(output_status_reg.address, 0);
      }
}