# Plan: Python SDK para AeroflyReader DLL

## Resumen Ejecutivo

Crear un SDK de Python que simplifique el uso del DLL simplificado `AeroflyReader.dll`, proporcionando una API limpia, tipada y Pythonic para acceder a los datos de telemetría de Aerofly FS4.

## Problemas Actuales (sin SDK)

Los ejemplos actuales en `simplified/examples/` muestran código repetitivo:

```python
# Cada script repite esto:
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))
buffer = ""
while True:
    data = sock.recv(4096).decode('utf-8')
    buffer += data
    while '\n' in buffer:
        line, buffer = buffer.split('\n', 1)
        flight_data = json.loads(line)
        # Conversiones manuales...
        lat = math.degrees(flight_data['latitude'])
        speed_kts = flight_data['indicated_airspeed'] * 1.94384
```

**Problemas identificados:**
1. Código de conexión TCP duplicado en cada script
2. Conversiones de unidades manuales (radians→degrees, m/s→knots, etc.)
3. Sin tipado - acceso via `dict.get()` sin autocompletado
4. Sin reconexión automática ante desconexiones
5. Sin soporte async para aplicaciones modernas
6. Shared Memory requiere conocer estructura binaria exacta

---

## Arquitectura Propuesta del SDK

```
simplified/
└── sdk/
    └── aerofly_reader/           # Paquete principal
        ├── __init__.py           # Exports públicos
        ├── client.py             # AeroflyClient (TCP)
        ├── shared_memory.py      # SharedMemoryClient
        ├── models.py             # Dataclasses tipados
        ├── units.py              # Conversiones de unidades
        ├── async_client.py       # AsyncAeroflyClient
        └── exceptions.py         # Excepciones personalizadas
```

---

## Componentes del SDK

### 1. Modelos de Datos Tipados (`models.py`)

```python
from dataclasses import dataclass
from typing import Optional

@dataclass
class Position:
    """Posición GPS con conversión automática a grados."""
    latitude_rad: float
    longitude_rad: float
    altitude_ft: float
    height_agl_ft: float

    @property
    def latitude(self) -> float:
        """Latitud en grados."""
        return math.degrees(self.latitude_rad)

    @property
    def longitude(self) -> float:
        """Longitud en grados (-180 a +180)."""
        lon = math.degrees(self.longitude_rad)
        return lon - 360 if lon > 180 else lon

@dataclass
class Speeds:
    """Velocidades con conversión automática a knots/fpm."""
    indicated_airspeed_ms: float
    ground_speed_ms: float
    vertical_speed_ms: float
    mach_number: float

    @property
    def indicated_airspeed_kts(self) -> float:
        return self.indicated_airspeed_ms * 1.94384

    @property
    def vertical_speed_fpm(self) -> float:
        return self.vertical_speed_ms * 196.85

@dataclass
class Orientation:
    """Orientación del avión."""
    pitch_rad: float
    bank_rad: float
    true_heading_rad: float
    magnetic_heading_rad: float

    @property
    def pitch(self) -> float:
        """Pitch en grados."""
        return math.degrees(self.pitch_rad)

    @property
    def heading(self) -> float:
        """Heading magnético en grados (0-360)."""
        return math.degrees(self.magnetic_heading_rad) % 360

@dataclass
class FlightData:
    """Datos completos de vuelo."""
    timestamp: int
    update_counter: int
    data_valid: bool
    update_hz: float

    position: Position
    speeds: Speeds
    orientation: Orientation

    # Estado del avión
    on_ground: bool
    gear: float
    flaps: float
    throttle: float
    parking_brake: bool

    # Motores
    engine1_running: bool
    engine2_running: bool
    engine1_throttle: float
    engine2_throttle: float

    # Autopilot
    autopilot_master: bool
    autopilot_heading_rad: float
    autopilot_altitude_ft: float

    # Info
    aircraft_name: str
    nearest_airport_id: str
    nearest_airport_name: str

    @classmethod
    def from_json(cls, data: dict) -> 'FlightData':
        """Crea FlightData desde JSON del DLL."""
        # ... parsing automático
```

### 2. Cliente TCP Síncrono (`client.py`)

```python
class AeroflyClient:
    """Cliente TCP para AeroflyReader DLL."""

    def __init__(
        self,
        host: str = 'localhost',
        port: int = 12345,
        auto_reconnect: bool = True,
        reconnect_delay: float = 2.0
    ):
        ...

    def connect(self) -> None:
        """Conecta al servidor TCP."""

    def disconnect(self) -> None:
        """Desconecta del servidor."""

    def read(self) -> FlightData:
        """Lee el siguiente frame de datos."""

    def stream(self) -> Iterator[FlightData]:
        """Genera frames de datos continuamente."""

    def __enter__(self) -> 'AeroflyClient':
        self.connect()
        return self

    def __exit__(self, *args):
        self.disconnect()

# Uso simplificado:
with AeroflyClient() as client:
    for flight in client.stream():
        print(f"Alt: {flight.position.altitude_ft:.0f} ft")
        print(f"Speed: {flight.speeds.indicated_airspeed_kts:.0f} kts")
```

### 3. Cliente Async (`async_client.py`)

```python
class AsyncAeroflyClient:
    """Cliente async para AeroflyReader DLL."""

    async def connect(self) -> None:
        ...

    async def read(self) -> FlightData:
        ...

    async def stream(self) -> AsyncIterator[FlightData]:
        ...

    async def __aenter__(self) -> 'AsyncAeroflyClient':
        await self.connect()
        return self

    async def __aexit__(self, *args):
        await self.disconnect()

# Uso:
async with AsyncAeroflyClient() as client:
    async for flight in client.stream():
        print(f"Alt: {flight.position.altitude_ft:.0f} ft")
```

### 4. Cliente Shared Memory (`shared_memory.py`)

```python
class SharedMemoryClient:
    """Acceso directo a shared memory (más rápido, solo Windows)."""

    def __init__(self, mapping_name: str = "AeroflyReaderData"):
        ...

    def read(self) -> FlightData:
        """Lee datos directamente de memoria compartida."""

    def is_valid(self) -> bool:
        """Verifica si los datos son válidos."""

# Uso:
with SharedMemoryClient() as client:
    data = client.read()
    print(f"Latency: ~0.01ms")
```

### 5. Conversiones de Unidades (`units.py`)

```python
# Constantes de conversión
MS_TO_KNOTS = 1.94384
MS_TO_FPM = 196.85
FEET_TO_METERS = 0.3048

def ms_to_knots(ms: float) -> float:
    """Convierte m/s a nudos."""
    return ms * MS_TO_KNOTS

def ms_to_fpm(ms: float) -> float:
    """Convierte m/s a pies/minuto."""
    return ms * MS_TO_FPM

def radians_to_degrees(rad: float) -> float:
    """Convierte radianes a grados."""
    return math.degrees(rad)

def normalize_heading(degrees: float) -> float:
    """Normaliza heading a 0-360."""
    return degrees % 360

def normalize_longitude(degrees: float) -> float:
    """Normaliza longitud a -180/+180."""
    return degrees - 360 if degrees > 180 else degrees
```

### 6. Excepciones (`exceptions.py`)

```python
class AeroflyError(Exception):
    """Base para excepciones del SDK."""

class ConnectionError(AeroflyError):
    """Error de conexión al DLL."""

class TimeoutError(AeroflyError):
    """Timeout esperando datos."""

class DataError(AeroflyError):
    """Error en los datos recibidos."""
```

---

## API Pública Simplificada

El SDK expondrá una API minimalista para casos comunes:

```python
from aerofly_reader import connect, FlightData

# Caso más simple - una línea para conectar
with connect() as flight:
    print(f"Aircraft: {flight.aircraft_name}")
    print(f"Position: {flight.position.latitude:.4f}, {flight.position.longitude:.4f}")
    print(f"Altitude: {flight.position.altitude_ft:.0f} ft")

# Streaming continuo
from aerofly_reader import stream

for flight in stream():
    if flight.on_ground:
        print("On ground!")
    else:
        print(f"Flying at {flight.speeds.indicated_airspeed_kts:.0f} kts")

# Async
from aerofly_reader import async_stream

async for flight in async_stream():
    await process_flight(flight)
```

---

## Estructura de Archivos Final

```
simplified/
└── sdk/
    ├── aerofly_reader/
    │   ├── __init__.py           # API pública
    │   ├── client.py             # AeroflyClient
    │   ├── async_client.py       # AsyncAeroflyClient
    │   ├── shared_memory.py      # SharedMemoryClient
    │   ├── models.py             # FlightData, Position, etc.
    │   ├── units.py              # Conversiones
    │   └── exceptions.py         # Excepciones
    ├── examples/
    │   ├── basic_usage.py        # Ejemplo básico
    │   ├── flight_logger.py      # Logger usando SDK
    │   ├── async_monitor.py      # Ejemplo async
    │   └── shared_memory_demo.py # Demo shared memory
    ├── tests/
    │   ├── test_models.py
    │   ├── test_client.py
    │   └── test_units.py
    ├── pyproject.toml            # Configuración del paquete
    ├── README.md                 # Documentación
    └── PLAN.md                   # Este archivo
```

---

## Dependencias

**Mínimas (solo stdlib):**
- `socket` - Conexión TCP
- `json` - Parsing JSON
- `struct` - Shared memory
- `mmap` - Shared memory (Windows)
- `asyncio` - Soporte async
- `dataclasses` - Modelos de datos
- `typing` - Type hints

**Opcionales:**
- `typing_extensions` - Para Python < 3.10

---

## Pasos de Implementación

1. **Crear estructura de directorios**
2. **Implementar `units.py`** - Funciones de conversión
3. **Implementar `exceptions.py`** - Excepciones base
4. **Implementar `models.py`** - Dataclasses con propiedades
5. **Implementar `client.py`** - Cliente TCP síncrono
6. **Implementar `async_client.py`** - Cliente async
7. **Implementar `shared_memory.py`** - Cliente shared memory
8. **Implementar `__init__.py`** - API pública
9. **Crear `pyproject.toml`** - Configuración del paquete
10. **Escribir ejemplos** - Demos de uso
11. **Escribir README.md** - Documentación
12. **Escribir tests** - Tests unitarios

---

## Beneficios del SDK

| Sin SDK | Con SDK |
|---------|---------|
| ~50 líneas para conectar y leer | 3 líneas |
| Conversiones manuales | Propiedades automáticas |
| Sin tipado | Autocompletado completo |
| Sin reconexión | Reconexión automática |
| Solo sync | Sync + Async |
| Código repetido | Reutilizable |

---

## Compatibilidad

- **Python**: 3.8+
- **Plataforma**: Windows (shared memory), Cross-platform (TCP)
- **Aerofly FS4**: Compatible con AeroflyReader.dll v1.1.0+
