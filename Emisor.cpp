// =============================================================================
// EMISOR ESP-01S + MPU9250  →  ESP-NOW
// =============================================================================
// Propósito:
//   Leer los 9 ejes del MPU9250 (acelerómetro, giroscopio, magnetómetro)
//   y transmitirlos inalámbricamente vía ESP-NOW cada 50ms (20Hz).
//   No realiza fusión de sensores ni filtrado — datos crudos en JSON.
//
// Protocolo de comunicación:
//   ESP-NOW — protocolo Espressif de bajo nivel sobre capa WiFi 802.11.
//   No requiere router. Latencia ~1ms. Payload máximo: 250 bytes.
//   Comunicación unidireccional: este nodo (CONTROLLER) → receptor (SLAVE).
//
// Hardware requerido:
//   - Módulo ESP-01S  (ESP8266EX, 1MB flash, 80KB RAM)
//   - Sensor MPU9250  (IMU 9-DOF: accel + gyro + magnetómetro AK8963)
// 
// Drivers para la placa:
//   - http://arduino.esp8266.com/stable/package_esp8266com_index.json
//
// Conexión I2C física:
//   MPU9250 VCC  → 3.3V
//   MPU9250 GND  → GND
//   MPU9250 SDA  → GPIO0  (resistor pull-up 10KΩ a 3.3V — OBLIGATORIO)
//   MPU9250 SCL  → GPIO2  (resistor pull-up 10KΩ a 3.3V — OBLIGATORIO)
//   MPU9250 AD0  → GND    (selecciona dirección I2C 0x68)
//
// Librería MPU9250 requerida:
//   "Bolder Flight Systems MPU9250" versión 1.x (LEGACY, sin Eigen)
//   Instalar desde: Arduino Library Manager → buscar "MPU9250 Bolder"
//   NO instalar v3.x–v5.x ("Bolder Flight Systems Invensense IMU")
//      esas versiones requieren Eigen (ARM only) y no compilan en ESP8266.
//
// Formato JSON de salida (≈140 bytes, dentro del límite de 250 de ESP-NOW):
//   {"mac":"AA:BB:CC:DD:EE:FF",
//    "ax":0.000,"ay":0.000,"az":9.807,
//    "gx":0.000,"gy":0.000,"gz":0.000,
//    "mx":0.000,"my":0.000,"mz":0.000}
// =============================================================================

// -----------------------------------------------------------------------------
// DEPENDENCIAS
// -----------------------------------------------------------------------------
#include <ESP8266WiFi.h>  // Gestión del chip WiFi: modo STA, lectura de MAC.
                          // Aunque no se conecta a ningún router, ESP-NOW
                          // necesita que el radio WiFi esté activo en modo STA.

#include <espnow.h>       // API ESP-NOW para ESP8266 (Espressif SDK).
                          // Funciones: init, roles, add_peer, send.
                          // Solo para ESP8266 — ESP32 usa <esp_now.h> diferente.

#include <Wire.h>         // Biblioteca I2C de Arduino.
                          // En ESP8266 los pines son configurables por software
                          // (no hay pines I2C fijos como en Arduino UNO).

#include "MPU9250.h"      // Driver del sensor — Bolder Flight Systems v1.0.1
                          // Abstrae los registros internos del MPU9250 y AK8963.
                          // Proporciona datos ya convertidos a unidades SI.


// -----------------------------------------------------------------------------
// CONFIGURACIÓN — DIRECCIÓN MAC DEL RECEPTOR
// -----------------------------------------------------------------------------
// La MAC del ESP receptor se obtiene ejecutando en él:
//   Serial.println(WiFi.macAddress());
// y se usa EN MODO STA (no la MAC de AP).
// ESP-NOW en ESP8266 requiere conocer la MAC destino de forma estática
// (no hay descubrimiento automático de pares como en BLE).
// -----------------------------------------------------------------------------
uint8_t receiverMac[] = {0x48, 0xCA, 0x43, 0x9B, 0x48, 0x80};


// -----------------------------------------------------------------------------
// INSTANCIA DEL SENSOR
// MPU9250(bus_I2C, dirección_I2C)
//   Wire  → bus I2C del ESP8266 (inicializado en setup con pines GPIO0/GPIO2)
//   0x68  → dirección I2C cuando AD0=GND  (0x69 si AD0=VCC)
// La librería no inicia la comunicación aquí — solo guarda la referencia.
// La comunicación real comienza en mpu.begin() dentro de setup().
// -----------------------------------------------------------------------------
MPU9250 mpu(Wire, 0x68);
int status;               // Código de retorno de mpu.begin():
                          //   >= 0  →  sensor encontrado y configurado OK
                          //   <  0  →  error (sensor no responde o mal cableado)


// =============================================================================
// SETUP — Ejecuta UNA sola vez al arrancar o después de un reset
// =============================================================================
void setup() {

  // ---------------------------------------------------------------------------
  // PUERTO SERIE — Diagnóstico y depuración
  // 115200 baudios es el estándar para ESP8266.
  // En el ESP-01S, Serial usa GPIO1 (TX) y GPIO3 (RX) — pines distintos
  // a los I2C (GPIO0/GPIO2), por lo que no hay conflicto entre ambos.
  // ---------------------------------------------------------------------------
  Serial.begin(115200);


  // ---------------------------------------------------------------------------
  // I2C — Inicialización con pines explícitos para ESP-01S
  //
  // El ESP-01S solo expone 4 pines GPIO:
  //   GPIO0 → disponible (usado como SDA)
  //   GPIO1 → TX del Serial
  //   GPIO2 → disponible (usado como SCL)
  //   GPIO3 → RX del Serial
  //
  // Wire.begin() sin argumentos usa GPIO4/GPIO5 por defecto en ESP8266,
  // pero esos pines no están físicamente accesibles en el módulo ESP-01S.
  // Sin Wire.begin(0,2) el sensor nunca responde y mpu.begin() falla.
  //
  // Nota sobre los pull-ups:
  //   I2C requiere pull-ups en SDA y SCL. Los resistores externos (10KΩ)
  //   son necesarios porque los pull-ups internos del ESP8266 son débiles
  //   (~50KΩ) y pueden causar fallos a 400kHz. Además, GPIO0 DEBE estar
  //   en HIGH al arrancar — el pull-up externo garantiza el arranque
  //   correcto del módulo (sin él puede entrar en modo de programación).
  // ---------------------------------------------------------------------------
  Wire.begin(0, 2);  // SDA = GPIO0 | SCL = GPIO2


  // ---------------------------------------------------------------------------
  // WIFI — Configuración mínima requerida por ESP-NOW
  //
  // ESP-NOW en ESP8266 depende del stack WiFi interno del SDK de Espressif.
  // El radio debe estar encendido en modo STATION (STA) para que ESP-NOW
  // funcione, pero NO se realiza ninguna conexión a un router.
  //
  // WiFi.mode(WIFI_STA) → activa el radio en modo cliente
  // WiFi.disconnect()   → descarta cualquier SSID guardado en flash,
  //                        evitando que el chip intente conectarse
  //                        automáticamente y agregue latencia al arranque.
  // ---------------------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();


  // ---------------------------------------------------------------------------
  // ESP-NOW — Inicialización y registro del par receptor
  //
  // esp_now_init():
  //   Inicializa el subsistema ESP-NOW del SDK.
  //   Retorna 0 si OK, distinto de 0 si hay error.
  //   Debe llamarse DESPUÉS de WiFi.mode() — requiere el radio activo.
  //
  // esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER):
  //   Define el rol de ESTE nodo como CONTROLLER (emisor/maestro).
  //   El receptor debe estar configurado como ESP_NOW_ROLE_SLAVE.
  //
  // esp_now_add_peer():
  //   Registra la MAC del receptor como par conocido.
  //   Parámetros: MAC, rol del par, canal WiFi (1), clave (NULL=sin cifrado),
  //   longitud de clave (0).
  //   ⚠ El canal WiFi (1) debe ser el mismo en emisor y receptor.
  // ---------------------------------------------------------------------------
  if (esp_now_init() != 0) {
    Serial.println("Error ESP-NOW");
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(receiverMac, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);


  // ---------------------------------------------------------------------------
  // MPU9250 — Inicialización del sensor
  //
  // mpu.begin():
  //   1. Verifica comunicación I2C con el MPU9250 (WHO_AM_I register)
  //   2. Saca el chip del modo sleep (registro PWR_MGMT_1)
  //   3. Calibra y configura el magnetómetro AK8963 interno
  //   4. Aplica rangos por defecto: ±2g acelerómetro, ±250°/s giroscopio
  //   Retorna valor negativo si el sensor no responde (revisar cableado).
  // ---------------------------------------------------------------------------
  status = mpu.begin();
  if (status < 0) {
    Serial.println("Error MPU9250");
    while(1) delay(10);
  }

  // mpu.setAccelRange(MPU9250::ACCEL_RANGE_4G);
  // mpu.setGyroRange(MPU9250::GYRO_RANGE_250DPS);
}


// =============================================================================
// LOOP — Ejecuta continuamente a ~20Hz (cada 50ms)
// =============================================================================
void loop() {

  // ---------------------------------------------------------------------------
  // LECTURA DEL SENSOR
  //
  // mpu.readSensor():
  //   Realiza una lectura en ráfaga (burst read) de todos los registros
  //   del MPU9250 en una sola transacción I2C.
  //   Internamente lee 14 bytes de acelerómetro+giroscopio+temperatura
  //   y 7 bytes del magnetómetro AK8963 (subbus I2C interno).
  //   Es más eficiente que leer cada eje por separado.
  // ---------------------------------------------------------------------------
  mpu.readSensor();


  // ---------------------------------------------------------------------------
  // CONSTRUCCIÓN DEL JSON
  //
  // Se usa char[] + snprintf en lugar de String (clase Arduino) porque:
  //   - El ESP-01S tiene ~20KB de heap dinámico disponible.
  //   - String fragmenta la memoria en cada ciclo del loop causando
  //     WDT reset o Exception 28 después de minutos de ejecución.
  //   - snprintf escribe directo al buffer estático sin allocaciones.
  //
  // Unidades de los datos enviados:
  //   ax/ay/az → m/s²  (aceleración lineal — incluye gravedad en reposo)
  //   gx/gy/gz → rad/s (velocidad angular — 0 en reposo estático)
  //   mx/my/mz → µT    (campo magnético — varía con orientación respecto norte)
  // ---------------------------------------------------------------------------
  char json[200];
  snprintf(json, sizeof(json),
    "{\"mac\":\"%s\","
    "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
    "\"mx\":%.3f,\"my\":%.3f,\"mz\":%.3f}",
    WiFi.macAddress().c_str(),
    mpu.getAccelX_mss(), mpu.getAccelY_mss(), mpu.getAccelZ_mss(),
    mpu.getGyroX_rads(), mpu.getGyroY_rads(), mpu.getGyroZ_rads(),
    mpu.getMagX_uT(),    mpu.getMagY_uT(),    mpu.getMagZ_uT()
  );


  // ---------------------------------------------------------------------------
  // TRANSMISIÓN ESP-NOW
  //
  // esp_now_send(mac_destino, datos, longitud):
  //   Encola el paquete para transmisión inmediata (no bloqueante).
  //
  // Guarda de 250 bytes:
  //   ESP-NOW limita el payload a 250 bytes por paquete.
  //   strlen() verifica el tamaño real antes de enviar para no truncar el JSON.
  // ---------------------------------------------------------------------------
  if (strlen(json) < 250) {
    esp_now_send(receiverMac, (uint8_t*)json, strlen(json));
  }


  // ---------------------------------------------------------------------------
  // FRECUENCIA DE MUESTREO — 20Hz
  //
  // delay(50) = 50ms entre lecturas = 20 muestras por segundo.
  // Suficiente para biomecánica de ciclista (movimiento de pedaleo 1-2Hz).
  // Para capturar impactos rápidos usar delay(20)→50Hz o delay(10)→100Hz.
  // ---------------------------------------------------------------------------
  delay(50);
}