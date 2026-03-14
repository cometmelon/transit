#include <ESP8266WiFi.h>
#include <espnow.h>

uint8_t peerMAC[] = {0xE0, 0x98, 0x06, 0x92, 0xE8, 0xF5};
String serialData = "";

void onSent(uint8_t *mac, uint8_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == 0 ? "Success" : "Fail");
}

void setup() {
  Serial.begin(9600);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init Failed!");
    ESP.restart();
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onSent);

  if (esp_now_add_peer(peerMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0) != 0) {
    Serial.println("Failed to Add Peer");
  }
}

void loop() {
  if (Serial.available()) {
    serialData = Serial.readStringUntil('\n'); // read full line

    if (serialData.length() > 0) {
      esp_now_send(peerMAC, (uint8_t *)serialData.c_str(), serialData.length());
      Serial.print("Sending: ");
      Serial.println(serialData);
      delay(300); // slow down sending for clarity
    }
  }
}
