/**
 * RECEPTOR ESP-NOW (BINARIO A TEXTO)
 * Dispositivo: ESP32
 * Propósito: Recibir paquetes binarios de sensores y reenviarlos como texto.
 * Formato: {MAC,timestamp,ax,ay,az,gx,gy,gz,mx,my,mz,t} — 24 bytes
 */

// --- LIBRERÍAS ---
#include <esp_now.h>   // Incluye la API para manejar el protocolo de comunicación inalámbrica ESP-NOW.
#include <WiFi.h>      // Controla el módulo WiFi interno del ESP32.
#include <esp_wifi.h>  // Da acceso a funciones avanzadas, como forzar el canal de radio WiFi.
#define WIFI_CHANNEL 1 // Establece el canal de comunicación (debe ser idéntico al del emisor).

// --- ESTRUCTURAS DE DATOS ---
// Estructura de entrada (20 bytes).
// __attribute__((__packed__)) asegura que los bytes estén contiguos en memoria, sin padding, para que el tamaño sea exacto.
struct __attribute__((__packed__)) PacketEstruct {
  int16_t ax; // Aceleración X
  int16_t ay; // Aceleración Y
  int16_t az; // Aceleración Z
  int16_t gx; // Giroscopio X
  int16_t gy; // Giroscopio Y
  int16_t gz; // Giroscopio Z
  int16_t mx; // Magnetómetro X
  int16_t my; // Magnetómetro Y
  int16_t mz; // Magnetómetro Z 
  int16_t t;  // Temperatura 
}; // Tamaño total: 20 bytes 

// Estructura para salida al PC (con timestamp agregado por el receptor).
struct __attribute__((__packed__)) PacketSalida {
  uint32_t timestamp;  // Variable adicional de tiempo, agregada por el receptor local.
  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;
  int16_t mx;
  int16_t my;
  int16_t mz;
  int16_t t;
}; // Tamaño total ampliado: 24 bytes 

// --- VARIABLES GLOBALES DE CONTROL ---
volatile unsigned long paquetesRecibidos = 0; // Contador (volátil porque se altera dentro de una interrupción/callback).
unsigned long tiempoInicio = 0;               // Guardará el tiempo de referencia en milisegundos desde que llega el primer paquete.

// --- FUNCIÓN CALLBACK DE RECEPCIÓN ---
// Esta función es un evento que se dispara automáticamente en segundo plano cuando el ESP32 recibe un paquete ESP-NOW.
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
  paquetesRecibidos++; // Registra la llegada de un nuevo paquete.

  // Verificar que el tamaño del paquete recibido coincida con los 20 bytes esperados.
  if (len != sizeof(PacketEstruct)) {
    // Si el tamaño no coincide, imprime un error de diagnóstico solo en los primeros 5 paquetes para no saturar el puerto serie.
    if (paquetesRecibidos <= 5) {
      Serial.print("[!] Tamaño inesperado: ");
      Serial.print(len);
      Serial.print(" bytes (esperaba ");
      Serial.print(sizeof(PacketEstruct));
      Serial.println(")");
    }
    return; // Descarta el paquete defectuoso y sale del evento.
  }

  // Recibir datos del sensor (sin timestamp) 
  PacketEstruct datosSensor;
  // Copia la memoria del flujo de bytes entrantes (incomingData) a la estructura limpia 'datosSensor'.
  memcpy(&datosSensor, incomingData, sizeof(PacketEstruct));

  // Crear paquete final de salida y añadir el timestamp local 
  PacketSalida paqueteSalida;
  paqueteSalida.timestamp = millis() - tiempoInicio;  // Calcula los milisegundos relativos desde el inicio de las transmisiones.
  
  // Traspaso de los valores recibidos a la nueva estructura ampliada.
  paqueteSalida.ax = datosSensor.ax;
  paqueteSalida.ay = datosSensor.ay;
  paqueteSalida.az = datosSensor.az;
  paqueteSalida.gx = datosSensor.gx;
  paqueteSalida.gy = datosSensor.gy;
  paqueteSalida.gz = datosSensor.gz;
  paqueteSalida.mx = datosSensor.mx;
  paqueteSalida.my = datosSensor.my;
  paqueteSalida.mz = datosSensor.mz;
  paqueteSalida.t = datosSensor.t;

  // Extrae la dirección MAC del emisor para identificar quién envía los datos.
  const uint8_t *mac = esp_now_info->src_addr;

  // Salida formateada al PC por Serial.
  // Se encarga de hacer el proceso inverso al emisor: divide los valores enteros para recuperar los números flotantes reales.
  Serial.printf("{%02X:%02X:%02X:%02X:%02X:%02X,%u,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.1f}\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5],
                paqueteSalida.timestamp,
                paqueteSalida.ax / 1000.0f, paqueteSalida.ay / 1000.0f, paqueteSalida.az / 1000.0f,
                paqueteSalida.gx / 10.0f, paqueteSalida.gy / 10.0f, paqueteSalida.gz / 10.0f,
                (float)paqueteSalida.mx, (float)paqueteSalida.my, (float)paqueteSalida.mz,
                paqueteSalida.t / 10.0f);

  // Si acaba de llegar el primer paquete, calibra el temporizador inicial y notifica.
  if (paquetesRecibidos == 1) {
    tiempoInicio = millis();  // Fija el momento cero ('0.000') del sistema.
    Serial.print("[OK]");     // Indica éxito de conexión incial.
  }
}

// --- FUNCIÓN DE INICIALIZACIÓN ---
void setup() {
  // Arranca el puerto serie a una tasa muy alta (1000000 baudios) para enviar rápidamente los datos formateados sin generar cuellos de botella.
  Serial.begin(1000000);
  delay(500);

  // Se configura como Estación WiFi y se desconecta de redes estándar, condición obligatoria de ESP-NOW.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Establece el canal primario de WiFi y deshabilita el canal secundario.
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Imprime los datos del receptor por consola, en especial su MAC, que el emisor necesita.
  Serial.println("\n--- RECEPTOR ESP-NOW ---");
  Serial.print("MAC propia: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Canal WiFi/ESP-NOW fijado: ");
  Serial.println(WIFI_CHANNEL);
  Serial.println("------------------------");

  // Inicia el sistema ESP-NOW y valida que no devuelva errores.
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW fallo");
    return; // En caso de fallo, aborta el resto de configuraciones.
  }

  // Vincula la función `OnDataRecv` al motor de ESP-NOW. 
  // Esto le dice al chip: "Ejecuta esta función cada vez que llegue algo".
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("[OK] Esperando datos...");
}

// --- BUCLE PRINCIPAL ---
void loop() {
  // Como la recepción es asíncrona y manejada por el evento Callback (OnDataRecv), 
  // el bucle principal se mantiene casi vacío con un breve retraso.
  delay(10);
}