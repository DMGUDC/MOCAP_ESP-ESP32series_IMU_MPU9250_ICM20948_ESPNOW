/**
 * TRANSMISOR MOCAP OPTIMIZADO - DATOS CRUDOS IMU (ESP8266 + ICM-20948)
 * Dispositivo: ESP-01S (ESP8266)
 * Sensor: ICM-20948 (9DoF)
 * Frecuencia: ~60 Hz
 * Formato del Paquete Binario:{ax,ay,az,gx,gy,gz,mx,my,mz,t} — 20 Bytes
 */

// --- LIBRERÍAS ---
#include <Wire.h>           // Permite la comunicación I2C con el sensor (Incluido por defecto en el IDE de arduino ver 2.3.10)
#include "ICM_20948.h"      // Librería específica para controlar el sensor IMU de 9 ejes (Libreria especifica SparkFun (DoF Breakout - ICM 20948 - Arduino Library) by SparkFun Electronics ver 1.3.2)
#include <ESP8266WiFi.h>    // Controla el hardware WiFi del ESP8266 instalar desde Archivos -> Preferencias -> Additional Manager Boards URLs, y pegar en el espacio en blanco (http://arduino.esp8266.com/stable/package_esp8266com_index.json) ver 3.1.2.
#include <espnow.h>         // Implementa el protocolo de baja latencia ESP-NOW (Incluido en la libreria de placas esp32 de Espressif Systems ver 3.3.10)

// Permite acceder a funciones de bajo nivel del SDK de Espressif (necesario para forzar el canal WiFi)
extern "C" {
  #include "user_interface.h"
}

// --- CONFIGURACIÓN DE PINES Y SENSOR ---
#define SDA_PIN 0           // Pin de datos para I2C
#define SCL_PIN 2           // Pin de reloj para I2C
ICM_20948_I2C ICM;          // Instancia del objeto para interactuar con el sensor
#define AD0_VAL 0           // Valor del pin AD0 (determina la dirección I2C del sensor)

// --- CONFIGURACIÓN DE RED ---
#define WIFI_CHANNEL 1      // Canal WiFi fijo para evitar interferencias y acelerar la conexión

// Dirección MAC del dispositivo ESP32 receptor (¡Debe coincidir exactamente con el receptor!)
uint8_t receiverAddress[] = {0xC8, 0x2E, 0x18, 0x24, 0x82, 0x64};

// --- ESTRUCTURA DE DATOS ---
// Estructura (20 bytes en total)
// __attribute__((__packed__)) evita que el compilador añada bytes vacíos (padding) 
// para alinear la memoria, garantizando que el tamaño sea exactamente 20 bytes.
struct __attribute__((__packed__)) PacketEstructural {
  int16_t ax; // Aceleración en X
  int16_t ay; // Aceleración en Y
  int16_t az; // Aceleración en Z
  int16_t gx; // Giroscopio en X
  int16_t gy; // Giroscopio en Y
  int16_t gz; // Giroscopio en Z
  int16_t mx; // Magnetómetro en X
  int16_t my; // Magnetómetro en Y
  int16_t mz; // Magnetómetro en Z
  int16_t t;  // Temperatura
};

PacketEstructural Paquete; // Variable global que almacenará los datos a enviar

// --- VARIABLES DE TEMPORIZACIÓN Y DIAGNÓSTICO ---
unsigned long tiempoAnterior = 0;
const unsigned long intervaloMs = 16; // 16 ms entre envíos para lograr aproximadamente 60 Hz

unsigned long enviosOk = 0;           // Contador de paquetes enviados con éxito
unsigned long enviosFail = 0;         // Contador de paquetes que fallaron al enviarse
unsigned long ultimoDiagnostico = 0;  // Temporizador para mostrar reportes en el monitor serie

void setup() {
  // Inicializa la comunicación serie para mensajes de depuración
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- INICIANDO EMISOR ICM-20948 ---");

  // Inicia el bus I2C en los pines definidos y a alta velocidad (400 kHz)
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // --- INICIALIZACIÓN DEL SENSOR ---
  bool inicializado = false;
  int reintentos = 0;
  // Bucle que bloquea la ejecución hasta que el sensor responda correctamente
  while (!inicializado) {
    reintentos++;
    Serial.print("[I2C] Intentando conectar ICM-20948 (Intento ");
    Serial.print(reintentos);
    Serial.println(")...");
    ICM.begin(Wire, AD0_VAL);
    
    if (ICM.status == ICM_20948_Stat_Ok) {
      inicializado = true;
      Serial.println("[OK] ICM-20948 inicializado y listo.");
    } else {
      Serial.print("[!] Error en ICM status: ");
      Serial.println(ICM.statusString());
      delay(500); // Espera medio segundo antes de reintentar
    }
  }

  // --- CONFIGURACIÓN WIFI Y ESP-NOW ---
  WiFi.mode(WIFI_STA); // Configura en modo Estación (necesario para ESP-NOW)
  WiFi.disconnect();   // Desconecta de cualquier red WiFi estándar para no perder tiempo
  delay(100);

  // Fija el canal de radio (debe ser el mismo en emisor y receptor)
  wifi_set_channel(WIFI_CHANNEL);

  // Inicializa el protocolo ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("[ERROR] No se pudo inicializar ESP-NOW");
    while (1) { delay(1000); } // Si falla, se queda en un bucle infinito
  }

  // Configura este dispositivo como controlador (el que envía los datos)
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  
  // Registra la dirección MAC del receptor como "esclavo" en el sistema
  int peerStatus = esp_now_add_peer(receiverAddress, ESP_NOW_ROLE_SLAVE, WIFI_CHANNEL, NULL, 0);

  if (peerStatus == 0) {
    Serial.println("[OK] Peer Receptor ESP32 registrado correctamente.");
  } else {
    Serial.println("[!] Advertencia al registrar Peer ESP32.");
  }

  // --- IMPRESIÓN DE RESUMEN ---
  Serial.print("Canal WiFi/ESP-NOW fijado: ");
  Serial.println(WIFI_CHANNEL);
  Serial.print("Receptor MAC: ");
  for (int i = 0; i < 6; i++) {
    if (receiverAddress[i] < 0x10) Serial.print("0");
    Serial.print(receiverAddress[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.print("Tamaño de paquete binario (SIN timestamp): ");
  Serial.print(sizeof(Paquete));
  Serial.println(" bytes.");
  Serial.println("----------------------------------");
}

void loop() {
  unsigned long tiempoActual = millis();

  // Ejecuta la lectura y envío solo si han pasado 16 ms (control de frecuencia a ~60Hz)
  if (tiempoActual - tiempoAnterior >= intervaloMs) {
    tiempoAnterior = tiempoActual;

    // Verifica si el sensor tiene nuevas mediciones listas para leer
    if (ICM.dataReady()) {
      ICM.getAGMT(); // Captura Aceleración, Giroscopio, Magnetómetro y Temperatura

      // --- ESCALADO Y EMPAQUETADO DE DATOS ---
      // Se multiplican los valores flotantes (decimales) por un factor para convertirlos 
      // en enteros (int16_t) sin perder precisión, ahorrando ancho de banda.
      
      // Aceleración: Multiplicada por 1000 (ej. 1.5 g pasa a ser 1500)
      Paquete.ax = (int16_t)(ICM.accX() * 1000.0f);
      Paquete.ay = (int16_t)(ICM.accY() * 1000.0f);
      Paquete.az = (int16_t)(ICM.accZ() * 1000.0f);

      // Giroscopio: Multiplicado por 10 (ej. 90.5 deg/s pasa a ser 905)
      Paquete.gx = (int16_t)(ICM.gyrX() * 10.0f);
      Paquete.gy = (int16_t)(ICM.gyrY() * 10.0f);
      Paquete.gz = (int16_t)(ICM.gyrZ() * 10.0f);

      // Magnetómetro: Se envía directo ya que sus valores brutos caben en un int16_t
      Paquete.mx = (int16_t)(ICM.magX());
      Paquete.my = (int16_t)(ICM.magY());
      Paquete.mz = (int16_t)(ICM.magZ());

      // Temperatura: Multiplicada por 10 (ej. 25.4 C pasa a ser 254)
      Paquete.t  = (int16_t)(ICM.temp() * 10.0f);

      // --- TRANSMISIÓN ---
      // Envía el paquete de 20 bytes directamente a la MAC del receptor
      int resultado = esp_now_send(receiverAddress, (uint8_t *)&Paquete, sizeof(Paquete));
      
      // Actualiza los contadores según el resultado del envío
      if (resultado == 0) {
        enviosOk++;
      } else {
        enviosFail++;
      }
    }
  }

  /* --- DIAGNÓSTICO EN TIEMPO REAL --- Descomentar para usar
  unsigned long ahora = millis();
  // Se ejecuta cada 1000 ms (1 segundo) para mostrar el rendimiento en el puerto serie
  if (ahora - ultimoDiagnostico >= 1000) {
    ultimoDiagnostico = ahora;
    Serial.print("[");
    Serial.print(ahora / 1000); // Muestra los segundos transcurridos
    Serial.print("s] Enviados OK: ");
    Serial.print(enviosOk);
    Serial.print(" | Fallidos: ");
    Serial.print(enviosFail);
    Serial.print(" | Temp: ");
    Serial.print(ICM.temp(), 1); // Muestra la temperatura real con 1 decimal
    Serial.println(" C");
    
    // Reinicia los contadores para el siguiente segundo
    enviosOk = 0;
    enviosFail = 0;
  }*/
}