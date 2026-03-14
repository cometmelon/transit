#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>

/* ================= RFID ================= */
String i = "";

/* ================= CARD IDs ================= */
String mainCard1   = "3E00F744038E";
String mainCard2   = "3E0062D58A03";
String mainCard3   = "3E0062D2DE50";
String defaultCard = "3E0062D569E0";

String num = "7395892866";

/* ================= SWITCH PINS ================= */
#define SW1 2
#define SW2 3
#define SW3 4

/* ================= ALERT OUTPUT ================= */
#define BUZZER     5
#define VIBRATION  6
#define HBpin A0
int HB = 0;

/* ================= DFPLAYER ================= */
DFRobotDFPlayerMini player;

/* ================= SWITCH STATE ================= */
bool lastSW1;
bool lastSW2;
bool lastSW3;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

/* ================= SYSTEM STATE ================= */
enum SystemState {
  IDLE,
  WAIT_FOR_DESTINATION
};

SystemState systemState = IDLE;

/* ✅ NEW FLAG TO PREVENT AUTO SELECTION */
bool destinationReady = false;

/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);     // RFID
  Serial1.begin(9600);    // GSM
  Serial2.begin(9600);    // DFPlayer

  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);

  pinMode(BUZZER, OUTPUT);
  pinMode(VIBRATION, OUTPUT);

  digitalWrite(BUZZER, LOW);
  digitalWrite(VIBRATION, LOW);

  if (!player.begin(Serial2)) {
    Serial.println("DFPlayer Error");
  } else {
    player.volume(30);
  }

  Serial.println("System Ready");
  Serial.println("Tap RFID Card");
}

/* ================= LOOP ================= */
void loop() {

  /* -------- RFID CHECK -------- */
  if (Serial.available()) {
    i = Serial.readStringUntil('\n');
    i.trim();
    processCard(i);
  }

  /* -------- SWITCH CHECK -------- */
  if (systemState == WAIT_FOR_DESTINATION && destinationReady) {
    checkSwitches();
  }

  /* -------- HEART BEAT + GSM -------- */
  HB = analogRead(HBpin) - 800;

  if (HB > 0 && HB < 100) {
    Serial.print("Heart Rate Value: ");
    Serial.println(HB);
    delay(1000);

    if (HB >= 150) {
      Serial.println("ALERT: Heart rate increased");
      player.play(11);

      digitalWrite(BUZZER, HIGH);
      delay(500);
      digitalWrite(BUZZER, LOW);

      Serial.println("AT\r");
      delay(500);
      Serial.println("AT+CMGF=1\r");
      delay(500);
      Serial.print("AT+CMGS=\"");
      Serial.print(num);
      Serial.println("\"\r");
      delay(500);
      Serial.print("ALERT: Heart rate increased");
      delay(1000);
      Serial.println((char)26);
      delay(1000);
      Serial.println("message sent");
    }
  }
}

/* ================= PROCESS CARD ================= */
void processCard(String card) {

  if (systemState == IDLE &&
      (card == mainCard1 || card == mainCard2 || card == mainCard3)) {

    Serial.println("Valid Card Detected");
    Serial.println("Please select destination");

    player.play(5);        // "Please select destination"
    delay(2500);           // wait for audio

    lastSW1 = digitalRead(SW1);
    lastSW2 = digitalRead(SW2);
    lastSW3 = digitalRead(SW3);

    destinationReady = true;
    systemState = WAIT_FOR_DESTINATION;
  }
  else {
    Serial.println("Wrong / Unknown Card");
    player.play(4);
    wrongAlert();
  }
}

/* ================= SWITCH HANDLER ================= */
void checkSwitches() {

  bool sw1 = digitalRead(SW1);
  bool sw2 = digitalRead(SW2);
  bool sw3 = digitalRead(SW3);

  if (lastSW1 == HIGH && sw1 == LOW && debounceOK()) {
    Serial.println("Destination: Tambaram | Bus No: 21G");
    player.play(1);
    resetSystem();
  }

  if (lastSW2 == HIGH && sw2 == LOW && debounceOK()) {
    Serial.println("Destination: Chrompet | Bus No: 52B");
    player.play(2);
    resetSystem();
  }

  if (lastSW3 == HIGH && sw3 == LOW && debounceOK()) {
    Serial.println("Destination: Pallavaram | Bus No: 18A");
    player.play(3);
    resetSystem();
  }

  lastSW1 = sw1;
  lastSW2 = sw2;
  lastSW3 = sw3;
}

/* ================= WRONG CARD ALERT ================= */
void wrongAlert() {
  digitalWrite(BUZZER, HIGH);
  digitalWrite(VIBRATION, HIGH);
  delay(1000);
  digitalWrite(BUZZER, LOW);
  digitalWrite(VIBRATION, LOW);
}

/* ================= RESET ================= */
void resetSystem() {
  delay(2000);
  destinationReady = false;
  systemState = IDLE;
  Serial.println("Ready for Next Passenger");
}

/* ================= DEBOUNCE ================= */
bool debounceOK() {
  if (millis() - lastDebounceTime > debounceDelay) {
    lastDebounceTime = millis();
    return true;
  }
  return false;
}
