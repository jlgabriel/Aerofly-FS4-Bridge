# Aerofly FS4 Reader DLL v1.1.0

Una versión simplificada y optimizada del Aerofly Bridge enfocada exclusivamente en **lectura de datos** del simulador. Ideal para integraciones que solo necesitan monitorear el estado del vuelo sin controlar el avión.

## Características Principales

- **Solo lectura** - Sin comandos, sin control del avión
- **~850 líneas de código** - vs 9,000+ del bridge completo
- **Alto rendimiento** - JSON builder optimizado con zero allocations
- **Dos interfaces** - Shared Memory (local) + TCP Streaming (red)
- **Robusto** - Protección automática contra valores NaN/Inf

## Arquitectura

```
Aerofly FS4 (50-60 Hz)
        │
        │ tm_external_message
        ▼
┌─────────────────────────────────────────────────┐
│              AeroflyReader.dll                  │
│                                                 │
│  ┌─────────────────┐    ┌─────────────────┐    │
│  │ SharedMemory    │    │ TCPDataServer   │    │
│  │ "AeroflyReader  │    │ Puerto 12345    │    │
│  │  Data"          │    │ JSON @ 50 Hz    │    │
│  └────────┬────────┘    └────────┬────────┘    │
└───────────┼──────────────────────┼─────────────┘
            │                      │
            ▼                      ▼
     Apps Locales            Clientes Red
    (Python, C++)         (Python, Web, etc.)
```

## Entrega de Datos

### 1. Shared Memory (Alto Rendimiento)

Acceso directo a memoria para aplicaciones locales. Latencia mínima (~0.01ms).

```python
import mmap
import struct

# Abrir shared memory
shm = mmap.mmap(-1, 1024, "AeroflyReaderData", access=mmap.ACCESS_READ)

while True:
    shm.seek(0)

    # Header (16 bytes)
    timestamp = struct.unpack('Q', shm.read(8))[0]
    data_valid = struct.unpack('I', shm.read(4))[0]
    update_counter = struct.unpack('I', shm.read(4))[0]

    # Posición (doubles)
    lat = struct.unpack('d', shm.read(8))[0]
    lon = struct.unpack('d', shm.read(8))[0]
    alt = struct.unpack('d', shm.read(8))[0]

    if data_valid:
        print(f"Pos: {lat:.4f}, {lon:.4f} | Alt: {alt:.0f} ft")
```

### 2. TCP Streaming (Red)

JSON streaming para clientes de red. Configurable hasta 50 Hz.

```python
import socket
import json

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))

buffer = ""
while True:
    buffer += sock.recv(4096).decode('utf-8')

    while '\n' in buffer:
        line, buffer = buffer.split('\n', 1)
        if line.strip():
            data = json.loads(line)
            print(f"Alt: {data['altitude']:.0f} ft @ {data['update_hz']:.1f} Hz")
```

## Formato JSON

```json
{
    "schema": "aerofly-reader-telemetry",
    "version": "1.1.0",
    "update_hz": 49.8,
    "timestamp": 1234567890123,
    "data_valid": 1,
    "update_counter": 42,

    "latitude": 47.449889,
    "longitude": -122.309444,
    "altitude": 5280.0,
    "height": 1500.0,
    "pitch": 0.087266,
    "bank": -0.034907,
    "true_heading": 3.141593,
    "magnetic_heading": 3.089233,

    "indicated_airspeed": 77.17,
    "ground_speed": 82.31,
    "vertical_speed": 2.540,
    "mach_number": 0.2300,
    "angle_of_attack": 0.052360,

    "on_ground": 0,
    "gear": 0.00,
    "flaps": 0.00,
    "throttle": 0.65,
    "parking_brake": 0,

    "engine_running_1": 1,
    "engine_running_2": 1,
    "engine_throttle_1": 0.65,
    "engine_throttle_2": 0.65,

    "nav1_frequency": 112.500,
    "nav2_frequency": 108.000,
    "com1_frequency": 121.500,
    "com2_frequency": 118.000,

    "autopilot_master": 1,
    "autopilot_heading": 3.141593,
    "autopilot_altitude": 10000,

    "vs0": 23.15,
    "vs1": 28.29,
    "vfe": 43.72,
    "vno": 66.88,
    "vne": 82.31,

    "position": {"x": 123.45, "y": 567.89, "z": 910.11},
    "velocity": {"x": 10.50, "y": 0.00, "z": 2.50},

    "aircraft_name": "Cessna 172",
    "nearest_airport_id": "KSEA",
    "nearest_airport_name": "Seattle-Tacoma International"
}
```

### Campos Especiales

| Campo | Descripción |
|-------|-------------|
| `version` | Versión del DLL (ej: "1.1.0") |
| `update_hz` | Frecuencia real de actualización calculada |
| `data_valid` | 1 = datos válidos, 0 = en actualización |

## Variables Disponibles

### Posición y Orientación
| Variable | Unidad | Descripción |
|----------|--------|-------------|
| `latitude` | grados | Latitud GPS |
| `longitude` | grados | Longitud GPS |
| `altitude` | pies | Altitud MSL |
| `height` | pies | Altura AGL (sobre el terreno) |
| `pitch` | radianes | Cabeceo |
| `bank` | radianes | Alabeo |
| `true_heading` | radianes | Rumbo verdadero |
| `magnetic_heading` | radianes | Rumbo magnético |

### Velocidades
| Variable | Unidad | Descripción |
|----------|--------|-------------|
| `indicated_airspeed` | m/s | Velocidad indicada |
| `ground_speed` | m/s | Velocidad sobre el suelo |
| `vertical_speed` | m/s | Velocidad vertical |
| `mach_number` | - | Número Mach |
| `angle_of_attack` | radianes | Ángulo de ataque |

### Estado del Avión
| Variable | Rango | Descripción |
|----------|-------|-------------|
| `on_ground` | 0/1 | En tierra |
| `gear` | 0-1 | Posición tren de aterrizaje |
| `flaps` | 0-1 | Posición flaps |
| `throttle` | 0-1 | Potencia general |
| `parking_brake` | 0/1 | Freno de estacionamiento |

### Motores
| Variable | Rango | Descripción |
|----------|-------|-------------|
| `engine_running_1` | 0/1 | Motor 1 encendido |
| `engine_running_2` | 0/1 | Motor 2 encendido |
| `engine_throttle_1` | 0-1 | Throttle motor 1 |
| `engine_throttle_2` | 0-1 | Throttle motor 2 |

### Navegación
| Variable | Unidad | Descripción |
|----------|--------|-------------|
| `nav1_frequency` | MHz | Frecuencia NAV1 |
| `nav2_frequency` | MHz | Frecuencia NAV2 |
| `com1_frequency` | MHz | Frecuencia COM1 |
| `com2_frequency` | MHz | Frecuencia COM2 |
| `selected_course_1` | radianes | Curso seleccionado 1 |
| `selected_course_2` | radianes | Curso seleccionado 2 |

### Autopilot (solo lectura)
| Variable | Unidad | Descripción |
|----------|--------|-------------|
| `autopilot_master` | 0/1 | AP activo |
| `autopilot_heading` | radianes | Rumbo seleccionado |
| `autopilot_altitude` | pies | Altitud seleccionada |

### V-Speeds
| Variable | Descripción |
|----------|-------------|
| `vs0` | Velocidad de pérdida (flaps extendidos) |
| `vs1` | Velocidad de pérdida (configuración limpia) |
| `vfe` | Velocidad máxima con flaps |
| `vno` | Velocidad máxima crucero estructural |
| `vne` | Velocidad de nunca exceder |

## Configuración

### Variables de Entorno

| Variable | Default | Descripción |
|----------|---------|-------------|
| `AEROFLY_READER_TCP_PORT` | 12345 | Puerto TCP para streaming |
| `AEROFLY_READER_BROADCAST_MS` | 20 | Intervalo de broadcast (ms) |

```powershell
# Ejemplo: cambiar puerto y reducir frecuencia
$env:AEROFLY_READER_TCP_PORT = "8888"
$env:AEROFLY_READER_BROADCAST_MS = "100"  # 10 Hz
```

## Compilación

### Requisitos
- Windows 10/11
- Visual Studio 2022 (C++ desktop development)
- CMake 3.15+
- Header del SDK: `tm_external_message.h`

### Pasos

```powershell
# 1. Copiar el header del SDK de Aerofly a este directorio
copy "path\to\tm_external_message.h" simplified\

# 2. Configurar y compilar
cd simplified
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 3. Instalar
copy build\Release\AeroflyReader.dll "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll\"
```

## Optimizaciones de Rendimiento

### v1.1.0 Mejoras

| Optimización | Beneficio |
|--------------|-----------|
| **JSON con snprintf** | 3-5x más rápido que ostringstream |
| **Buffer estático** | Zero allocations por frame |
| **Macros para handlers** | Código 15% más compacto |
| **Protección NaN/Inf** | JSON siempre válido |

### Características Técnicas

- **Thread-local buffer**: Cada thread tiene su propio buffer JSON
- **Validación automática**: Valores NaN/Inf se convierten a 0.0
- **Hash map O(1)**: Lookup instantáneo de message handlers
- **Non-blocking sockets**: TCP no bloquea el thread principal

## Casos de Uso

| Aplicación | Descripción |
|------------|-------------|
| **Flight Logger** | Graba vuelos a CSV/base de datos |
| **Map Tracker** | Posición en tiempo real en mapas |
| **Training Dashboard** | Métricas para entrenamiento |
| **Stream Overlay** | Overlay OBS con datos de vuelo |
| **Home Cockpit** | Alimenta instrumentos físicos |

## Ejemplos Incluidos

```
simplified/
└── examples/
    ├── simple_reader.py   # Visualización en tiempo real
    └── flight_logger.py   # Grabación a CSV
```

## Comparación con Bridge Completo

| Característica | Reader | Bridge Completo |
|----------------|--------|-----------------|
| Líneas de código | ~850 | ~9,000 |
| Variables | ~50 | 358 |
| Lectura de datos | ✅ | ✅ |
| Control del avión | ❌ | ✅ |
| Shared Memory | ✅ | ✅ |
| TCP Streaming | ✅ | ✅ |
| TCP Commands | ❌ | ✅ |
| WebSocket | ❌ | ✅ |
| C172 específico | ❌ | ✅ |
| Warnings | ❌ | ✅ |

## Licencia

MIT License - Mismo que el proyecto principal.

---

**Versión**: 1.1.0
**Compatibilidad**: Aerofly FS 4
**Plataforma**: Windows x64
