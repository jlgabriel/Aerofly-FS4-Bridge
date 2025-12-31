"""
Aerofly Reader SDK

A Python SDK for easy integration with the AeroflyReader DLL,
providing access to Aerofly FS4 flight telemetry data.

Quick Start:
    >>> from aerofly_reader import stream
    >>>
    >>> for flight in stream():
    ...     print(f"Aircraft: {flight.aircraft_name}")
    ...     print(f"Position: {flight.position.latitude:.4f}°, {flight.position.longitude:.4f}°")
    ...     print(f"Altitude: {flight.position.altitude_ft:.0f} ft")
    ...     print(f"Speed: {flight.speeds.indicated_airspeed:.0f} kts")

Using the client directly:
    >>> from aerofly_reader import AeroflyClient
    >>>
    >>> with AeroflyClient() as client:
    ...     data = client.read()
    ...     print(data.aircraft_name)

Async usage:
    >>> import asyncio
    >>> from aerofly_reader import AsyncAeroflyClient
    >>>
    >>> async def main():
    ...     async with AsyncAeroflyClient() as client:
    ...         async for flight in client.stream():
    ...             print(flight.position.altitude_ft)
    >>>
    >>> asyncio.run(main())

Shared memory (Windows, lowest latency):
    >>> from aerofly_reader import SharedMemoryClient
    >>>
    >>> with SharedMemoryClient() as client:
    ...     data = client.read()
    ...     print(f"Latency: ~0.01ms")
"""

__version__ = "1.0.0"
__author__ = "Aerofly FS4 Bridge Project"

# Main clients
from .client import (
    AeroflyClient,
    connect,
    stream,
    read_once,
)

from .async_client import (
    AsyncAeroflyClient,
    async_connect,
    async_stream,
    async_read_once,
)

from .shared_memory import SharedMemoryClient

# Data models
from .models import (
    FlightData,
    Position,
    Speeds,
    Orientation,
    Engines,
    EngineState,
    Autopilot,
    Navigation,
    VSpeeds,
    AircraftInfo,
    Vector3D,
)

# Exceptions
from .exceptions import (
    AeroflyError,
    ConnectionError,
    DisconnectedError,
    TimeoutError,
    DataError,
    SharedMemoryError,
    NotConnectedError,
)

# Unit conversion utilities
from .units import (
    ms_to_knots,
    knots_to_ms,
    ms_to_fpm,
    fpm_to_ms,
    ms_to_kmh,
    radians_to_degrees,
    degrees_to_radians,
    normalize_heading,
    normalize_longitude,
    feet_to_meters,
    meters_to_feet,
    format_heading,
    format_altitude,
    format_speed_kts,
    format_vertical_speed,
)

__all__ = [
    # Version
    "__version__",
    # Clients
    "AeroflyClient",
    "AsyncAeroflyClient",
    "SharedMemoryClient",
    # Convenience functions
    "connect",
    "stream",
    "read_once",
    "async_connect",
    "async_stream",
    "async_read_once",
    # Models
    "FlightData",
    "Position",
    "Speeds",
    "Orientation",
    "Engines",
    "EngineState",
    "Autopilot",
    "Navigation",
    "VSpeeds",
    "AircraftInfo",
    "Vector3D",
    # Exceptions
    "AeroflyError",
    "ConnectionError",
    "DisconnectedError",
    "TimeoutError",
    "DataError",
    "SharedMemoryError",
    "NotConnectedError",
    # Units
    "ms_to_knots",
    "knots_to_ms",
    "ms_to_fpm",
    "fpm_to_ms",
    "ms_to_kmh",
    "radians_to_degrees",
    "degrees_to_radians",
    "normalize_heading",
    "normalize_longitude",
    "feet_to_meters",
    "meters_to_feet",
    "format_heading",
    "format_altitude",
    "format_speed_kts",
    "format_vertical_speed",
]
