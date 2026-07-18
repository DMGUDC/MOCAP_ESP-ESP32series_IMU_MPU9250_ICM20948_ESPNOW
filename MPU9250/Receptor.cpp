// =============================================================================
// RECEPTOR ESP32 / ESP32-S3  ←  ESP-NOW → Serial
// =============================================================================
// Propósito:
//   Recibir paquetes ESP-NOW de uno o varios nodos emisores ESP-01S
//   y reenviarlos exactamente tal como llegan al puerto Serial (USB→PC).
//   Actúa como puente transparente ESP-NOW ↔ Serial sin modificar los datos.
//
// Protocolo de comunicación:
//   ESP-NOW — protocolo Espressif de bajo nivel sobre capa WiFi 802.11.
//   No requiere router ni infraestructura WiFi adicional.
//   Soporta múltiples emisores simultáneos (identifica cada nodo por MAC).
//
// Hardware requerido:
//   - Módulo ESP32 o ESP32-S3 (cualquier variante con WiFi)
//
// Sin hardware adicional — solo conexión USB al PC para el Serial.
//
// Formato recibido (tal como lo envía el emisor):
//   {50:02:91:B3:14:C1,2.193,-1.983,-10.640,-0.039,0.152,-0.066,26.971,73.362,11.789}
//   Orden: MAC, ax, ay, az, gx, gy, gz, mx, my, mz
//   Unidades: m/s² | rad/s | µT
//
// Uso:
//   Abrir Monitor Serial a 115200 baudios.
//   La MAC de este receptor (mostrada al arrancar) debe copiarse al
//   array receiverMac[] del código emisor.
// =============================================================================

// -----------------------------------------------------------------------------
// DEPENDENCIAS
// -----------------------------------------------------------------------------
#include <WiFi.h>       // Gestión del chip WiFi del ESP32.
                        // Necesario para activar el radio antes de ESP-NOW
                        // y para leer la dirección MAC del módulo.

#include <esp_now.h>    // API ESP-NOW para ESP32 (Espressif IDF).
                        // ⚠ Distinta a la del ESP8266 (<espnow.h>):
                        //   - Callback usa esp_now_recv_info en lugar de uint8_t*
                        //   - esp_now_init() retorna ESP_OK en lugar de 0
                        //   - No tiene roles (CONTROLLER/SLAVE) — cualquier
                        //     nodo puede enviar y recibir simultáneamente.

#include <esp_wifi.h>   // API de bajo nivel del stack WiFi del ESP32.
                        // Se usa para leer la MAC directamente desde el
                        // hardware (esp_wifi_get_mac) — más confiable que
                        // WiFi.macAddress() en el core v3+ del ESP32, que
                        // puede retornar 00:00:00:00:00:00 si la interfaz
                        // no está completamente inicializada.


// =============================================================================
// CALLBACK — Se ejecuta automáticamente al recibir un paquete ESP-NOW
// =============================================================================
// Parámetros:
//   info → metadatos del paquete (MAC origen, RSSI, canal)
//   data → puntero al payload recibido (bytes crudos)
//   len  → longitud del payload en bytes
//
// El callback se ejecuta en el contexto de la tarea WiFi del SDK —
// debe ser breve y no bloqueante. Serial.write() es seguro aquí.
// =============================================================================
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {

  // Reenviar el payload exactamente como llegó, sin modificaciones.
  // Serial.write() envía bytes crudos — preserva el formato del emisor.
  Serial.write(data, len);

  // Salto de línea para separar visualmente cada paquete en el Monitor Serial.
  // Un paquete por línea a 20Hz = 20 líneas por segundo por nodo emisor.
  Serial.println();
}


// =============================================================================
// SETUP — Ejecuta UNA sola vez al arrancar
// =============================================================================
void setup() {

  // ---------------------------------------------------------------------------
  // PUERTO SERIE
  // 115200 baudios — debe coincidir con la velocidad del Monitor Serial del PC.
  // A 20Hz con ~85 bytes por paquete: ~1700 bytes/s → muy por debajo del
  // límite de 115200 baudios (~11500 bytes/s). Sin riesgo de desbordamiento.
  // ---------------------------------------------------------------------------
  Serial.begin(115200);
  delay(500);


  // ---------------------------------------------------------------------------
  // WIFI — Configuración mínima requerida por ESP-NOW
  //
  // ESP-NOW en ESP32 requiere el radio WiFi activo en modo STATION.
  // WiFi.disconnect(true) borra credenciales guardadas y evita intentos
  // de conexión automática a redes previas.
  // ---------------------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);


  // ---------------------------------------------------------------------------
  // MAC ADDRESS — Lectura directa desde hardware
  //
  // esp_wifi_get_mac(WIFI_IF_STA, mac):
  //   Lee la MAC directamente del periférico WiFi del ESP32.
  //   Más confiable que WiFi.macAddress() en el core arduino-esp32 v3+,
  //   donde WiFi.macAddress() puede retornar "00:00:00:00:00:00" si
  //   la interfaz todavía no terminó de inicializarse.
  //
  // ⚠ IMPORTANTE: Copiar esta MAC al array receiverMac[] del emisor:
  //   uint8_t receiverMac[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
  // ---------------------------------------------------------------------------
  Serial.println();
  Serial.println("ESP-NOW Receptor listo");
  Serial.println("─────────────────────────────────");

  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println("─────────────────────────────────");
  Serial.println("Esperando paquetes ESP-NOW...");
  Serial.println();


  // ---------------------------------------------------------------------------
  // ESP-NOW — Inicialización y registro del callback de recepción
  //
  // esp_now_init():
  //   Inicializa el subsistema ESP-NOW del IDF.
  //   Retorna ESP_OK si exitoso. Debe llamarse después de WiFi.mode().
  //
  // esp_now_register_recv_cb(OnDataRecv):
  //   Registra la función que se invoca automáticamente al llegar
  //   cualquier paquete ESP-NOW, sin importar el nodo origen.
  //   Soporta múltiples emisores — la MAC de origen está en info->src_addr
  //   si se necesita identificar qué nodo envió cada paquete.
  // ---------------------------------------------------------------------------
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error inicializando ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
}


// =============================================================================
// LOOP — Vacío: toda la lógica está en el callback OnDataRecv
// =============================================================================
// El callback se invoca automáticamente por el stack WiFi del SDK cada vez
// que llega un paquete — no se necesita polling en el loop principal.
// =============================================================================
void loop() {
}