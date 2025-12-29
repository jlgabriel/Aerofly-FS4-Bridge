#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Simple Example

This script connects to the AeroflyReader DLL via TCP and displays
real-time flight data.

Usage:
    python simple_reader.py

Requirements:
    - AeroflyReader.dll installed in Aerofly FS 4
    - Aerofly FS 4 running with an active flight
"""

import socket
import json
import sys
import time
import math
from datetime import datetime


def connect_to_aerofly(host: str = 'localhost', port: int = 12345) -> socket.socket:
    """Connects to the AeroflyReader TCP server."""
    print(f"Connecting to {host}:{port}...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)

    try:
        sock.connect((host, port))
        print("Connected!")
        return sock
    except socket.timeout:
        print("Error: Connection timeout. Make sure Aerofly is running.")
        sys.exit(1)
    except ConnectionRefusedError:
        print("Error: Connection refused. Make sure AeroflyReader.dll is loaded.")
        sys.exit(1)


def format_heading(radians: float) -> str:
    """Converts radians to heading degrees (000-360)."""
    import math
    degrees = math.degrees(radians) % 360
    return f"{degrees:03.0f}°"


def format_speed_kts(ms: float) -> str:
    """Converts m/s to knots."""
    kts = ms * 1.94384
    return f"{kts:.0f} kts"


def format_altitude(feet: float) -> str:
    """Formats altitude in feet."""
    return f"{feet:,.0f} ft"


def format_vs(ms: float) -> str:
    """Converts vertical speed from m/s to ft/min."""
    ftmin = ms * 196.85
    sign = "+" if ftmin > 0 else ""
    return f"{sign}{ftmin:.0f} fpm"


def display_flight_data(data: dict):
    """Displays flight data in a readable format."""

    # Clear screen (optional, comment out if it doesn't work on your terminal)
    print("\033[H\033[J", end="")

    print("=" * 60)
    print("         AEROFLY FS4 READER - Flight Data")
    print("=" * 60)
    print()

    # Aircraft and basic information
    print(f"  Aircraft: {data.get('aircraft_name', 'Unknown')}")
    print(f"  Nearest airport: {data.get('nearest_airport_id', '----')} - {data.get('nearest_airport_name', 'Unknown')}")
    print()

    # Position (convert from radians to degrees, normalize longitude to -180/+180)
    lat = math.degrees(data.get('latitude', 0))
    lon = math.degrees(data.get('longitude', 0))
    if lon > 180:
        lon -= 360
    print("  POSITION")
    print(f"    Lat: {lat:+.6f}°")
    print(f"    Lon: {lon:+.6f}°")
    print()

    # Altitudes and speeds
    print("  ALTITUDE & SPEED")
    print(f"    Altitude MSL: {format_altitude(data.get('altitude', 0))}")
    print(f"    Height AGL:   {format_altitude(data.get('height', 0))}")
    print(f"    IAS:          {format_speed_kts(data.get('indicated_airspeed', 0))}")
    print(f"    GS:           {format_speed_kts(data.get('ground_speed', 0))}")
    print(f"    VS:           {format_vs(data.get('vertical_speed', 0))}")
    print()

    # Orientation
    print("  ORIENTATION")
    print(f"    Mag Heading:  {format_heading(data.get('magnetic_heading', 0))}")
    print(f"    True Heading: {format_heading(data.get('true_heading', 0))}")
    pitch_deg = math.degrees(data.get('pitch', 0))
    bank_deg = math.degrees(data.get('bank', 0))
    print(f"    Pitch:        {pitch_deg:+.1f}°")
    print(f"    Bank:         {bank_deg:+.1f}°")
    print()

    # State
    print("  STATE")
    on_ground = "ON GROUND" if data.get('on_ground', 0) > 0.5 else "IN FLIGHT"
    gear = "DOWN" if data.get('gear', 0) > 0.5 else "UP"
    flaps = f"{data.get('flaps', 0) * 100:.0f}%"
    throttle = f"{data.get('throttle', 0) * 100:.0f}%"
    print(f"    {on_ground}")
    print(f"    Gear:     {gear}")
    print(f"    Flaps:    {flaps}")
    print(f"    Throttle: {throttle}")
    print()

    # Engines
    eng1 = "ON" if data.get('engine_running_1', 0) > 0.5 else "OFF"
    eng2 = "ON" if data.get('engine_running_2', 0) > 0.5 else "OFF"
    print("  ENGINES")
    print(f"    Engine 1: {eng1} ({data.get('engine_throttle_1', 0) * 100:.0f}%)")
    print(f"    Engine 2: {eng2} ({data.get('engine_throttle_2', 0) * 100:.0f}%)")
    print()

    # Autopilot
    ap_on = "ON" if data.get('autopilot_master', 0) > 0.5 else "OFF"
    print("  AUTOPILOT")
    print(f"    Master:   {ap_on}")
    if ap_on == "ON":
        print(f"    HDG:      {format_heading(data.get('autopilot_heading', 0))}")
        print(f"    ALT:      {format_altitude(data.get('autopilot_altitude', 0))}")
    print()

    # V-Speeds
    print("  V-SPEEDS")
    print(f"    VS0: {format_speed_kts(data.get('vs0', 0))}  VS1: {format_speed_kts(data.get('vs1', 0))}")
    print(f"    VFE: {format_speed_kts(data.get('vfe', 0))}  VNO: {format_speed_kts(data.get('vno', 0))}  VNE: {format_speed_kts(data.get('vne', 0))}")
    print()

    # Footer
    print("-" * 60)
    update = data.get('update_counter', 0)
    valid = "✓" if data.get('data_valid', 0) else "✗"
    print(f"  Update #{update} | Data valid: {valid} | {datetime.now().strftime('%H:%M:%S')}")
    print("  Press Ctrl+C to exit")
    print("=" * 60)


def main():
    """Main function."""
    print()
    print("  Aerofly FS4 Reader - Simple Example")
    print("  ------------------------------------")
    print()

    sock = connect_to_aerofly()
    buffer = ""

    try:
        while True:
            # Receive data
            try:
                data = sock.recv(4096).decode('utf-8')
                if not data:
                    print("Connection closed by server.")
                    break
                buffer += data
            except socket.timeout:
                continue

            # Process complete JSON lines
            while '\n' in buffer:
                line, buffer = buffer.split('\n', 1)
                line = line.strip()

                if not line:
                    continue

                try:
                    flight_data = json.loads(line)
                    display_flight_data(flight_data)
                except json.JSONDecodeError as e:
                    print(f"Error parsing JSON: {e}")
                    continue

            # Small pause to avoid CPU saturation
            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\n\nDisconnecting...")
    finally:
        sock.close()
        print("Connection closed.")


if __name__ == "__main__":
    main()
