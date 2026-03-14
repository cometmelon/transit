/*
 * ===========================================================================
 *  CHENNAI TRANSIT STATION -- ARDUINO MEGA SKETCH
 * ===========================================================================
 *
 *  Hardware (from project setup):
 *    - EM-18 RFID Reader   -> Serial  (RX0)           [12-char hex output]
 *    - GSM SIM800          -> Serial1 (TX1/RX1)        [reserved]
 *    - DFPlayer Mini       -> Serial2 (TX2/RX2)        [audio prompts]
 *    - ESP32               -> Serial3 (TX3 pin14/RX3 pin15) [WiFi bridge]
 *    - Button 1            -> D2 (INPUT_PULLUP)
 *    - Button 2            -> D3 (INPUT_PULLUP)
 *    - Button 3            -> D4 (INPUT_PULLUP)
 *    - Buzzer              -> D5
 *    - Vibration Motor     -> D6
 *    - Speaker             -> DFPlayer SPK pins
 *
 *  DFPlayer SD Card Tracks:
 *    Track 01 -> "Welcome to Chennai Transit Station"
 *    Track 02 -> "Please tap your RFID card"
 *    Track 03 -> "Card recognized. Please select your destination"
 *    Track 04 -> "Invalid card. Please try again"
 *    Track 05 -> "New user registered. Please select your destination"
 *    Track 06 -> "Destination selected. Finding your bus"
 *    Track 07 -> "Bus found. Please wait at the stop"
 *    Track 08 -> "No bus available. Please try again later"
 *    Track 09 -> "Error occurred. Please try again"
 *    Track 10 -> "System ready"
 *    Track 17 -> "Now arriving at Koyambedu"
 *    Track 18 -> "Now arriving at T. Nagar"
 *    Track 19 -> "Now arriving at Tambaram"
 *    Track 20 -> "Now arriving at Broadway"
 *    Track 21 -> "Now arriving at Adyar"
 *
 *  Flow:
 *    1. Boot -> request assigned stop from backend (user_id = 1)
 *    2. Request all stops -> assign 3 buttons (exclude current stop)
 *    3. Wait for RFID tap -> EM-18 outputs 12-char hex ID
 *    4. Lookup user by RFID from backend via ESP32
 *       - If found -> announce greeting via DFPlayer
 *       - If not found -> register as new user
 *    5. Wait for button press -> select destination
 *    6. Register trip to backend + request nearest bus
 *    7. Announce bus info via DFPlayer + buzzer feedback
 *    8. Reset for next passenger
 */
#include <DFRobotDFPlayerMini.h>
/* =================== DFPLAYER =================== */
DFRobotDFPlayerMini player;
/* =================== AUDIO TRACK NUMBERS =================== */
#define TRACK_WELCOME          1
#define TRACK_TAP_CARD         2
#define TRACK_CARD_OK          3
#define TRACK_CARD_INVALID     4
#define TRACK_NEW_USER         5
#define TRACK_FINDING_BUS      6
#define TRACK_BUS_FOUND        7
#define TRACK_NO_BUS           8
#define TRACK_ERROR            9
#define TRACK_READY            10
#define TRACK_BOARDED_OK       11
#define TRACK_NOT_YOUR_STOP    12
#define TRACK_REACHED_DEST     13
#define TRACK_WRONG_BUS        14
#define TRACK_WAIT_FOR_BUS     15
#define TRACK_HEART_ALERT      16
/* Stop-name announcement tracks (mapped to stop IDs 1-5) */
#define TRACK_STOP_KOYAMBEDU   17
#define TRACK_STOP_TNAGAR      18
#define TRACK_STOP_TAMBARAM    19
#define TRACK_STOP_BROADWAY    20
#define TRACK_STOP_ADYAR       21
/* =================== TRACK NAME LOOKUP =================== */
const char* trackNames[] = {
  "",                                                    // 0 (unused)
  "Welcome to Chennai Transit Station",                  // 1
  "Please tap your RFID card",                           // 2
  "Card recognized. Please select your destination",     // 3
  "Invalid card. Please try again",                      // 4
  "New user registered. Please select your destination", // 5
  "Destination selected. Finding your bus",              // 6
  "Bus found. Please wait at the stop",                  // 7
  "No bus available. Please try again later",            // 8
  "Error occurred. Please try again",                    // 9
  "System ready",                                        // 10
  "You have boarded the correct bus",                    // 11
  "Not your stop yet. Please stay on the bus",           // 12
  "You have reached your destination. Please get down",  // 13
  "Wrong bus. Please check your bus number",             // 14
  "Your bus is on the way. Please wait",                  // 15
  "Alert. Abnormal heart rate detected",                  // 16
  "Now arriving at Koyambedu",                           // 17
  "Now arriving at T. Nagar",                            // 18
  "Now arriving at Tambaram",                            // 19
  "Now arriving at Broadway",                            // 20
  "Now arriving at Adyar"                                // 21
};
#define TRACK_COUNT 21
void playTrack(int track) {
  player.play(track);
  if (track >= 1 && track <= TRACK_COUNT) {
    Serial.println("[AUDIO] Playing: " + String(trackNames[track]));
  } else {
    Serial.println("[AUDIO] Playing track: " + String(track));
  }
}
/* =================== PINS =================== */
#define SW1       2
#define SW2       3
#define SW3       4
#define BUZZER    5
#define VIBRATION 6
#define HEART_PIN A0
/* =================== GSM CONFIG =================== */
String guardianPhone = "+919876543210";  // Default guardian number
/* =================== HEARTBEAT CONFIG =================== */
#define BPM_HIGH_THRESHOLD 150
#define BPM_LOW_THRESHOLD  40
#define BPM_READ_INTERVAL  10000   // Read every 10 seconds
#define BPM_ALERT_COUNT    3       // 3 consecutive abnormal = alert
unsigned long lastBpmRead = 0;
int currentBPM = 0;
int abnormalCount = 0;
bool alertSent = false;   // Prevent spamming alerts
bool isMonitoring = false;
/* =================== DEFAULT CONFIG =================== */
#define STATION_USER_ID  1
/* =================== DEBOUNCE =================== */
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 300;
/* =================== STATE MACHINE =================== */
enum SystemState {
  WAITING_WIFI,
  BOOTING,
  LOADING_STOPS,
  IDLE_WAITING_RFID,
  CHECKING_TRIP,
  LOOKING_UP_USER,
  REGISTERING_USER,
  WAIT_FOR_DESTINATION,
  FETCHING_BUS,
  CREATING_TRIP,
  VERIFYING_TRIP,
  WAITING_FOR_BUS,   // NEW: polling until assigned bus arrives at station
  MONITORING_RIDE,   // NEW: polling bus position while passenger is boarded
  DISPLAY_RESULT
};
/* =================== STOP ANNOUNCEMENT POLLING =================== */
#define BUS_POLL_INTERVAL 10000   // Poll bus position every 10 seconds
unsigned long lastBusPoll = 0;
int lastAnnouncedStopId = -1;     // Track the last stop we announced
SystemState systemState = WAITING_WIFI;
/* =================== BUS STOP DATA =================== */
struct BusStopInfo {
  int id;
  String name;
};
BusStopInfo currentStop;                // The station's assigned stop
BusStopInfo buttonStops[3];             // 3 stops mapped to buttons
int buttonStopCount = 0;
/* =================== USER DATA =================== */
int     userId   = 1;
String  userName = "Surya";
String  userRfid = "3E0062D2DE50";
/* =================== DESTINATION & BUS =================== */
BusStopInfo selectedDest;
String assignedBusNumber = "";
/* =================== TIMEOUT PROTECTION =================== */
unsigned long stateEnteredAt = 0;
const unsigned long ESP_TIMEOUT = 15000; // 15 seconds
/* =================== ESP32 UART BUFFER =================== */
String espBuffer = "";
/* =================== RFID BUFFER =================== */
String rfidBuffer = "";
/* =================== BUTTON STATE =================== */
bool lastSW1, lastSW2, lastSW3;
/* =================== SETUP =================== */
void setup() {
  Serial.begin(9600);   // EM-18 RFID reader + USB debug
  Serial1.begin(9600);  // GSM SIM800 (reserved)
  Serial2.begin(9600);  // DFPlayer Mini
  Serial3.begin(9600);  // ESP32 UART
  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(VIBRATION, OUTPUT);
  digitalWrite(BUZZER, LOW);
  digitalWrite(VIBRATION, LOW);
  // Initialize DFPlayer
  if (!player.begin(Serial2)) {
    Serial.println("[MEGA] DFPlayer Error!");
  } else {
    player.volume(25);
    Serial.println("[MEGA] DFPlayer OK");
  }
  delay(1000);
  // Flush any garbage from ESP32 serial buffer
  while (Serial3.available()) { Serial3.read(); }
  playTrack(TRACK_WELCOME);
  Serial.println("[MEGA] Chennai Transit Station -- Booting...");
  Serial.println("[MEGA] Waiting for ESP32 WiFi...");
  // DON'T send commands yet -- wait for WIFI_OK from ESP32
  systemState = WAITING_WIFI;
}
/* =================== MAIN LOOP =================== */
void loop() {
  // Always check for ESP32 responses
  checkESPResponse();
  // Always check heart rate (runs only when isMonitoring=true)
  checkHeartBeat();
  switch (systemState) {
    case WAITING_WIFI:
      // Waiting for ESP32 to connect WiFi and send WIFI_OK
      break;
    case BOOTING:
      // Waiting for assigned stop response from ESP32
      break;
    case LOADING_STOPS:
      // Waiting for stop list response from ESP32
      break;
    case IDLE_WAITING_RFID:
      checkRFID();
      break;
    case CHECKING_TRIP:
    case LOOKING_UP_USER:
    case REGISTERING_USER:
    case FETCHING_BUS:
    case CREATING_TRIP:
    case VERIFYING_TRIP:
      // All these states wait for ESP32 response - timeout if too long
      if (millis() - stateEnteredAt > ESP_TIMEOUT) {
        Serial.println("[MEGA] TIMEOUT waiting for ESP32 response!");
        playTrack(TRACK_ERROR);
        beepError();
        delay(2000);
        resetForNextPassenger();
      }
      break;
    case WAIT_FOR_DESTINATION:
      checkButtons();
      break;
    case MONITORING_RIDE:
      // Periodically poll bus position and announce stop names
      checkRFID();      // Allow card tap to alight/verify
      pollBusPosition();
      break;
    case WAITING_FOR_BUS:
      // Poll to check if assigned bus has arrived at this station
      pollBusArrival();
      break;
    case DISPLAY_RESULT:
      // Result announced -- wait, then reset
      delay(8000);
      resetForNextPassenger();
      break;
  }
}
/* =================== ESP32 COMMUNICATION =================== */
void sendToESP(String cmd) {
  delay(50);  // Small delay to let ESP32 be ready
  Serial3.println(cmd);
  Serial.println("[MEGA] -> ESP: " + cmd);
}
void checkESPResponse() {
  while (Serial3.available()) {
    char c = Serial3.read();
    // Filter out garbage/null bytes
    if (c == '\0' || c < 0x0A) {
      continue;
    }
    if (c == '\n') {
      espBuffer.trim();
      if (espBuffer.length() > 0) {
        // Check for printable ASCII -- skip garbled responses
        bool valid = true;
        for (unsigned int i = 0; i < espBuffer.length(); i++) {
          if (espBuffer[i] < 0x20 || espBuffer[i] > 0x7E) {
            valid = false;
            break;
          }
        }
        if (valid) {
          Serial.println("[MEGA] ? ESP: " + espBuffer);
          processESPResponse(espBuffer);
        } else {
          Serial.println("[MEGA] ? ESP: (garbled data, ignored)");
        }
      }
      espBuffer = "";
    } else {
      espBuffer += c;
      // Safety: prevent buffer overflow
      if (espBuffer.length() > 200) {
        espBuffer = "";
      }
    }
  }
}
void processESPResponse(String response) {
  // ?? ASSIGNED_STOP:{id},{name} ??
  if (response.startsWith("ASSIGNED_STOP:")) {
    String data = response.substring(14);
    int commaIdx = data.indexOf(',');
    currentStop.id = data.substring(0, commaIdx).toInt();
    currentStop.name = data.substring(commaIdx + 1);
    Serial.println("[MEGA] Station: " + currentStop.name);
    beepOK();
    delay(1500);
    // Step 3: Request all stops
    sendToESP("GET_STOPS");
    systemState = LOADING_STOPS;
  }
  // ?? STOPS:{id1},{name1};{id2},{name2};... ??
  else if (response.startsWith("STOPS:")) {
    String data = response.substring(6);
    buttonStopCount = 0;
    while (data.length() > 0 && buttonStopCount < 3) {
      int semiIdx = data.indexOf(';');
      String entry;
      if (semiIdx == -1) {
        entry = data;
        data = "";
      } else {
        entry = data.substring(0, semiIdx);
        data = data.substring(semiIdx + 1);
      }
      int commaIdx = entry.indexOf(',');
      int stopId = entry.substring(0, commaIdx).toInt();
      String stopName = entry.substring(commaIdx + 1);
      // Exclude the current station stop
      if (stopId != currentStop.id) {
        buttonStops[buttonStopCount].id = stopId;
        buttonStops[buttonStopCount].name = stopName;
        buttonStopCount++;
      }
    }
    // Log button assignments to Serial monitor
    Serial.println("[MEGA] Button assignments:");
    for (int i = 0; i < buttonStopCount && i < 3; i++) {
      Serial.println("  Btn" + String(i + 1) + " -> " +
                     buttonStops[i].name + " (ID:" + String(buttonStops[i].id) + ")");
    }
    // Record initial button states
    lastSW1 = digitalRead(SW1);
    lastSW2 = digitalRead(SW2);
    lastSW3 = digitalRead(SW3);
    // Ready for RFID
    playTrack(TRACK_TAP_CARD);
    Serial.println("[MEGA] Ready -- Tap RFID Card");
    systemState = IDLE_WAITING_RFID;
  }
  // ?? USER_FOUND:{user_id},{name} ??
  else if (response.startsWith("USER_FOUND:")) {
    String data = response.substring(11);
    int commaIdx = data.indexOf(',');
    userId   = data.substring(0, commaIdx).toInt();
    userName = data.substring(commaIdx + 1);
    beepOK();
    playTrack(TRACK_CARD_OK);
    Serial.println("[MEGA] Welcome " + userName + "! Select destination.");
    delay(2500);
    // Ready for destination selection
    lastSW1 = digitalRead(SW1);
    lastSW2 = digitalRead(SW2);
    lastSW3 = digitalRead(SW3);
    systemState = WAIT_FOR_DESTINATION;
  }
  // ?? USER_NOT_FOUND ??
  else if (response == "USER_NOT_FOUND") {
    // Register this RFID as a new user
    Serial.println("[MEGA] New user -- registering...");
    sendToESP("SAVE_USER:Passenger," + userRfid);
    stateEnteredAt = millis();
    systemState = REGISTERING_USER;
  }
  // ?? USER_SAVED:{user_id} ??
  else if (response.startsWith("USER_SAVED:")) {
    String data = response.substring(11);
    userId   = data.toInt();
    userName = "Passenger";
    beepOK();
    playTrack(TRACK_NEW_USER);
    Serial.println("[MEGA] New user registered! Select destination.");
    delay(2500);
    // Ready for destination selection
    lastSW1 = digitalRead(SW1);
    lastSW2 = digitalRead(SW2);
    lastSW3 = digitalRead(SW3);
    systemState = WAIT_FOR_DESTINATION;
  }
  // ?? NO_ACTIVE_TRIP ?? (no existing trip, proceed to booking)
  else if (response == "NO_ACTIVE_TRIP") {
    Serial.println("[MEGA] No active trip -- starting booking flow");
    // Look up the user by RFID
    sendToESP("LOOKUP_USER:" + userRfid);
    stateEnteredAt = millis();
    systemState = LOOKING_UP_USER;
  }
  // ?? TRIP_ACTIVE:{status},{dest_name},{bus_number} ?? (existing trip found)
  else if (response.startsWith("TRIP_ACTIVE:")) {
    String data = response.substring(12);
    int c1 = data.indexOf(',');
    int c2 = data.indexOf(',', c1 + 1);
    String tripStatus = data.substring(0, c1);
    String destName   = data.substring(c1 + 1, c2);
    String busNum     = data.substring(c2 + 1);
    if (tripStatus == "waiting") {
      // Second tap = boarding the bus - verify via backend
      Serial.println("[MEGA] Active trip: WAITING, verifying boarding for bus " + busNum + " to " + destName);
      sendToESP("VERIFY_TRIP:" + userRfid + "," + String(currentStop.id));
      stateEnteredAt = millis();
      systemState = VERIFYING_TRIP;
    }
    else if (tripStatus == "boarded") {
      // On the bus - verify if they reached destination
      Serial.println("[MEGA] Active trip: boarded, verifying stop...");
      // Start heart monitoring (trip is already boarded)
      assignedBusNumber = busNum;
      selectedDest.name = destName;
      isMonitoring = true;
      alertSent = false;
      abnormalCount = 0;
      Serial.println("[HEART] Monitoring started for " + userName);
      sendToESP("VERIFY_TRIP:" + userRfid + "," + String(currentStop.id));
      stateEnteredAt = millis();
      systemState = VERIFYING_TRIP;
    }
    else {
      // Unknown trip status - log and reset
      Serial.println("[MEGA] Unknown trip status: " + tripStatus + ", resetting...");
      playTrack(TRACK_ERROR);
      beepError();
      delay(2000);
      resetForNextPassenger();
    }
  }
  // ?? VERIFY_RESULT:{status},{destination},{bus_at},{message} ??
  else if (response.startsWith("VERIFY_RESULT:")) {
    String data = response.substring(14);
    int c1 = data.indexOf(',');
    int c2 = data.indexOf(',', c1 + 1);
    int c3 = data.indexOf(',', c2 + 1);
    String vStatus  = data.substring(0, c1);
    String destName = data.substring(c1 + 1, c2);
    String busAt    = data.substring(c2 + 1, c3);
    String vMessage = data.substring(c3 + 1);
    if (vStatus == "BOARDED") {
      playTrack(TRACK_BOARDED_OK);
      beepOK();
      Serial.println("[MEGA] BUS VERIFIED: Your bus has arrived! Please board.");
      Serial.println("[MEGA] Destination: " + destName);
      // Start heart rate monitoring
      isMonitoring = true;
      alertSent = false;
      abnormalCount = 0;
      Serial.println("[HEART] Monitoring started for " + userName);
    }
    else if (vStatus == "BUS_NOT_HERE") {
      playTrack(TRACK_WAIT_FOR_BUS);
      beepOK();
      Serial.println("[MEGA] Bus not here yet. Bus is at: " + busAt);
      Serial.println("[MEGA] Destination: " + destName);
    }
    else if (vStatus == "COMPLETED") {
      playTrack(TRACK_REACHED_DEST);
      beepOK();
      Serial.println("[MEGA] You reached your destination: " + destName + "!");
      Serial.println("[MEGA] Bus is at: " + busAt);
    }
    else if (vStatus == "ON_ROUTE") {
      playTrack(TRACK_NOT_YOUR_STOP);
      beepOK();
      Serial.println("[MEGA] Not your stop yet. Bus is at: " + busAt);
      Serial.println("[MEGA] Destination: " + destName);
    }
    if (vStatus == "BOARDED" || vStatus == "ON_ROUTE") {
      // Passenger is on the bus -- switch to ride monitoring mode
      delay(2000);
      lastAnnouncedStopId = -1;  // Reset so first poll announces
      lastBusPoll = 0;          // Poll immediately
      Serial.println("[MEGA] Entering MONITORING_RIDE mode (stop announcements active)");
      systemState = MONITORING_RIDE;
    } else {
      // COMPLETED or BUS_NOT_HERE -- full reset
      delay(4000);
      resetForNextPassenger();
    }
  }
  // ?? TRIP_CREATED ??
  else if (response == "TRIP_CREATED") {
    Serial.println("[MEGA] Trip created! Waiting for bus " + assignedBusNumber + " to arrive...");
    playTrack(TRACK_WAIT_FOR_BUS);
    beepOK();
    lastBusPoll = 0;  // Poll immediately
    systemState = WAITING_FOR_BUS;
  }
  // ?? NEAREST:{bus_number},{current_stop_name},{stops_away} ??
  else if (response.startsWith("NEAREST:")) {
    String data = response.substring(8);
    int c1 = data.indexOf(',');
    int c2 = data.indexOf(',', c1 + 1);
    String busNumber = data.substring(0, c1);
    String busAt = data.substring(c1 + 1, c2);
    String stopsAway = data.substring(c2 + 1);
    assignedBusNumber = busNumber;
    // Announce via DFPlayer
    playTrack(TRACK_BUS_FOUND);
    beepOK();
    // Log full details to serial monitor
    Serial.println("[MEGA] ==============================");
    Serial.println("[MEGA] Bus: " + busNumber);
    Serial.println("[MEGA] Currently at: " + busAt);
    Serial.println("[MEGA] Stops away: " + stopsAway);
    Serial.println("[MEGA] Destination: " + selectedDest.name);
    Serial.println("[MEGA] ==============================");
    // Now create the trip in backend
    sendToESP("CREATE_TRIP:" + String(userId) + "," +
              String(currentStop.id) + "," + String(selectedDest.id) + "," +
              assignedBusNumber);
    stateEnteredAt = millis();
    systemState = CREATING_TRIP;
  }
  // ?? WIFI_OK ??
  else if (response == "WIFI_OK") {
    Serial.println("[MEGA] ESP32 WiFi connected!");
    beepOK();
    // NOW safe to start the boot sequence
    if (systemState == WAITING_WIFI) {
      delay(500);
      sendToESP("GET_ASSIGNED_STOP:" + String(STATION_USER_ID));
      systemState = BOOTING;
    }
  }
  // ?? BUS_ARRIVED ?? (assigned bus is at station)
  else if (response == "BUS_ARRIVED") {
    Serial.println("[MEGA] *** YOUR BUS HAS ARRIVED! ***");
    playTrack(TRACK_BUS_FOUND);  // "Bus found. Please wait at the stop"
    beepOK();
    delay(500);
    beepOK();
    delay(500);
    beepOK();
    // Vibrate to alert the blind passenger
    digitalWrite(VIBRATION, HIGH);
    delay(2000);
    digitalWrite(VIBRATION, LOW);
    Serial.println("[MEGA] Bus " + assignedBusNumber + " arrived at your stop!");
    Serial.println("[MEGA] Tap your RFID card to board.");
    // Now wait for RFID tap -- boarding happens only when card is tapped
    systemState = IDLE_WAITING_RFID;
  }
  // ?? BUS_NOT_YET:{current_stop_name} ?? (bus hasn't arrived yet)
  else if (response.startsWith("BUS_NOT_YET:")) {
    String busAt = response.substring(12);
    Serial.println("[MEGA] Bus " + assignedBusNumber + " is at " + busAt + " - waiting...");
  }
  // ?? BUS_AT_STOP:{stop_id},{stop_name} ?? (bus position poll response)
  else if (response.startsWith("BUS_AT_STOP:")) {
    String data = response.substring(12);
    int commaIdx = data.indexOf(',');
    int stopId = data.substring(0, commaIdx).toInt();
    String stopName = data.substring(commaIdx + 1);
    Serial.println("[ANNOUNCE] Bus is at: " + stopName + " (ID:" + String(stopId) + ")");
    // Only announce if the stop changed
    if (stopId != lastAnnouncedStopId) {
      lastAnnouncedStopId = stopId;
      int track = getStopTrack(stopId);
      if (track > 0) {
        playTrack(track);
        beepOK();
        Serial.println("[ANNOUNCE] Now arriving at: " + stopName);
      }
    }
  }
  // ?? BUS_POLL_FAIL ?? (poll failed, ignore silently)
  else if (response == "BUS_POLL_FAIL") {
    Serial.println("[ANNOUNCE] Bus poll failed (may have alighted)");
  }
  // ?? ERROR:{message} ??
  else if (response.startsWith("ERROR:")) {
    String errMsg = response.substring(6);
    Serial.println("[MEGA] ERROR: " + errMsg);
    playTrack(TRACK_ERROR);
    beepError();
    delay(3000);
    resetForNextPassenger();
  }
}
/* =================== RFID READING (EM-18) =================== */
void checkRFID() {
  if (Serial.available()) {
    String rawData = Serial.readStringUntil('\n');
    rawData.trim();
    if (rawData.length() == 0) return;
    // Debug: show raw data and length
    Serial.print("[RFID] Raw data (len=");
    Serial.print(rawData.length());
    Serial.print("): ");
    Serial.println(rawData);
    // Strip non-printable chars (STX=0x02, ETX=0x03, etc.)
    String cleanRfid = "";
    for (unsigned int i = 0; i < rawData.length(); i++) {
      char c = rawData[i];
      // Keep only hex characters (0-9, A-F, a-f)
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
        cleanRfid += c;
      }
    }
    Serial.println("[RFID] Clean ID: " + cleanRfid);
    // EM-18 outputs 10-char ID + 2-char checksum = 12 hex chars
    // Cap at 12 to prevent double-reads
    if (cleanRfid.length() > 12) {
      cleanRfid = cleanRfid.substring(0, 12);
      Serial.println("[RFID] Trimmed to 12: " + cleanRfid);
    }
    if (cleanRfid.length() >= 10) {
      userRfid = cleanRfid;
      beepOK();
      Serial.println("[MEGA] RFID Scanned: " + userRfid);
      // SMART CHECK: first see if this card has an active trip
      sendToESP("CHECK_TRIP:" + userRfid);
      stateEnteredAt = millis();
      systemState = CHECKING_TRIP;
    } else {
      Serial.println("[RFID] Too short, ignoring");
    }
  }
}
/* =================== BUTTON HANDLING =================== */
void checkButtons() {
  bool sw1 = digitalRead(SW1);
  bool sw2 = digitalRead(SW2);
  bool sw3 = digitalRead(SW3);
  if (buttonStopCount >= 1 && lastSW1 == HIGH && sw1 == LOW && debounceOK()) {
    selectDestination(0);
  }
  if (buttonStopCount >= 2 && lastSW2 == HIGH && sw2 == LOW && debounceOK()) {
    selectDestination(1);
  }
  if (buttonStopCount >= 3 && lastSW3 == HIGH && sw3 == LOW && debounceOK()) {
    selectDestination(2);
  }
  lastSW1 = sw1;
  lastSW2 = sw2;
  lastSW3 = sw3;
}
void selectDestination(int index) {
  selectedDest = buttonStops[index];
  playTrack(TRACK_FINDING_BUS);
  beepOK();
  Serial.println("[MEGA] Destination: " + selectedDest.name + " -- Finding bus...");
  delay(500);
  // Find nearest bus first, then create trip with bus number
  sendToESP("GET_NEAREST:" + String(currentStop.id) + "," + String(selectedDest.id));
  stateEnteredAt = millis();
  systemState = FETCHING_BUS;
}
/* =================== BUS POSITION POLLING =================== */
void pollBusPosition() {
  if (millis() - lastBusPoll < BUS_POLL_INTERVAL) return;
  lastBusPoll = millis();
  if (userRfid.length() > 0) {
    Serial.println("[ANNOUNCE] Polling bus position...");
    sendToESP("POLL_BUS:" + userRfid);
  }
}
/* =================== BUS ARRIVAL POLLING =================== */
void pollBusArrival() {
  if (millis() - lastBusPoll < BUS_POLL_INTERVAL) return;
  lastBusPoll = millis();
  if (assignedBusNumber.length() > 0) {
    Serial.println("[MEGA] Checking if bus " + assignedBusNumber + " arrived at stop " + String(currentStop.id) + "...");
    sendToESP("CHECK_BUS_AT:" + assignedBusNumber + "," + String(currentStop.id));
  }
}
/* =================== STOP -> TRACK MAPPING =================== */
int getStopTrack(int stopId) {
  // Map stop IDs (1-5) to DFPlayer track numbers (17-21)
  switch (stopId) {
    case 1: return TRACK_STOP_KOYAMBEDU;   // 17
    case 2: return TRACK_STOP_TNAGAR;      // 18
    case 3: return TRACK_STOP_TAMBARAM;    // 19
    case 4: return TRACK_STOP_BROADWAY;    // 20
    case 5: return TRACK_STOP_ADYAR;       // 21
    default: return 0;  // Unknown stop
  }
}
/* =================== FEEDBACK =================== */
void beepOK() {
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
}
void beepError() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    digitalWrite(VIBRATION, HIGH);
    delay(200);
    digitalWrite(BUZZER, LOW);
    digitalWrite(VIBRATION, LOW);
    delay(100);
  }
}
/* =================== RESET =================== */
void resetForNextPassenger() {
  userId   = 0;
  userName = "";
  userRfid = "";
  rfidBuffer = "";
  selectedDest.id = 0;
  selectedDest.name = "";
  assignedBusNumber = "";
  // Reset heart monitoring
  isMonitoring = false;
  abnormalCount = 0;
  alertSent = false;
  currentBPM = 0;
  playTrack(TRACK_TAP_CARD);
  Serial.println("[MEGA] Ready -- Tap RFID Card");
  systemState = IDLE_WAITING_RFID;
}
/* =================== DEBOUNCE =================== */
bool debounceOK() {
  if (millis() - lastDebounceTime > debounceDelay) {
    lastDebounceTime = millis();
    return true;
  }
  return false;
}
/* =================== HEARTBEAT SENSOR =================== */
int readHeartRate() {
  // Read analog pulse sensor on A0
  // Take 10 samples and average for smoother reading
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(HEART_PIN);
    delay(10);
  }
  int avg = total / 10;
  // Map analog value (0-1023) to approximate BPM (40-200)
  // This is a simplified mapping for demo purposes
  int bpm = map(avg, 0, 1023, 40, 200);
  return bpm;
}
/* =================== GSM SMS =================== */
void sendSMS(String phone, String message) {
  Serial.println("[GSM] Sending SMS to: " + phone);
  Serial.println("[GSM] Message: " + message);
  Serial1.println("AT");
  delay(500);
  Serial1.println("AT+CMGF=1");  // Text mode
  delay(500);
  Serial1.println("AT+CMGS=\"" + phone + "\"");
  delay(500);
  Serial1.print(message);
  delay(100);
  Serial1.write(26);  // Ctrl+Z to send
  delay(3000);
  Serial.println("[GSM] SMS sent!");
}
/* =================== HEARTBEAT MONITOR =================== */
void checkHeartBeat() {
  // Only monitor when passenger is BOARDED (active trip on bus)
  if (!isMonitoring) return;
  if (millis() - lastBpmRead < BPM_READ_INTERVAL) return;
  lastBpmRead = millis();
  currentBPM = readHeartRate();
  // Always log the reading
  Serial.println("[HEART] BPM=" + String(currentBPM) + " | Status: " +
    ((currentBPM > BPM_HIGH_THRESHOLD || currentBPM < BPM_LOW_THRESHOLD) ? "WARNING" : "OK"));
  // Always send live heartbeat to backend (for driver portal display)
  sendToESP("POST_HEARTBEAT:" + userRfid + "," + String(currentBPM));
  // Check if abnormal
  if (currentBPM > BPM_HIGH_THRESHOLD || currentBPM < BPM_LOW_THRESHOLD) {
    abnormalCount++;
    Serial.println("[HEART] Abnormal count: " + String(abnormalCount) + "/" + String(BPM_ALERT_COUNT));
    if (abnormalCount >= BPM_ALERT_COUNT && !alertSent) {
      // ALERT!
      Serial.println("[HEART] !! ALERT !! Abnormal heart rate: " + String(currentBPM) + " BPM");
      // 1. Audio alert
      playTrack(TRACK_HEART_ALERT);
      // 2. Buzzer alarm (long beep)
      digitalWrite(BUZZER, HIGH);
      digitalWrite(VIBRATION, HIGH);
      delay(3000);
      digitalWrite(BUZZER, LOW);
      digitalWrite(VIBRATION, LOW);
      // 3. Send SMS via GSM
      String smsMsg = "ALERT: Passenger " + userName +
                      " has abnormal heart rate (" + String(currentBPM) + " BPM)." +
                      " Bus: " + assignedBusNumber +
                      ", Destination: " + selectedDest.name +
                      ". Please check immediately.";
      sendSMS(guardianPhone, smsMsg);
      // 4. Log to backend via ESP32
      sendToESP("POST_ALERT:" + userRfid + "," + String(currentBPM) + ",1");
      alertSent = true;  // Prevent repeated alerts
    }
  } else {
    // Normal reading - reset counter
    abnormalCount = 0;
  }
}
