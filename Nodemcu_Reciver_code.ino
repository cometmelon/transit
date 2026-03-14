#include <ESP8266WiFi.h>                                                             //pin Connection(ESP8266-->USB Serial port)
#include <espnow.h>                                                                  //3v->VCC
                                                                                     //Gnd->Gnd
String receivedMessage = "";                                                         //Rx->TX
bool newMessage = false;                                                             //TX->RX

void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  receivedMessage = "";
  for (int i = 0; i < len; i++) {
    receivedMessage += (char)data[i];
  }

  if (receivedMessage.length() > 0) {
    newMessage = true;
  }
}

void setup() {
  Serial.begin(9600);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init Failed!");
    ESP.restart();
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onReceive);

  Serial.println("Receiver Ready... Waiting for Data...\n");
}

void loop() {
  if (newMessage) {
    Serial.println(receivedMessage); // print exactly same format
    Serial.println();                 // blank line to match Arduino
    delay(400);                       // visible slow delay
    newMessage = false;
  }
}
