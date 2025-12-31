#!/usr/bin/env python3
"""
Aerofly Reader SDK - Flight Logger Example

Records flight data to CSV using the SDK's typed models.
Much simpler than the non-SDK version!
"""

import csv
import sys
import time
from datetime import datetime
from pathlib import Path

from aerofly_reader import AeroflyClient, FlightData


# CSV columns - using SDK's automatic conversions
CSV_COLUMNS = [
    'timestamp',
    'elapsed_seconds',
    'latitude',
    'longitude',
    'altitude_ft',
    'height_agl_ft',
    'ias_kts',
    'gs_kts',
    'vs_fpm',
    'heading',
    'pitch',
    'bank',
    'on_ground',
    'gear',
    'flaps',
    'throttle',
    'aircraft',
    'nearest_airport',
]


def flight_to_row(flight: FlightData, start_time: float) -> dict:
    """Convert FlightData to CSV row using SDK properties."""
    return {
        'timestamp': datetime.now().isoformat(),
        'elapsed_seconds': round(time.time() - start_time, 2),
        # Position - SDK auto-converts radians to degrees
        'latitude': round(flight.position.latitude, 6),
        'longitude': round(flight.position.longitude, 6),
        'altitude_ft': round(flight.position.altitude_ft, 1),
        'height_agl_ft': round(flight.position.height_agl_ft, 1),
        # Speeds - SDK auto-converts m/s to knots/fpm
        'ias_kts': round(flight.speeds.indicated_airspeed, 1),
        'gs_kts': round(flight.speeds.ground_speed, 1),
        'vs_fpm': round(flight.speeds.vertical_speed, 0),
        # Orientation - SDK auto-converts radians to degrees
        'heading': round(flight.orientation.heading, 1),
        'pitch': round(flight.orientation.pitch, 2),
        'bank': round(flight.orientation.bank, 2),
        # State - SDK provides boolean properties
        'on_ground': 1 if flight.on_ground else 0,
        'gear': round(flight.gear, 2),
        'flaps': round(flight.flaps, 2),
        'throttle': round(flight.throttle, 2),
        # Info
        'aircraft': flight.aircraft_name,
        'nearest_airport': flight.aircraft.nearest_airport_id,
    }


def main():
    print("\nAerofly Reader SDK - Flight Logger")
    print("=" * 50)

    # Determine filename
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"flight_log_{timestamp}.csv"

    filepath = Path(filename)
    print(f"Log file: {filepath.absolute()}\n")

    start_time = time.time()
    record_count = 0
    log_interval = 1.0  # Record every second

    # Open CSV and connect to Aerofly
    with open(filepath, 'w', newline='', encoding='utf-8') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=CSV_COLUMNS)
        writer.writeheader()

        print("Recording... (Ctrl+C to stop)\n")

        # Use SDK client with context manager
        with AeroflyClient() as client:
            last_log_time = 0

            try:
                for flight in client.stream():
                    current_time = time.time()

                    # Record at interval
                    if current_time - last_log_time >= log_interval:
                        row = flight_to_row(flight, start_time)
                        writer.writerow(row)
                        csvfile.flush()

                        record_count += 1
                        last_log_time = current_time

                        # Show progress
                        elapsed = row['elapsed_seconds']
                        alt = row['altitude_ft']
                        spd = row['ias_kts']
                        apt = row['nearest_airport']
                        print(f"\r[{elapsed:>7.1f}s] Alt: {alt:>6.0f} ft | "
                              f"IAS: {spd:>5.0f} kts | Near: {apt} | "
                              f"Records: {record_count}", end="")

            except KeyboardInterrupt:
                print("\n\nStopping...")

    # Summary
    duration = time.time() - start_time
    print("\n" + "=" * 50)
    print(f"Recording complete!")
    print(f"  File: {filepath.absolute()}")
    print(f"  Records: {record_count}")
    print(f"  Duration: {duration:.1f} seconds")
    print("=" * 50)


if __name__ == "__main__":
    main()
