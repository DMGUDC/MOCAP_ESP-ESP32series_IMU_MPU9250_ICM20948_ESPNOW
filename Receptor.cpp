// Receptor ESP32 para ~12 emisores ESP-NOW
// Recibe JSON con: id, online, calibrated, coords{x,y,z}, inertial_vel{x,y,z}, inertia_per_cycle
// Salida: Serial a 921600 y (opcional) reenvío UDP a PC

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFiUdp.h>

// ---------- Configuración ----------
const uint8_t WIFI_CH = 1;      // Debe coincidir con los emisores
const bool FORWARD_UDP = false; // true para reenviar por UDP
const char *UDP_TARGET_IP = "192.168.1.100";
const uint16_t UDP_TARGET_PORT = 7777;

// Serial alta velocidad para telemetría
const uint32_t SERIAL_BAUD = 921600;

// Buffers (ESP-NOW suele limitar ~250 bytes de payload; se dan márgenes)
static char rxBuf[320];
WiFiUDP Udp;

// ---------- Estado de emisores vistos ----------
struct SenderInfo
{
    uint8_t mac[6];
    uint32_t lastMs;
};
SenderInfo senders[24];
int senderCount = 0;

void printMac(const uint8_t *mac)
{
    for (int i = 0; i < 6; i++)
    {
        if (i)
            Serial.print(":");
        if (mac[i] < 16)
            Serial.print("0");
        Serial.print(mac[i], HEX);
    }
}

// Parseo muy simple: verifica llaves y campos clave, sin librerías JSON para máxima velocidad
bool containsKey(const char *s, const char *key)
{
    return strstr(s, key) != nullptr;
}

void onRecv(const uint8_t *mac, const uint8_t *data, int len)
{
    // Registrar emisor
    uint32_t now = millis();
    bool known = false;
    for (int i = 0; i < senderCount; i++)
    {
        if (memcmp(senders[i].mac, mac, 6) == 0)
        {
            senders[i].lastMs = now;
            known = true;
            break;
        }
    }
    if (!known && senderCount < (int)(sizeof(senders) / sizeof(senders[0])))
    {
        memcpy(senders[senderCount].mac, mac, 6);
        senders[senderCount].lastMs = now;
        senderCount++;
    }

    // Copiar y terminar cadena
    int n = len < (int)sizeof(rxBuf) - 1 ? len : (int)sizeof(rxBuf) - 1;
    memcpy(rxBuf, data, n);
    rxBuf[n] = '\0';

    // Validaciones mínimas del JSON esperado
    bool ok = (rxBuf[0] == '{') &&
              containsKey(rxBuf, "\"id\"") &&
              containsKey(rxBuf, "\"coords\"") &&
              containsKey(rxBuf, "\"inertial_vel\"") &&
              containsKey(rxBuf, "\"inertia_per_cycle\"");
    if (!ok)
    {
        Serial.print("WARN JSON invalido de ");
        printMac(mac);
        Serial.print(" len=");
        Serial.print(len);
        Serial.print(" data=");
        Serial.println(rxBuf);
        return;
    }

    // Salida por Serial: imprimir línea cruda JSON precedida por MAC
    Serial.print("MAC=");
    printMac(mac);
    Serial.print(" ");
    Serial.println(rxBuf);

    // Reenvío por UDP (opcional)
    if (FORWARD_UDP)
    {
        Udp.beginPacket(UDP_TARGET_IP, UDP_TARGET_PORT);
        Udp.write((const uint8_t *)rxBuf, strlen(rxBuf));
        Udp.endPacket();
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(50);

    // Fijar canal para coincidir con emisores
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ERROR: ESP-NOW init");
        ESP.restart();
    }
    esp_now_register_recv_cb(onRecv);

    if (FORWARD_UDP)
    {
        Udp.begin(); // puerto efímero
    }

    Serial.print("Receptor listo | Canal=");
    Serial.print(WIFI_CH);
    Serial.println(" | ESP-NOW RX activo | Serial 921600");
}

void loop()
{
    // Monitoreo cada 5 s
    static uint32_t t0 = 0;
    if (millis() - t0 > 5000)
    {
        Serial.print("Emisores activos: ");
        Serial.println(senderCount);
        t0 = millis();
    }
}
