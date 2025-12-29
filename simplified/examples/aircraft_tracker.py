#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Aircraft Tracker (Map Visualization)

Real-time aircraft tracker that visualizes Aerofly FS4 data on an
interactive map using the AeroflyReader DLL.

Features:
- Real-time position tracking on interactive map
- Multiple map styles (OpenStreetMap, Google, etc.)
- Live flight data display (altitude, speed, heading, etc.)
- Connection status indicator
- Automatic aircraft icon rotation based on heading

Requirements:
    - AeroflyReader.dll installed in Aerofly FS 4
    - tkintermapview: pip install tkintermapview
    - Pillow: pip install pillow

Usage:
    python aircraft_tracker.py
"""

import json
import socket
import threading
import time
from dataclasses import dataclass
from typing import Optional, Dict, Any, Tuple, List

import tkinter as tk
from tkinter import font as tkfont
from tkintermapview import TkinterMapView
from PIL import Image, ImageTk, ImageDraw


# Constants
TCP_HOST = "localhost"
TCP_PORT = 12345
WINDOW_SIZE = "1000x600"
MAP_SIZE: Tuple[int, int] = (800, 600)
CONTROL_FRAME_WIDTH = 200
INFO_DISPLAY_SIZE: Tuple[int, int] = (24, 9)
UPDATE_INTERVAL_MS = 100  # GUI refresh interval
RECEIVE_TIMEOUT_S = 5.0   # Connection considered lost beyond this


@dataclass
class FlightData:
    """Container for current flight data"""
    longitude: float
    latitude: float
    altitude_ft: float
    ground_speed_ms: float
    heading_rad: float
    pitch_rad: float
    bank_rad: float


class DataReceiver:
    """Receives telemetry from AeroflyReader TCP JSON stream (port 12345)."""

    def __init__(self, host: str = TCP_HOST, port: int = TCP_PORT) -> None:
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.latest_data: Optional[FlightData] = None
        self.running: bool = False
        self.receive_thread: Optional[threading.Thread] = None
        self.last_receive_time: float = 0.0

    def start_receiving(self) -> None:
        """Start the background receiver thread."""
        self.running = True
        self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.receive_thread.start()

    def _connect(self) -> None:
        """Connect or reconnect to the TCP server."""
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((self.host, self.port))
        sock.settimeout(1.0)
        self.socket = sock

    def _receive_loop(self) -> None:
        """Main receive loop running in background thread."""
        buffer = ""
        while self.running:
            try:
                if not self.socket:
                    self._connect()

                data = self.socket.recv(16384).decode("utf-8")
                if not data:
                    # Connection closed by peer; reconnect
                    time.sleep(0.5)
                    self._connect()
                    continue

                buffer += data
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    if not line.strip():
                        continue
                    self._process_line(line)

            except (socket.timeout, TimeoutError):
                continue
            except (ConnectionResetError, ConnectionAbortedError, OSError) as e:
                print(f"DataReceiver reconnecting: {e}")
                try:
                    if self.socket:
                        self.socket.close()
                except Exception:
                    pass
                self.socket = None
                time.sleep(1.0)
            except Exception as e:
                print(f"DataReceiver error: {e}")
                time.sleep(1.0)

    def _process_line(self, line: str) -> None:
        """Process a single JSON line from the stream."""
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            return

        # Extract data from the simplified JSON format
        # Note: latitude/longitude come from sim in radians, convert to degrees
        # Longitude needs normalization from 0-360 to -180/+180
        import math
        lat = math.degrees(float(data.get("latitude", 0.0)))
        lon = math.degrees(float(data.get("longitude", 0.0)))
        if lon > 180:
            lon -= 360
        alt_ft = float(data.get("altitude", 0.0))
        gs_ms = float(data.get("ground_speed", 0.0))

        # Heading is in radians in simplified format
        # Use magnetic heading if available, fallback to true heading
        heading_rad = float(data.get("magnetic_heading", data.get("true_heading", 0.0)))
        pitch_rad = float(data.get("pitch", 0.0))
        bank_rad = float(data.get("bank", 0.0))

        self.latest_data = FlightData(
            longitude=lon,
            latitude=lat,
            altitude_ft=alt_ft,
            ground_speed_ms=gs_ms,
            heading_rad=heading_rad,
            pitch_rad=pitch_rad,
            bank_rad=bank_rad,
        )
        self.last_receive_time = time.time()

    def get_latest_data(self) -> Dict[str, Any]:
        """Get the latest received data and connection status."""
        return {
            "data": self.latest_data,
            "connected": (time.time() - self.last_receive_time) < RECEIVE_TIMEOUT_S,
        }

    def stop(self) -> None:
        """Stop the receiver and close connection."""
        self.running = False
        if self.receive_thread:
            self.receive_thread.join(timeout=1.5)
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass


class AircraftTrackerApp:
    """Main GUI application for the Aircraft Tracker."""

    def __init__(self, master: tk.Tk) -> None:
        self.master = master
        self.master.title("Aircraft Tracker - Aerofly FS4 Reader")
        self.master.geometry(WINDOW_SIZE)

        self._setup_ui()

        self.receiver = DataReceiver()
        self.receiver.start_receiving()

        self._setup_aircraft_marker()
        self._update_aircraft_position()

    def _setup_ui(self) -> None:
        """Setup the user interface."""
        self.main_frame = tk.Frame(self.master)
        self.main_frame.pack(fill="both", expand=True)

        # Map widget
        self.map_widget = TkinterMapView(
            self.main_frame,
            width=MAP_SIZE[0],
            height=MAP_SIZE[1],
            corner_radius=0,
        )
        self.map_widget.pack(side="left", fill="both", expand=True)

        # Control frame
        self.control_frame = tk.Frame(self.main_frame, width=CONTROL_FRAME_WIDTH)
        self.control_frame.pack(side="right", fill="y")

        self._setup_map_selection()
        self._setup_info_display()

        # Connection status
        self.connection_status = tk.Label(self.control_frame, text="Disconnected", fg="red")
        self.connection_status.pack(pady=5)

        # Close button
        self.close_button = tk.Button(
            self.control_frame, text="Close Map", command=self._close_application
        )
        self.close_button.pack(side="bottom", pady=10)

        self.master.protocol("WM_DELETE_WINDOW", self._close_application)

    def _setup_map_selection(self) -> None:
        """Setup the map style selection listbox."""
        tk.Label(self.control_frame, text="Select Map:").pack(pady=(10, 5))

        listbox_frame = tk.Frame(self.control_frame)
        listbox_frame.pack(padx=0, pady=5)

        self.map_listbox = tk.Listbox(listbox_frame, width=24, height=13)
        self.map_listbox.pack(side="left")

        for option, _ in self._get_map_options():
            self.map_listbox.insert(tk.END, option)

        self.map_listbox.bind("<<ListboxSelect>>", lambda e: self._change_map())

    def _setup_info_display(self) -> None:
        """Setup the flight info text display."""
        tk.Label(self.control_frame, text="Aircraft Position:").pack(pady=(10, 5))

        info_font = tkfont.Font(family="Consolas", size=9)
        self.info_display = tk.Text(
            self.control_frame,
            width=INFO_DISPLAY_SIZE[0],
            height=INFO_DISPLAY_SIZE[1],
            wrap=tk.NONE,
            font=info_font,
        )
        self.info_display.pack(padx=10, pady=10)

    def _setup_aircraft_marker(self) -> None:
        """Setup the aircraft marker icon."""
        try:
            self.aircraft_image = Image.open("aircraft_icon.png").resize((32, 32))
        except Exception:
            self.aircraft_image = self._generate_default_icon(32, 32)
        self.rotated_image = ImageTk.PhotoImage(self.aircraft_image)
        self.aircraft_marker = None
        self.initial_position_set = False

    def _update_aircraft_position(self) -> None:
        """Periodic update of aircraft position and info display."""
        state = self.receiver.get_latest_data()

        if state["connected"]:
            self.connection_status.config(text="Connected", fg="green")
            if state["data"]:
                self._update_map_and_marker(state["data"])
                self._update_info_display(state["data"])
        else:
            self.connection_status.config(text="Disconnected", fg="red")
            self._clear_info_display()

        self.master.after(UPDATE_INTERVAL_MS, self._update_aircraft_position)

    def _clear_info_display(self) -> None:
        """Clear the info display when disconnected."""
        self.info_display.delete(1.0, tk.END)
        self.info_display.insert(tk.END, "Waiting for data...")

    def _update_map_and_marker(self, data: FlightData) -> None:
        """Update map position and aircraft marker."""
        import math

        if not self.initial_position_set:
            self.map_widget.set_position(data.latitude, data.longitude)
            self.map_widget.set_zoom(10)
            self.initial_position_set = True

        # Convert heading from radians to degrees for rotation
        heading_deg = math.degrees(data.heading_rad) % 360
        self.rotated_image = self._rotate_image(heading_deg)

        if self.aircraft_marker:
            self.aircraft_marker.delete()

        self.aircraft_marker = self.map_widget.set_marker(
            data.latitude,
            data.longitude,
            icon=self.rotated_image,
            icon_anchor="center",
        )

        self.map_widget.set_position(data.latitude, data.longitude)

    def _update_info_display(self, data: FlightData) -> None:
        """Update the flight info text display."""
        import math

        # Convert to display units
        MPS_TO_KT = 1.943844492
        gs_kts = data.ground_speed_ms * MPS_TO_KT
        heading_deg = math.degrees(data.heading_rad) % 360
        pitch_deg = math.degrees(data.pitch_rad)
        bank_deg = math.degrees(data.bank_rad)

        info_text = "=" * 24 + "\n"
        info_text += f"{'Latitude:':<15}{data.latitude:>8.2f}°\n"
        info_text += f"{'Longitude:':<15}{data.longitude:>8.2f}°\n"
        info_text += f"{'Altitude:':<15}{data.altitude_ft:>6.0f} ft\n"
        info_text += f"{'Ground Speed:':<15}{gs_kts:>5.2f} kts\n"
        info_text += f"{'Heading:':<15}{heading_deg:>8.2f}°\n"
        info_text += f"{'Pitch:':<15}{pitch_deg:>8.2f}°\n"
        info_text += f"{'Bank:':<15}{bank_deg:>8.2f}°\n"
        info_text += "=" * 24 + "\n"

        self.info_display.delete(1.0, tk.END)
        self.info_display.insert(tk.END, info_text)

    def _rotate_image(self, angle_deg: float) -> ImageTk.PhotoImage:
        """Rotate the aircraft icon to match heading."""
        return ImageTk.PhotoImage(self.aircraft_image.rotate(-angle_deg))

    @staticmethod
    def _generate_default_icon(width: int, height: int) -> Image.Image:
        """Generate a default aircraft icon if custom icon is not available."""
        img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        w, h = width, height
        triangle = [(w * 0.5, h * 0.1), (w * 0.1, h * 0.9), (w * 0.9, h * 0.9)]
        draw.polygon(triangle, fill=(14, 35, 64, 255))

        r = max(1, int(min(w, h) * 0.06))
        cx, cy = int(w * 0.5), int(h * 0.6)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255, 255))

        return img

    def _change_map(self) -> None:
        """Change the map tile server based on selection."""
        selected = self.map_listbox.curselection()
        if selected:
            _, tile_server = self._get_map_options()[selected[0]]
            self.map_widget.set_tile_server(tile_server)

    @staticmethod
    def _get_map_options() -> List[Tuple[str, str]]:
        """Get available map tile servers."""
        return [
            ("OpenStreetMap", "https://a.tile.openstreetmap.org/{z}/{x}/{y}.png"),
            ("OpenStreetMap DE", "https://tile.openstreetmap.de/tiles/osmde/{z}/{x}/{y}.png"),
            ("OpenStreetMap FR", "https://a.tile.openstreetmap.fr/osmfr/{z}/{x}/{y}.png"),
            ("OpenTopoMap", "https://a.tile.opentopomap.org/{z}/{x}/{y}.png"),
            ("Google Normal", "https://mt0.google.com/vt/lyrs=m&hl=en&x={x}&y={y}&z={z}"),
            ("Google Satellite", "https://mt0.google.com/vt/lyrs=s&hl=en&x={x}&y={y}&z={z}"),
            ("Google Terrain", "https://mt0.google.com/vt/lyrs=p&hl=en&x={x}&y={y}&z={z}"),
            ("Google Hybrid", "https://mt0.google.com/vt/lyrs=y&hl=en&x={x}&y={y}&z={z}"),
            ("Carto Dark Matter", "https://cartodb-basemaps-a.global.ssl.fastly.net/dark_all/{z}/{x}/{y}.png"),
            ("Carto Positron", "https://cartodb-basemaps-a.global.ssl.fastly.net/light_all/{z}/{x}/{y}.png"),
            ("ESRI World Imagery", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"),
            ("ESRI World Street", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}"),
            ("ESRI World Topo", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}"),
        ]

    def _close_application(self) -> None:
        """Close the application gracefully."""
        print("Closing Aircraft Tracker...")
        self.receiver.stop()
        self.master.destroy()


def main() -> None:
    """Main entry point."""
    print("Starting Aircraft Tracker (AeroflyReader)...")
    print(f"Connecting to AeroflyReader at {TCP_HOST}:{TCP_PORT}...")
    root = tk.Tk()
    app = AircraftTrackerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
