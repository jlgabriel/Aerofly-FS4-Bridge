# Simple Tutorial #3: Engine Monitor Dashboard (Python + Shared Memory)

> **What we'll build**: A high-performance engine monitoring dashboard that reads individual variables directly from shared memory, providing ultra-fast updates for critical engine parameters with visual gauges.

## Prerequisites
- Python 3.6+ installed
- Aerofly FS4 with AeroflyBridge.dll installed  
- Windows OS (for shared memory access)

## What You'll Learn
- How to access shared memory directly for maximum performance
- How to read individual variables by offset
- How to create real-time visual gauges with terminal graphics

## Step 1: Install Required Libraries

```bash
pip install ctypes struct time threading
```

(These are built-in Python libraries)

## Step 2: Get the Variable Offsets

First, start Aerofly FS4 with AeroflyBridge.dll. The DLL automatically creates an `AeroflyBridge_offsets.json` file in its directory. You'll need this file for variable access.

## Step 3: Create the Engine Monitor

Create a file called `engine_monitor.py`:

```python
import ctypes
import struct
import time
import threading
import json
import os
import math
from ctypes import wintypes

class SharedMemoryInterface:
    """Direct shared memory access for ultra-fast variable reading"""
    
    def __init__(self, memory_name="AeroflyBridgeData", offsets_file=None):
        self.memory_name = memory_name
        self.handle = None
        self.memory_view = None
        self.memory_size = 8192  # Adjust if needed
        self.offsets = {}
        self.variables = {}
        
        # Load variable offsets from JSON
        if offsets_file and os.path.exists(offsets_file):
            self.load_offsets(offsets_file)
        else:
            # Fallback: define key engine variables manually with their typical offsets
            self.setup_fallback_offsets()

    def load_offsets(self, offsets_file):
        """Load variable offsets from AeroflyBridge_offsets.json"""
        try:
            with open(offsets_file, 'r') as f:
                data = json.load(f)
            
            base_offset = data.get('array_base_offset', 0)
            stride = data.get('stride_bytes', 8)  # sizeof(double)
            
            # Build offset map from variable list
            for i, var_info in enumerate(data.get('variables', [])):
                var_name = var_info.get('name', '')
                logical_index = var_info.get('logical_index', i)
                offset = base_offset + (logical_index * stride)
                self.offsets[var_name] = offset
            
            print(f"✅ Loaded {len(self.offsets)} variable offsets from {offsets_file}")
            
        except Exception as e:
            print(f"❌ Failed to load offsets: {e}")
            self.setup_fallback_offsets()

    def setup_fallback_offsets(self):
        """Fallback offsets for key engine variables (estimated)"""
        print("⚠️ Using fallback offsets - may not be accurate")
        base = 64  # Estimated base offset
        stride = 8  # sizeof(double)
        
        # These are example offsets - real offsets come from the JSON file
        engine_vars = {
            'Engine1RPM': 50,
            'Engine1FuelFlow': 51, 
            'Engine1Oil': 52,
            'Engine1Temperature': 53,
            'Engine2RPM': 60,
            'Engine2FuelFlow': 61,
            'Engine2Oil': 62, 
            'Engine2Temperature': 63,
            'IndicatedAirspeed': 10,
            'BarometricAltitude': 15,
            'VerticalSpeed': 20
        }
        
        for name, index in engine_vars.items():
            self.offsets[name] = base + (index * stride)

    def connect(self):
        """Connect to shared memory"""
        try:
            # Windows API calls
            kernel32 = ctypes.windll.kernel32
            
            # Open existing shared memory
            self.handle = kernel32.OpenFileMappingW(
                0x0004,  # FILE_MAP_READ
                False,
                self.memory_name
            )
            
            if not self.handle:
                raise Exception(f"Could not open shared memory '{self.memory_name}'")
            
            # Map view of file
            self.memory_view = kernel32.MapViewOfFile(
                self.handle,
                0x0004,  # FILE_MAP_READ
                0, 0,    # Offset high, low
                self.memory_size
            )
            
            if not self.memory_view:
                raise Exception("Could not map view of shared memory")
            
            print(f"✅ Connected to shared memory: {self.memory_name}")
            return True
            
        except Exception as e:
            print(f"❌ Shared memory connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from shared memory"""
        if self.memory_view:
            ctypes.windll.kernel32.UnmapViewOfFile(self.memory_view)
        if self.handle:
            ctypes.windll.kernel32.CloseHandle(self.handle)
        print("🔌 Disconnected from shared memory")

    def read_variable(self, variable_name):
        """Read a single variable by name"""
        if not self.memory_view or variable_name not in self.offsets:
            return 0.0
        
        try:
            offset = self.offsets[variable_name]
            # Read 8 bytes (double) from memory at offset
            data_ptr = self.memory_view + offset
            value = struct.unpack('d', ctypes.string_at(data_ptr, 8))[0]
            return value
        except Exception as e:
            print(f"❌ Error reading {variable_name}: {e}")
            return 0.0

    def read_multiple_variables(self, variable_names):
        """Read multiple variables efficiently"""
        results = {}
        for name in variable_names:
            results[name] = self.read_variable(name)
        return results

class EngineMonitor:
    def __init__(self, offsets_file=None):
        self.smi = SharedMemoryInterface(offsets_file=offsets_file)
        self.running = False
        self.engine_data = {}
        self.update_thread = None
        
        # Engine warning thresholds
        self.thresholds = {
            'rpm_redline': 2700,
            'rpm_caution': 2500,
            'oil_pressure_min': 25,
            'oil_temp_max': 240,
            'fuel_flow_max': 20
        }

    def start_monitoring(self):
        """Start the engine monitoring system"""
        if not self.smi.connect():
            return False
        
        self.running = True
        self.update_thread = threading.Thread(target=self.update_loop, daemon=True)
        self.update_thread.start()
        
        print("🚀 Engine monitoring started!")
        return True

    def stop_monitoring(self):
        """Stop monitoring and disconnect"""
        self.running = False
        if self.update_thread:
            self.update_thread.join(timeout=1)
        self.smi.disconnect()

    def update_loop(self):
        """High-speed data update loop"""
        engine_variables = [
            'Engine1RPM', 'Engine1FuelFlow', 'Engine1Oil', 'Engine1Temperature',
            'Engine2RPM', 'Engine2FuelFlow', 'Engine2Oil', 'Engine2Temperature',
            'IndicatedAirspeed', 'BarometricAltitude', 'VerticalSpeed'
        ]
        
        while self.running:
            try:
                # Read all engine variables in one batch
                self.engine_data = self.smi.read_multiple_variables(engine_variables)
                time.sleep(0.05)  # 20 Hz update rate
            except Exception as e:
                print(f"❌ Update error: {e}")
                time.sleep(0.1)

    def create_gauge(self, value, min_val, max_val, width=30, warning_threshold=None, critical_threshold=None):
        """Create a text-based gauge visualization"""
        if max_val <= min_val:
            return "Invalid range"
        
        # Normalize value to 0-1 range
        normalized = max(0, min(1, (value - min_val) / (max_val - min_val)))
        filled_width = int(normalized * width)
        
        # Choose color based on thresholds
        if critical_threshold and value >= critical_threshold:
            color = '\033[91m'  # Red
            indicator = '█'
        elif warning_threshold and value >= warning_threshold:
            color = '\033[93m'  # Yellow
            indicator = '▓'
        else:
            color = '\033[92m'  # Green
            indicator = '▓'
        
        # Build gauge
        gauge = color + (indicator * filled_width) + '\033[0m'
        gauge += '░' * (width - filled_width)
        
        return gauge

    def display_dashboard(self):
        """Display the main engine dashboard"""
        while self.running:
            try:
                os.system('cls' if os.name == 'nt' else 'clear')
                
                data = self.engine_data
                
                print("═" * 80)
                print("🔧 AEROFLY ENGINE MONITOR DASHBOARD")
                print("═" * 80)
                
                # Flight data header
                ias = data.get('IndicatedAirspeed', 0)
                alt = data.get('BarometricAltitude', 0)
                vs = data.get('VerticalSpeed', 0)
                
                print(f"✈️ Flight Data: {ias:.0f} kts IAS | {alt:.0f} ft | {vs:+.0f} ft/min")
                print("─" * 80)
                
                # Engine 1
                rpm1 = data.get('Engine1RPM', 0)
                ff1 = data.get('Engine1FuelFlow', 0)
                oil1 = data.get('Engine1Oil', 0)
                temp1 = data.get('Engine1Temperature', 0)
                
                print("🔧 ENGINE 1:")
                print(f"   RPM:  {rpm1:7.0f} {self.create_gauge(rpm1, 0, 3000, 25, 2500, 2700)} [{rpm1:4.0f}/3000]")
                print(f"   FUEL: {ff1:7.1f} {self.create_gauge(ff1, 0, 25, 25, 18, 22)} [{ff1:4.1f}/25.0] gph")
                print(f"   OIL:  {oil1:7.1f} {self.create_gauge(oil1, 0, 100, 25, None, 25)} [{oil1:4.1f}/100] psi")
                print(f"   TEMP: {temp1:7.0f} {self.create_gauge(temp1, 0, 300, 25, 200, 240)} [{temp1:3.0f}/300]°F")
                
                # Engine 2 (if twin engine)
                rpm2 = data.get('Engine2RPM', 0)
                if rpm2 > 0:  # Only show if Engine 2 is active
                    ff2 = data.get('Engine2FuelFlow', 0)
                    oil2 = data.get('Engine2Oil', 0)
                    temp2 = data.get('Engine2Temperature', 0)
                    
                    print("\n🔧 ENGINE 2:")
                    print(f"   RPM:  {rpm2:7.0f} {self.create_gauge(rpm2, 0, 3000, 25, 2500, 2700)} [{rpm2:4.0f}/3000]")
                    print(f"   FUEL: {ff2:7.1f} {self.create_gauge(ff2, 0, 25, 25, 18, 22)} [{ff2:4.1f}/25.0] gph")
                    print(f"   OIL:  {oil2:7.1f} {self.create_gauge(oil2, 0, 100, 25, None, 25)} [{oil2:4.1f}/100] psi")
                    print(f"   TEMP: {temp2:7.0f} {self.create_gauge(temp2, 0, 300, 25, 200, 240)} [{temp2:3.0f}/300]°F")
                
                # Warning checks
                warnings = []
                if rpm1 > self.thresholds['rpm_redline']:
                    warnings.append("🚨 ENGINE 1 RPM REDLINE!")
                if rpm1 > self.thresholds['rpm_caution']:
                    warnings.append("⚠️ Engine 1 RPM Caution")
                if oil1 < self.thresholds['oil_pressure_min']:
                    warnings.append("🚨 ENGINE 1 LOW OIL PRESSURE!")
                if temp1 > self.thresholds['oil_temp_max']:
                    warnings.append("🚨 ENGINE 1 OVERTEMP!")
                
                if rpm2 > 0:  # Twin engine warnings
                    if rpm2 > self.thresholds['rpm_redline']:
                        warnings.append("🚨 ENGINE 2 RPM REDLINE!")
                    if oil2 < self.thresholds['oil_pressure_min']:
                        warnings.append("🚨 ENGINE 2 LOW OIL PRESSURE!")
                
                # Display warnings
                if warnings:
                    print("\n" + "🚨" * 20)
                    for warning in warnings:
                        print(f"   {warning}")
                    print("🚨" * 20)
                else:
                    print("\n✅ All engine parameters normal")
                
                print("\n" + "═" * 80)
                print("📊 Updates every 50ms | Press Ctrl+C to exit")
                
                time.sleep(0.5)  # Update display every 500ms
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"❌ Display error: {e}")
                time.sleep(1)

def main():
    print("🛩️ Aerofly FS4 Engine Monitor")
    print("🔧 Initializing shared memory interface...")
    
    # Look for offsets file in common locations
    offsets_file = None
    possible_paths = [
        "AeroflyBridge_offsets.json",
        "../AeroflyBridge_offsets.json",
        "../../AeroflyBridge_offsets.json"
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            offsets_file = path
            break
    
    if not offsets_file:
        print("⚠️ AeroflyBridge_offsets.json not found - using fallback offsets")
    
    monitor = EngineMonitor(offsets_file=offsets_file)
    
    try:
        if monitor.start_monitoring():
            print("⏳ Waiting for engine data...")
            time.sleep(2)  # Allow time for initial data collection
            monitor.display_dashboard()
        else:
            print("❌ Failed to start monitoring")
    except KeyboardInterrupt:
        print("\n🛑 Shutting down...")
    finally:
        monitor.stop_monitoring()
    
    print("👋 Engine monitor stopped")

if __name__ == "__main__":
    main()
```

## Step 4: Run the Engine Monitor

1. **Start Aerofly FS4** with AeroflyBridge.dll installed
2. **Start your aircraft** and run up the engines
3. **Run the monitor**:
   ```bash
   python engine_monitor.py
   ```

## Step 5: Understanding the Display

### Visual Gauges
- **Green bars**: Normal operation
- **Yellow bars**: Caution range  
- **Red bars**: Warning/critical range

### Engine Parameters
- **RPM**: Engine rotations per minute
- **FUEL**: Fuel flow in gallons per hour
- **OIL**: Oil pressure in PSI
- **TEMP**: Engine temperature in °F

### Real-time Warnings
- RPM redline/caution alerts
- Low oil pressure warnings
- Engine overtemperature alerts

## How It Works

### Shared Memory Access
```python
self.handle = kernel32.OpenFileMappingW(0x0004, False, self.memory_name)
self.memory_view = kernel32.MapViewOfFile(self.handle, 0x0004, 0, 0, self.memory_size)
```
Direct access to the Windows shared memory created by AeroflyBridge.dll.

### Variable Reading
```python
def read_variable(self, variable_name):
    offset = self.offsets[variable_name]
    data_ptr = self.memory_view + offset
    value = struct.unpack('d', ctypes.string_at(data_ptr, 8))[0]
    return value
```
Reads individual `double` values directly from memory at calculated offsets.

### High-Frequency Updates
- Memory reads at 20 Hz (50ms intervals)
- Display updates at 2 Hz (500ms intervals)
- Zero network latency compared to TCP/WebSocket

## Performance Benefits

### Speed Comparison
- **Shared Memory**: ~0.01ms per variable read
- **TCP JSON**: ~10-50ms per complete dataset
- **WebSocket**: ~15-100ms per complete dataset

### Use Cases for Shared Memory
- Real-time engine monitoring
- Flight control systems
- High-frequency data logging
- Performance-critical applications

## Next Steps

Extend the monitor to:
- **Add flight control surface monitoring**
- **Create custom gauge layouts**
- **Log high-frequency engine data**
- **Add audio alerts for critical parameters**
- **Export data for analysis tools**

## Troubleshooting

**Shared Memory Access Failed?**
- Ensure Aerofly FS4 is running with AeroflyBridge.dll
- Run Python script as Administrator if needed
- Check that shared memory name matches (default: "AeroflyBridgeData")

**Wrong Variable Values?**
- Verify `AeroflyBridge_offsets.json` file exists and is current
- Check that offsets match the current DLL version
- Enable fallback mode if JSON is unavailable

**Display Issues?**
- Ensure terminal supports ANSI color codes
- Adjust gauge width if terminal is narrow
- Use Command Prompt or PowerShell on Windows

---

🎯 **Mission Complete!** You now have an ultra-high-performance engine monitor that reads variables directly from shared memory. This approach provides the fastest possible access to flight data and can be extended to build professional-grade flight monitoring systems.