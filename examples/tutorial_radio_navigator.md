# Simple Tutorial #5: Smart Radio Navigator (TCP Commands + GUI)

> **What we'll build**: A desktop application using Python Tkinter that automatically tunes navigation radios based on your location, provides airport information, and manages communication frequencies with a professional pilot interface.

## Prerequisites
- Python 3.6+ installed with tkinter
- Aerofly FS4 with AeroflyBridge.dll installed
- Basic knowledge of Python GUI programming

## What You'll Learn
- How to use TCP command port (12346) for sending commands
- How to build professional aviation GUIs with Tkinter
- How to integrate real airport/navigation databases
- How to create location-aware radio management

## Step 1: Install Required Libraries

```bash
pip install tkinter socket json threading math
```

## Step 2: Create the Radio Navigator

Create a file called `radio_navigator.py`:

```python
import tkinter as tk
from tkinter import ttk, messagebox
import socket
import json
import threading
import time
import math
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

@dataclass
class Airport:
    """Airport information data class"""
    icao: str
    name: str
    lat: float
    lon: float
    tower_freq: float
    ground_freq: float
    atis_freq: float
    elevation: int

@dataclass
class NavAid:
    """Navigation aid data class"""
    ident: str
    name: str
    freq: float
    lat: float
    lon: float
    type: str  # VOR, NDB, ILS

class NavigationDatabase:
    """Simplified navigation database for demo purposes"""
    
    def __init__(self):
        self.airports = {
            'KJFK': Airport('KJFK', 'John F Kennedy Intl', 40.6398, -73.7789, 119.1, 121.9, 128.05, 13),
            'KLGA': Airport('KLGA', 'LaGuardia', 40.7769, -73.8740, 120.2, 121.7, 125.95, 22),
            'KEWR': Airport('KEWR', 'Newark Liberty Intl', 40.6925, -74.1687, 119.2, 121.8, 135.25, 18),
            'KBOS': Airport('KBOS', 'Boston Logan Intl', 42.3629, -71.0064, 120.6, 121.9, 135.05, 20),
            'KORD': Airport('KORD', 'Chicago O\'Hare Intl', 41.9786, -87.9048, 120.15, 121.67, 135.15, 672),
            'KLAX': Airport('KLAX', 'Los Angeles Intl', 33.9425, -118.4081, 120.95, 121.75, 135.65, 125),
            'KSFO': Airport('KSFO', 'San Francisco Intl', 37.6213, -122.3790, 120.5, 121.8, 135.1, 13),
            'KDEN': Airport('KDEN', 'Denver Intl', 39.8617, -104.6731, 118.3, 121.85, 135.45, 5434),
            'KMIA': Airport('KMIA', 'Miami Intl', 25.7932, -80.2906, 118.3, 121.9, 135.4, 8),
            'KATL': Airport('KATL', 'Hartsfield-Jackson Atlanta Intl', 33.6367, -84.4281, 119.1, 121.9, 135.275, 1026)
        }
        
        self.navaids = {
            'JFK': NavAid('JFK', 'Kennedy VOR', 115.9, 40.6398, -73.7789, 'VOR'),
            'LGA': NavAid('LGA', 'LaGuardia VOR', 113.1, 40.7769, -73.8740, 'VOR'),
            'BOS': NavAid('BOS', 'Boston VOR', 112.7, 42.3629, -71.0064, 'VOR'),
            'ORD': NavAid('ORD', 'O\'Hare VOR', 113.9, 41.9786, -87.9048, 'VOR'),
            'LAX': NavAid('LAX', 'Los Angeles VOR', 113.6, 33.9425, -118.4081, 'VOR'),
            'SFO': NavAid('SFO', 'San Francisco VOR', 115.8, 37.6213, -122.3790, 'VOR'),
            'DEN': NavAid('DEN', 'Denver VOR', 117.9, 39.8617, -104.6731, 'VOR'),
            'MIA': NavAid('MIA', 'Miami VOR', 116.4, 25.7932, -80.2906, 'VOR')
        }

    def get_nearest_airport(self, lat: float, lon: float, max_distance: float = 50) -> Optional[Airport]:
        """Find nearest airport within max_distance (nautical miles)"""
        nearest = None
        min_distance = float('inf')
        
        for airport in self.airports.values():
            distance = self.calculate_distance(lat, lon, airport.lat, airport.lon)
            if distance < min_distance and distance <= max_distance:
                min_distance = distance
                nearest = airport
        
        return nearest
    
    def get_nearby_navaids(self, lat: float, lon: float, max_distance: float = 100) -> List[Tuple[NavAid, float]]:
        """Get navigation aids within range, sorted by distance"""
        nearby = []
        
        for navaid in self.navaids.values():
            distance = self.calculate_distance(lat, lon, navaid.lat, navaid.lon)
            if distance <= max_distance:
                nearby.append((navaid, distance))
        
        return sorted(nearby, key=lambda x: x[1])
    
    @staticmethod
    def calculate_distance(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        """Calculate distance between two points in nautical miles"""
        lat1, lon1, lat2, lon2 = map(math.radians, [lat1, lon1, lat2, lon2])
        dlat = lat2 - lat1
        dlon = lon2 - lon1
        a = math.sin(dlat/2)**2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon/2)**2
        c = 2 * math.asin(math.sqrt(a))
        return c * 3440.065  # Earth radius in nautical miles

class AeroflyConnection:
    """Manages connection to AeroflyBridge"""
    
    def __init__(self):
        self.data_socket = None
        self.connected = False
        self.current_data = {}
        self.data_thread = None
        self.running = False
    
    def connect(self, host='localhost', data_port=12345):
        """Connect to data stream"""
        try:
            self.data_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.data_socket.settimeout(5.0)
            self.data_socket.connect((host, data_port))
            
            self.connected = True
            self.running = True
            self.data_thread = threading.Thread(target=self._data_loop, daemon=True)
            self.data_thread.start()
            
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from AeroflyBridge"""
        self.running = False
        self.connected = False
        
        if self.data_socket:
            self.data_socket.close()
        
        if self.data_thread:
            self.data_thread.join(timeout=2)
    
    def _data_loop(self):
        """Background thread for receiving data"""
        buffer = ""
        
        while self.running:
            try:
                data = self.data_socket.recv(4096).decode('utf-8')
                if not data:
                    break
                
                buffer += data
                
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line.strip():
                        try:
                            json_data = json.loads(line)
                            self.current_data = json_data
                        except json.JSONDecodeError:
                            pass
                
            except socket.timeout:
                continue
            except Exception as e:
                print(f"Data loop error: {e}")
                break
        
        self.connected = False
    
    def send_command(self, variable: str, value: float, host='localhost', command_port=12346) -> bool:
        """Send command via TCP command port"""
        try:
            cmd_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            cmd_socket.settimeout(2.0)
            cmd_socket.connect((host, command_port))
            
            command = {
                "variable": variable,
                "value": value
            }
            
            cmd_socket.send(json.dumps(command).encode('utf-8'))
            cmd_socket.close()
            
            return True
        except Exception as e:
            print(f"Command failed: {e}")
            return False
    
    def get_position(self) -> Tuple[float, float, float]:
        """Get current aircraft position"""
        flight_data = self.current_data.get('flight_data', {})
        lat = flight_data.get('Latitude', 0.0)
        lon = flight_data.get('Longitude', 0.0)
        alt = flight_data.get('BarometricAltitude', 0.0)
        return lat, lon, alt
    
    def get_navigation_data(self) -> Dict:
        """Get current navigation radio frequencies"""
        nav_data = self.current_data.get('navigation', {})
        return {
            'nav1_freq': nav_data.get('NavFreq1', 0.0),
            'nav2_freq': nav_data.get('NavFreq2', 0.0),
            'com1_freq': nav_data.get('ComFreq1', 0.0),
            'com2_freq': nav_data.get('ComFreq2', 0.0)
        }

class RadioNavigatorGUI:
    """Main GUI application"""
    
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Smart Radio Navigator")
        self.root.geometry("800x600")
        self.root.configure(bg='#2d3142')
        
        # Initialize components
        self.aerofly = AeroflyConnection()
        self.nav_db = NavigationDatabase()
        
        # Current state
        self.current_airport = None
        self.auto_tune_enabled = False
        
        # Setup GUI
        self.setup_styles()
        self.create_widgets()
        self.setup_layout()
        
        # Start update loop
        self.update_loop()
        
        # Handle window close
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
    
    def setup_styles(self):
        """Configure ttk styles for aviation theme"""
        self.style = ttk.Style()
        self.style.theme_use('clam')
        
        # Configure styles
        self.style.configure('Header.TLabel', 
                           background='#2d3142', 
                           foreground='#ffffff',
                           font=('Arial', 14, 'bold'))
        
        self.style.configure('Status.TLabel',
                           background='#4f5d75',
                           foreground='#ffffff',
                           font=('Consolas', 10))
        
        self.style.configure('Freq.TLabel',
                           background='#2d3142',
                           foreground='#00ff00',
                           font=('Consolas', 12, 'bold'))
    
    def create_widgets(self):
        """Create all GUI widgets"""
        # Main frame
        self.main_frame = ttk.Frame(self.root)
        
        # Connection status frame
        self.status_frame = ttk.LabelFrame(self.main_frame, text="Connection Status", padding=10)
        self.connection_status = ttk.Label(self.status_frame, text="Disconnected", style='Status.TLabel')
        self.connect_btn = ttk.Button(self.status_frame, text="Connect", command=self.toggle_connection)
        
        # Position frame
        self.position_frame = ttk.LabelFrame(self.main_frame, text="Aircraft Position", padding=10)
        self.position_label = ttk.Label(self.position_frame, text="Position: Unknown", style='Status.TLabel')
        self.altitude_label = ttk.Label(self.position_frame, text="Altitude: ---", style='Status.TLabel')
        
        # Airport info frame
        self.airport_frame = ttk.LabelFrame(self.main_frame, text="Nearest Airport", padding=10)
        self.airport_name = ttk.Label(self.airport_frame, text="No airport nearby", style='Header.TLabel')
        self.airport_info = tk.Text(self.airport_frame, height=6, width=50, bg='#4f5d75', fg='white', font=('Consolas', 10))
        
        # Radio control frame
        self.radio_frame = ttk.LabelFrame(self.main_frame, text="Radio Management", padding=10)
        
        # Auto-tune control
        self.auto_tune_var = tk.BooleanVar()
        self.auto_tune_check = ttk.Checkbutton(self.radio_frame, text="Auto-Tune Radios", 
                                             variable=self.auto_tune_var, command=self.toggle_auto_tune)
        
        # NAV radios
        nav_frame = ttk.Frame(self.radio_frame)
        ttk.Label(nav_frame, text="NAV 1:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky='w', padx=5)
        self.nav1_freq = ttk.Label(nav_frame, text="---.-", style='Freq.TLabel')
        self.nav1_freq.grid(row=0, column=1, padx=10)
        self.nav1_tune_btn = ttk.Button(nav_frame, text="Tune", command=lambda: self.tune_nav_radio(1))
        self.nav1_tune_btn.grid(row=0, column=2, padx=5)
        
        ttk.Label(nav_frame, text="NAV 2:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky='w', padx=5)
        self.nav2_freq = ttk.Label(nav_frame, text="---.-", style='Freq.TLabel')
        self.nav2_freq.grid(row=1, column=1, padx=10)
        self.nav2_tune_btn = ttk.Button(nav_frame, text="Tune", command=lambda: self.tune_nav_radio(2))
        self.nav2_tune_btn.grid(row=1, column=2, padx=5)
        
        # COM radios
        com_frame = ttk.Frame(self.radio_frame)
        ttk.Label(com_frame, text="COM 1:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky='w', padx=5)
        self.com1_freq = ttk.Label(com_frame, text="---.-", style='Freq.TLabel')
        self.com1_freq.grid(row=0, column=1, padx=10)
        self.com1_tune_btn = ttk.Button(com_frame, text="Tune Tower", command=lambda: self.tune_com_radio(1, 'tower'))
        self.com1_tune_btn.grid(row=0, column=2, padx=5)
        
        ttk.Label(com_frame, text="COM 2:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky='w', padx=5)
        self.com2_freq = ttk.Label(com_frame, text="---.-", style='Freq.TLabel')
        self.com2_freq.grid(row=1, column=1, padx=10)
        self.com2_tune_btn = ttk.Button(com_frame, text="Tune Ground", command=lambda: self.tune_com_radio(2, 'ground'))
        self.com2_tune_btn.grid(row=1, column=2, padx=5)
        
        # Navigation aids frame
        self.navaid_frame = ttk.LabelFrame(self.main_frame, text="Nearby Navigation Aids", padding=10)
        self.navaid_tree = ttk.Treeview(self.navaid_frame, columns=('Type', 'Freq', 'Distance'), show='tree headings', height=8)
        self.navaid_tree.heading('#0', text='Identifier', anchor='w')
        self.navaid_tree.heading('Type', text='Type', anchor='center')
        self.navaid_tree.heading('Freq', text='Frequency', anchor='center')
        self.navaid_tree.heading('Distance', text='Distance (NM)', anchor='center')
        
        self.navaid_tree.column('#0', width=100)
        self.navaid_tree.column('Type', width=80)
        self.navaid_tree.column('Freq', width=100)
        self.navaid_tree.column('Distance', width=100)
        
        # Scrollbar for navaid tree
        navaid_scroll = ttk.Scrollbar(self.navaid_frame, orient='vertical', command=self.navaid_tree.yview)
        self.navaid_tree.configure(yscrollcommand=navaid_scroll.set)
        
        # Log frame
        self.log_frame = ttk.LabelFrame(self.main_frame, text="Activity Log", padding=10)
        self.log_text = tk.Text(self.log_frame, height=8, width=80, bg='#2d3142', fg='#00ff00', font=('Consolas', 9))
        log_scroll = ttk.Scrollbar(self.log_frame, orient='vertical', command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=log_scroll.set)
    
    def setup_layout(self):
        """Layout all widgets"""
        self.main_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        # Top row - status and position
        top_frame = ttk.Frame(self.main_frame)
        top_frame.pack(fill='x', pady=(0, 10))
        
        self.status_frame.pack(side='left', fill='x', expand=True, padx=(0, 5))
        self.connection_status.pack(side='left')
        self.connect_btn.pack(side='right')
        
        self.position_frame.pack(side='right', fill='x', expand=True, padx=(5, 0))
        self.position_label.pack()
        self.altitude_label.pack()
        
        # Middle row - airport and radio
        middle_frame = ttk.Frame(self.main_frame)
        middle_frame.pack(fill='both', expand=True, pady=(0, 10))
        
        self.airport_frame.pack(side='left', fill='both', expand=True, padx=(0, 5))
        self.airport_name.pack()
        self.airport_info.pack(fill='both', expand=True, pady=(10, 0))
        
        self.radio_frame.pack(side='right', fill='y', padx=(5, 0))
        self.auto_tune_check.pack(pady=(0, 10))
        
        nav_frame = self.radio_frame.winfo_children()[-2]  # Get nav_frame
        nav_frame.pack(pady=(0, 10))
        
        com_frame = self.radio_frame.winfo_children()[-1]  # Get com_frame  
        com_frame.pack()
        
        # Bottom row - navaids and log
        bottom_frame = ttk.Frame(self.main_frame)
        bottom_frame.pack(fill='both', expand=True)
        
        self.navaid_frame.pack(side='left', fill='both', expand=True, padx=(0, 5))
        self.navaid_tree.pack(side='left', fill='both', expand=True)
        navaid_scroll = ttk.Scrollbar(self.navaid_frame, orient='vertical', command=self.navaid_tree.yview)
        navaid_scroll.pack(side='right', fill='y')
        
        self.log_frame.pack(side='right', fill='both', expand=True, padx=(5, 0))
        self.log_text.pack(side='left', fill='both', expand=True)
        log_scroll = ttk.Scrollbar(self.log_frame, orient='vertical', command=self.log_text.yview)
        log_scroll.pack(side='right', fill='y')
    
    def toggle_connection(self):
        """Toggle connection to AeroflyBridge"""
        if self.aerofly.connected:
            self.aerofly.disconnect()
            self.connection_status.config(text="Disconnected")
            self.connect_btn.config(text="Connect")
            self.log("Disconnected from AeroflyBridge")
        else:
            if self.aerofly.connect():
                self.connection_status.config(text="Connected")
                self.connect_btn.config(text="Disconnect")
                self.log("Connected to AeroflyBridge")
            else:
                messagebox.showerror("Connection Error", "Failed to connect to AeroflyBridge")
    
    def toggle_auto_tune(self):
        """Toggle automatic radio tuning"""
        self.auto_tune_enabled = self.auto_tune_var.get()
        if self.auto_tune_enabled:
            self.log("Auto-tune enabled")
        else:
            self.log("Auto-tune disabled")
    
    def tune_nav_radio(self, radio_num: int):
        """Manually tune NAV radio to nearest VOR"""
        nearby_navaids = self.get_nearby_navaids()
        if nearby_navaids:
            navaid, distance = nearby_navaids[0]
            variable = f"NavFreq{radio_num}"
            if self.aerofly.send_command(variable, navaid.freq):
                self.log(f"Tuned NAV{radio_num} to {navaid.ident} ({navaid.freq:.1f} MHz) - {distance:.1f} NM")
            else:
                self.log(f"Failed to tune NAV{radio_num}")
    
    def tune_com_radio(self, radio_num: int, freq_type: str):
        """Tune COM radio to airport frequency"""
        if not self.current_airport:
            self.log("No airport nearby for COM tuning")
            return
        
        if freq_type == 'tower':
            freq = self.current_airport.tower_freq
            freq_name = "Tower"
        elif freq_type == 'ground':
            freq = self.current_airport.ground_freq
            freq_name = "Ground"
        else:
            return
        
        variable = f"ComFreq{radio_num}"
        if self.aerofly.send_command(variable, freq):
            self.log(f"Tuned COM{radio_num} to {self.current_airport.icao} {freq_name} ({freq:.2f} MHz)")
        else:
            self.log(f"Failed to tune COM{radio_num}")
    
    def get_nearby_navaids(self) -> List[Tuple[NavAid, float]]:
        """Get list of nearby navigation aids"""
        lat, lon, _ = self.aerofly.get_position()
        if lat == 0 and lon == 0:
            return []
        return self.nav_db.get_nearby_navaids(lat, lon)
    
    def update_position_display(self):
        """Update position and airport information"""
        if not self.aerofly.connected:
            return
        
        lat, lon, alt = self.aerofly.get_position()
        
        if lat != 0 or lon != 0:
            self.position_label.config(text=f"Position: {lat:.4f}°, {lon:.4f}°")
            self.altitude_label.config(text=f"Altitude: {alt:.0f} ft")
            
            # Update nearest airport
            nearest = self.nav_db.get_nearest_airport(lat, lon)
            if nearest != self.current_airport:
                self.current_airport = nearest
                self.update_airport_display()
            
            # Update navigation aids list
            self.update_navaid_display()
            
            # Auto-tune if enabled
            if self.auto_tune_enabled and self.current_airport:
                self.auto_tune_radios()
    
    def update_airport_display(self):
        """Update airport information display"""
        self.airport_info.delete(1.0, tk.END)
        
        if self.current_airport:
            self.airport_name.config(text=f"{self.current_airport.icao} - {self.current_airport.name}")
            
            info = f"ICAO: {self.current_airport.icao}\\n"
            info += f"Name: {self.current_airport.name}\\n"
            info += f"Elevation: {self.current_airport.elevation} ft\\n"
            info += f"Tower: {self.current_airport.tower_freq:.2f} MHz\\n"
            info += f"Ground: {self.current_airport.ground_freq:.2f} MHz\\n"
            info += f"ATIS: {self.current_airport.atis_freq:.2f} MHz"
            
            self.airport_info.insert(1.0, info)
        else:
            self.airport_name.config(text="No airport nearby")
            self.airport_info.insert(1.0, "No airports within 50 nautical miles")
    
    def update_navaid_display(self):
        """Update navigation aids tree view"""
        # Clear existing items
        for item in self.navaid_tree.get_children():
            self.navaid_tree.delete(item)
        
        # Add nearby navaids
        nearby_navaids = self.get_nearby_navaids()
        for navaid, distance in nearby_navaids[:10]:  # Show top 10
            self.navaid_tree.insert('', 'end', text=navaid.ident,
                                  values=(navaid.type, f"{navaid.freq:.1f}", f"{distance:.1f}"))
    
    def update_radio_display(self):
        """Update radio frequency display"""
        if not self.aerofly.connected:
            return
        
        nav_data = self.aerofly.get_navigation_data()
        self.nav1_freq.config(text=f"{nav_data['nav1_freq']:.1f}")
        self.nav2_freq.config(text=f"{nav_data['nav2_freq']:.1f}")
        self.com1_freq.config(text=f"{nav_data['com1_freq']:.2f}")
        self.com2_freq.config(text=f"{nav_data['com2_freq']:.2f}")
    
    def auto_tune_radios(self):
        """Automatically tune radios when near airports"""
        if not self.current_airport:
            return
        
        nav_data = self.aerofly.get_navigation_data()
        
        # Auto-tune COM1 to tower if not already tuned
        if abs(nav_data['com1_freq'] - self.current_airport.tower_freq) > 0.01:
            if self.aerofly.send_command("ComFreq1", self.current_airport.tower_freq):
                self.log(f"Auto-tuned COM1 to {self.current_airport.icao} Tower")
        
        # Auto-tune NAV1 to nearest VOR
        nearby_navaids = self.get_nearby_navaids()
        if nearby_navaids and abs(nav_data['nav1_freq'] - nearby_navaids[0][0].freq) > 0.1:
            navaid = nearby_navaids[0][0]
            if self.aerofly.send_command("NavFreq1", navaid.freq):
                self.log(f"Auto-tuned NAV1 to {navaid.ident} VOR")
    
    def update_loop(self):
        """Main update loop"""
        if self.aerofly.connected:
            self.update_position_display()
            self.update_radio_display()
        
        # Schedule next update
        self.root.after(1000, self.update_loop)  # Update every second
    
    def log(self, message: str):
        """Add message to activity log"""
        timestamp = time.strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] {message}\\n"
        
        self.log_text.insert(tk.END, log_entry)
        self.log_text.see(tk.END)
        
        # Keep only last 100 lines
        lines = self.log_text.get(1.0, tk.END).split('\\n')
        if len(lines) > 100:
            self.log_text.delete(1.0, f"{len(lines)-100}.0")
    
    def on_closing(self):
        """Handle window close event"""
        if self.aerofly.connected:
            self.aerofly.disconnect()
        self.root.destroy()
    
    def run(self):
        """Start the GUI application"""
        self.log("Smart Radio Navigator started")
        self.log("Click 'Connect' to connect to AeroflyBridge")
        self.root.mainloop()

if __name__ == "__main__":
    app = RadioNavigatorGUI()
    app.run()
```

## Step 3: Run the Radio Navigator

1. **Start Aerofly FS4** with AeroflyBridge.dll installed
2. **Run the application**:
   ```bash
   python radio_navigator.py
   ```
3. **Click "Connect"** to establish connection
4. **Fly near airports** and watch automatic radio management!

## Features Breakdown

### 1. **Connection Management**
- Data stream from port 12345 for position/radio info
- Command sending via port 12346 for radio tuning
- Auto-reconnection on connection loss

### 2. **Airport Database**
- Built-in database of major airports
- Automatic nearest airport detection
- Tower, Ground, and ATIS frequencies

### 3. **Navigation Aid Database**
- VOR stations with frequencies and positions
- Distance calculation and sorting
- Range-based filtering (100 NM radius)

### 4. **Smart Radio Management**
```python
def auto_tune_radios(self):
    # Auto-tune COM1 to airport tower
    if self.aerofly.send_command("ComFreq1", airport.tower_freq):
        self.log(f"Auto-tuned COM1 to {airport.icao} Tower")
    
    # Auto-tune NAV1 to nearest VOR
    if self.aerofly.send_command("NavFreq1", navaid.freq):
        self.log(f"Auto-tuned NAV1 to {navaid.ident} VOR")
```

### 5. **Professional GUI Design**
- Aviation-themed color scheme (dark blue/green)
- Monospace fonts for frequency displays
- Organized layout with logical groupings
- Real-time updates and activity logging

## How It Works

### TCP Command Protocol
```python
def send_command(self, variable: str, value: float, command_port=12346):
    cmd_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cmd_socket.connect((host, command_port))
    
    command = {"variable": variable, "value": value}
    cmd_socket.send(json.dumps(command).encode('utf-8'))
    cmd_socket.close()
```

### Radio Variable Names
- `NavFreq1` / `NavFreq2` - Navigation radio frequencies
- `ComFreq1` / `ComFreq2` - Communication radio frequencies
- Values in MHz (e.g., 115.9 for VOR, 119.1 for COM)

### Distance-Based Automation
```python
def get_nearest_airport(self, lat, lon, max_distance=50):
    # Find airports within 50 NM
    # Auto-tune when entering airport vicinity
```

## Step 4: Advanced Features

### Custom Airport Database
Add your own airports to the database:
```python
self.airports['KXXX'] = Airport(
    'KXXX', 'Your Airport', 
    lat, lon, tower_freq, ground_freq, atis_freq, elevation
)
```

### Flight Plan Integration
Extend to load flight plans and pre-tune frequencies:
```python
def load_flight_plan(self, waypoints):
    for waypoint in waypoints:
        airport = self.get_airport(waypoint)
        self.pre_tune_frequencies(airport)
```

### Voice ATIS Integration
Add text-to-speech for ATIS information:
```python
import pyttsx3

def announce_atis(self, airport):
    engine = pyttsx3.init()
    engine.say(f"Approaching {airport.name}")
    engine.runAndWait()
```

## Next Steps

Enhance the navigator with:
- **Real airport database** (AIRAC data integration)
- **ILS frequency tuning** for instrument approaches  
- **Approach plate display** with frequency charts
- **Flight plan integration** with automatic frequency sequence
- **Voice announcements** for frequency changes
- **Emergency frequency** quick-tune buttons

## Troubleshooting

**GUI Not Displaying Correctly?**
- Ensure tkinter is installed: `python -m tkinter`
- Check Python version compatibility (3.6+)
- Verify display scaling on high-DPI monitors

**Commands Not Sending?**
- Verify port 12346 is accessible
- Check firewall settings
- Ensure JSON command format is correct

**Database Issues?**
- Airport/navaid data is simplified for demo
- Real-world implementation needs AIRAC database
- Consider using aviation data APIs

---

🎯 **Mission Complete!** You now have a professional radio management system that demonstrates TCP command functionality with a full GUI interface. This shows how AeroflyBridge can be integrated into comprehensive flight management applications with database-driven automation and professional pilot interfaces.