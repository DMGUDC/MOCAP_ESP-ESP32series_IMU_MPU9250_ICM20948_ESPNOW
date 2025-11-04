// Emisor ESP32 por ESP-NOW: envía JSON compacto con id, online, calibrated,
// coords{x,y,z}, inertial_vel{x,y,z}, inertia_per_cycle
// Requiere: Arduino core ESP8266EX, <WiFi.h>, <esp_now.h>
// (Opcional: sustituir mocks por lecturas reales de IMU)

// Emisor ESP32 (UDP) con MPU-9250 + Madgwick
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "MPU9250.h"
#include "MadgwickAHRS.h"

const char *WIFI_SSID = "SSID";
const char *WIFI_PASSWORD = "PASSWORD";
const char *RECEIVER_IP = "192.168.1.100";
const uint16_t RECEIVER_PORT = 7777;
const uint16_t LOCAL_PORT = 7776;

const char *SENSOR_ID = "imu_01";
const float SAMPLE_HZ = 100.0f;
const uint16_t SEND_INTERVAL_MS = 20;
const float OMEGA_THRESH = 2.0f;
const uint32_t MIN_CYCLE_MS = 300;

MPU9250 IMU(Wire, 0x68);
Madgwick filter;
WiFiUDP Udp;
uint8_t isOnline = 0, isCalibrated = 0;
float gBiasX = 0, gBiasY = 0, gBiasZ = 0;
uint32_t lastSampleUs = 0, lastSendMs = 0;
float inertiaAccum = 0, lastInertiaPerCycle = 0;
bool prevAbove = false;
uint32_t lastCycleMs = 0;

struct Euler
{
    float r, p, y;
};
Euler qt2e(float q0, float q1, float q2, float q3)
{
    Euler e;
    float sr = 2.f * (q0 * q1 + q2 * q3), cr = 1.f - 2.f * (q1 * q1 + q2 * q2);
    float r = atan2f(sr, cr);
    float sp = 2.f * (q0 * q2 - q3 * q1);
    float p = fabsf(sp) >= 1.f ? copysignf(M_PI / 2.f, sp) : asinf(sp);
    float sy = 2.f * (q0 * q3 + q1 * q2), cy = 1.f - 2.f * (q2 * q2 + q3 * q3);
    float y = atan2f(sy, cy);
    e.r = r * 180.f / M_PI;
    e.p = p * 180.f / M_PI;
    e.y = y * 180.f / M_PI;
    return e;
}
void calibrateGyro(uint16_t ms = 2000)
{
    uint32_t t0 = millis();
    uint32_t n = 0;
    double sx = 0, sy = 0, sz = 0;
    while (millis() - t0 < ms)
    {
        IMU.readSensor();
        sx += IMU.getGyroX_rads();
        sy += IMU.getGyroY_rads();
        sz += IMU.getGyroZ_rads();
        n++;
        delay(5);
    }
    if (n)
    {
        gBiasX = sx / n;
        gBiasY = sy / n;
        gBiasZ = sz / n;
    }
    isCalibrated = 1;
}
void updateCycle(float omegaAbs, float dt)
{
    inertiaAccum += omegaAbs * dt;
    bool above = (omegaAbs >= OMEGA_THRESH);
    uint32_t now = millis();
    if (!prevAbove && above && (now - lastCycleMs) > MIN_CYCLE_MS)
    {
        lastInertiaPerCycle = inertiaAccum;
        inertiaAccum = 0;
        lastCycleMs = now;
    }
    prevAbove = above;
}
void setup()
{
    Serial.begin(115200);
    Wire.begin();
    if (IMU.begin() < 0)
    {
        Serial.println("IMU fail");
        delay(1000);
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(300);
    }
    Udp.begin(LOCAL_PORT);
    isOnline = 1;
    filter.begin(SAMPLE_HZ);
    calibrateGyro(2000);
    lastSampleUs = micros();
    lastSendMs = millis();
}
void loop()
{
    uint32_t nowUs = micros();
    float dt = (nowUs - lastSampleUs) / 1e6f;
    if (dt < (1.0f / SAMPLE_HZ))
        return;
    lastSampleUs = nowUs;

    IMU.readSensor();
    float ax = IMU.getAccelX_mss(), ay = IMU.getAccelY_mss(), az = IMU.getAccelZ_mss();
    float gx = IMU.getGyroX_rads() - gBiasX, gy = IMU.getGyroY_rads() - gBiasY, gz = IMU.getGyroZ_rads() - gBiasZ;

    filter.updateIMU(gx, gy, gz, ax, ay, az);
    Euler e = qt2e(filter.q0, filter.q1, filter.q2, filter.q3);

    const float RAD2DEG = 180.f / M_PI;
    float gvx = gx * RAD2DEG, gvy = gy * RAD2DEG, gvz = gz * RAD2DEG;

    float omegaAbs = sqrtf(gx * gx + gy * gy + gz * gz);
    updateCycle(omegaAbs, dt);

    uint32_t nowMs = millis();
    if (nowMs - lastSendMs >= SEND_INTERVAL_MS)
    {
        char json[220];
        int n = snprintf(json, sizeof(json),
                         "{\"id\":\"%s\",\"online\":%u,\"cal\":%u,"
                         "\"coor\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
                         "\"iner_vel\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
                         "\"iner_per_cycle\":%.4f}",
                         SENSOR_ID, isOnline, isCalibrated, e.r, e.p, e.y, gvx, gvy, gvz, lastInertiaPerCycle);
        if (n > 0)
        {
            Udp.beginPacket(RECEIVER_IP, RECEIVER_PORT);
            Udp.write((const uint8_t *)json, n);
            Udp.endPacket();
        }
        lastSendMs = nowMs;
    }
}
