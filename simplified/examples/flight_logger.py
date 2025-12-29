#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Flight Logger

This script records flight data to a CSV file for
later analysis. Ideal for:
- Reviewing training flights
- Performance analysis
- Flight path reconstruction

Usage:
    python flight_logger.py [filename.csv]

The CSV file includes timestamp and all main variables.
"""

import socket
import json
import csv
import sys
import time
import math
from datetime import datetime
from pathlib import Path


# CSV Columns
CSV_COLUMNS = [
    'timestamp',
    'elapsed_seconds',
    'latitude',
    'longitude',
    'altitude_ft',
    'height_agl_ft',
    'indicated_airspeed_kts',
    'ground_speed_kts',
    'vertical_speed_fpm',
    'magnetic_heading_deg',
    'true_heading_deg',
    'pitch_deg',
    'bank_deg',
    'mach_number',
    'angle_of_attack_deg',
    'on_ground',
    'gear',
    'flaps_pct',
    'throttle_pct',
    'parking_brake',
    'engine1_running',
    'engine2_running',
    'autopilot_master',
    'aircraft_name',
    'nearest_airport'
]


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
        print("Error: Connection timeout.")
        sys.exit(1)
    except ConnectionRefusedError:
        print("Error: Connection refused.")
        sys.exit(1)


def ms_to_kts(ms: float) -> float:
    """Converts m/s to knots."""
    return ms * 1.94384


def ms_to_fpm(ms: float) -> float:
    """Converts m/s to ft/min."""
    return ms * 196.85


def rad_to_deg(rad: float) -> float:
    """Converts radians to degrees."""
    return math.degrees(rad)


def normalize_longitude(lon_deg: float) -> float:
    """Normalizes longitude from 0-360 to -180/+180."""
    if lon_deg > 180:
        return lon_deg - 360
    return lon_deg


def process_data(data: dict, start_time: float) -> dict:
    """Processes raw data and converts to standard units."""
    elapsed = time.time() - start_time

    return {
        'timestamp': datetime.now().isoformat(),
        'elapsed_seconds': round(elapsed, 2),
        'latitude': round(rad_to_deg(data.get('latitude', 0)), 6),
        'longitude': round(normalize_longitude(rad_to_deg(data.get('longitude', 0))), 6),
        'altitude_ft': round(data.get('altitude', 0), 1),
        'height_agl_ft': round(data.get('height', 0), 1),
        'indicated_airspeed_kts': round(ms_to_kts(data.get('indicated_airspeed', 0)), 1),
        'ground_speed_kts': round(ms_to_kts(data.get('ground_speed', 0)), 1),
        'vertical_speed_fpm': round(ms_to_fpm(data.get('vertical_speed', 0)), 0),
        'magnetic_heading_deg': round(rad_to_deg(data.get('magnetic_heading', 0)) % 360, 1),
        'true_heading_deg': round(rad_to_deg(data.get('true_heading', 0)) % 360, 1),
        'pitch_deg': round(rad_to_deg(data.get('pitch', 0)), 2),
        'bank_deg': round(rad_to_deg(data.get('bank', 0)), 2),
        'mach_number': round(data.get('mach_number', 0), 3),
        'angle_of_attack_deg': round(rad_to_deg(data.get('angle_of_attack', 0)), 2),
        'on_ground': 1 if data.get('on_ground', 0) > 0.5 else 0,
        'gear': round(data.get('gear', 0), 2),
        'flaps_pct': round(data.get('flaps', 0) * 100, 0),
        'throttle_pct': round(data.get('throttle', 0) * 100, 0),
        'parking_brake': 1 if data.get('parking_brake', 0) > 0.5 else 0,
        'engine1_running': 1 if data.get('engine_running_1', 0) > 0.5 else 0,
        'engine2_running': 1 if data.get('engine_running_2', 0) > 0.5 else 0,
        'autopilot_master': 1 if data.get('autopilot_master', 0) > 0.5 else 0,
        'aircraft_name': data.get('aircraft_name', 'Unknown'),
        'nearest_airport': data.get('nearest_airport_id', '----')
    }


def main():
    """Main function."""
    print()
    print("  Aerofly FS4 Reader - Flight Logger")
    print("  -----------------------------------")
    print()

    # Determine filename
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"flight_log_{timestamp}.csv"

    filepath = Path(filename)
    print(f"  Log file: {filepath.absolute()}")
    print()

    sock = connect_to_aerofly()
    buffer = ""
    start_time = time.time()
    record_count = 0
    last_log_time = 0
    log_interval = 1.0  # Record every 1 second

    # Create CSV file
    with open(filepath, 'w', newline='', encoding='utf-8') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=CSV_COLUMNS)
        writer.writeheader()

        print("  Recording data... (Ctrl+C to stop)")
        print()

        try:
            while True:
                # Receive data
                try:
                    data = sock.recv(4096).decode('utf-8')
                    if not data:
                        print("Connection closed.")
                        break
                    buffer += data
                except socket.timeout:
                    continue

                # Process JSON lines
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()

                    if not line:
                        continue

                    try:
                        flight_data = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    # Record according to interval
                    current_time = time.time()
                    if current_time - last_log_time >= log_interval:
                        row = process_data(flight_data, start_time)
                        writer.writerow(row)
                        csvfile.flush()

                        record_count += 1
                        last_log_time = current_time

                        # Show progress
                        elapsed = row['elapsed_seconds']
                        alt = row['altitude_ft']
                        spd = row['indicated_airspeed_kts']
                        apt = row['nearest_airport']
                        print(f"\r  [{elapsed:>8.1f}s] Alt: {alt:>7.0f} ft | IAS: {spd:>5.0f} kts | Near: {apt} | Records: {record_count}", end="")

                time.sleep(0.02)

        except KeyboardInterrupt:
            print("\n")
            print("  Stopping recording...")

    # Final summary
    print()
    print("  " + "=" * 50)
    print(f"  Recording completed!")
    print(f"  File: {filepath.absolute()}")
    print(f"  Records: {record_count}")
    print(f"  Duration: {time.time() - start_time:.1f} seconds")
    print("  " + "=" * 50)

    sock.close()


if __name__ == "__main__":
    main()
