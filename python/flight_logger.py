import socket
import json
import csv
import datetime
import threading
import time
import os

class FlightLogger:
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
        """Connect to AeroflyBridge TCP server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.tcp_host, self.tcp_port))
            print(f"✅ Connected to AeroflyBridge at {self.tcp_host}:{self.tcp_port}")
            return True
        except Exception as e:
            print(f"❌ Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from TCP server"""
        self.running = False
        if self.socket:
            self.socket.close()
        if self.csv_file:
            self.csv_file.close()
        print("🔌 Disconnected")

    def setup_csv_logging(self):
        """Create CSV file for this flight session"""
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"flight_log_{timestamp}.csv"
        
        # Create logs directory if it doesn't exist
        os.makedirs("flight_logs", exist_ok=True)
        filepath = os.path.join("flight_logs", filename)
        
        self.csv_file = open(filepath, 'w', newline='')
        
        # Define columns to log (derived from canonical variables)
        fieldnames = [
            'timestamp', 'elapsed_time', 'latitude_deg', 'longitude_deg', 'altitude_ft',
            'indicated_airspeed_kt', 'ground_speed_kt', 'vertical_speed_fpm',
            'heading_deg', 'pitch_deg', 'bank_deg',
            'com1_freq_mhz', 'nav1_freq_mhz'
        ]
        
        self.csv_writer = csv.DictWriter(self.csv_file, fieldnames=fieldnames)
        self.csv_writer.writeheader()
        
        print(f"📊 Logging to: {filepath}")
        return filepath

    def parse_flight_data(self, json_data):
        """Extract relevant data from JSON and update flight statistics"""
        try:
            v = json_data.get('variables', {})
            M_TO_FT = 3.280839895
            MPS_TO_KT = 1.943844492
            RAD_TO_DEG = 180 / 3.141592653589793
            MPS_TO_FPM = 196.8503937
            
            # Current position and basic data (convert to friendly units)
            current_data = {
                'timestamp': datetime.datetime.now().isoformat(),
                'elapsed_time': self.get_elapsed_time(),
                'latitude_deg': v.get('Aircraft.Latitude', 0.0),
                'longitude_deg': v.get('Aircraft.Longitude', 0.0),
                'altitude_ft': v.get('Aircraft.Altitude', 0.0) * M_TO_FT,
                'indicated_airspeed_kt': v.get('Aircraft.IndicatedAirspeed', 0.0) * MPS_TO_KT,
                'ground_speed_kt': v.get('Aircraft.GroundSpeed', 0.0) * MPS_TO_KT,
                'vertical_speed_fpm': v.get('Aircraft.VerticalSpeed', 0.0) * MPS_TO_FPM,
                # Heading normalization:
                # Prefer MagneticHeading; fallback to TrueHeading. Detect radians vs degrees
                # and convert to compass heading (0°=North, clockwise).
                **(lambda hv: {
                    'heading_deg': (
                        (lambda hdg_deg: hdg_deg if hdg_deg >= 0 else hdg_deg + 360)(
                            (90.0 - (
                                ((hv * RAD_TO_DEG) % 360.0) if abs(hv) <= 6.5 else (hv % 360.0)
                            )) % 360.0
                        )
                    )
                })(
                    v.get('Aircraft.MagneticHeading', v.get('Aircraft.TrueHeading', 0.0))
                ),
                'pitch_deg': v.get('Aircraft.Pitch', 0.0) * RAD_TO_DEG,
                'bank_deg': v.get('Aircraft.Bank', 0.0) * RAD_TO_DEG,
                'com1_freq_mhz': v.get('Communication.COM1Frequency', 0.0),
                'nav1_freq_mhz': v.get('Navigation.NAV1Frequency', 0.0)
            }
            
            # Update flight statistics
            self.update_flight_stats(current_data)
            
            return current_data
            
        except Exception as e:
            print(f"❌ Error parsing flight data: {e}")
            return None

    def update_flight_stats(self, data):
        """Update flight session statistics"""
        altitude = data['altitude_ft']
        speed = data['ground_speed_kt']
        lat = data['latitude_deg']
        lon = data['longitude_deg']
        
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
        """Calculate distance between two lat/lon points in nautical miles"""
        import math
        
        # Convert to radians
        lat1, lon1, lat2, lon2 = map(math.radians, [lat1, lon1, lat2, lon2])
        
        # Haversine formula
        dlat = lat2 - lat1
        dlon = lon2 - lon1
        a = (math.sin(dlat/2)**2 + 
             math.cos(lat1) * math.cos(lat2) * math.sin(dlon/2)**2)
        c = 2 * math.asin(math.sqrt(a))
        
        # Earth radius in nautical miles
        r = 3440.065
        
        return c * r

    def get_elapsed_time(self):
        """Get elapsed time since flight start"""
        if self.flight_start_time:
            elapsed = datetime.datetime.now() - self.flight_start_time
            return str(elapsed).split('.')[0]  # Remove microseconds
        return "00:00:00"

    def print_flight_summary(self):
        """Print current flight statistics"""
        os.system('cls' if os.name == 'nt' else 'clear')  # Clear screen
        
        print("=" * 60)
        print("✈️  AEROFLY FLIGHT DATA LOGGER")
        print("=" * 60)
        print(f"🕐 Flight Time: {self.get_elapsed_time()}")
        print(f"📏 Distance: {self.total_distance:.1f} NM")
        print(f"📈 Max Altitude: {self.max_altitude:.0f} ft")
        print(f"🏃 Max Speed: {self.max_speed:.0f} kts")
        
        if self.current_flight_data:
            data = self.current_flight_data
            print("\n📊 CURRENT STATUS:")
            print(f"   Position: {data['latitude_deg']:.4f}, {data['longitude_deg']:.4f}")
            print(f"   Altitude: {data['altitude_ft']:.0f} ft")
            print(f"   Speed: {data['indicated_airspeed_kt']:.0f} kts IAS / {data['ground_speed_kt']:.0f} kts GS")
            print(f"   Heading: {data['heading_deg']:.0f}°")
            print(f"   V/S: {data['vertical_speed_fpm']:.0f} ft/min")
        
        print(f"\n🎯 Status: {'🛫 Airborne' if self.takeoff_detected else '🛬 On Ground'}")
        print("=" * 60)
        print("Press Ctrl+C to stop logging and generate final report")

    def start_logging(self):
        """Main logging loop"""
        if not self.connect():
            return
        
        self.flight_start_time = datetime.datetime.now()
        csv_file_path = self.setup_csv_logging()
        self.running = True
        
        print("🚀 Flight logging started!")
        print("✈️ Take off in Aerofly FS4 to begin data collection")
        
        buffer = ""
        
        try:
            while self.running:
                # Receive data from TCP socket
                data = self.socket.recv(4096).decode('utf-8')
                if not data:
                    break
                
                buffer += data
                
                # Process complete JSON messages
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line.strip():
                        try:
                            json_data = json.loads(line)
                            flight_data = self.parse_flight_data(json_data)
                            
                            if flight_data:
                                # Log to CSV
                                self.csv_writer.writerow(flight_data)
                                self.csv_file.flush()
                                
                                # Update current data for display
                                self.current_flight_data = flight_data
                                
                                # Update display every 2 seconds
                                if int(time.time()) % 2 == 0:
                                    self.print_flight_summary()
                                
                        except json.JSONDecodeError as e:
                            print(f"❌ JSON decode error: {e}")
                
        except KeyboardInterrupt:
            print("\n🛑 Logging stopped by user")
        except Exception as e:
            print(f"❌ Error during logging: {e}")
        finally:
            self.disconnect()
            self.generate_final_report(csv_file_path)

    def generate_final_report(self, csv_file_path):
        """Generate a final flight report"""
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
            print("🛫 Takeoff detected (still airborne)")
        else:
            print("🛬 Ground operations only")
        
        print("\n🎯 Flight logging session complete!")

if __name__ == "__main__":
    logger = FlightLogger()
    
    print("🛩️  Aerofly FS4 Flight Data Logger")
    print("📡 Connecting to AeroflyBridge...")
    
    try:
        logger.start_logging()
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
    
    input("\nPress Enter to exit...")