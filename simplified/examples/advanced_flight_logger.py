#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Advanced Flight Logger

Enhanced flight data logger with flight statistics, takeoff/landing
detection, and distance tracking.

Features:
- CSV logging of all flight parameters
- Automatic takeoff/landing detection
- Total distance calculation
- Flight statistics (max altitude, speed, etc.)
- Real-time flight summary display
- Final flight report

Usage:
    python advanced_flight_logger.py

Output:
    Creates flight_logs/flight_log_YYYYMMDD_HHMMSS.csv

Requirements:
    - AeroflyReader.dll installed in Aerofly FS 4
    - Aerofly FS 4 running with an active flight
"""

import socket
import json
import csv
import datetime
import time
import os
import math


class FlightLogger:
    """Advanced flight data logger with statistics and event detection."""

    def __init__(self, tcp_host='localhost', tcp_port=12345):
        self.tcp_host = tcp_host
        self.tcp_port = tcp_port
        self.running = False
        self.socket = None
        self.current_flight_data = {}
        self.flight_start_time = None
        self.csv_file = None
        self.csv_writer = None

        # Flight session tracking
        self.total_distance = 0
        self.max_altitude = 0
        self.max_speed = 0
        self.takeoff_detected = False
        self.landing_detected = False

        # Previous position for distance calculation
        self.prev_lat = None
        self.prev_lon = None

    def connect(self):
        """Connect to AeroflyReader TCP server."""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.tcp_host, self.tcp_port))
            self.socket.settimeout(1.0)
            print(f"✅ Connected to AeroflyReader at {self.tcp_host}:{self.tcp_port}")
            return True
        except Exception as e:
            print(f"❌ Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from TCP server."""
        self.running = False
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        if self.csv_file:
            try:
                self.csv_file.close()
            except Exception:
                pass
        print("🔌 Disconnected")

    def setup_csv_logging(self):
        """Create CSV file for this flight session."""
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"flight_log_{timestamp}.csv"

        # Create logs directory if it doesn't exist
        os.makedirs("flight_logs", exist_ok=True)
        filepath = os.path.join("flight_logs", filename)

        self.csv_file = open(filepath, 'w', newline='', encoding='utf-8')

        # Define columns to log
        fieldnames = [
            'timestamp',
            'elapsed_time',
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
            'gear_position',
            'flaps_position',
            'throttle_position',
            'parking_brake',
            'engine1_running',
            'engine2_running',
            'engine1_throttle',
            'engine2_throttle',
            'nav1_freq_mhz',
            'nav2_freq_mhz',
            'com1_freq_mhz',
            'com2_freq_mhz',
            'autopilot_master',
            'autopilot_heading_deg',
            'autopilot_altitude_ft',
            'aircraft_name',
            'nearest_airport'
        ]

        self.csv_writer = csv.DictWriter(self.csv_file, fieldnames=fieldnames)
        self.csv_writer.writeheader()

        print(f"📊 Logging to: {filepath}")
        return filepath

    def parse_flight_data(self, json_data):
        """Extract relevant data from JSON and update flight statistics."""
        try:
            # Conversion constants
            MPS_TO_KT = 1.943844492
            MPS_TO_FPM = 196.8503937
            RAD_TO_DEG = 180.0 / math.pi

            # Extract data (convert from radians where needed)
            # Longitude needs normalization from 0-360 to -180/+180
            lon_deg = RAD_TO_DEG * json_data.get('longitude', 0.0)
            if lon_deg > 180:
                lon_deg -= 360

            current_data = {
                'timestamp': datetime.datetime.now().isoformat(),
                'elapsed_time': self.get_elapsed_time(),
                'latitude': RAD_TO_DEG * json_data.get('latitude', 0.0),
                'longitude': lon_deg,
                'altitude_ft': json_data.get('altitude', 0.0),
                'height_agl_ft': json_data.get('height', 0.0),
                'indicated_airspeed_kts': json_data.get('indicated_airspeed', 0.0) * MPS_TO_KT,
                'ground_speed_kts': json_data.get('ground_speed', 0.0) * MPS_TO_KT,
                'vertical_speed_fpm': json_data.get('vertical_speed', 0.0) * MPS_TO_FPM,
                'magnetic_heading_deg': (RAD_TO_DEG * json_data.get('magnetic_heading', 0.0)) % 360,
                'true_heading_deg': (RAD_TO_DEG * json_data.get('true_heading', 0.0)) % 360,
                'pitch_deg': RAD_TO_DEG * json_data.get('pitch', 0.0),
                'bank_deg': RAD_TO_DEG * json_data.get('bank', 0.0),
                'mach_number': json_data.get('mach_number', 0.0),
                'angle_of_attack_deg': RAD_TO_DEG * json_data.get('angle_of_attack', 0.0),
                'on_ground': 1 if json_data.get('on_ground', 0) > 0.5 else 0,
                'gear_position': json_data.get('gear', 0.0),
                'flaps_position': json_data.get('flaps', 0.0),
                'throttle_position': json_data.get('throttle', 0.0),
                'parking_brake': 1 if json_data.get('parking_brake', 0) > 0.5 else 0,
                'engine1_running': 1 if json_data.get('engine_running_1', 0) > 0.5 else 0,
                'engine2_running': 1 if json_data.get('engine_running_2', 0) > 0.5 else 0,
                'engine1_throttle': json_data.get('engine_throttle_1', 0.0),
                'engine2_throttle': json_data.get('engine_throttle_2', 0.0),
                'nav1_freq_mhz': json_data.get('nav1_frequency', 0.0),
                'nav2_freq_mhz': json_data.get('nav2_frequency', 0.0),
                'com1_freq_mhz': json_data.get('com1_frequency', 0.0),
                'com2_freq_mhz': json_data.get('com2_frequency', 0.0),
                'autopilot_master': 1 if json_data.get('autopilot_master', 0) > 0.5 else 0,
                'autopilot_heading_deg': (RAD_TO_DEG * json_data.get('autopilot_heading', 0.0)) % 360,
                'autopilot_altitude_ft': json_data.get('autopilot_altitude', 0.0),
                'aircraft_name': json_data.get('aircraft_name', 'Unknown'),
                'nearest_airport': json_data.get('nearest_airport_id', '----')
            }

            # Update flight statistics
            self.update_flight_stats(current_data)

            return current_data

        except Exception as e:
            print(f"❌ Error parsing flight data: {e}")
            return None

    def update_flight_stats(self, data):
        """Update flight session statistics."""
        altitude = data['altitude_ft']
        speed = data['ground_speed_kts']
        lat = data['latitude']
        lon = data['longitude']

        # Track maximum values
        if altitude > self.max_altitude:
            self.max_altitude = altitude
        if speed > self.max_speed:
            self.max_speed = speed

        # Calculate distance traveled
        if self.prev_lat is not None and self.prev_lon is not None:
            distance = self.calculate_distance(
                self.prev_lat, self.prev_lon, lat, lon
            )
            self.total_distance += distance

        self.prev_lat = lat
        self.prev_lon = lon

        # Detect takeoff (speed > 40 knots and altitude increasing)
        if not self.takeoff_detected and speed > 40 and altitude > 100:
            self.takeoff_detected = True
            print("🛫 Takeoff detected!")

        # Detect landing (was airborne, now slow and low)
        if (self.takeoff_detected and not self.landing_detected and
                speed < 30 and altitude < 200):
            self.landing_detected = True
            print("🛬 Landing detected!")

    def calculate_distance(self, lat1, lon1, lat2, lon2):
        """Calculate distance between two lat/lon points in nautical miles."""
        # Convert to radians
        lat1_rad = math.radians(lat1)
        lon1_rad = math.radians(lon1)
        lat2_rad = math.radians(lat2)
        lon2_rad = math.radians(lon2)

        # Haversine formula
        dlat = lat2_rad - lat1_rad
        dlon = lon2_rad - lon1_rad
        a = (math.sin(dlat / 2) ** 2 +
             math.cos(lat1_rad) * math.cos(lat2_rad) * math.sin(dlon / 2) ** 2)
        c = 2 * math.asin(math.sqrt(a))

        # Earth radius in nautical miles
        r = 3440.065

        return c * r

    def get_elapsed_time(self):
        """Get elapsed time since flight start."""
        if self.flight_start_time:
            elapsed = datetime.datetime.now() - self.flight_start_time
            return str(elapsed).split('.')[0]  # Remove microseconds
        return "00:00:00"

    def print_flight_summary(self):
        """Print current flight statistics."""
        os.system('cls' if os.name == 'nt' else 'clear')  # Clear screen

        print("=" * 60)
        print("✈️  AEROFLY FS4 ADVANCED FLIGHT LOGGER")
        print("=" * 60)
        print(f"🕐 Flight Time: {self.get_elapsed_time()}")
        print(f"📏 Distance: {self.total_distance:.1f} NM")
        print(f"📈 Max Altitude: {self.max_altitude:.0f} ft")
        print(f"🏃 Max Speed: {self.max_speed:.0f} kts")

        if self.current_flight_data:
            data = self.current_flight_data
            print("\n📊 CURRENT STATUS:")
            print(f"   Aircraft: {data['aircraft_name']}")
            print(f"   Position: {data['latitude']:.6f}, {data['longitude']:.6f}")
            print(f"   Altitude: {data['altitude_ft']:.0f} ft MSL / {data['height_agl_ft']:.0f} ft AGL")
            print(f"   Speed: {data['indicated_airspeed_kts']:.0f} kts IAS / {data['ground_speed_kts']:.0f} kts GS")
            print(f"   Heading: {data['magnetic_heading_deg']:.0f}° (mag) / {data['true_heading_deg']:.0f}° (true)")
            print(f"   V/S: {data['vertical_speed_fpm']:+.0f} ft/min")
            print(f"   Pitch/Bank: {data['pitch_deg']:+.1f}° / {data['bank_deg']:+.1f}°")
            print(f"   Nearest: {data['nearest_airport']}")

        print(f"\n🎯 Status: {'🛫 Airborne' if self.takeoff_detected else '🛬 On Ground'}")
        print("=" * 60)
        print("Press Ctrl+C to stop logging and generate final report")

    def start_logging(self):
        """Main logging loop."""
        if not self.connect():
            return

        self.flight_start_time = datetime.datetime.now()
        csv_file_path = self.setup_csv_logging()
        self.running = True

        print("🚀 Flight logging started!")
        print("✈️ Take off in Aerofly FS4 to begin data collection")

        buffer = ""
        last_display_update = 0

        try:
            while self.running:
                # Receive data from TCP socket
                try:
                    data = self.socket.recv(16384).decode('utf-8')
                except socket.timeout:
                    continue

                if not data:
                    print("Connection closed by server")
                    break

                buffer += data

                # Process complete JSON messages
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if not line.strip():
                        continue

                    try:
                        json_data = json.loads(line)
                        flight_data = self.parse_flight_data(json_data)

                        if flight_data:
                            # Log to CSV
                            self.csv_writer.writerow(flight_data)
                            self.csv_file.flush()

                            # Update current data for display
                            self.current_flight_data = flight_data

                            # Update display every 1 second
                            current_time = time.time()
                            if current_time - last_display_update >= 1.0:
                                self.print_flight_summary()
                                last_display_update = current_time

                    except json.JSONDecodeError as e:
                        print(f"❌ JSON decode error: {e}")

        except KeyboardInterrupt:
            print("\n🛑 Logging stopped by user")
        except Exception as e:
            print(f"❌ Error during logging: {e}")
            import traceback
            traceback.print_exc()
        finally:
            self.disconnect()
            self.generate_final_report(csv_file_path)

    def generate_final_report(self, csv_file_path):
        """Generate a final flight report."""
        print("\n" + "=" * 60)
        print("📋 FINAL FLIGHT REPORT")
        print("=" * 60)
        print(f"🕐 Total Flight Time: {self.get_elapsed_time()}")
        print(f"📏 Total Distance: {self.total_distance:.1f} nautical miles")
        print(f"📈 Maximum Altitude: {self.max_altitude:.0f} feet")
        print(f"🏃 Maximum Speed: {self.max_speed:.0f} knots")
        print(f"📊 CSV Log File: {csv_file_path}")

        if self.takeoff_detected and self.landing_detected:
            print("✅ Complete flight detected (takeoff and landing)")
        elif self.takeoff_detected:
            print("🛫 Takeoff detected (still airborne or incomplete)")
        else:
            print("🛬 Ground operations only")

        print("\n🎯 Flight logging session complete!")


def main():
    """Main entry point."""
    logger = FlightLogger()

    print()
    print("=" * 60)
    print("🛩️  Aerofly FS4 Advanced Flight Logger")
    print("=" * 60)
    print()
    print("📡 Connecting to AeroflyReader...")
    print()

    try:
        logger.start_logging()
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        import traceback
        traceback.print_exc()

    input("\nPress Enter to exit...")


if __name__ == "__main__":
    main()
