/**
 * RECEPTOR ESP-NOW HÍBRIDO (BINARIO A TEXTO)
 * Dispositivo: ESP32
 * Propósito: Recibir paquetes binarios de múltiples sensores y reenviarlos
 * como texto de alta velocidad.
 */

#include <esp_now.h>
#include <WiFi.h>

struct __attribute__((__packed__)) PacketEstruct {
  float qw; // 4 bytes
  float qx; // 4 bytes
  float qy; // 4 bytes
  float qz; // 4 bytes
};          // Total: 16 bytes

void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
  if (len == sizeof(PacketEstruct)) {
    PacketEstruct paqueteRecibido;
    memcpy(&paqueteRecibido, incomingData, sizeof(PacketEstruct));

    // La MAC del emisor llega en el header de ESP-NOW
    const uint8_t* mac = esp_now_info->src_addr;

    Serial.printf("{%02X:%02X:%02X:%02X:%02X:%02X,%.4f,%.4f,%.4f,%.4f}\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  paqueteRecibido.qw, paqueteRecibido.qx,
                  paqueteRecibido.qy, paqueteRecibido.qz);
  }
}

void setup() {
  Serial.begin(1000000);
  delay(500);

  WiFi.mode(WIFI_STA);
  delay(100);

  Serial.println("\n--- INICIANDO RECEPTOR ---");
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("--------------------------");

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error inicializando ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  delay(10);
}