# Tutorial: Usando el AeroflyReader DLL

Este tutorial te guiará paso a paso para conectar tus proyectos con Aerofly FS4 usando el DLL simplificado. Está pensado para hobbyistas y entusiastas de la simulación, no necesitas ser programador profesional.

## Tabla de Contenidos

1. [Instalación del DLL](#1-instalación-del-dll)
2. [Entendiendo las dos formas de conexión](#2-entendiendo-las-dos-formas-de-conexión)
3. [Conexión TCP (la más fácil)](#3-conexión-tcp-la-más-fácil)
4. [Conexión por Shared Memory (la más rápida)](#4-conexión-por-shared-memory-la-más-rápida)
5. [Conversión de unidades](#5-conversión-de-unidades)
6. [Ejemplos prácticos completos](#6-ejemplos-prácticos-completos)
7. [Solución de problemas](#7-solución-de-problemas)

---

## 1. Instalación del DLL

### Paso 1: Descarga o compila el DLL

Ya hay un DLL precompilado listo para usar en la carpeta `bin/`:

```
simplified/bin/AeroflyReader.dll
```

### Paso 2: Copia el DLL a la carpeta de Aerofly

Abre PowerShell y ejecuta:

```powershell
copy simplified\bin\AeroflyReader.dll "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll\"
```

O hazlo manualmente:
1. Ve a `Documentos\Aerofly FS 4\`
2. Crea la carpeta `external_dll` si no existe
3. Copia `AeroflyReader.dll` dentro de esa carpeta

### Paso 3: Inicia Aerofly FS4

El DLL se carga automáticamente al iniciar el simulador. No hay configuración adicional necesaria.

---

## 2. Entendiendo las dos formas de conexión

El DLL ofrece dos métodos para obtener datos:

| Método | Descripción | Cuándo usarlo |
|--------|-------------|---------------|
| **TCP** | Recibe datos como texto JSON por red | Más fácil de usar. Ideal para empezar. |
| **Shared Memory** | Lee directamente de la memoria | Más rápido. Ideal para aplicaciones de alto rendimiento. |

**Recomendación:** Si estás empezando, usa TCP. Es más simple y funciona perfectamente para la mayoría de proyectos.

---

## 3. Conexión TCP (la más fácil)

### ¿Qué es TCP?

TCP es como un "teléfono" entre programas. Tu programa llama al puerto 12345, el DLL responde, y te envía datos continuamente en formato JSON (texto estructurado fácil de leer).

### Ejemplo mínimo en Python

```python
import socket
import json

# 1. Conectarse al DLL
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))

# 2. Recibir y mostrar datos
buffer = ""
while True:
    buffer += sock.recv(4096).decode('utf-8')

    # Buscar líneas completas (terminan con \n)
    while '\n' in buffer:
        line, buffer = buffer.split('\n', 1)
        if line.strip():
            data = json.loads(line)

            # Mostrar algunos datos
            print(f"Altitud: {data['altitude']:.0f} ft")
            print(f"Velocidad: {data['indicated_airspeed'] * 1.94384:.0f} nudos")
```

### Ejemplo mínimo en C#

```csharp
using System;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

class Program
{
    static void Main()
    {
        // 1. Conectarse al DLL
        TcpClient client = new TcpClient("localhost", 12345);
        NetworkStream stream = client.GetStream();

        byte[] buffer = new byte[4096];
        string accumulated = "";

        // 2. Recibir datos continuamente
        while (true)
        {
            int bytesRead = stream.Read(buffer, 0, buffer.Length);
            accumulated += Encoding.UTF8.GetString(buffer, 0, bytesRead);

            // Procesar líneas completas
            while (accumulated.Contains("\n"))
            {
                int index = accumulated.IndexOf('\n');
                string line = accumulated.Substring(0, index);
                accumulated = accumulated.Substring(index + 1);

                if (!string.IsNullOrWhiteSpace(line))
                {
                    var data = JsonSerializer.Deserialize<JsonElement>(line);
                    double altitude = data.GetProperty("altitude").GetDouble();
                    Console.WriteLine($"Altitud: {altitude:F0} ft");
                }
            }
        }
    }
}
```

### Ejemplo mínimo en JavaScript/Node.js

```javascript
const net = require('net');

// 1. Conectarse al DLL
const client = new net.Socket();
client.connect(12345, 'localhost', () => {
    console.log('Conectado a Aerofly!');
});

// 2. Recibir datos
let buffer = '';
client.on('data', (data) => {
    buffer += data.toString();

    // Procesar líneas completas
    while (buffer.includes('\n')) {
        const index = buffer.indexOf('\n');
        const line = buffer.substring(0, index);
        buffer = buffer.substring(index + 1);

        if (line.trim()) {
            const flightData = JSON.parse(line);
            console.log(`Altitud: ${flightData.altitude.toFixed(0)} ft`);
        }
    }
});
```

### Ejemplo en Arduino (ESP32/ESP8266 con WiFi)

```cpp
#include <WiFi.h>
#include <ArduinoJson.h>

const char* ssid = "TU_WIFI";
const char* password = "TU_PASSWORD";
const char* host = "192.168.1.100";  // IP de tu PC
const int port = 12345;

WiFiClient client;
String buffer = "";

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi conectado!");

    // Conectar al DLL
    if (client.connect(host, port)) {
        Serial.println("Conectado a Aerofly!");
    }
}

void loop() {
    while (client.available()) {
        char c = client.read();
        buffer += c;

        if (c == '\n') {
            // Parsear JSON
            StaticJsonDocument<2048> doc;
            if (deserializeJson(doc, buffer) == DeserializationOk) {
                double altitude = doc["altitude"];
                double speed = doc["indicated_airspeed"];

                Serial.printf("Alt: %.0f ft, IAS: %.0f kts\n",
                              altitude, speed * 1.94384);
            }
            buffer = "";
        }
    }

    // Reconectar si se perdió la conexión
    if (!client.connected()) {
        client.connect(host, port);
    }
    delay(10);
}
```

---

## 4. Conexión por Shared Memory (la más rápida)

### ¿Qué es Shared Memory?

Es una zona de memoria que comparten Aerofly y tu programa. Como leen del mismo lugar, es extremadamente rápido (microsegundos).

**Nota:** Solo funciona en la misma computadora donde corre Aerofly.

### Estructura de datos en memoria

Los datos están organizados así (en orden):

| Offset | Tipo | Tamaño | Campo |
|--------|------|--------|-------|
| 0 | uint64 | 8 bytes | timestamp_us |
| 8 | uint32 | 4 bytes | data_valid (1=válido, 0=no) |
| 12 | uint32 | 4 bytes | update_counter |
| 16 | double | 8 bytes | latitude (radianes) |
| 24 | double | 8 bytes | longitude (radianes) |
| 32 | double | 8 bytes | altitude (pies) |
| 40 | double | 8 bytes | height (pies AGL) |
| 48 | double | 8 bytes | pitch (radianes) |
| 56 | double | 8 bytes | bank (radianes) |
| 64 | double | 8 bytes | true_heading (radianes) |
| 72 | double | 8 bytes | magnetic_heading (radianes) |
| 80 | double | 8 bytes | indicated_airspeed (m/s) |
| 88 | double | 8 bytes | ground_speed (m/s) |
| 96 | double | 8 bytes | vertical_speed (m/s) |
| ... | ... | ... | (continúa) |

### Ejemplo completo en Python

```python
import mmap
import struct
import time
import math

# Nombre de la memoria compartida
SHM_NAME = "AeroflyReaderData"
SHM_SIZE = 1024  # bytes

def main():
    print("Conectando a Shared Memory...")

    try:
        # Abrir memoria compartida
        shm = mmap.mmap(-1, SHM_SIZE, SHM_NAME, access=mmap.ACCESS_READ)
        print("Conectado!")
    except Exception as e:
        print(f"Error: {e}")
        print("Asegúrate de que Aerofly esté corriendo con el DLL cargado.")
        return

    try:
        while True:
            # Volver al inicio
            shm.seek(0)

            # Leer cabecera (16 bytes)
            timestamp = struct.unpack('Q', shm.read(8))[0]  # uint64
            data_valid = struct.unpack('I', shm.read(4))[0]  # uint32
            update_counter = struct.unpack('I', shm.read(4))[0]  # uint32

            # Leer posición y orientación
            latitude = struct.unpack('d', shm.read(8))[0]  # double
            longitude = struct.unpack('d', shm.read(8))[0]
            altitude = struct.unpack('d', shm.read(8))[0]
            height = struct.unpack('d', shm.read(8))[0]
            pitch = struct.unpack('d', shm.read(8))[0]
            bank = struct.unpack('d', shm.read(8))[0]
            true_heading = struct.unpack('d', shm.read(8))[0]
            magnetic_heading = struct.unpack('d', shm.read(8))[0]

            # Leer velocidades
            indicated_airspeed = struct.unpack('d', shm.read(8))[0]
            ground_speed = struct.unpack('d', shm.read(8))[0]
            vertical_speed = struct.unpack('d', shm.read(8))[0]

            if data_valid:
                # Convertir radianes a grados
                lat_deg = math.degrees(latitude)
                lon_deg = math.degrees(longitude)
                if lon_deg > 180:
                    lon_deg -= 360
                heading_deg = math.degrees(magnetic_heading) % 360

                # Convertir m/s a nudos
                ias_kts = indicated_airspeed * 1.94384

                # Mostrar
                print(f"Pos: {lat_deg:.4f}, {lon_deg:.4f} | "
                      f"Alt: {altitude:.0f} ft | "
                      f"HDG: {heading_deg:.0f}° | "
                      f"IAS: {ias_kts:.0f} kts | "
                      f"Update: #{update_counter}")

            time.sleep(0.1)  # 10 Hz es suficiente para visualización

    except KeyboardInterrupt:
        print("\nCerrando...")
    finally:
        shm.close()

if __name__ == "__main__":
    main()
```

### Ejemplo en C/C++ (Windows)

```cpp
#include <windows.h>
#include <stdio.h>
#include <cmath>

// Estructura que coincide con los datos del DLL
#pragma pack(push, 1)
struct AeroflyReaderData {
    uint64_t timestamp_us;
    uint32_t data_valid;
    uint32_t update_counter;
    double latitude;
    double longitude;
    double altitude;
    double height;
    double pitch;
    double bank;
    double true_heading;
    double magnetic_heading;
    double indicated_airspeed;
    double ground_speed;
    double vertical_speed;
    // ... más campos según necesites
};
#pragma pack(pop)

int main() {
    // Abrir memoria compartida
    HANDLE hMapFile = OpenFileMappingA(
        FILE_MAP_READ,
        FALSE,
        "AeroflyReaderData"
    );

    if (hMapFile == NULL) {
        printf("Error: No se pudo abrir la memoria compartida.\n");
        printf("Asegurate de que Aerofly este corriendo.\n");
        return 1;
    }

    // Mapear a nuestro espacio de direcciones
    AeroflyReaderData* data = (AeroflyReaderData*)MapViewOfFile(
        hMapFile,
        FILE_MAP_READ,
        0, 0,
        sizeof(AeroflyReaderData)
    );

    if (data == NULL) {
        printf("Error: No se pudo mapear la memoria.\n");
        CloseHandle(hMapFile);
        return 1;
    }

    printf("Conectado! Presiona Ctrl+C para salir.\n\n");

    while (true) {
        if (data->data_valid) {
            // Convertir unidades
            double lat_deg = data->latitude * 180.0 / 3.14159265359;
            double lon_deg = data->longitude * 180.0 / 3.14159265359;
            if (lon_deg > 180) lon_deg -= 360;

            double heading = fmod(data->magnetic_heading * 180.0 / 3.14159265359, 360.0);
            double ias_kts = data->indicated_airspeed * 1.94384;

            printf("Lat: %.4f | Lon: %.4f | Alt: %.0f ft | HDG: %.0f | IAS: %.0f kts\r",
                   lat_deg, lon_deg, data->altitude, heading, ias_kts);
        }

        Sleep(100);  // 10 Hz
    }

    UnmapViewOfFile(data);
    CloseHandle(hMapFile);
    return 0;
}
```

### Ejemplo en C# (Windows)

```csharp
using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Threading;

class Program
{
    // Estructura que coincide con los datos del DLL
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    struct AeroflyReaderData
    {
        public ulong timestamp_us;
        public uint data_valid;
        public uint update_counter;
        public double latitude;
        public double longitude;
        public double altitude;
        public double height;
        public double pitch;
        public double bank;
        public double true_heading;
        public double magnetic_heading;
        public double indicated_airspeed;
        public double ground_speed;
        public double vertical_speed;
        // ... agregar más campos según necesites
    }

    static void Main()
    {
        try
        {
            // Abrir memoria compartida
            var mmf = MemoryMappedFile.OpenExisting("AeroflyReaderData");
            var accessor = mmf.CreateViewAccessor(0, Marshal.SizeOf<AeroflyReaderData>());

            Console.WriteLine("Conectado! Presiona Ctrl+C para salir.\n");

            while (true)
            {
                // Leer estructura completa
                accessor.Read(0, out AeroflyReaderData data);

                if (data.data_valid == 1)
                {
                    // Convertir unidades
                    double latDeg = data.latitude * 180.0 / Math.PI;
                    double lonDeg = data.longitude * 180.0 / Math.PI;
                    if (lonDeg > 180) lonDeg -= 360;

                    double heading = (data.magnetic_heading * 180.0 / Math.PI) % 360;
                    double iasKts = data.indicated_airspeed * 1.94384;

                    Console.Write($"\rLat: {latDeg:F4} | Lon: {lonDeg:F4} | " +
                                  $"Alt: {data.altitude:F0} ft | HDG: {heading:F0}° | " +
                                  $"IAS: {iasKts:F0} kts     ");
                }

                Thread.Sleep(100);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error: {ex.Message}");
            Console.WriteLine("Asegurate de que Aerofly este corriendo con el DLL.");
        }
    }
}
```

---

## 5. Conversión de unidades

Los datos vienen del simulador en unidades específicas. Aquí está cómo convertirlos:

### Posición

| Dato | Viene en | Para obtener | Fórmula |
|------|----------|--------------|---------|
| Latitud | Radianes | Grados | `lat_deg = lat_rad × 180 ÷ π` |
| Longitud | Radianes | Grados | `lon_deg = lon_rad × 180 ÷ π` |
| Longitud | 0-360 | -180 a +180 | Si `lon > 180`: `lon = lon - 360` |

### Velocidades

| Dato | Viene en | Para obtener | Fórmula |
|------|----------|--------------|---------|
| Velocidades | m/s | Nudos (kts) | `kts = m_s × 1.94384` |
| Velocidades | m/s | km/h | `kmh = m_s × 3.6` |
| Velocidades | m/s | mph | `mph = m_s × 2.23694` |
| Velocidad vertical | m/s | ft/min | `fpm = m_s × 196.85` |

### Ángulos

| Dato | Viene en | Para obtener | Fórmula |
|------|----------|--------------|---------|
| Pitch, Bank, Heading | Radianes | Grados | `deg = rad × 180 ÷ π` |
| Heading | Grados | 0-360 | `hdg = deg % 360` (módulo) |

### Altitud

| Dato | Viene en | Notas |
|------|----------|-------|
| Altitude (MSL) | Pies | Ya está en pies, no necesita conversión |
| Height (AGL) | Pies | Ya está en pies, no necesita conversión |

### Funciones de conversión rápidas (Python)

```python
import math

def rad_to_deg(radians):
    """Radianes a grados"""
    return radians * 180 / math.pi

def ms_to_kts(ms):
    """Metros por segundo a nudos"""
    return ms * 1.94384

def ms_to_fpm(ms):
    """Metros por segundo a pies por minuto"""
    return ms * 196.85

def normalize_longitude(lon_deg):
    """Normaliza longitud de 0-360 a -180/+180"""
    if lon_deg > 180:
        return lon_deg - 360
    return lon_deg

def format_heading(radians):
    """Radianes a heading 000-360"""
    degrees = rad_to_deg(radians) % 360
    return f"{degrees:03.0f}"
```

---

## 6. Ejemplos prácticos completos

### Ejemplo 1: Mostrar altitud en display 7 segmentos (Arduino)

```cpp
#include <WiFi.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>

// Pines del display
#define CLK 22
#define DIO 23

TM1637Display display(CLK, DIO);
WiFiClient client;

void setup() {
    Serial.begin(115200);
    display.setBrightness(7);

    WiFi.begin("TU_SSID", "TU_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    client.connect("192.168.1.100", 12345);
}

void loop() {
    static String buffer = "";

    while (client.available()) {
        char c = client.read();
        buffer += c;

        if (c == '\n') {
            StaticJsonDocument<1024> doc;
            if (deserializeJson(doc, buffer) == DeserializationOk) {
                int altitude = doc["altitude"];

                // Mostrar en display (dividir por 100 si es muy grande)
                if (altitude > 9999) {
                    display.showNumberDec(altitude / 100);
                } else {
                    display.showNumberDec(altitude);
                }
            }
            buffer = "";
        }
    }
    delay(50);
}
```

### Ejemplo 2: Grabar vuelo a CSV

```python
import socket
import json
import csv
import math
from datetime import datetime

def main():
    # Crear archivo CSV
    filename = f"flight_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

    with open(filename, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)

        # Encabezados
        writer.writerow([
            'Timestamp', 'Latitude', 'Longitude', 'Altitude_ft',
            'Heading', 'IAS_kts', 'GS_kts', 'VS_fpm',
            'Pitch', 'Bank', 'Throttle', 'Gear', 'Flaps'
        ])

        # Conectar
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('localhost', 12345))

        buffer = ""
        last_write = 0

        print(f"Grabando vuelo a {filename}...")
        print("Presiona Ctrl+C para detener.")

        try:
            while True:
                buffer += sock.recv(4096).decode('utf-8')

                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if not line.strip():
                        continue

                    data = json.loads(line)

                    # Grabar cada segundo
                    now = data.get('timestamp', 0)
                    if now - last_write >= 1000:  # 1000 ms
                        last_write = now

                        # Convertir valores
                        lat = math.degrees(data['latitude'])
                        lon = math.degrees(data['longitude'])
                        if lon > 180:
                            lon -= 360

                        writer.writerow([
                            datetime.now().isoformat(),
                            f"{lat:.6f}",
                            f"{lon:.6f}",
                            f"{data['altitude']:.0f}",
                            f"{math.degrees(data['magnetic_heading']) % 360:.0f}",
                            f"{data['indicated_airspeed'] * 1.94384:.0f}",
                            f"{data['ground_speed'] * 1.94384:.0f}",
                            f"{data['vertical_speed'] * 196.85:.0f}",
                            f"{math.degrees(data['pitch']):.1f}",
                            f"{math.degrees(data['bank']):.1f}",
                            f"{data['throttle'] * 100:.0f}",
                            f"{data['gear']:.2f}",
                            f"{data['flaps']:.2f}"
                        ])
                        csvfile.flush()  # Guardar inmediatamente

        except KeyboardInterrupt:
            print(f"\nVuelo guardado en {filename}")
        finally:
            sock.close()

if __name__ == "__main__":
    main()
```

### Ejemplo 3: Dashboard web simple (HTML + JavaScript)

Crea un archivo `dashboard.html`:

```html
<!DOCTYPE html>
<html>
<head>
    <title>Aerofly Dashboard</title>
    <style>
        body {
            font-family: 'Segoe UI', Arial, sans-serif;
            background: #1a1a2e;
            color: #eee;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        h1 {
            color: #00d4ff;
            text-align: center;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 15px;
        }
        .card {
            background: #16213e;
            border-radius: 10px;
            padding: 20px;
            text-align: center;
        }
        .card .label {
            color: #888;
            font-size: 12px;
            text-transform: uppercase;
        }
        .card .value {
            font-size: 32px;
            font-weight: bold;
            color: #00d4ff;
            margin: 10px 0;
        }
        .card .unit {
            color: #666;
            font-size: 14px;
        }
        #status {
            text-align: center;
            padding: 10px;
            margin-bottom: 20px;
        }
        .connected { color: #4caf50; }
        .disconnected { color: #f44336; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Aerofly FS4 Dashboard</h1>
        <div id="status" class="disconnected">Desconectado</div>

        <div class="grid">
            <div class="card">
                <div class="label">Altitud</div>
                <div class="value" id="altitude">---</div>
                <div class="unit">ft</div>
            </div>
            <div class="card">
                <div class="label">Velocidad</div>
                <div class="value" id="speed">---</div>
                <div class="unit">kts</div>
            </div>
            <div class="card">
                <div class="label">Heading</div>
                <div class="value" id="heading">---</div>
                <div class="unit">°</div>
            </div>
            <div class="card">
                <div class="label">Vel. Vertical</div>
                <div class="value" id="vs">---</div>
                <div class="unit">fpm</div>
            </div>
            <div class="card">
                <div class="label">Pitch</div>
                <div class="value" id="pitch">---</div>
                <div class="unit">°</div>
            </div>
            <div class="card">
                <div class="label">Bank</div>
                <div class="value" id="bank">---</div>
                <div class="unit">°</div>
            </div>
        </div>

        <p style="text-align: center; margin-top: 30px; color: #666;">
            Avion: <span id="aircraft">---</span>
        </p>
    </div>

    <script>
        // Para usar este dashboard, necesitas un servidor WebSocket
        // que haga de puente entre TCP y WebSocket.
        // Puedes usar el ejemplo "websocket_bridge.py" incluido.

        function connect() {
            const ws = new WebSocket('ws://localhost:8765');

            ws.onopen = () => {
                document.getElementById('status').textContent = 'Conectado';
                document.getElementById('status').className = 'connected';
            };

            ws.onclose = () => {
                document.getElementById('status').textContent = 'Desconectado - Reconectando...';
                document.getElementById('status').className = 'disconnected';
                setTimeout(connect, 2000);
            };

            ws.onmessage = (event) => {
                const data = JSON.parse(event.data);

                // Convertir y mostrar
                const altitude = data.altitude.toFixed(0);
                const speed = (data.indicated_airspeed * 1.94384).toFixed(0);
                const heading = ((data.magnetic_heading * 180 / Math.PI) % 360).toFixed(0);
                const vs = (data.vertical_speed * 196.85).toFixed(0);
                const pitch = (data.pitch * 180 / Math.PI).toFixed(1);
                const bank = (data.bank * 180 / Math.PI).toFixed(1);

                document.getElementById('altitude').textContent = altitude;
                document.getElementById('speed').textContent = speed;
                document.getElementById('heading').textContent = heading.padStart(3, '0');
                document.getElementById('vs').textContent = (vs > 0 ? '+' : '') + vs;
                document.getElementById('pitch').textContent = pitch;
                document.getElementById('bank').textContent = bank;
                document.getElementById('aircraft').textContent = data.aircraft_name;
            };
        }

        connect();
    </script>
</body>
</html>
```

Y el servidor bridge WebSocket (`websocket_bridge.py`):

```python
import asyncio
import websockets
import socket
import json

# Clientes WebSocket conectados
clients = set()

async def websocket_handler(websocket, path):
    """Maneja conexiones WebSocket"""
    clients.add(websocket)
    print(f"Cliente conectado. Total: {len(clients)}")
    try:
        await websocket.wait_closed()
    finally:
        clients.remove(websocket)
        print(f"Cliente desconectado. Total: {len(clients)}")

async def tcp_reader():
    """Lee del DLL por TCP y envía a todos los clientes WebSocket"""
    while True:
        try:
            reader, writer = await asyncio.open_connection('localhost', 12345)
            print("Conectado al DLL por TCP")

            buffer = ""
            while True:
                data = await reader.read(4096)
                if not data:
                    break

                buffer += data.decode('utf-8')

                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line.strip() and clients:
                        # Enviar a todos los clientes WebSocket
                        websockets.broadcast(clients, line)

        except Exception as e:
            print(f"Error TCP: {e}. Reconectando en 2s...")
            await asyncio.sleep(2)

async def main():
    # Iniciar servidor WebSocket
    ws_server = await websockets.serve(websocket_handler, "localhost", 8765)
    print("Servidor WebSocket en ws://localhost:8765")

    # Iniciar lector TCP
    tcp_task = asyncio.create_task(tcp_reader())

    await asyncio.gather(ws_server.wait_closed(), tcp_task)

if __name__ == "__main__":
    asyncio.run(main())
```

---

## 7. Solución de problemas

### No puedo conectarme (TCP)

**Error:** `Connection refused`

**Solución:**
1. Verifica que Aerofly FS4 esté corriendo
2. Verifica que el DLL esté en la carpeta correcta:
   `Documentos\Aerofly FS 4\external_dll\AeroflyReader.dll`
3. Reinicia Aerofly después de copiar el DLL

### No puedo conectarme (Shared Memory)

**Error:** `FileNotFoundError` o `No se pudo abrir la memoria compartida`

**Solución:**
1. Verifica que Aerofly esté corriendo con un vuelo activo
2. El DLL crea la memoria compartida cuando recibe el primer dato

### Los datos no se actualizan

**Causa:** Probablemente estás en el menú principal de Aerofly.

**Solución:** Inicia un vuelo. Los datos solo se envían durante un vuelo activo.

### Los valores de latitud/longitud se ven raros

**Causa:** Están en radianes, no en grados.

**Solución:** Convierte a grados:
```python
lat_deg = lat_rad * 180 / 3.14159265359
lon_deg = lon_rad * 180 / 3.14159265359
if lon_deg > 180:
    lon_deg -= 360
```

### La velocidad parece muy baja

**Causa:** Está en metros por segundo, no en nudos.

**Solución:** Multiplica por 1.94384 para obtener nudos.

### Quiero usar otro puerto TCP

**Solución:** Configura la variable de entorno antes de iniciar Aerofly:

```powershell
$env:AEROFLY_READER_TCP_PORT = "8888"
# Luego inicia Aerofly
```

### Quiero menos actualizaciones (ahorrar CPU)

**Solución:** Aumenta el intervalo de broadcast:

```powershell
# 100 ms = 10 actualizaciones por segundo
$env:AEROFLY_READER_BROADCAST_MS = "100"
```

---

## Recursos adicionales

- **Ejemplos incluidos:** Revisa la carpeta `simplified/examples/` para más ejemplos
- **README técnico:** `simplified/README.md` tiene todos los detalles técnicos
- **Código fuente:** `simplified/aerofly_reader_dll.cpp` si quieres ver cómo funciona internamente

---

**Versión:** 1.1.0
**Plataforma:** Windows x64
**Compatibilidad:** Aerofly FS 4
