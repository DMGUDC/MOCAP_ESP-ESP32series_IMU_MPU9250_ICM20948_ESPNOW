/*
 * DIAGNÓSTICO I2C + LECTURA MPU9250 — ESP-01S con Servidor Web
 * Escanea el bus I2C y muestra valores del sensor en tiempo real.
 *
 * LIBRERÍAS:
 *   - ESP8266WebServer (incluida en el core ESP8266)
 *   - "Bolder Flight Systems MPU9250" v1.x (LEGACY, sin Eigen)
 *
 * HARDWARE:
 *   SDA → GPIO0  (pull-up 10KΩ a 3.3V)
 *   SCL → GPIO2  (pull-up 10KΩ a 3.3V)
 *   AD0 → GND    (dirección confirmada: 0x68)
 *
 * ACCESO: http://192.168.1.200
 */
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include "MPU9250.h"


// ─────────────────────────────────────────────────────────────────────────────
// CONFIGURACIÓN — EDITAR ESTOS VALORES
// ─────────────────────────────────────────────────────────────────────────────
const char* SSID  = "";
const char* PASSWORD = "";

IPAddress IP_ESTATICA (192, 168, 1, 200);
IPAddress GATEWAY     (192, 168, 1,   1);
IPAddress SUBNET      (255, 255, 255,  0);
// ─────────────────────────────────────────────────────────────────────────────


ESP8266WebServer server(80);
MPU9250 mpu(Wire, 0x68);
bool mpuListo = false;


// =============================================================================
// HANDLER — Página principal
// =============================================================================
void handleRoot() {
  // --- Escaneo I2C ---
  String filasI2C = "";
  int encontrados = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      encontrados++;
      String nombre = "Desconocido";
      switch (addr) {
        case 0x68: nombre = "MPU9250 (AD0=GND)";  break;
        case 0x69: nombre = "MPU9250 (AD0=3.3V)"; break;
        case 0x3C: nombre = "OLED SSD1306";        break;
        case 0x76: nombre = "BMP280/BME280";        break;
        case 0x77: nombre = "BMP280/BME280 alt";    break;
        case 0x1E: nombre = "HMC5883L";             break;
      }
      filasI2C += "<tr><td>0x";
      if (addr < 16) filasI2C += "0";
      filasI2C += String(addr, HEX) + "</td><td>" + nombre + "</td><td class='ok'>&#10003; Activo</td></tr>";
    }
  }
  if (encontrados == 0) {
    filasI2C = "<tr><td colspan='3' class='err'>&#10007; Sin dispositivos — revisa el cableado</td></tr>";
  }

  // --- Lectura MPU9250 ---
  String filasIMU = "";
  if (mpuListo) {
    mpu.readSensor();
    char buf[32];

    snprintf(buf, sizeof(buf), "%.4f", mpu.getAccelX_mss());
    filasIMU += "<tr><td>Acelerómetro X</td><td class='val'>" + String(buf) + "</td><td class='unit'>m/s²</td></tr>";
    snprintf(buf, sizeof(buf), "%.4f", mpu.getAccelY_mss());
    filasIMU += "<tr><td>Acelerómetro Y</td><td class='val'>" + String(buf) + "</td><td class='unit'>m/s²</td></tr>";
    snprintf(buf, sizeof(buf), "%.4f", mpu.getAccelZ_mss());
    filasIMU += "<tr><td>Acelerómetro Z</td><td class='val'>" + String(buf) + "</td><td class='unit'>m/s²</td></tr>";

    snprintf(buf, sizeof(buf), "%.4f", mpu.getGyroX_rads());
    filasIMU += "<tr><td>Giroscopio X</td><td class='val'>" + String(buf) + "</td><td class='unit'>rad/s</td></tr>";
    snprintf(buf, sizeof(buf), "%.4f", mpu.getGyroY_rads());
    filasIMU += "<tr><td>Giroscopio Y</td><td class='val'>" + String(buf) + "</td><td class='unit'>rad/s</td></tr>";
    snprintf(buf, sizeof(buf), "%.4f", mpu.getGyroZ_rads());
    filasIMU += "<tr><td>Giroscopio Z</td><td class='val'>" + String(buf) + "</td><td class='unit'>rad/s</td></tr>";

    snprintf(buf, sizeof(buf), "%.4f", mpu.getMagX_uT());
    filasIMU += "<tr><td>Magnetómetro X</td><td class='val'>" + String(buf) + "</td><td class='unit'>µT</td></tr>";
    snprintf(buf, sizeof(buf), "%.4f", mpu.getMagY_uT());
    filasIMU += "<tr><td>Magnetómetro Y</td><td class='val'>" + String(buf) + "</td><td class='unit'>µT</td></tr>";
    snprintf(buf, sizeof(buf), "%.4f", mpu.getMagZ_uT());
    filasIMU += "<tr><td>Magnetómetro Z</td><td class='val'>" + String(buf) + "</td><td class='unit'>µT</td></tr>";

  } else {
    filasIMU = "<tr><td colspan='3' class='err'>&#10007; MPU9250 no inicializado — ver Serial Monitor</td></tr>";
  }

  // --- HTML ---
  String html = F("<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='1'>"
    "<title>Diagnóstico I2C</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:monospace;background:#0d0d1a;color:#ccc;padding:16px}"
    "h1{color:#00d2ff;font-size:1.1rem;margin-bottom:4px}"
    "h2{color:#7ecfff;font-size:0.9rem;margin:16px 0 6px}"
    ".sub{font-size:0.72rem;color:#555;margin-bottom:14px}"
    "table{width:100%;border-collapse:collapse;font-size:0.82rem;margin-bottom:4px}"
    "th{background:#0f2a4a;padding:8px;text-align:left;color:#00d2ff}"
    "td{padding:8px;border-bottom:1px solid #1a1a2e}"
    ".ok{color:#2ecc71}.err{color:#e74c3c;text-align:center}"
    ".val{color:#f0c040;font-weight:bold;text-align:right}"
    ".unit{color:#888;padding-left:6px}"
    ".dot{display:inline-block;width:7px;height:7px;border-radius:50%;"
    "background:#2ecc71;margin-right:5px;animation:blink 1s infinite}"
    "@keyframes blink{0%,100%{opacity:1}50%{opacity:0.2}}"
    ".footer{margin-top:14px;font-size:0.7rem;color:#444}"
    "</style></head><body>");

  html += F("<h1>&#128269; Diagnóstico I2C — ESP-01S</h1>");
  html += "<p class='sub'><span class='dot'></span>Actualizando cada segundo &nbsp;|&nbsp; SDA=GPIO0 &nbsp; SCL=GPIO2</p>";

  html += F("<h2>&#128268; Bus I2C</h2>"
    "<table><thead><tr><th>Dirección</th><th>Dispositivo</th><th>Estado</th></tr></thead><tbody>");
  html += filasI2C;
  html += F("</tbody></table>");

  html += F("<h2>&#9883; Lecturas MPU9250 (0x68)</h2>"
    "<table><thead><tr><th>Sensor</th><th style='text-align:right'>Valor</th><th>Unidad</th></tr></thead><tbody>");
  html += filasIMU;
  html += F("</tbody></table>");

  html += "<p class='footer'>IP: " + WiFi.localIP().toString();
  html += " &nbsp;|&nbsp; MAC: " + WiFi.macAddress();
  html += F(" &nbsp;|&nbsp; ESP-01S</p></body></html>");

  server.send(200, "text/html", html);
}


// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C con pines explícitos del ESP-01S
  Wire.begin(0, 2);

  // ── FIX: delay de arranque + reintentos ─────────────────────────────────
  // El MPU9250 necesita tiempo para estabilizarse tras encender.
  // Sin este delay, mpu.begin() falla aunque el scanner encuentre 0x68.
  // status=-1: no responde | status=-2: chip es MPU6500, no MPU9250
  delay(250);

  int status  = -1;
  int intentos = 0;

  while (status < 0 && intentos < 5) {
    status = mpu.begin();
    if (status < 0) {
      intentos++;
      Serial.print("Reintento ");
      Serial.print(intentos);
      Serial.print("/5 — status: ");
      Serial.println(status);
      delay(500);
    }
  }

  if (status < 0) {
    Serial.print("Error MPU9250 definitivo. Status = ");
    Serial.println(status);
    Serial.println("status=-1: revisar cableado o voltaje");
    Serial.println("status=-2: el chip puede ser MPU6500 (sin magnetómetro)");
    mpuListo = false;
  } else {
    Serial.print("MPU9250 listo en 0x68 tras ");
    Serial.print(intentos);
    Serial.println(" reintento(s)");
    mpuListo = true;
  }
  // ────────────────────────────────────────────────────────────────────────

  // Conectar WiFi con IP estática
  Serial.print("Conectando a WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.config(IP_ESTATICA, GATEWAY, SUBNET);
  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado");
  Serial.print("Servidor en: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.onNotFound([]() {
    server.send(404, "text/plain", "No encontrado");
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado.");
}


// =============================================================================
// LOOP
// =============================================================================
void loop() {
  server.handleClient();
}