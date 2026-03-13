/*
 * EMISOR ESP8266 + MPU9250 -> ESP-NOW
 * Datos: {MAC, Calibración, Accel(XYZ), Gyro(XYZ), Mag(XYZ)}
 * Sin cálculos complejos, solo lectura directa.
 */
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include "MPU9250.h" // Librería "MPU9250" de Bolder Flight Systems

// --- CONFIGURACIÓN ---
uint8_t receiverMac[] = {0x48, 0xCA, 0x43, 0x9B, 0x48, 0x80};

MPU9250 mpu(Wire, 0x68);
int status;
int calibStatus = 0; // 0=No calibrado, 1=OK

void setup() {
  Serial.begin(115200);
  Wire.begin(); 
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) {
    Serial.println("Error ESP-NOW");
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(receiverMac, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  status = mpu.begin();
  if (status < 0) {
    Serial.println("Error MPU9250");
    while(1) delay(10);
  }
  
  // Opcional: Configurar rangos si necesitas más sensibilidad
  // mpu.setAccelRange(MPU9250::ACCEL_RANGE_4G);
  // mpu.setGyroRange(MPU9250::GYRO_RANGE_250DPS);
  
  calibStatus = 1; // Marcamos como 'listo' tras iniciar
}

void loop() {
  // Leer todos los sensores de una vez
  mpu.readSensor();

  // CONSTRUCCIÓN DEL JSON OPTIMIZADA
  // Usamos String concatenado para claridad.
  // Formato: {"mac":"XX...","c":1,"ax":1.2,"ay":...}
  
  String json = "{";
  
  // 1. MAC ADDRESS
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  
  // 2. Estado (Calibration Status)
  json += "\"c\":" + String(calibStatus) + ",";
  
  // 3. Acelerómetro (m/s^2)
  json += "\"ax\":" + String(mpu.getAccelX_mss(), 3) + ",";
  json += "\"ay\":" + String(mpu.getAccelY_mss(), 3) + ",";
  json += "\"az\":" + String(mpu.getAccelZ_mss(), 3) + ",";
  
  // 4. Giroscopio (rad/s)
  json += "\"gx\":" + String(mpu.getGyroX_rads(), 3) + ",";
  json += "\"gy\":" + String(mpu.getGyroY_rads(), 3) + ",";
  json += "\"gz\":" + String(mpu.getGyroZ_rads(), 3) + ",";
  
  // 5. Magnetómetro (uT - microteslas)
  json += "\"mx\":" + String(mpu.getMagX_uT(), 3) + ",";
  json += "\"my\":" + String(mpu.getMagY_uT(), 3) + ",";
  json += "\"mz\":" + String(mpu.getMagZ_uT(), 3); // Último valor sin coma extra
  
  json += "}";

  // Enviar si cabe en 250 bytes
  if (json.length() < 250) {
    esp_now_send(receiverMac, (uint8_t *)json.c_str(), json.length());
  }

  // Frecuencia de muestreo controlada
  // 50ms = 20Hz (Suficiente para visualización fluida sin saturar)
  delay(50); 
}
