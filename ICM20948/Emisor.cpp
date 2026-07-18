/**
 * TRANSMISOR MOCAP OPTIMIZADO (CUATERNIONES BINARIOS)
 * Dispositivo: ESP-01S (ESP8266)
 * Frecuencia: 60 Hz
 */

#include <Wire.h>
#include "ICM_20948.h"   
#include <ESP8266WiFi.h> 
#include <espnow.h>      

#define SDA_PIN 0
#define SCL_PIN 2
ICM_20948_I2C ICM;
#define AD0_VAL 0

// REEMPLAZAR CON LA MAC EXACTA DE EL ESP32 RECEPTOR
uint8_t receiverAddress[] = {0xC8, 0x2E, 0x18, 0x24, 0x82, 0x64};

struct __attribute__((__packed__)) PacketEstructural {
  float qw; // 4 bytes
  float qx; // 4 bytes
  float qy; // 4 bytes
  float qz; // 4 bytes
};          // Total: 16 bytes

PacketEstructural Paquete;
unsigned long tiempoAnterior = 0;
const unsigned long intervalo = 16; // ~60Hz

float beta = 0.1f;
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;

void MadgwickIMU(float gx, float gy, float gz, float ax, float ay, float az) {
  float recipNorm;
  float s0, s1, s2, s3;
  float qDot1, qDot2, qDot3, qDot4;
  float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2, _8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

  gx *= 0.0174532925f;
  gy *= 0.0174532925f;
  gz *= 0.0174532925f;

  qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
  qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
  qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
  qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

  if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
    recipNorm = 1.0f / sqrt(ax * ax + ay * ay + az * az);
    ax *= recipNorm; ay *= recipNorm; az *= recipNorm;

    _2q0 = 2.0f * q0; _2q1 = 2.0f * q1; _2q2 = 2.0f * q2; _2q3 = 2.0f * q3;
    _4q0 = 4.0f * q0; _4q1 = 4.0f * q1; _4q2 = 4.0f * q2;
    _8q1 = 8.0f * q1; _8q2 = 8.0f * q2;
    q0q0 = q0 * q0; q1q1 = q1 * q1; q2q2 = q2 * q2; q3q3 = q3 * q3;

    s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
    s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
    s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
    s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

    recipNorm = 1.0f / sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
    s0 *= recipNorm; s1 *= recipNorm; s2 *= recipNorm; s3 *= recipNorm;

    qDot1 -= beta * s0; qDot2 -= beta * s1; qDot3 -= beta * s2; qDot4 -= beta * s3;
  }

  float dt = 0.016666f;
  q0 += qDot1 * dt; q1 += qDot2 * dt; q2 += qDot3 * dt; q3 += qDot4 * dt;

  recipNorm = 1.0f / sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm; q1 *= recipNorm; q2 *= recipNorm; q3 *= recipNorm;
}

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  bool initialized = false;
  while (!initialized) {
    ICM.begin(Wire, AD0_VAL);
    if (ICM.status == ICM_20948_Stat_Ok) initialized = true;
    else delay(500);
  }

  WiFi.mode(WIFI_STA);
  delay(100);

  if (esp_now_init() != 0) return;
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(receiverAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

void loop() {
  unsigned long tiempoActual = millis();

  if (tiempoActual - tiempoAnterior >= intervalo) {
    tiempoAnterior = tiempoActual;

    if (ICM.dataReady()) {
      ICM.getAGMT();

      MadgwickIMU(ICM.gyrX(), ICM.gyrY(), ICM.gyrZ(),
                  ICM.accX(), ICM.accY(), ICM.accZ());

      Paquete.qw = q0;
      Paquete.qx = q1;
      Paquete.qy = q2;
      Paquete.qz = q3;

      esp_now_send(receiverAddress, (uint8_t *)&Paquete, sizeof(Paquete));
    }
  }
}