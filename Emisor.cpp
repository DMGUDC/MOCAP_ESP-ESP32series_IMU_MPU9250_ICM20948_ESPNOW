// =============================================================================
// EMISOR ESP-01S + MPU9250  →  ESP-NOW
// =============================================================================
// Propósito:
//   Leer los 9 ejes del MPU9250 (acelerómetro, giroscopio, magnetómetro)
//   y transmitirlos inalámbricamente vía ESP-NOW cada 50ms (20Hz).
//   No realiza fusión de sensores ni filtrado — datos crudos.
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
// Conexión I2C física:
//   MPU9250 VCC  → 3.3V
//   MPU9250 GND  → GND
//   MPU9250 SDA  → GPIO0  (resistor pull-up 10KΩ a 3.3V — OBLIGATORIO)
//   MPU9250 SCL  → GPIO2  (resistor pull-up 10KΩ a 3.3V — OBLIGATORIO)
//   MPU9250 AD0  → GND    (selecciona dirección I2C 0x68 — confirmada)
//
// Librería MPU9250 requerida:
//   "Bolder Flight Systems MPU9250" versión 1.0.2 (LEGACY, sin Eigen)
//   Instalar desde: Arduino Library Manager → buscar "MPU9250 Bolder"
//   ⚠ NO instalar v3.x–v5.x ("Bolder Flight Systems Invensense IMU")
//      esas versiones requieren Eigen (ARM only) y no compilan en ESP8266.
//
// Drivers para la placa:
//   - http://arduino.esp8266.com/stable/package_esp8266com_index.json
//
// Formato de salida (≈80 bytes, dentro del límite de 250 de ESP-NOW):
//   {50:02:91:B3:14:C1,2.193,-1.983,-10.640,-0.039,0.152,-0.066,26.971,73.362,11.789}
//   Orden: MAC, ax, ay, az, gx, gy, gz, mx, my, mz
//   Unidades: m/s² | rad/s | µT
// =============================================================================

// -----------------------------------------------------------------------------
// DEPENDENCIAS
// -----------------------------------------------------------------------------
#include <ESP8266WiFi.h>  // Gestión del chip WiFi: modo STA, lectura de MAC.
                          // ESP-NOW necesita el radio WiFi activo en modo STA
                          // aunque no se conecte a ningún router.

#include <espnow.h>       // API ESP-NOW para ESP8266 (Espressif SDK).
                          // Funciones: init, roles, add_peer, send.
                          // ⚠ Solo para ESP8266 — ESP32 usa <esp_now.h>.

#include <Wire.h>         // Biblioteca I2C de Arduino.
                          // En ESP8266 los pines son configurables por software
                          // (no hay pines I2C fijos como en Arduino UNO).

#include "MPU9250.h"      // Driver del sensor — Bolder Flight Systems v1.x.
                          // Abstrae los registros internos del MPU9250 y AK8963.
                          // Proporciona datos convertidos a unidades SI.


// -----------------------------------------------------------------------------
// CONFIGURACIÓN — DIRECCIÓN MAC DEL RECEPTOR
// -----------------------------------------------------------------------------
// La MAC del ESP32 receptor se obtiene ejecutando en él:
//   Serial.println(WiFi.macAddress());
// y se usa EN MODO STA (no la MAC de AP).
// ESP-NOW requiere conocer la MAC destino de forma estática.
// -----------------------------------------------------------------------------
uint8_t receiverMac[] = {0x48, 0xCA, 0x43, 0x9B, 0x48, 0x80};


// -----------------------------------------------------------------------------
// INSTANCIA DEL SENSOR
// MPU9250(bus_I2C, dirección_I2C)
//   Wire  → bus I2C del ESP8266 (inicializado en setup con GPIO0/GPIO2)
//   0x68  → dirección confirmada por scanner I2C con AD0 conectado a GND
// -----------------------------------------------------------------------------
MPU9250 mpu(Wire, 0x68);


// =============================================================================
// SETUP — Ejecuta UNA sola vez al arrancar o después de un reset
// =============================================================================
void setup() {

  // ---------------------------------------------------------------------------
  // PUERTO SERIE — Diagnóstico
  // 115200 baudios estándar para ESP8266.
  // GPIO1 (TX) y GPIO3 (RX) — pines distintos a I2C, sin conflicto.
  // ---------------------------------------------------------------------------
  Serial.begin(115200);


  // ---------------------------------------------------------------------------
  // I2C — Inicialización con pines explícitos para ESP-01S
  //
  // El ESP-01S solo expone GPIO0 y GPIO2 para uso general.
  // Wire.begin() sin argumentos usa GPIO4/GPIO5 por defecto, que no
  // están físicamente accesibles en el módulo ESP-01S.
  //
  // Pull-ups externos (10KΩ):
  //   Necesarios porque los internos del ESP8266 (~50KΩ) son débiles.
  //   GPIO0 DEBE estar en HIGH al arrancar para evitar modo de
  //   programación — el pull-up externo garantiza esto.
  // ---------------------------------------------------------------------------
  Wire.begin(0, 2); // SDA = GPIO0 | SCL = GPIO2


  // ---------------------------------------------------------------------------
  // WIFI — Configuración mínima requerida por ESP-NOW
  //
  // El radio debe estar en modo STATION (STA) para que ESP-NOW funcione,
  // pero NO se realiza ninguna conexión a un router.
  // WiFi.disconnect() descarta SSIDs guardados en flash para evitar
  // intentos de conexión automática que agreguen latencia al arranque.
  // ---------------------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();


  // ---------------------------------------------------------------------------
  // ESP-NOW — Inicialización y registro del par receptor
  //
  // esp_now_init():
  //   Inicializa el subsistema ESP-NOW. Retorna 0 si OK.
  //   Debe llamarse DESPUÉS de WiFi.mode().
  //
  // esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER):
  //   Este nodo actúa como emisor/maestro.
  //   El receptor debe estar configurado como ESP_NOW_ROLE_SLAVE.
  //
  // esp_now_add_peer():
  //   Registra la MAC destino como par conocido.
  //   Parámetros: MAC, rol del par, canal WiFi (1), clave (NULL), longitud (0).
  //   ⚠ El canal WiFi debe coincidir entre emisor y receptor.
  // ---------------------------------------------------------------------------
  if (esp_now_init() != 0) {
    Serial.println("Error ESP-NOW");
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(receiverMac, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);


  // ---------------------------------------------------------------------------
  // MPU9250 — Inicialización con delay de arranque y reintentos
  //
  // El sensor necesita ~250ms para estabilizarse tras encender.
  // Sin este delay, mpu.begin() puede fallar aunque el scanner I2C
  // encuentre el dispositivo en 0x68.
  //
  // Códigos de error de mpu.begin():
  //   >= 0  →  OK
  //   -1    →  No responde en I2C (revisar cableado o voltaje)
  //   -2    →  WHO_AM_I incorrecto (el chip es MPU6500, no MPU9250)
  // ---------------------------------------------------------------------------
  delay(250);

  int status   = -1;
  int intentos =  0;

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
    while(1) delay(10);
  }

  Serial.print("MPU9250 listo en 0x68 tras ");
  Serial.print(intentos);
  Serial.println(" reintento(s)");

  // Rangos opcionales — descomentar si se necesita mayor rango de medición:
  // mpu.setAccelRange(MPU9250::ACCEL_RANGE_4G);   // default: ±2g
  // mpu.setGyroRange(MPU9250::GYRO_RANGE_500DPS); // default: ±250°/s
}


// =============================================================================
// LOOP — Ejecuta continuamente a ~20Hz (cada 50ms)
// =============================================================================
void loop() {

  // ---------------------------------------------------------------------------
  // LECTURA DEL SENSOR
  //
  // mpu.readSensor():
  //   Lectura en ráfaga (burst read) de todos los registros en una sola
  //   transacción I2C — más eficiente que leer cada eje por separado.
  //   Lee 14 bytes de accel+gyro+temp y 7 bytes del magnetómetro AK8963.
  //   Los valores quedan almacenados internamente para los getters.
  // ---------------------------------------------------------------------------
  mpu.readSensor();


  // ---------------------------------------------------------------------------
  // CONSTRUCCIÓN DEL PAYLOAD
  //
  // Formato compacto sin claves — menor tamaño, más eficiente:
  //   {MAC,ax,ay,az,gx,gy,gz,mx,my,mz}
  //
  // char[] + snprintf en lugar de String (clase Arduino):
  //   El ESP-01S tiene ~20KB de heap dinámico. String fragmenta la memoria
  //   en cada ciclo causando WDT reset o Exception 28 tras minutos de uso.
  //   snprintf escribe directo al buffer estático sin allocaciones.
  //
  // Unidades:
  //   ax/ay/az → m/s²  (incluye gravedad: ~9.8 en reposo vertical)
  //   gx/gy/gz → rad/s (0 en reposo estático)
  //   mx/my/mz → µT    (campo magnético terrestre)
  // ---------------------------------------------------------------------------
  char payload[150];
  snprintf(payload, sizeof(payload),
    "{%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f}",
    WiFi.macAddress().c_str(),
    mpu.getAccelX_mss(), mpu.getAccelY_mss(), mpu.getAccelZ_mss(),
    mpu.getGyroX_rads(), mpu.getGyroY_rads(), mpu.getGyroZ_rads(),
    mpu.getMagX_uT(),    mpu.getMagY_uT(),    mpu.getMagZ_uT()
  );


  // ---------------------------------------------------------------------------
  // TRANSMISIÓN ESP-NOW
  //
  // esp_now_send() es no bloqueante — retorna antes de la confirmación.
  // La guarda de 250 bytes evita enviar un payload truncado al receptor
  // si por alguna razón snprintf generara un string demasiado largo.
  // ---------------------------------------------------------------------------
  if (strlen(payload) < 250) {
    esp_now_send(receiverMac, (uint8_t*)payload, strlen(payload));
  }


  // ---------------------------------------------------------------------------
  // FRECUENCIA DE MUESTREO — 20Hz
  //
  // 50ms entre lecturas = 20 muestras por segundo.
  // Adecuado para biomecánica de ciclista (pedaleo: 1-2Hz).
  // Teorema de Nyquist: 2 × 10Hz máximo de interés = 20Hz mínimo.
  // Para capturar impactos: delay(20)→50Hz | delay(10)→100Hz
  // ---------------------------------------------------------------------------
  delay(50);
}