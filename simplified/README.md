# Aerofly FS4 Reader - Simplified Read-Only Version

Una versión simplificada del Aerofly Bridge enfocada exclusivamente en **lectura de datos** del simulador. Ideal para integraciones que solo necesitan monitorear el estado del vuelo sin controlar el avión.

## Características

### Lo que INCLUYE:
- **Lectura de datos** del simulador a 50-60 Hz
- **Shared Memory** para aplicaciones locales de alto rendimiento
- **TCP Streaming** (puerto 12345) para clientes de red
- **JSON simplificado** con las variables más importantes
- **~800 líneas de código** (vs 9000+ del bridge completo)

### Lo que NO incluye:
- ❌ Envío de comandos al simulador
- ❌ Control de throttle, flaps, gear, etc.
- ❌ Puerto de comandos TCP
- ❌ WebSocket server
- ❌ Variables especializadas (C172, warnings, etc.)

## Arquitectura

```
Aerofly FS4 (50-60 Hz)
        │
        │ [Messages]
        ▼
┌─────────────────────────────┐
│    AeroflyReader DLL        │
│  ┌───────────────────────┐  │
│  │ SharedMemoryReader    │  │  → Aplicaciones locales (Python, C++)
│  │ "AeroflyReaderData"   │  │
│  └───────────────────────┘  │
│  ┌───────────────────────┐  │
│  │ TCPDataServer         │  │  → Clientes de red (JSON streaming)
│  │ Port 12345            │  │
│  └───────────────────────┘  │
└─────────────────────────────┘
```

## Variables Disponibles

### Posición y Orientación
| Variable | Descripción | Unidad |
|----------|-------------|--------|
| `latitude` | Latitud GPS | grados |
| `longitude` | Longitud GPS | grados |
| `altitude` | Altitud MSL | pies |
| `height` | Altura AGL | pies |
| `pitch` | Cabeceo | radianes |
| `bank` | Alabeo | radianes |
| `true_heading` | Rumbo verdadero | radianes |
| `magnetic_heading` | Rumbo magnético | radianes |

### Velocidades
| Variable | Descripción | Unidad |
|----------|-------------|--------|
| `indicated_airspeed` | Velocidad indicada | m/s |
| `ground_speed` | Velocidad respecto al suelo | m/s |
| `vertical_speed` | Velocidad vertical | m/s |
| `mach_number` | Número Mach | - |
| `angle_of_attack` | Ángulo de ataque | radianes |

### Estado del Avión
| Variable | Descripción | Rango |
|----------|-------------|-------|
| `on_ground` | En tierra | 0/1 |
| `gear` | Tren de aterrizaje | 0-1 |
| `flaps` | Flaps | 0-1 |
| `throttle` | Potencia general | 0-1 |
| `parking_brake` | Freno de estacionamiento | 0/1 |

### Motores
| Variable | Descripción | Rango |
|----------|-------------|-------|
| `engine_running_1` | Motor 1 encendido | 0/1 |
| `engine_running_2` | Motor 2 encendido | 0/1 |
| `engine_throttle_1` | Throttle motor 1 | 0-1 |
| `engine_throttle_2` | Throttle motor 2 | 0-1 |

### Navegación
| Variable | Descripción | Unidad |
|----------|-------------|--------|
| `nav1_frequency` | Frecuencia NAV1 | MHz |
| `nav2_frequency` | Frecuencia NAV2 | MHz |
| `com1_frequency` | Frecuencia COM1 | MHz |
| `com2_frequency` | Frecuencia COM2 | MHz |

### V-Speeds
| Variable | Descripción |
|----------|-------------|
| `vs0` | Velocidad de pérdida (flaps abajo) |
| `vs1` | Velocidad de pérdida (limpio) |
| `vfe` | Velocidad máxima con flaps |
| `vno` | Velocidad máxima crucero estructural |
| `vne` | Velocidad de nunca exceder |

### Strings
| Variable | Descripción |
|----------|-------------|
| `aircraft_name` | Nombre del avión |
| `nearest_airport_id` | Código ICAO aeropuerto cercano |
| `nearest_airport_name` | Nombre aeropuerto cercano |

## Compilación

### Requisitos
- Windows 10/11
- Visual Studio 2022 (con C++ tools)
- CMake 3.15+
- Aerofly SDK (`tm_external_message.h`)

### Pasos

1. **Obtener el header del SDK de Aerofly:**
   ```
   Descarga tm_external_message.h del SDK de Aerofly
   Cópialo a este directorio (simplified/)
   ```

2. **Compilar:**
   ```powershell
   cd simplified
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

3. **Instalar:**
   ```powershell
   copy build\Release\AeroflyReader.dll "%USERPROFILE%\Documents\Aerofly FS 4\external_dll\"
   ```

## Uso

### Cliente Python (TCP)

```python
import socket
import json

def read_aerofly_data():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('localhost', 12345))

    buffer = ""
    while True:
        data = sock.recv(4096).decode('utf-8')
        buffer += data

        # Buscar líneas completas de JSON
        while '\n' in buffer:
            line, buffer = buffer.split('\n', 1)
            if line.strip():
                flight_data = json.loads(line)

                # Ejemplo: mostrar posición
                print(f"Pos: {flight_data['latitude']:.4f}, {flight_data['longitude']:.4f}")
                print(f"Alt: {flight_data['altitude']:.0f} ft")
                print(f"GS: {flight_data['ground_speed'] * 1.944:.0f} kts")
                print("---")

if __name__ == "__main__":
    read_aerofly_data()
```

### Cliente Python (Shared Memory)

```python
import mmap
import struct
import time

# Estructura simplificada (primeros campos)
def read_shared_memory():
    try:
        # Abrir shared memory
        shm = mmap.mmap(-1, 1024, "AeroflyReaderData", access=mmap.ACCESS_READ)

        while True:
            shm.seek(0)

            # Leer header (16 bytes)
            timestamp = struct.unpack('Q', shm.read(8))[0]
            data_valid = struct.unpack('I', shm.read(4))[0]
            update_counter = struct.unpack('I', shm.read(4))[0]

            # Leer posición (8 doubles = 64 bytes)
            lat = struct.unpack('d', shm.read(8))[0]
            lon = struct.unpack('d', shm.read(8))[0]
            alt = struct.unpack('d', shm.read(8))[0]

            if data_valid:
                print(f"[{update_counter}] Lat: {lat:.4f}, Lon: {lon:.4f}, Alt: {alt:.0f} ft")

            time.sleep(0.1)

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    read_shared_memory()
```

## Configuración

### Variables de Entorno

| Variable | Descripción | Default |
|----------|-------------|---------|
| `AEROFLY_READER_TCP_PORT` | Puerto TCP para streaming | 12345 |
| `AEROFLY_READER_BROADCAST_MS` | Intervalo de broadcast (ms) | 20 |

### Ejemplo

```powershell
# Cambiar puerto TCP
$env:AEROFLY_READER_TCP_PORT = "8888"

# Reducir frecuencia de broadcast (100ms = 10Hz)
$env:AEROFLY_READER_BROADCAST_MS = "100"
```

## JSON Output Format

```json
{
    "schema": "aerofly-reader-telemetry",
    "schema_version": 1,
    "timestamp": 1234567890,
    "data_valid": 1,
    "update_counter": 42,
    "latitude": 47.123456,
    "longitude": -122.654321,
    "altitude": 5280.0,
    "height": 1500.0,
    "pitch": 0.087,
    "bank": 0.0,
    "true_heading": 3.14159,
    "magnetic_heading": 3.12,
    "indicated_airspeed": 75.5,
    "ground_speed": 80.2,
    "vertical_speed": 2.5,
    "on_ground": 0,
    "gear": 0.0,
    "flaps": 0.0,
    "throttle": 0.65,
    "engine_running_1": 1,
    "engine_running_2": 1,
    "vs0": 45.0,
    "vs1": 55.0,
    "vfe": 85.0,
    "vno": 130.0,
    "vne": 160.0,
    "position": {"x": 123.4, "y": 567.8, "z": 910.1},
    "velocity": {"x": 10.5, "y": 0.0, "z": 2.5},
    "aircraft_name": "Cessna 172",
    "nearest_airport_id": "KSEA",
    "nearest_airport_name": "Seattle-Tacoma International"
}
```

## Casos de Uso

### Flight Logger
Graba todos los datos del vuelo a un archivo CSV o base de datos para análisis posterior.

### Map Tracker
Muestra la posición del avión en un mapa en tiempo real (Google Maps, OpenStreetMap, etc.).

### Training Dashboard
Monitorea métricas de vuelo para entrenamiento: velocidades, actitudes, procedimientos.

### Stream Overlay
Genera overlays para streaming (OBS) con datos del vuelo en tiempo real.

### Home Cockpit Integration
Alimenta instrumentos físicos o digitales con datos del simulador.

## Comparación con Bridge Completo

| Característica | Reader (Simplificado) | Bridge (Completo) |
|---------------|----------------------|-------------------|
| Líneas de código | ~800 | ~9,000 |
| Variables | ~50 | 358 |
| Lectura de datos | ✅ | ✅ |
| Control del avión | ❌ | ✅ |
| Shared Memory | ✅ | ✅ |
| TCP Streaming | ✅ | ✅ |
| TCP Commands | ❌ | ✅ |
| WebSocket | ❌ | ✅ |
| Variables C172 | ❌ | ✅ |
| Warnings | ❌ | ✅ |

## Licencia

MIT License - Mismo que el proyecto principal.
