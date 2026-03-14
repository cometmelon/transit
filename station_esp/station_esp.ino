/*
 * ===========================================================================
 *  CHENNAI TRANSIT STATION -- ESP32 SKETCH
 * ===========================================================================
 *
 *  Role:  WiFi + HTTP bridge between Arduino Mega and the backend API.
 *         Receives text commands from Mega via Serial2 (UART),
 *         makes HTTP requests to the FastAPI backend,
 *         and sends parsed responses back to Mega.
 *
 *  Wiring (ESP32 ? Mega):
 *    ESP32 GPIO16 (RX2)  ->  Mega TX3 (pin 14)
 *    ESP32 GPIO17 (TX2)  ->  Mega RX3 (pin 15)
 *    ESP32 GND           ->  Mega GND
 *    ESP32 VIN            ->  5V
 *
 *  Serial Ports:
 *    Serial   (USB)   -> Debug monitor
 *    Serial2  (UART)  -> Arduino Mega communication
 *
 *  Protocol (Mega -> ESP32):
 *    GET_ASSIGNED_STOP:{user_id}
 *    GET_STOPS
 *    LOOKUP_USER:{rfid}
 *    SAVE_USER:{name},{rfid}
 *    REGISTER_TRIP:{user_id},{current_stop_id},{dest_stop_id}
 *    GET_NEAREST:{current_id},{dest_id}
 *
 *  Protocol (ESP32 -> Mega):
 *    WIFI_OK
 *    ASSIGNED_STOP:{id},{name}
 *    STOPS:{id1},{name1};{id2},{name2};...
 *    USER_FOUND:{user_id},{name}
 *    USER_NOT_FOUND
 *    USER_SAVED:{user_id}
 *    TRIP_SAVED
 *    NEAREST:{bus_number},{current_stop_name},{stops_away}
 *    ERROR:{message}
 */
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
/* =================== WiFi CONFIG =================== */
const char* WIFI_SSID     = "TP-Link_6812";
const char* WIFI_PASSWORD = "36392698";
/* =================== BACKEND CONFIG =================== */
const char* BASE_URL = "http://192.168.1.232:8000";  // Change to your backend IP
/* =================== MEGA UART (Serial2) =================== */
#define MEGA_RX  16   // ESP32 RX2 ? Mega TX3
#define MEGA_TX  17   // ESP32 TX2 -> Mega RX3
/* =================== SERIAL BUFFER =================== */
String serialBuffer = "";
/* =================== SETUP =================== */
void setup() {
  Serial.begin(9600);                        // USB debug monitor
  Serial2.begin(9600, SERIAL_8N1, MEGA_RX, MEGA_TX);  // UART to Mega
  delay(1000);
  Serial.println("[ESP32] Starting...");
  // Flush any garbage in UART buffer from Mega boot
  while (Serial2.available()) { Serial2.read(); }
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[ESP32] Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  // Flush again -- Mega may have sent garbage while WiFi was connecting
  while (Serial2.available()) { Serial2.read(); }
  serialBuffer = "";
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[ESP32] WiFi connected! IP: " + WiFi.localIP().toString());
    sendToMega("WIFI_OK");
  } else {
    Serial.println("[ESP32] WiFi FAILED");
    sendToMega("ERROR:WiFi connection failed");
  }
}
/* =================== MAIN LOOP =================== */
void loop() {
  // Read commands from Mega via Serial2
  while (Serial2.available()) {
    char c = Serial2.read();
    // Filter out garbage/null bytes
    if (c == '\0' || c < 0x0A) {
      continue;
    }
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        // Validate all characters are printable ASCII
        bool valid = true;
        for (unsigned int i = 0; i < serialBuffer.length(); i++) {
          if (serialBuffer[i] < 0x20 || serialBuffer[i] > 0x7E) {
            valid = false;
            break;
          }
        }
        if (valid) {
          Serial.println("[ESP32] CMD: " + serialBuffer);
          processCommand(serialBuffer);
        } else {
          Serial.println("[ESP32] CMD: (garbled data, ignored)");
        }
      }
      serialBuffer = "";
    } else {
      serialBuffer += c;
      // Safety: prevent buffer overflow
      if (serialBuffer.length() > 200) {
        serialBuffer = "";
      }
    }
  }
}
/* =================== SEND TO MEGA =================== */
void sendToMega(String msg) {
  Serial2.println(msg);
  Serial.println("[ESP32] -> Mega: " + msg);  // Debug
}
/* =================== COMMAND DISPATCHER =================== */
void processCommand(String cmd) {
  if (cmd.startsWith("GET_ASSIGNED_STOP:")) {
    String userId = cmd.substring(18);
    handleGetAssignedStop(userId);
  }
  else if (cmd == "GET_STOPS") {
    handleGetStops();
  }
  else if (cmd.startsWith("LOOKUP_USER:")) {
    String rfid = cmd.substring(12);
    handleLookupUser(rfid);
  }
  else if (cmd.startsWith("SAVE_USER:")) {
    String params = cmd.substring(10);
    int commaIdx = params.indexOf(',');
    String name = params.substring(0, commaIdx);
    String rfid = params.substring(commaIdx + 1);
    handleSaveUser(name, rfid);
  }
  else if (cmd.startsWith("CHECK_TRIP:")) {
    String rfid = cmd.substring(11);
    handleCheckTrip(rfid);
  }
  else if (cmd.startsWith("CREATE_TRIP:")) {
    String params = cmd.substring(12);
    int c1 = params.indexOf(',');
    int c2 = params.indexOf(',', c1 + 1);
    int c3 = params.indexOf(',', c2 + 1);
    String userId   = params.substring(0, c1);
    String fromId   = params.substring(c1 + 1, c2);
    String toId     = params.substring(c2 + 1, c3);
    String busNum   = params.substring(c3 + 1);
    handleCreateTrip(userId, fromId, toId, busNum);
  }
  else if (cmd.startsWith("VERIFY_TRIP:")) {
    String params = cmd.substring(12);
    int commaIdx = params.indexOf(',');
    String rfid     = params.substring(0, commaIdx);
    String stopId   = params.substring(commaIdx + 1);
    handleVerifyTrip(rfid, stopId);
  }
  else if (cmd.startsWith("GET_NEAREST:")) {
    String params = cmd.substring(12);
    int commaIdx = params.indexOf(',');
    String currentId = params.substring(0, commaIdx);
    String destId = params.substring(commaIdx + 1);
    handleGetNearest(currentId, destId);
  }
  else if (cmd.startsWith("POST_ALERT:")) {
    String params = cmd.substring(11);
    int c1 = params.indexOf(',');
    int c2 = params.indexOf(',', c1 + 1);
    String rfid    = params.substring(0, c1);
    String bpm     = params.substring(c1 + 1, c2);
    String smsSent = params.substring(c2 + 1);
    handlePostAlert(rfid, bpm, smsSent);
  }
  else if (cmd.startsWith("POLL_BUS:")) {
    String rfid = cmd.substring(9);
    handlePollBus(rfid);
  }
  else if (cmd.startsWith("CHECK_BUS_AT:")) {
    String params = cmd.substring(13);
    int commaIdx = params.indexOf(',');
    String busNum = params.substring(0, commaIdx);
    String stopId = params.substring(commaIdx + 1);
    handleCheckBusAt(busNum, stopId);
  }
  else if (cmd.startsWith("POST_HEARTBEAT:")) {
    String params = cmd.substring(15);
    int commaIdx = params.indexOf(',');
    String rfid = params.substring(0, commaIdx);
    String bpm  = params.substring(commaIdx + 1);
    handlePostHeartbeat(rfid, bpm);
  }
  else {
    sendToMega("ERROR:Unknown command");
  }
}
/* =================== HTTP HELPERS =================== */
String httpGET(String endpoint) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ESP32] WiFi not connected!");
    return "ERROR";
  }
  HTTPClient http;
  String url = String(BASE_URL) + endpoint;
  http.begin(url);
  int httpCode = http.GET();
  String payload = "";
  if (httpCode == HTTP_CODE_OK) {
    payload = http.getString();
  } else {
    Serial.println("[ESP32] HTTP GET failed: " + String(httpCode));
    payload = "ERROR";
  }
  http.end();
  return payload;
}
String httpPOST(String endpoint, String jsonBody) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ESP32] WiFi not connected!");
    return "ERROR";
  }
  HTTPClient http;
  String url = String(BASE_URL) + endpoint;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(jsonBody);
  String payload = "";
  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
    payload = http.getString();
  } else {
    Serial.println("[ESP32] HTTP POST failed: " + String(httpCode));
    payload = "ERROR";
  }
  http.end();
  return payload;
}
/* =================== HANDLER: GET ASSIGNED STOP =================== */
void handleGetAssignedStop(String userId) {
  // Use the new station module endpoint (no longer per-user)
  String response = httpGET("/api/station/");
  if (response == "ERROR") {
    sendToMega("ERROR:Failed to fetch assigned stop");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("ERROR:JSON parse failed");
    return;
  }
  // New API returns: {"id":1, "name":"...", "assigned_stop": {"id":1, "name":"Koyambedu", ...}}
  int stopId = doc["assigned_stop"]["id"];
  String stopName = doc["assigned_stop"]["name"].as<String>();
  sendToMega("ASSIGNED_STOP:" + String(stopId) + "," + stopName);
}
/* =================== HANDLER: GET ALL STOPS =================== */
void handleGetStops() {
  String response = httpGET("/api/stops/");
  if (response == "ERROR") {
    sendToMega("ERROR:Failed to fetch stops");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("ERROR:JSON parse failed");
    return;
  }
  JsonArray stops = doc.as<JsonArray>();
  String result = "STOPS:";
  for (int i = 0; i < stops.size(); i++) {
    if (i > 0) result += ";";
    int id = stops[i]["id"];
    String name = stops[i]["name"].as<String>();
    result += String(id) + "," + name;
  }
  sendToMega(result);
}
/* =================== HANDLER: LOOKUP USER BY RFID =================== */
void handleLookupUser(String rfid) {
  String response = httpGET("/api/users/rfid/" + rfid);
  if (response == "ERROR") {
    sendToMega("USER_NOT_FOUND");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("USER_NOT_FOUND");
    return;
  }
  int userId = doc["id"];
  String userName = doc["name"].as<String>();
  sendToMega("USER_FOUND:" + String(userId) + "," + userName);
}
/* =================== HANDLER: SAVE NEW USER =================== */
void handleSaveUser(String name, String rfid) {
  String jsonBody = "{\"name\":\"" + name + "\",\"rfid_number\":\"" + rfid + "\"}";
  String response = httpPOST("/api/users/", jsonBody);
  if (response == "ERROR") {
    sendToMega("USER_SAVED:0");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("USER_SAVED:0");
    return;
  }
  int userId = doc["id"];
  sendToMega("USER_SAVED:" + String(userId));
}
/* =================== HANDLER: CHECK ACTIVE TRIP =================== */
void handleCheckTrip(String rfid) {
  String response = httpGET("/api/trips/active/" + rfid);
  if (response == "ERROR") {
    // 404 = no active trip (expected), other errors = real problem
    // httpGET logs the status code, so we just treat all as NO_ACTIVE_TRIP
    // since the Mega will start a new booking flow either way
    Serial.println("[ESP32] No active trip or error for RFID: " + rfid);
    sendToMega("NO_ACTIVE_TRIP");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.println("[ESP32] JSON parse error in CHECK_TRIP");
    sendToMega("NO_ACTIVE_TRIP");
    return;
  }
  String status = doc["status"].as<String>();
  String destName = doc["to_stop"]["name"].as<String>();
  String busNumber = doc["assigned_bus_number"].as<String>();
  Serial.println("[ESP32] Active trip found: " + status + " bus=" + busNumber);
  sendToMega("TRIP_ACTIVE:" + status + "," + destName + "," + busNumber);
}
/* =================== HANDLER: CREATE TRIP =================== */
void handleCreateTrip(String userId, String fromId, String toId, String busNum) {
  String jsonBody = "{\"user_id\":" + userId +
                    ",\"from_stop_id\":" + fromId +
                    ",\"to_stop_id\":" + toId +
                    ",\"assigned_bus_number\":\"" + busNum + "\"}";
  String response = httpPOST("/api/trips/", jsonBody);
  if (response == "ERROR") {
    sendToMega("ERROR:Failed to create trip");
    return;
  }
  sendToMega("TRIP_CREATED");
}
/* =================== HANDLER: VERIFY TRIP =================== */
void handleVerifyTrip(String rfid, String stopId) {
  String jsonBody = "{\"current_stop_id\":" + stopId + "}";
  String response = httpPOST("/api/trips/verify/" + rfid, jsonBody);
  if (response == "ERROR") {
    Serial.println("[ESP32] VERIFY_TRIP HTTP failed for RFID: " + rfid);
    sendToMega("ERROR:Trip verification failed");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.println("[ESP32] JSON parse error in VERIFY_TRIP");
    sendToMega("ERROR:JSON parse failed");
    return;
  }
  String status = doc["status"].as<String>();
  String message = doc["message"].as<String>();
  String destination = doc.containsKey("destination") ? doc["destination"].as<String>() : "unknown";
  String busAt = doc.containsKey("bus_at") ? doc["bus_at"].as<String>() : "";
  Serial.println("[ESP32] Verify result: " + status + " dest=" + destination + " bus_at=" + busAt);
  // Format: VERIFY_RESULT:{status},{destination},{bus_at},{message}
  sendToMega("VERIFY_RESULT:" + status + "," + destination + "," + busAt + "," + message);
}
/* =================== HANDLER: GET NEAREST BUS =================== */
void handleGetNearest(String currentId, String destId) {
  String endpoint = "/api/buses/nearest?current_stop_id=" + currentId +
                    "&destination_stop_id=" + destId;
  String response = httpGET(endpoint);
  if (response == "ERROR") {
    sendToMega("ERROR:No bus found");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("ERROR:JSON parse failed");
    return;
  }
  String busNumber = doc["bus_number"].as<String>();
  String busCurrentStop = doc["current_stop"]["name"].as<String>();
  // Read stops_away from the backend (computed from actual route positions)
  int stopsAway = doc.containsKey("stops_away") ? doc["stops_away"].as<int>() : 0;
  sendToMega("NEAREST:" + busNumber + "," + busCurrentStop + "," + String(stopsAway));
}
/* =================== HANDLER: POST HEALTH ALERT =================== */
void handlePostAlert(String rfid, String bpm, String smsSent) {
  String jsonBody = "{\"rfid\":\"" + rfid + "\",\"bpm\":" + bpm + ",\"sms_sent\":" + smsSent + "}";
  String response = httpPOST("/api/alerts/", jsonBody);
  if (response == "ERROR") {
    Serial.println("[ESP32] Alert POST failed for RFID: " + rfid);
    sendToMega("ALERT_LOGGED:0");
    return;
  }
  Serial.println("[ESP32] Alert logged: BPM=" + bpm + " RFID=" + rfid);
  sendToMega("ALERT_LOGGED:1");
}
/* =================== HANDLER: POLL BUS POSITION =================== */
void handlePollBus(String rfid) {
  String response = httpGET("/api/trips/bus-position/" + rfid);
  if (response == "ERROR") {
    sendToMega("BUS_POLL_FAIL");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("BUS_POLL_FAIL");
    return;
  }
  int stopId = doc["stop_id"];
  String stopName = doc["stop_name"].as<String>();
  sendToMega("BUS_AT_STOP:" + String(stopId) + "," + stopName);
}
/* =================== HANDLER: CHECK BUS AT STOP =================== */
void handleCheckBusAt(String busNum, String stopId) {
  String response = httpGET("/api/buses/" + busNum + "/at-stop/" + stopId);
  if (response == "ERROR") {
    sendToMega("BUS_NOT_YET:unknown");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    sendToMega("BUS_NOT_YET:unknown");
    return;
  }
  bool arrived = doc["arrived"];
  String currentStopName = doc["current_stop_name"].as<String>();
  if (arrived) {
    sendToMega("BUS_ARRIVED");
  } else {
    sendToMega("BUS_NOT_YET:" + currentStopName);
  }
}
/* =================== HANDLER: POST LIVE HEARTBEAT =================== */
void handlePostHeartbeat(String rfid, String bpm) {
  String jsonBody = "{\"rfid\":\"" + rfid + "\",\"bpm\":" + bpm + "}";
  String response = httpPOST("/api/alerts/heartbeat", jsonBody);
  if (response == "ERROR") {
    Serial.println("[ESP32] Heartbeat POST failed for RFID: " + rfid);
  } else {
    Serial.println("[ESP32] Heartbeat posted: BPM=" + bpm + " RFID=" + rfid);
  }
}
