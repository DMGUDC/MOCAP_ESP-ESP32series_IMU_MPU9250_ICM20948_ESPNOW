/*
 * RECEPTOR ESP32 / ESP32-S3
 * Puente ESP-NOW -> Serial
 */

#include <WiFi.h>
#include <esp_now.h>

// Callback cuando llegan datos
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {

  // Enviar directamente al PC
  Serial.write(data, len);

  // Separar paquetes
  Serial.println();
}

void setup() {

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("ESP-NOW Receiver Ready");

  // Inicializar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Registrar callback de recepción
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
}
