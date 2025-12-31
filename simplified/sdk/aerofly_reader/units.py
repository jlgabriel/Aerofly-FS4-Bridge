"""
Aerofly Reader SDK - Unit Conversions

Provides conversion functions between simulator units and standard aviation units.
"""

import math

# Conversion constants
MS_TO_KNOTS = 1.94384
MS_TO_FPM = 196.85039  # feet per minute
MS_TO_KMH = 3.6
FEET_TO_METERS = 0.3048
METERS_TO_FEET = 3.28084
NM_TO_METERS = 1852.0
METERS_TO_NM = 1 / NM_TO_METERS


def ms_to_knots(ms: float) -> float:
    """Convert meters per second to knots.

    Args:
        ms: Speed in meters per second

    Returns:
        Speed in knots
    """
    return ms * MS_TO_KNOTS


def knots_to_ms(knots: float) -> float:
    """Convert knots to meters per second.

    Args:
        knots: Speed in knots

    Returns:
        Speed in meters per second
    """
    return knots / MS_TO_KNOTS


def ms_to_fpm(ms: float) -> float:
    """Convert meters per second to feet per minute.

    Args:
        ms: Vertical speed in meters per second

    Returns:
        Vertical speed in feet per minute
    """
    return ms * MS_TO_FPM


def fpm_to_ms(fpm: float) -> float:
    """Convert feet per minute to meters per second.

    Args:
        fpm: Vertical speed in feet per minute

    Returns:
        Vertical speed in meters per second
    """
    return fpm / MS_TO_FPM


def ms_to_kmh(ms: float) -> float:
    """Convert meters per second to kilometers per hour.

    Args:
        ms: Speed in meters per second

    Returns:
        Speed in kilometers per hour
    """
    return ms * MS_TO_KMH


def radians_to_degrees(rad: float) -> float:
    """Convert radians to degrees.

    Args:
        rad: Angle in radians

    Returns:
        Angle in degrees
    """
    return math.degrees(rad)


def degrees_to_radians(deg: float) -> float:
    """Convert degrees to radians.

    Args:
        deg: Angle in degrees

    Returns:
        Angle in radians
    """
    return math.radians(deg)


def normalize_heading(degrees: float) -> float:
    """Normalize heading to 0-360 range.

    Args:
        degrees: Heading in degrees (any value)

    Returns:
        Heading in degrees (0-360)
    """
    return degrees % 360


def normalize_longitude(degrees: float) -> float:
    """Normalize longitude to -180 to +180 range.

    The simulator returns longitude in 0-360 range.
    This converts it to standard -180/+180 format.

    Args:
        degrees: Longitude in degrees (0-360)

    Returns:
        Longitude in degrees (-180 to +180)
    """
    if degrees > 180:
        return degrees - 360
    return degrees


def feet_to_meters(feet: float) -> float:
    """Convert feet to meters.

    Args:
        feet: Distance in feet

    Returns:
        Distance in meters
    """
    return feet * FEET_TO_METERS


def meters_to_feet(meters: float) -> float:
    """Convert meters to feet.

    Args:
        meters: Distance in meters

    Returns:
        Distance in feet
    """
    return meters * METERS_TO_FEET


def format_heading(radians: float) -> str:
    """Format heading as 3-digit string with degree symbol.

    Args:
        radians: Heading in radians

    Returns:
        Formatted heading (e.g., "045°", "270°")
    """
    degrees = normalize_heading(radians_to_degrees(radians))
    return f"{degrees:03.0f}°"


def format_altitude(feet: float) -> str:
    """Format altitude with thousands separator.

    Args:
        feet: Altitude in feet

    Returns:
        Formatted altitude (e.g., "10,500 ft")
    """
    return f"{feet:,.0f} ft"


def format_speed_kts(ms: float) -> str:
    """Format speed in knots.

    Args:
        ms: Speed in meters per second

    Returns:
        Formatted speed (e.g., "250 kts")
    """
    return f"{ms_to_knots(ms):.0f} kts"


def format_vertical_speed(ms: float) -> str:
    """Format vertical speed in feet per minute with sign.

    Args:
        ms: Vertical speed in meters per second

    Returns:
        Formatted vertical speed (e.g., "+500 fpm", "-1200 fpm")
    """
    fpm = ms_to_fpm(ms)
    sign = "+" if fpm > 0 else ""
    return f"{sign}{fpm:.0f} fpm"
