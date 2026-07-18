# Créditos
El presente código fue desarrollado por el semillero de **GIMATICA** en la línea de inteligencia computacional del programa de **Ingeniería de sistemas** de la **Universidad de Cartagena** para necesidades particulares.

Agradecimientos a los involucrados en el proyecto.

Desarrolladores principales:

- Didier Martinez - dmartinezg1@unicartagena.edu.co
- Gybram Llamas - gllamasp@unicartagena.edu.co

Colaboradores:

---

# Puente ESP8266 A ESP32 - ESPNOW 

Sistema de captura y transmisión inalámbrica de movimiento humano basado en sensores IMU y el protocolo ESP-NOW, con salida de quaterniones hacia motores gráficos mediante puerto serial.

---

## ¿Qué es ESP-NOW?

<cite index="4-1">ESP-NOW es un protocolo de comunicación inalámbrica definido por Espressif que permite el control directo, rápido y de bajo consumo de dispositivos inteligentes sin necesidad de un router.</cite> <cite index="2-1">Se trata de un protocolo sin conexión que permite la transmisión de paquetes cortos y puede usarse tanto con placas modelo ESP8266 como en placas modelo ESP32 y sus derivados.</cite>

<cite index="6-1">ESP-NOW opera como un protocolo peer-to-peer (P2P), lo que significa que permite la comunicación directa entre dos dispositivos ESP sin necesidad de un servidor central ni un punto de acceso (como un router Wi-Fi). Cada dispositivo ESP tiene una dirección MAC única que se utiliza para identificar la placa receptora.</cite>

---

## Arquitectura del Sistema

Este proyecto implementa una jerarquía **muchos-a-uno**:

```
[ESP8266EX + IMU] ──┐
[ESP8266EX + IMU] ──┤  ESP-NOW   ──►  [ESP32 Receptor]  ──►  [Motor Gráfico] 
[ESP8266EX + IMU] ──┘        (2.4 GHz)         (vía USB Serial 1 Mbps)         
      ...
```

<cite index="3-1">En la comunicación muchos-a-uno existe un nodo central o gateway que recolecta todos los datos de los dispositivos ESP conectados.</cite>

### Roles

| Dispositivo     | Rol              | Función                                                |
|-----------------|------------------|--------------------------------------------------------|
| ESP8266EX       | Emisor           | Lee el sensor IMU y transmite quaterniones por ESP-NOW |
| ESP32 (familia) | Receptor/Gateway | Recibe los paquetes y los reenvía por Serial           |

> **Nota de compatibilidad:** <cite index="4-1">ESP-NOW soporta los SoC de las series ESP8266, ESP32, ESP32-S y ESP32-C.</cite> La interoperabilidad entre ESP8266EX (emisor) y ESP32 (receptor) está confirmada por Espressif.

---

## Sensores IMU Compatibles

El sistema es compatible con sensores IMU de 9 ejes. Los modelos soportados son:

### MPU-9250
<cite index="20-1">El MPU-9250 es una unidad de procesamiento de movimiento de 9 ejes (giroscopio + acelerómetro + magnetómetro) que incluye tres convertidores analógico-digital de 16 bits para cada uno de los tres sensores, además de filtros digitales programables, un reloj de precisión, un sensor de temperatura integrado e interrupciones programables.</cite>

### ICM-20948 *(sucesor del MPU-9250)*
<cite index="19-1">El ICM-20948 rastrea el movimiento en 9 ejes combinando un acelerómetro, giroscopio y magnetómetro en un paquete de tan solo 3×3×1 mm. Usa muy poca energía, lo que lo hace ideal para dispositivos alimentados por batería como wearables y gadgets portátiles.</cite>

<cite index="17-1">El ICM-20948 combina el acelerómetro y giroscopio MEMS de 3 ejes de InvenSense con el magnetómetro de 3 ejes AK09916 de Asahi Kasei Microdevices Corporation, y es considerado la actualización de TDK para el popular MPU-9250 (actualmente descontinuado).</cite>

>**Importante:** El ICM-20948 **no es directamente compatible en código** con el MPU-9250. Se requieren librerías distintas (Ambas incluidas en el repositorio).

---

## ¿Cómo Funciona?

### 1. Captura de datos en el emisor (ESP8266EX)

El microcontrolador ESP8266EX lee los datos crudos del sensor IMU mediante I²C. Los datos de interés son:

| Variable        | Sensor       | Descripción                                 |
|-----------------|--------------|---------------------------------------------|
| `Ax, Ay, Az`    | Acelerómetro | Aceleración lineal en los 3 ejes espaciales |
| `Gx, Gy, Gz`    | Giroscopio   | Velocidad angular en los 3 ejes espaciales  |

Esto configura un sistema de **6 Grados de Libertad (6-DOF)** que permite calcular la orientación y el movimiento relativo de un objeto o segmento corporal.

### 2. Fusión de sensores y cálculo del Quaternion

A partir de los 6 valores capturados, se aplica un algoritmo de fusión de sensores (como Madgwick o Mahony) que produce como resultado un **quaternion**:

```
Q = { Qw, Qx, Qy, Qz }
```

<cite index="13-1">Los quaterniones son representaciones de 4 dimensiones (w, x, y, z) de rotaciones en 3 dimensiones. Son menos intuitivos de leer pero son superiores para manejar movimiento continuo y evitan el bloqueo de cardán (gimbal lock) propio de los ángulos de Euler.</cite>

Donde:
- **Qx, Qy, Qz** — componentes vectoriales que definen el eje de rotación
- **Qw** — componente escalar que representa la magnitud de la rotación

> **Notas:** 
- `Qw` no representa aceleración; es el componente escalar del quaternion unitario de orientación.
- El calculo de los quaterniones se realiza directamente dentro de los nodos emisores (ESP8266 + IMU)

### 3. Transmisión inalámbrica (ESP-NOW)

El quaternion se empaqueta en una estructura binaria compacta y se transmite al receptor ESP32 mediante ESP-NOW. <cite index="2-1">Una vez realizado el emparejamiento entre dispositivos, la conexión es persistente: si alguno de los nodos pierde alimentación o se reinicia, al volver a encenderse se reconectará automáticamente con su par para continuar la comunicación.</cite>

### 4. Reenvío (Serial USB)

El ESP32 receptor recibe los paquetes, los decodifica y los reenvía por puerto Serial a 1 Mbps en el siguiente formato de texto:

```
{AA:BB:CC:DD:EE:FF,0.9980,0.0412,-0.0318,0.0021}
```

`{MAC, Qw, Qx, Qy, Qz}`

---

## ¿Por qué 6-DOF y no 9-DOF?

Los sensores IMU compatibles cuentan físicamente con 9 ejes al incluir un magnetómetro. Sin embargo, este proyecto opera deliberadamente en **6-DOF (acelerómetro + giroscopio)** por las siguientes razones técnicas:

<cite index="10-1">Los sistemas de 9-DOF son vulnerables a distorsiones magnéticas ambientales y carecen de información de posición útil sin ayudas externas.</cite>

En entornos de uso real (sobre el cuerpo humano, cerca de fuentes de alimentación, baterías o cables), el magnetómetro capta interferencia electromagnética parásita que degrada la estimación de orientación. Prescindir de él a favor de un sistema **6-DOF** ofrece:

- Mayor robustez ante interferencia electromagnética
- Menor complejidad de calibración
- Comportamiento más predecible en entornos no controlados
- Estándar adoptado por la mayoría de sistemas de captura de movimiento de bajo costo

> La pérdida de la referencia absoluta de orientación (norte magnético) no es relevante para el rastreo de movimiento relativo de segmentos corporales, que es el objetivo de este proyecto.

---

## Formato del Paquete Binario

```cpp
struct __attribute__((__packed__)) PacketEstructural {
  float qw;   // 4 bytes
  float qx;   // 4 bytes
  float qy;   // 4 bytes
  float qz;   // 4 bytes
};            // Total: 16 bytes por paquete
```

> La dirección MAC del emisor se obtiene directamente del campo `esp_now_info->src_addr` en el receptor, sin necesidad de incluirla en el payload (ahorro del 27% de overhead).

---

## Requisitos

### Hardware

- 1× ESP32 (cualquier variante) — Receptor
- N× ESP8266EX — Emisores (uno por sensor)
- N× Sensor IMU: MPU-9250 o ICM-20948

### Software / Librerías

- Arduino IDE con soporte para ESP8266 y ESP32
- Librería `esp_now.h` (incluida en el core de ESP32)
- Librería IMU correspondiente al sensor utilizado:
  - Para el sernsor MPU-9250: **"MPU9250.h"** de **Bolder Flight Systems v1.x.**
  - Para el sernsor el ICM-20948: **"ICM_20948.h"** de **SparkFun Electronics en su version 1.3.2**

---

## Licencia

MIT License — libre uso, modificación y distribución con atribución.