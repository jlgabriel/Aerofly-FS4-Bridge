"""
Aerofly Reader SDK - Shared Memory Client

Direct access to shared memory for lowest latency data access.
Windows-only feature.
"""

import struct
import logging
import sys
from typing import Optional
from dataclasses import dataclass

from .models import FlightData, Position, Speeds, Orientation, Vector3D
from .models import Engines, EngineState, Autopilot, Navigation, VSpeeds, AircraftInfo
from .exceptions import SharedMemoryError, NotConnectedError

logger = logging.getLogger(__name__)


# Shared memory structure offsets and sizes
# Based on AeroflyReaderData struct in aerofly_reader_dll.cpp

@dataclass
class MemoryLayout:
    """Memory layout for AeroflyReaderData structure."""
    # Header (16 bytes)
    TIMESTAMP_OFFSET = 0          # uint64_t (8 bytes)
    DATA_VALID_OFFSET = 8         # uint32_t (4 bytes)
    UPDATE_COUNTER_OFFSET = 12    # uint32_t (4 bytes)

    # Position and orientation (doubles, 8 bytes each)
    LATITUDE_OFFSET = 16
    LONGITUDE_OFFSET = 24
    ALTITUDE_OFFSET = 32
    HEIGHT_OFFSET = 40
    PITCH_OFFSET = 48
    BANK_OFFSET = 56
    TRUE_HEADING_OFFSET = 64
    MAGNETIC_HEADING_OFFSET = 72

    # Speeds
    INDICATED_AIRSPEED_OFFSET = 80
    GROUND_SPEED_OFFSET = 88
    VERTICAL_SPEED_OFFSET = 96
    MACH_NUMBER_OFFSET = 104
    ANGLE_OF_ATTACK_OFFSET = 112

    # State
    ON_GROUND_OFFSET = 120
    GEAR_OFFSET = 128
    FLAPS_OFFSET = 136
    THROTTLE_OFFSET = 144
    PARKING_BRAKE_OFFSET = 152

    # Engines
    ENGINE_RUNNING_1_OFFSET = 160
    ENGINE_RUNNING_2_OFFSET = 168
    ENGINE_THROTTLE_1_OFFSET = 176
    ENGINE_THROTTLE_2_OFFSET = 184

    # Navigation
    NAV1_FREQUENCY_OFFSET = 192
    NAV2_FREQUENCY_OFFSET = 200
    COM1_FREQUENCY_OFFSET = 208
    COM2_FREQUENCY_OFFSET = 216
    SELECTED_COURSE_1_OFFSET = 224
    SELECTED_COURSE_2_OFFSET = 232

    # Autopilot
    AUTOPILOT_MASTER_OFFSET = 240
    AUTOPILOT_HEADING_OFFSET = 248
    AUTOPILOT_ALTITUDE_OFFSET = 256
    AUTOPILOT_VS_OFFSET = 264

    # V-Speeds
    VS0_OFFSET = 272
    VS1_OFFSET = 280
    VFE_OFFSET = 288
    VNO_OFFSET = 296
    VNE_OFFSET = 304

    # Position vectors (3 doubles each = 24 bytes)
    POSITION_OFFSET = 312
    VELOCITY_OFFSET = 336
    ACCELERATION_OFFSET = 360
    WIND_OFFSET = 384

    # Airport info
    NEAREST_AIRPORT_ELEVATION_OFFSET = 408
    NEAREST_AIRPORT_LATITUDE_OFFSET = 416
    NEAREST_AIRPORT_LONGITUDE_OFFSET = 424

    # Strings at end (fixed size buffers)
    AIRCRAFT_NAME_OFFSET = 432      # 64 bytes
    NEAREST_AIRPORT_ID_OFFSET = 496  # 8 bytes
    NEAREST_AIRPORT_NAME_OFFSET = 504  # 64 bytes

    # Total size
    TOTAL_SIZE = 1024


class SharedMemoryClient:
    """Shared memory client for direct access to flight data.

    This provides the lowest latency access to flight data (~0.01ms)
    by reading directly from Windows shared memory.

    Note: This is Windows-only. On other platforms, use AeroflyClient
    for TCP-based access.

    Example:
        >>> from aerofly_reader import SharedMemoryClient
        >>>
        >>> with SharedMemoryClient() as client:
        ...     data = client.read()
        ...     print(f"Alt: {data.position.altitude_ft:.0f} ft")

    Args:
        mapping_name: Name of the shared memory mapping (default: "AeroflyReaderData")
    """

    DEFAULT_MAPPING_NAME = "AeroflyReaderData"

    def __init__(self, mapping_name: str = DEFAULT_MAPPING_NAME):
        self.mapping_name = mapping_name
        self._mmap = None
        self._connected = False
        self._layout = MemoryLayout()

    @property
    def is_connected(self) -> bool:
        """Check if shared memory is open."""
        return self._connected and self._mmap is not None

    def connect(self) -> None:
        """Open the shared memory mapping.

        Raises:
            SharedMemoryError: If shared memory cannot be opened
        """
        if self._connected:
            return

        if sys.platform != 'win32':
            raise SharedMemoryError(
                "Shared memory access is only available on Windows. "
                "Use AeroflyClient for TCP-based access on other platforms."
            )

        try:
            import mmap

            # On Windows, we can open existing named shared memory
            self._mmap = mmap.mmap(
                -1,  # No file handle (named mapping)
                self._layout.TOTAL_SIZE,
                self.mapping_name,
                access=mmap.ACCESS_READ
            )
            self._connected = True
            logger.info(f"Opened shared memory: {self.mapping_name}")

        except Exception as e:
            self._cleanup()
            raise SharedMemoryError(
                f"Cannot open shared memory '{self.mapping_name}': {e}. "
                "Make sure Aerofly FS4 is running with AeroflyReader.dll loaded."
            )

    def disconnect(self) -> None:
        """Close the shared memory mapping."""
        if self._mmap:
            logger.info("Closing shared memory...")
            self._cleanup()
            logger.info("Shared memory closed")

    def _cleanup(self) -> None:
        """Clean up resources."""
        if self._mmap:
            try:
                self._mmap.close()
            except Exception:
                pass
        self._mmap = None
        self._connected = False

    def _read_double(self, offset: int) -> float:
        """Read a double (8 bytes) from shared memory."""
        self._mmap.seek(offset)
        return struct.unpack('d', self._mmap.read(8))[0]

    def _read_uint64(self, offset: int) -> int:
        """Read a uint64 (8 bytes) from shared memory."""
        self._mmap.seek(offset)
        return struct.unpack('Q', self._mmap.read(8))[0]

    def _read_uint32(self, offset: int) -> int:
        """Read a uint32 (4 bytes) from shared memory."""
        self._mmap.seek(offset)
        return struct.unpack('I', self._mmap.read(4))[0]

    def _read_string(self, offset: int, max_length: int) -> str:
        """Read a null-terminated string from shared memory."""
        self._mmap.seek(offset)
        data = self._mmap.read(max_length)
        # Find null terminator
        null_pos = data.find(b'\x00')
        if null_pos >= 0:
            data = data[:null_pos]
        return data.decode('utf-8', errors='replace')

    def _read_vector3d(self, offset: int) -> Vector3D:
        """Read a 3D vector (3 doubles) from shared memory."""
        return Vector3D(
            x=self._read_double(offset),
            y=self._read_double(offset + 8),
            z=self._read_double(offset + 16)
        )

    def read(self) -> FlightData:
        """Read current flight data from shared memory.

        Returns:
            FlightData with the latest flight information

        Raises:
            NotConnectedError: If shared memory is not open
            SharedMemoryError: If read fails
        """
        if not self.is_connected:
            raise NotConnectedError("Shared memory not open. Call connect() first.")

        try:
            layout = self._layout

            # Header
            timestamp = self._read_uint64(layout.TIMESTAMP_OFFSET)
            data_valid = self._read_uint32(layout.DATA_VALID_OFFSET) > 0
            update_counter = self._read_uint32(layout.UPDATE_COUNTER_OFFSET)

            # Position
            position = Position(
                latitude_rad=self._read_double(layout.LATITUDE_OFFSET),
                longitude_rad=self._read_double(layout.LONGITUDE_OFFSET),
                altitude_ft=self._read_double(layout.ALTITUDE_OFFSET),
                height_agl_ft=self._read_double(layout.HEIGHT_OFFSET),
            )

            # Speeds
            speeds = Speeds(
                indicated_airspeed_ms=self._read_double(layout.INDICATED_AIRSPEED_OFFSET),
                ground_speed_ms=self._read_double(layout.GROUND_SPEED_OFFSET),
                vertical_speed_ms=self._read_double(layout.VERTICAL_SPEED_OFFSET),
                mach_number=self._read_double(layout.MACH_NUMBER_OFFSET),
                angle_of_attack_rad=self._read_double(layout.ANGLE_OF_ATTACK_OFFSET),
            )

            # Orientation
            orientation = Orientation(
                pitch_rad=self._read_double(layout.PITCH_OFFSET),
                bank_rad=self._read_double(layout.BANK_OFFSET),
                true_heading_rad=self._read_double(layout.TRUE_HEADING_OFFSET),
                magnetic_heading_rad=self._read_double(layout.MAGNETIC_HEADING_OFFSET),
            )

            # State
            on_ground = self._read_double(layout.ON_GROUND_OFFSET) > 0.5
            gear = self._read_double(layout.GEAR_OFFSET)
            flaps = self._read_double(layout.FLAPS_OFFSET)
            throttle = self._read_double(layout.THROTTLE_OFFSET)
            parking_brake = self._read_double(layout.PARKING_BRAKE_OFFSET) > 0.5

            # Engines
            engines = Engines(
                engine1=EngineState(
                    running=self._read_double(layout.ENGINE_RUNNING_1_OFFSET) > 0.5,
                    throttle=self._read_double(layout.ENGINE_THROTTLE_1_OFFSET),
                ),
                engine2=EngineState(
                    running=self._read_double(layout.ENGINE_RUNNING_2_OFFSET) > 0.5,
                    throttle=self._read_double(layout.ENGINE_THROTTLE_2_OFFSET),
                ),
            )

            # Navigation
            navigation = Navigation(
                nav1_frequency=self._read_double(layout.NAV1_FREQUENCY_OFFSET),
                nav2_frequency=self._read_double(layout.NAV2_FREQUENCY_OFFSET),
                com1_frequency=self._read_double(layout.COM1_FREQUENCY_OFFSET),
                com2_frequency=self._read_double(layout.COM2_FREQUENCY_OFFSET),
                selected_course_1_rad=self._read_double(layout.SELECTED_COURSE_1_OFFSET),
                selected_course_2_rad=self._read_double(layout.SELECTED_COURSE_2_OFFSET),
            )

            # Autopilot
            autopilot = Autopilot(
                master=self._read_double(layout.AUTOPILOT_MASTER_OFFSET) > 0.5,
                heading_rad=self._read_double(layout.AUTOPILOT_HEADING_OFFSET),
                altitude_ft=self._read_double(layout.AUTOPILOT_ALTITUDE_OFFSET),
                vertical_speed_fpm=self._read_double(layout.AUTOPILOT_VS_OFFSET),
            )

            # V-Speeds
            vspeeds = VSpeeds(
                vs0_ms=self._read_double(layout.VS0_OFFSET),
                vs1_ms=self._read_double(layout.VS1_OFFSET),
                vfe_ms=self._read_double(layout.VFE_OFFSET),
                vno_ms=self._read_double(layout.VNO_OFFSET),
                vne_ms=self._read_double(layout.VNE_OFFSET),
            )

            # Physics vectors
            world_position = self._read_vector3d(layout.POSITION_OFFSET)
            velocity = self._read_vector3d(layout.VELOCITY_OFFSET)
            acceleration = self._read_vector3d(layout.ACCELERATION_OFFSET)
            wind = self._read_vector3d(layout.WIND_OFFSET)

            # Aircraft info
            aircraft = AircraftInfo(
                name=self._read_string(layout.AIRCRAFT_NAME_OFFSET, 64),
                nearest_airport_id=self._read_string(layout.NEAREST_AIRPORT_ID_OFFSET, 8),
                nearest_airport_name=self._read_string(layout.NEAREST_AIRPORT_NAME_OFFSET, 64),
                nearest_airport_elevation_ft=self._read_double(layout.NEAREST_AIRPORT_ELEVATION_OFFSET),
                nearest_airport_latitude_rad=self._read_double(layout.NEAREST_AIRPORT_LATITUDE_OFFSET),
                nearest_airport_longitude_rad=self._read_double(layout.NEAREST_AIRPORT_LONGITUDE_OFFSET),
            )

            return FlightData(
                timestamp=timestamp,
                update_counter=update_counter,
                data_valid=data_valid,
                update_hz=0.0,  # Not available in shared memory
                schema='aerofly-reader-telemetry',
                version='1.1.0',
                position=position,
                speeds=speeds,
                orientation=orientation,
                on_ground=on_ground,
                gear=gear,
                flaps=flaps,
                throttle=throttle,
                parking_brake=parking_brake,
                engines=engines,
                autopilot=autopilot,
                navigation=navigation,
                vspeeds=vspeeds,
                world_position=world_position,
                velocity=velocity,
                acceleration=acceleration,
                wind=wind,
                aircraft=aircraft,
            )

        except Exception as e:
            raise SharedMemoryError(f"Failed to read shared memory: {e}")

    def is_valid(self) -> bool:
        """Check if current data is valid.

        Returns:
            True if data_valid flag is set
        """
        if not self.is_connected:
            return False
        try:
            return self._read_uint32(self._layout.DATA_VALID_OFFSET) > 0
        except Exception:
            return False

    def __enter__(self) -> 'SharedMemoryClient':
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.disconnect()

    def __repr__(self) -> str:
        status = "connected" if self.is_connected else "disconnected"
        return f"SharedMemoryClient({self.mapping_name}, {status})"
