"""
Aerofly Reader SDK - Data Models

Typed dataclasses for flight data with automatic unit conversions.
"""

from dataclasses import dataclass
from typing import Optional, Dict, Any
import math

from .units import (
    ms_to_knots,
    ms_to_fpm,
    radians_to_degrees,
    normalize_heading,
    normalize_longitude,
)


@dataclass(frozen=True)
class Vector3D:
    """3D vector for position, velocity, acceleration, etc."""
    x: float
    y: float
    z: float

    @classmethod
    def from_dict(cls, data: Optional[Dict[str, float]]) -> 'Vector3D':
        """Create from dictionary with x, y, z keys."""
        if data is None:
            return cls(0.0, 0.0, 0.0)
        return cls(
            x=data.get('x', 0.0),
            y=data.get('y', 0.0),
            z=data.get('z', 0.0)
        )

    @property
    def magnitude(self) -> float:
        """Calculate vector magnitude."""
        return math.sqrt(self.x**2 + self.y**2 + self.z**2)


@dataclass(frozen=True)
class Position:
    """GPS position with automatic conversion to degrees.

    Raw values are in radians (latitude, longitude) and feet (altitude, height).
    Properties provide converted values in degrees with proper normalization.
    """
    latitude_rad: float
    longitude_rad: float
    altitude_ft: float
    height_agl_ft: float

    @property
    def latitude(self) -> float:
        """Latitude in degrees (-90 to +90)."""
        return radians_to_degrees(self.latitude_rad)

    @property
    def longitude(self) -> float:
        """Longitude in degrees (-180 to +180)."""
        lon_deg = radians_to_degrees(self.longitude_rad)
        return normalize_longitude(lon_deg)

    @property
    def altitude(self) -> float:
        """Alias for altitude_ft (MSL altitude in feet)."""
        return self.altitude_ft

    @property
    def height(self) -> float:
        """Alias for height_agl_ft (AGL height in feet)."""
        return self.height_agl_ft

    def __str__(self) -> str:
        return f"({self.latitude:.6f}°, {self.longitude:.6f}°) @ {self.altitude_ft:.0f} ft"


@dataclass(frozen=True)
class Speeds:
    """Aircraft speeds with automatic conversion to knots/fpm.

    Raw values are in m/s. Properties provide converted values.
    """
    indicated_airspeed_ms: float
    ground_speed_ms: float
    vertical_speed_ms: float
    mach_number: float
    angle_of_attack_rad: float

    @property
    def indicated_airspeed(self) -> float:
        """Indicated airspeed in knots."""
        return ms_to_knots(self.indicated_airspeed_ms)

    @property
    def ias(self) -> float:
        """Alias for indicated_airspeed (knots)."""
        return self.indicated_airspeed

    @property
    def ground_speed(self) -> float:
        """Ground speed in knots."""
        return ms_to_knots(self.ground_speed_ms)

    @property
    def gs(self) -> float:
        """Alias for ground_speed (knots)."""
        return self.ground_speed

    @property
    def vertical_speed(self) -> float:
        """Vertical speed in feet per minute."""
        return ms_to_fpm(self.vertical_speed_ms)

    @property
    def vs(self) -> float:
        """Alias for vertical_speed (fpm)."""
        return self.vertical_speed

    @property
    def mach(self) -> float:
        """Alias for mach_number."""
        return self.mach_number

    @property
    def angle_of_attack(self) -> float:
        """Angle of attack in degrees."""
        return radians_to_degrees(self.angle_of_attack_rad)

    @property
    def aoa(self) -> float:
        """Alias for angle_of_attack (degrees)."""
        return self.angle_of_attack


@dataclass(frozen=True)
class Orientation:
    """Aircraft orientation with automatic conversion to degrees.

    Raw values are in radians. Properties provide degrees.
    """
    pitch_rad: float
    bank_rad: float
    true_heading_rad: float
    magnetic_heading_rad: float

    @property
    def pitch(self) -> float:
        """Pitch in degrees (positive = nose up)."""
        return radians_to_degrees(self.pitch_rad)

    @property
    def bank(self) -> float:
        """Bank/roll in degrees (positive = right bank)."""
        return radians_to_degrees(self.bank_rad)

    @property
    def roll(self) -> float:
        """Alias for bank (degrees)."""
        return self.bank

    @property
    def true_heading(self) -> float:
        """True heading in degrees (0-360)."""
        return normalize_heading(radians_to_degrees(self.true_heading_rad))

    @property
    def magnetic_heading(self) -> float:
        """Magnetic heading in degrees (0-360)."""
        return normalize_heading(radians_to_degrees(self.magnetic_heading_rad))

    @property
    def heading(self) -> float:
        """Alias for magnetic_heading (degrees, 0-360)."""
        return self.magnetic_heading


@dataclass(frozen=True)
class EngineState:
    """Engine state for a single engine."""
    running: bool
    throttle: float  # 0.0 to 1.0

    @property
    def throttle_percent(self) -> float:
        """Throttle as percentage (0-100)."""
        return self.throttle * 100


@dataclass(frozen=True)
class Engines:
    """All engines state."""
    engine1: EngineState
    engine2: EngineState

    @property
    def any_running(self) -> bool:
        """True if any engine is running."""
        return self.engine1.running or self.engine2.running

    @property
    def all_running(self) -> bool:
        """True if all engines are running."""
        return self.engine1.running and self.engine2.running


@dataclass(frozen=True)
class Autopilot:
    """Autopilot state (read-only from DLL)."""
    master: bool
    heading_rad: float
    altitude_ft: float
    vertical_speed_fpm: float

    @property
    def heading(self) -> float:
        """Selected heading in degrees (0-360)."""
        return normalize_heading(radians_to_degrees(self.heading_rad))

    @property
    def altitude(self) -> float:
        """Selected altitude in feet."""
        return self.altitude_ft

    @property
    def vertical_speed(self) -> float:
        """Selected vertical speed in fpm."""
        return self.vertical_speed_fpm


@dataclass(frozen=True)
class Navigation:
    """Radio navigation frequencies."""
    nav1_frequency: float  # MHz
    nav2_frequency: float  # MHz
    com1_frequency: float  # MHz
    com2_frequency: float  # MHz
    selected_course_1_rad: float
    selected_course_2_rad: float

    @property
    def selected_course_1(self) -> float:
        """Selected course 1 in degrees."""
        return normalize_heading(radians_to_degrees(self.selected_course_1_rad))

    @property
    def selected_course_2(self) -> float:
        """Selected course 2 in degrees."""
        return normalize_heading(radians_to_degrees(self.selected_course_2_rad))


@dataclass(frozen=True)
class VSpeeds:
    """V-speeds for the current aircraft (in m/s from DLL)."""
    vs0_ms: float  # Stall speed (flaps extended)
    vs1_ms: float  # Stall speed (clean)
    vfe_ms: float  # Max flaps extended speed
    vno_ms: float  # Max structural cruise speed
    vne_ms: float  # Never exceed speed

    @property
    def vs0(self) -> float:
        """VS0 in knots (stall speed, flaps extended)."""
        return ms_to_knots(self.vs0_ms)

    @property
    def vs1(self) -> float:
        """VS1 in knots (stall speed, clean)."""
        return ms_to_knots(self.vs1_ms)

    @property
    def vfe(self) -> float:
        """VFE in knots (max flaps extended)."""
        return ms_to_knots(self.vfe_ms)

    @property
    def vno(self) -> float:
        """VNO in knots (max structural cruise)."""
        return ms_to_knots(self.vno_ms)

    @property
    def vne(self) -> float:
        """VNE in knots (never exceed)."""
        return ms_to_knots(self.vne_ms)


@dataclass(frozen=True)
class AircraftInfo:
    """Aircraft and airport information."""
    name: str
    nearest_airport_id: str
    nearest_airport_name: str
    nearest_airport_elevation_ft: float
    nearest_airport_latitude_rad: float
    nearest_airport_longitude_rad: float

    @property
    def nearest_airport_latitude(self) -> float:
        """Nearest airport latitude in degrees."""
        return radians_to_degrees(self.nearest_airport_latitude_rad)

    @property
    def nearest_airport_longitude(self) -> float:
        """Nearest airport longitude in degrees (-180 to +180)."""
        lon_deg = radians_to_degrees(self.nearest_airport_longitude_rad)
        return normalize_longitude(lon_deg)


@dataclass(frozen=True)
class FlightData:
    """Complete flight data from AeroflyReader DLL.

    This is the main data structure returned by the client.
    All sub-components provide automatic unit conversions via properties.

    Example:
        >>> data = client.read()
        >>> print(f"Altitude: {data.position.altitude_ft:.0f} ft")
        >>> print(f"Speed: {data.speeds.indicated_airspeed:.0f} kts")
        >>> print(f"Heading: {data.orientation.heading:.0f}°")
    """
    # Metadata
    timestamp: int
    update_counter: int
    data_valid: bool
    update_hz: float
    schema: str
    version: str

    # Core flight data
    position: Position
    speeds: Speeds
    orientation: Orientation

    # Aircraft state
    on_ground: bool
    gear: float  # 0.0 = up, 1.0 = down
    flaps: float  # 0.0 to 1.0
    throttle: float  # 0.0 to 1.0
    parking_brake: bool

    # Systems
    engines: Engines
    autopilot: Autopilot
    navigation: Navigation
    vspeeds: VSpeeds

    # Physics vectors
    world_position: Vector3D
    velocity: Vector3D
    acceleration: Vector3D
    wind: Vector3D

    # Info
    aircraft: AircraftInfo

    @property
    def gear_percent(self) -> float:
        """Gear position as percentage (0 = up, 100 = down)."""
        return self.gear * 100

    @property
    def flaps_percent(self) -> float:
        """Flaps position as percentage."""
        return self.flaps * 100

    @property
    def throttle_percent(self) -> float:
        """Throttle position as percentage."""
        return self.throttle * 100

    @property
    def is_flying(self) -> bool:
        """True if aircraft is airborne."""
        return not self.on_ground

    @property
    def aircraft_name(self) -> str:
        """Alias for aircraft.name."""
        return self.aircraft.name

    @classmethod
    def from_json(cls, data: Dict[str, Any]) -> 'FlightData':
        """Create FlightData from JSON dictionary received from DLL.

        Args:
            data: Dictionary parsed from JSON

        Returns:
            FlightData instance with all fields populated
        """
        # Position
        position = Position(
            latitude_rad=data.get('latitude', 0.0),
            longitude_rad=data.get('longitude', 0.0),
            altitude_ft=data.get('altitude', 0.0),
            height_agl_ft=data.get('height', 0.0),
        )

        # Speeds
        speeds = Speeds(
            indicated_airspeed_ms=data.get('indicated_airspeed', 0.0),
            ground_speed_ms=data.get('ground_speed', 0.0),
            vertical_speed_ms=data.get('vertical_speed', 0.0),
            mach_number=data.get('mach_number', 0.0),
            angle_of_attack_rad=data.get('angle_of_attack', 0.0),
        )

        # Orientation
        orientation = Orientation(
            pitch_rad=data.get('pitch', 0.0),
            bank_rad=data.get('bank', 0.0),
            true_heading_rad=data.get('true_heading', 0.0),
            magnetic_heading_rad=data.get('magnetic_heading', 0.0),
        )

        # Engines
        engines = Engines(
            engine1=EngineState(
                running=data.get('engine_running_1', 0) > 0.5,
                throttle=data.get('engine_throttle_1', 0.0),
            ),
            engine2=EngineState(
                running=data.get('engine_running_2', 0) > 0.5,
                throttle=data.get('engine_throttle_2', 0.0),
            ),
        )

        # Autopilot
        autopilot = Autopilot(
            master=data.get('autopilot_master', 0) > 0.5,
            heading_rad=data.get('autopilot_heading', 0.0),
            altitude_ft=data.get('autopilot_altitude', 0.0),
            vertical_speed_fpm=data.get('autopilot_vertical_speed', 0.0),
        )

        # Navigation
        navigation = Navigation(
            nav1_frequency=data.get('nav1_frequency', 0.0),
            nav2_frequency=data.get('nav2_frequency', 0.0),
            com1_frequency=data.get('com1_frequency', 0.0),
            com2_frequency=data.get('com2_frequency', 0.0),
            selected_course_1_rad=data.get('selected_course_1', 0.0),
            selected_course_2_rad=data.get('selected_course_2', 0.0),
        )

        # V-Speeds
        vspeeds = VSpeeds(
            vs0_ms=data.get('vs0', 0.0),
            vs1_ms=data.get('vs1', 0.0),
            vfe_ms=data.get('vfe', 0.0),
            vno_ms=data.get('vno', 0.0),
            vne_ms=data.get('vne', 0.0),
        )

        # Physics vectors
        world_position = Vector3D.from_dict(data.get('position'))
        velocity = Vector3D.from_dict(data.get('velocity'))
        acceleration = Vector3D.from_dict(data.get('acceleration'))
        wind = Vector3D.from_dict(data.get('wind'))

        # Aircraft info
        aircraft = AircraftInfo(
            name=data.get('aircraft_name', 'Unknown'),
            nearest_airport_id=data.get('nearest_airport_id', '----'),
            nearest_airport_name=data.get('nearest_airport_name', 'Unknown'),
            nearest_airport_elevation_ft=data.get('nearest_airport_elevation', 0.0),
            nearest_airport_latitude_rad=data.get('nearest_airport_latitude', 0.0),
            nearest_airport_longitude_rad=data.get('nearest_airport_longitude', 0.0),
        )

        return cls(
            # Metadata
            timestamp=data.get('timestamp', 0),
            update_counter=data.get('update_counter', 0),
            data_valid=data.get('data_valid', 0) > 0,
            update_hz=data.get('update_hz', 0.0),
            schema=data.get('schema', 'aerofly-reader-telemetry'),
            version=data.get('version', '0.0.0'),
            # Core
            position=position,
            speeds=speeds,
            orientation=orientation,
            # State
            on_ground=data.get('on_ground', 0) > 0.5,
            gear=data.get('gear', 0.0),
            flaps=data.get('flaps', 0.0),
            throttle=data.get('throttle', 0.0),
            parking_brake=data.get('parking_brake', 0) > 0.5,
            # Systems
            engines=engines,
            autopilot=autopilot,
            navigation=navigation,
            vspeeds=vspeeds,
            # Physics
            world_position=world_position,
            velocity=velocity,
            acceleration=acceleration,
            wind=wind,
            # Info
            aircraft=aircraft,
        )

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary with converted units.

        Returns:
            Dictionary with all values in standard aviation units
        """
        return {
            'timestamp': self.timestamp,
            'update_counter': self.update_counter,
            'data_valid': self.data_valid,
            'update_hz': self.update_hz,
            'latitude': self.position.latitude,
            'longitude': self.position.longitude,
            'altitude_ft': self.position.altitude_ft,
            'height_agl_ft': self.position.height_agl_ft,
            'indicated_airspeed_kts': self.speeds.indicated_airspeed,
            'ground_speed_kts': self.speeds.ground_speed,
            'vertical_speed_fpm': self.speeds.vertical_speed,
            'mach': self.speeds.mach_number,
            'pitch_deg': self.orientation.pitch,
            'bank_deg': self.orientation.bank,
            'heading_deg': self.orientation.heading,
            'on_ground': self.on_ground,
            'gear': self.gear,
            'flaps': self.flaps,
            'throttle': self.throttle,
            'aircraft_name': self.aircraft.name,
            'nearest_airport': self.aircraft.nearest_airport_id,
        }
