# Simple Tutorial #2: Flight Data Logger (Python + JSON)

> **What we'll build**: A Python application that connects to the TCP interface, logs complete flight data to CSV files, and generates a real-time flight summary report.

## Prerequisites
- Python 3.6+ installed
- Aerofly FS4 with AeroflyBridge.dll installed
- Basic knowledge of Python

## What You'll Learn
- How to connect to the TCP JSON interface
- How to process complete flight data
- How to create CSV logs and generate reports

## Step 1: Install Required Libraries

```bash
pip install socket json csv datetime threading
```

(These are mostly built-in Python libraries)

## Step 2: Create the Flight Logger

Create a file called `flight_logger.py`:

```python
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
        
        # Define columns to log
        fieldnames = [
            'timestamp', 'elapsed_time', 'latitude', 'longitude', 'altitude_ft',
            'indicated_airspeed', 'ground_speed', 'vertical_speed',
            'heading', 'pitch', 'bank', 'engine_rpm', 'fuel_flow',
            'nav_freq1', 'com_freq1'
        ]
        
        self.csv_writer = csv.DictWriter(self.csv_file, fieldnames=fieldnames)
        self.csv_writer.writeheader()
        
        print(f"📊 Logging to: {filepath}")
        return filepath

    def parse_flight_data(self, json_data):
        """Extract relevant data from JSON and update flight statistics"""
        try:
            flight_data = json_data.get('flight_data', {})
            navigation = json_data.get('navigation', {})
            
            # Current position and basic data
            current_data = {
                'timestamp': datetime.datetime.now().isoformat(),
                'elapsed_time': self.get_elapsed_time(),
                'latitude': flight_data.get('Latitude', 0),
                'longitude': flight_data.get('Longitude', 0),
                'altitude_ft': flight_data.get('BarometricAltitude', 0),
                'indicated_airspeed': flight_data.get('IndicatedAirspeed', 0),
                'ground_speed': flight_data.get('GroundSpeed', 0),
                'vertical_speed': flight_data.get('VerticalSpeed', 0),
                'heading': flight_data.get('TrueHeading', 0),
                'pitch': flight_data.get('Pitch', 0),
                'bank': flight_data.get('Bank', 0),
                'engine_rpm': flight_data.get('Engine1RPM', 0),
                'fuel_flow': flight_data.get('Engine1FuelFlow', 0),
                'nav_freq1': navigation.get('NavFreq1', 0),
                'com_freq1': navigation.get('ComFreq1', 0)
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
        speed = data['ground_speed']
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
            print(f"   Position: {data['latitude']:.4f}, {data['longitude']:.4f}")
            print(f"   Altitude: {data['altitude_ft']:.0f} ft")
            print(f"   Speed: {data['indicated_airspeed']:.0f} kts IAS / {data['ground_speed']:.0f} kts GS")
            print(f"   Heading: {data['heading']:.0f}°")
            print(f"   V/S: {data['vertical_speed']:.0f} ft/min")
            print(f"   Engine: {data['engine_rpm']:.0f} RPM")
        
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
```

## Step 3: Run the Logger

1. **Start Aerofly FS4** with AeroflyBridge.dll installed
2. **Run the Python script**:
   ```bash
   python flight_logger.py
   ```
3. **Start flying** and watch the real-time data!

## Step 4: Understanding the Output

### Real-time Display
The script shows:
- Current flight time and statistics
- Live position, altitude, speed
- Maximum values achieved
- Flight phase detection (ground/airborne)

### CSV Log Files
Each session creates a timestamped CSV file in the `flight_logs/` folder with columns:
- `timestamp`, `elapsed_time` 
- `latitude`, `longitude`, `altitude_ft`
- `indicated_airspeed`, `ground_speed`, `vertical_speed`
- `heading`, `pitch`, `bank`
- `engine_rpm`, `fuel_flow`
- `nav_freq1`, `com_freq1`

## How It Works

### TCP Connection
```python
self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
self.socket.connect((self.tcp_host, self.tcp_port))
```
Connects to the AeroflyBridge TCP server on port 12345 for real-time JSON data.

### JSON Processing
```python
flight_data = json_data.get('flight_data', {})
altitude = flight_data.get('BarometricAltitude', 0)
```
Extracts specific values from the complete JSON structure provided by the bridge.

### Flight Detection Logic
- **Takeoff**: Speed > 40 knots + altitude > 100 feet
- **Landing**: Was airborne, now speed < 30 knots + altitude < 200 feet

### Distance Calculation
Uses the Haversine formula to calculate distance between GPS coordinates.

## Next Steps

Extend the logger to:
- **Send flight data to a web dashboard**
- **Detect flight phases** (taxi, takeoff, cruise, approach, landing)
- **Generate flight path KML files** for Google Earth
- **Alert on dangerous conditions** (stall speed, altitude warnings)
- **Upload logs to cloud storage**

## Troubleshooting

**Connection Refused?**
- Ensure Aerofly FS4 is running with AeroflyBridge.dll
- Check that TCP server is enabled (default: yes)
- Verify port 12345 is not blocked by firewall

**No Data Updating?**
- Start a flight in Aerofly FS4
- Check that aircraft systems are powered
- Verify JSON data format in terminal output

**CSV File Issues?**
- Check write permissions in the script directory
- Ensure enough disk space for logging

---

🎯 **Mission Complete!** You now have a comprehensive flight data logger that captures complete JSON data streams and generates useful flight reports. This foundation can be expanded into full flight analysis and monitoring systems.