"""
Flight Performance Analyzer (Example Script)

This example implements the tutorial "Flight Performance Analyzer" as a runnable
Python script. It combines three interfaces exposed by AeroflyBridge (TCP, WebSocket,
and Shared Memory) to collect telemetry, compute basic performance metrics, and
expose a small Dash dashboard.

Requirements (install as needed):
  pip install pandas numpy plotly dash websocket-client

Notes:
- TCP data stream: localhost:12345 (JSON, read-only)
- WebSocket stream: ws://localhost:8765 (JSON, bidirectional but we only read)
- Shared Memory: "AeroflyBridgeData" (Windows). This example demonstrates reading a
  couple of doubles via the all_variables[] array. For production, use the provided
  offsets JSON file for complete layout.

Run:
  python examples/flight_analyzer.py
Then open: http://localhost:8050
"""

from __future__ import annotations

import ctypes
import json
import queue
import socket
import struct
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Dict, List, Optional

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots

import dash
from dash import dcc, html, Input, Output, State


# ------------------------------ Data Structures ------------------------------


@dataclass
class FlightMetrics:
    timestamp: datetime
    altitude: float
    airspeed: float
    ground_speed: float
    fuel_flow: float
    engine_rpm: float
    climb_rate: float
    efficiency_score: float
    fuel_economy: float
    source_interface: str


# ------------------------------- Connectors ----------------------------------


class MultiInterfaceConnector:
    """Manages connections to all three AeroflyBridge interfaces."""

    def __init__(self) -> None:
        # Connection states
        self.tcp_connected: bool = False
        self.ws_connected: bool = False
        self.shmem_connected: bool = False

        # Handles
        self.tcp_socket: Optional[socket.socket] = None
        self.ws_app = None  # created in loop to avoid import at module import
        self.shmem_handle: Optional[int] = None
        self.shmem_view: Optional[int] = None

        # Data queues
        self.tcp_data_queue: queue.Queue = queue.Queue(maxsize=1000)
        self.ws_data_queue: queue.Queue = queue.Queue(maxsize=1000)
        self.shmem_data_queue: queue.Queue = queue.Queue(maxsize=1000)

        # Control flags
        self.running: bool = False
        self.threads: List[threading.Thread] = []

        # Stats
        self.interface_stats: Dict[str, Dict[str, float]] = {
            "tcp": {"count": 0, "last_update": 0.0, "rate": 0.0},
            "websocket": {"count": 0, "last_update": 0.0, "rate": 0.0},
            "shared_memory": {"count": 0, "last_update": 0.0, "rate": 0.0},
        }

    # ---- Lifecycle ----

    def start_all_interfaces(self) -> None:
        self.running = True

        tcp_thread = threading.Thread(target=self._tcp_loop, daemon=True)
        tcp_thread.start()
        self.threads.append(tcp_thread)

        ws_thread = threading.Thread(target=self._websocket_loop, daemon=True)
        ws_thread.start()
        self.threads.append(ws_thread)

        shmem_thread = threading.Thread(target=self._shared_memory_loop, daemon=True)
        shmem_thread.start()
        self.threads.append(shmem_thread)

        stats_thread = threading.Thread(target=self._stats_loop, daemon=True)
        stats_thread.start()
        self.threads.append(stats_thread)

        print("🚀 All interfaces started!")

    def stop_all_interfaces(self) -> None:
        self.running = False
        if self.tcp_socket:
            try:
                self.tcp_socket.close()
            except Exception:
                pass
        if self.shmem_view:
            try:
                ctypes.windll.kernel32.UnmapViewOfFile(self.shmem_view)
            except Exception:
                pass
        if self.shmem_handle:
            try:
                ctypes.windll.kernel32.CloseHandle(self.shmem_handle)
            except Exception:
                pass
        print("🔌 All interfaces stopped")

    # ---- Producer Loops ----

    def _tcp_loop(self) -> None:
        while self.running:
            try:
                if not self.tcp_connected:
                    self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self.tcp_socket.settimeout(5.0)
                    self.tcp_socket.connect(("localhost", 12345))
                    self.tcp_connected = True
                    print("✅ TCP connected")

                buffer = ""
                while self.running and self.tcp_connected:
                    data = self.tcp_socket.recv(4096).decode("utf-8")
                    if not data:
                        break
                    buffer += data
                    while "\n" in buffer:
                        line, buffer = buffer.split("\n", 1)
                        if not line.strip():
                            continue
                        try:
                            flight_data = json.loads(line)
                            self.tcp_data_queue.put(
                                {"source": "tcp", "timestamp": time.time(), "data": flight_data},
                                block=False,
                            )
                            self.interface_stats["tcp"]["count"] += 1
                        except (json.JSONDecodeError, queue.Full):
                            pass

            except Exception as e:
                print(f"❌ TCP error: {e}")
                self.tcp_connected = False
                try:
                    if self.tcp_socket:
                        self.tcp_socket.close()
                except Exception:
                    pass
                time.sleep(2)

    def _websocket_loop(self) -> None:
        while self.running:
            try:
                import websocket  # websocket-client

                def on_message(ws, message):  # noqa: N802 (external callback signature)
                    try:
                        flight_data = json.loads(message)
                        self.ws_data_queue.put(
                            {"source": "websocket", "timestamp": time.time(), "data": flight_data},
                            block=False,
                        )
                        self.interface_stats["websocket"]["count"] += 1
                    except (json.JSONDecodeError, queue.Full):
                        pass

                def on_open(ws):  # noqa: N802
                    self.ws_connected = True
                    print("✅ WebSocket connected")

                def on_close(ws, code, msg):  # noqa: N802
                    self.ws_connected = False
                    print("❌ WebSocket disconnected")

                def on_error(ws, error):  # noqa: N802
                    print(f"❌ WebSocket error: {error}")

                ws = websocket.WebSocketApp(
                    "ws://localhost:8765",
                    on_message=on_message,
                    on_open=on_open,
                    on_close=on_close,
                    on_error=on_error,
                )
                ws.run_forever()

            except Exception as e:
                print(f"❌ WebSocket error: {e}")
                time.sleep(2)

    def _shared_memory_loop(self) -> None:
        while self.running:
            try:
                if not self.shmem_connected:
                    kernel32 = ctypes.windll.kernel32

                    # Signatures
                    kernel32.OpenFileMappingW.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_wchar_p]
                    kernel32.OpenFileMappingW.restype = ctypes.c_void_p
                    kernel32.MapViewOfFile.argtypes = [
                        ctypes.c_void_p,
                        ctypes.c_ulong,
                        ctypes.c_ulong,
                        ctypes.c_ulong,
                        ctypes.c_size_t,
                    ]
                    kernel32.MapViewOfFile.restype = ctypes.c_void_p

                    FILE_MAP_READ = 0x0004
                    self.shmem_handle = kernel32.OpenFileMappingW(FILE_MAP_READ, False, "AeroflyBridgeData")
                    if self.shmem_handle:
                        self.shmem_view = kernel32.MapViewOfFile(self.shmem_handle, FILE_MAP_READ, 0, 0, 0)
                        if self.shmem_view:
                            self.shmem_connected = True
                            print("✅ Shared Memory connected")

                while self.running and self.shmem_connected:
                    try:
                        # Minimal example via all_variables[] access
                        array_base = 16  # See docs/api_reference.md
                        stride = 8
                        alt_offset = array_base + (1 * stride)  # AIRCRAFT_ALTITUDE index = 1
                        ias_offset = array_base + (5 * stride)  # AIRCRAFT_INDICATED_AIRSPEED index = 5

                        altitude_m = struct.unpack('<d', ctypes.string_at(self.shmem_view + alt_offset, 8))[0]
                        ias_mps = struct.unpack('<d', ctypes.string_at(self.shmem_view + ias_offset, 8))[0]

                        flight_data = {
                            "variables": {
                                "Aircraft.Altitude": altitude_m,
                                "Aircraft.IndicatedAirspeed": ias_mps,
                            }
                        }
                        self.shmem_data_queue.put(
                            {
                                "source": "shared_memory",
                                "timestamp": time.time(),
                                "data": flight_data,
                            },
                            block=False,
                        )
                        self.interface_stats["shared_memory"]["count"] += 1
                        time.sleep(0.01)  # 100 Hz
                    except Exception as e:
                        print(f"❌ Shared memory read error: {e}")
                        break

            except Exception as e:
                print(f"❌ Shared memory error: {e}")
                self.shmem_connected = False
                time.sleep(2)

    # ---- Stats ----

    def _stats_loop(self) -> None:
        while self.running:
            now = time.time()
            for name, stats in self.interface_stats.items():
                if stats["last_update"] > 0:
                    elapsed = now - stats["last_update"]
                    if elapsed > 0:
                        stats["rate"] = stats["count"] / elapsed
                        stats["count"] = 0
                        stats["last_update"] = now
                else:
                    stats["last_update"] = now
            time.sleep(1)

    # ---- Consumer API ----

    def get_latest_data(self) -> Dict[str, Dict[str, object]]:
        """Return the most recent data packets from each interface."""
        results: Dict[str, Dict[str, object]] = {}
        for key, q in (
            ("tcp", self.tcp_data_queue),
            ("websocket", self.ws_data_queue),
            ("shared_memory", self.shmem_data_queue),
        ):
            latest = None
            while True:
                try:
                    latest = q.get_nowait()
                except queue.Empty:
                    break
            if latest:
                results[key] = latest
        return results


# ----------------------------- Analyzer Engine -------------------------------


class FlightPerformanceAnalyzer:
    def __init__(self) -> None:
        self.connector = MultiInterfaceConnector()
        self.flight_data: List[FlightMetrics] = []
        self.session_start: Optional[datetime] = None
        self.running = False

        self.thresholds = {
            "optimal_climb_rate": 500.0,  # ft/min
            "cruise_altitude_min": 3000.0,  # ft
            "fuel_efficiency_target": 12.0,  # NM/gal (placeholder)
            "max_fuel_flow": 20.0,  # gph (placeholder)
            "optimal_cruise_speed": 120.0,  # kts
        }

        self.analysis_results: Dict[str, object] = {}

    # ---- Lifecycle ----

    def start_analysis(self) -> None:
        self.session_start = datetime.now()
        self.running = True
        self.connector.start_all_interfaces()

        analysis_thread = threading.Thread(target=self._analysis_loop, daemon=True)
        analysis_thread.start()
        print("📊 Flight Performance Analysis Started!")

    def stop_analysis(self) -> None:
        self.running = False
        self.connector.stop_all_interfaces()
        print("📊 Flight Performance Analysis Stopped!")

    # ---- Core Loops ----

    def _analysis_loop(self) -> None:
        last_analysis = time.time()
        while self.running:
            try:
                latest = self.connector.get_latest_data()
                for interface, packet in latest.items():
                    self._process_packet(packet, interface)

                if time.time() - last_analysis > 5:
                    self._analyze_performance()
                    last_analysis = time.time()

                time.sleep(0.1)
            except Exception as e:
                print(f"❌ Analysis error: {e}")
                time.sleep(1)

    def _process_packet(self, packet: Dict[str, object], interface: str) -> None:
        try:
            variables = (packet.get("data") or {}).get("variables", {})  # type: ignore
            ts = datetime.fromtimestamp(packet.get("timestamp", time.time()))  # type: ignore

            # Metric extraction (canonical variables)
            altitude_m = float(variables.get("Aircraft.Altitude", 0.0))
            airspeed_mps = float(variables.get("Aircraft.IndicatedAirspeed", 0.0))
            ground_mps = float(variables.get("Aircraft.GroundSpeed", 0.0))
            engine_rpm = float(variables.get("Aircraft.EngineRotationSpeed1", 0.0))
            climb_mps = float(variables.get("Aircraft.VerticalSpeed", 0.0))

            # Conversions
            M_TO_FT = 3.280839895
            MPS_TO_KT = 1.943844492
            FT_PER_MIN_PER_MPS = 196.850394

            altitude_ft = altitude_m * M_TO_FT
            airspeed_kt = airspeed_mps * MPS_TO_KT
            ground_kt = ground_mps * MPS_TO_KT
            climb_fpm = climb_mps * FT_PER_MIN_PER_MPS

            fuel_flow = 0.0  # Placeholder; not universally available

            fuel_economy = self._calculate_fuel_economy(ground_kt, fuel_flow)
            efficiency = self._calculate_efficiency_score(altitude_ft, airspeed_kt, fuel_flow, climb_fpm)

            metrics = FlightMetrics(
                timestamp=ts,
                altitude=altitude_ft,
                airspeed=airspeed_kt,
                ground_speed=ground_kt,
                fuel_flow=fuel_flow,
                engine_rpm=engine_rpm,
                climb_rate=climb_fpm,
                efficiency_score=efficiency,
                fuel_economy=fuel_economy,
                source_interface=interface,
            )

            self.flight_data.append(metrics)
            cutoff = ts - timedelta(minutes=10)
            self.flight_data = [m for m in self.flight_data if m.timestamp > cutoff]
        except Exception as e:
            print(f"❌ Data processing error: {e}")

    # ---- Metrics ----

    def _calculate_fuel_economy(self, ground_speed_kt: float, fuel_flow_gph: float) -> float:
        if fuel_flow_gph > 0:
            return ground_speed_kt / fuel_flow_gph
        return 0.0

    def _calculate_efficiency_score(
        self, altitude_ft: float, airspeed_kt: float, fuel_flow_gph: float, climb_fpm: float
    ) -> float:
        score = 100.0
        if altitude_ft > self.thresholds["cruise_altitude_min"]:
            score += min(10.0, (altitude_ft - self.thresholds["cruise_altitude_min"]) / 1000.0)
        else:
            score -= 10.0

        speed_diff = abs(airspeed_kt - self.thresholds["optimal_cruise_speed"])
        score -= min(20.0, speed_diff / 5.0)

        if fuel_flow_gph > self.thresholds["max_fuel_flow"]:
            score -= 15.0

        if abs(climb_fpm) > self.thresholds["optimal_climb_rate"]:
            score -= 10.0

        return float(max(0.0, min(100.0, score)))

    def _analyze_performance(self) -> None:
        if len(self.flight_data) < 10:
            return

        df = pd.DataFrame(
            [
                {
                    "timestamp": m.timestamp,
                    "altitude": m.altitude,
                    "airspeed": m.airspeed,
                    "ground_speed": m.ground_speed,
                    "fuel_flow": m.fuel_flow,
                    "engine_rpm": m.engine_rpm,
                    "climb_rate": m.climb_rate,
                    "efficiency_score": m.efficiency_score,
                    "fuel_economy": m.fuel_economy,
                    "source": m.source_interface,
                }
                for m in self.flight_data
            ]
        )

        self.analysis_results = {
            "flight_duration": (datetime.now() - (self.session_start or datetime.now())).total_seconds() / 60.0,
            "avg_altitude": float(df["altitude"].mean()),
            "max_altitude": float(df["altitude"].max()),
            "avg_airspeed": float(df["airspeed"].mean()),
            "avg_fuel_flow": float(df["fuel_flow"].mean()),
            "avg_fuel_economy": float(df["fuel_economy"].mean()),
            "avg_efficiency_score": float(df["efficiency_score"].mean()),
            "climb_performance": float(df[df["climb_rate"] > 100]["climb_rate"].mean()) if not df.empty else 0.0,
            "interface_performance": {
                "tcp_rate": self.connector.interface_stats["tcp"]["rate"],
                "ws_rate": self.connector.interface_stats["websocket"]["rate"],
                "shmem_rate": self.connector.interface_stats["shared_memory"]["rate"],
            },
            "data_quality": {
                "tcp_samples": int((df["source"] == "tcp").sum()) if "source" in df else 0,
                "ws_samples": int((df["source"] == "websocket").sum()) if "source" in df else 0,
                "shmem_samples": int((df["source"] == "shared_memory").sum()) if "source" in df else 0,
            },
        }

    # ---- Presentation ----

    def generate_report(self) -> str:
        if not self.analysis_results:
            return "No analysis data available"
        r = self.analysis_results
        return (
            f"\n🛩️  FLIGHT PERFORMANCE ANALYSIS REPORT\n"
            f"{'='*60}\n\n"
            f"📊 FLIGHT SUMMARY:\n"
            f"   Duration: {r['flight_duration']:.1f} minutes\n"
            f"   Average Altitude: {r['avg_altitude']:.0f} ft\n"
            f"   Maximum Altitude: {r['max_altitude']:.0f} ft\n"
            f"   Average Airspeed: {r['avg_airspeed']:.0f} kts\n\n"
            f"⛽ FUEL PERFORMANCE:\n"
            f"   Average Fuel Flow: {r['avg_fuel_flow']:.1f} gph\n"
            f"   Fuel Economy: {r['avg_fuel_economy']:.1f} NM/gal\n\n"
            f"📈 EFFICIENCY METRICS:\n"
            f"   Overall Score: {r['avg_efficiency_score']:.1f}/100\n"
            f"   Climb Performance: {r.get('climb_performance', 0):.0f} ft/min\n\n"
            f"🔗 INTERFACE PERFORMANCE:\n"
            f"   TCP Rate: {r['interface_performance']['tcp_rate']:.1f} Hz\n"
            f"   WebSocket Rate: {r['interface_performance']['ws_rate']:.1f} Hz\n"
            f"   Shared Memory Rate: {r['interface_performance']['shmem_rate']:.1f} Hz\n\n"
            f"📋 DATA QUALITY:\n"
            f"   TCP Samples: {r['data_quality']['tcp_samples']}\n"
            f"   WebSocket Samples: {r['data_quality']['ws_samples']}\n"
            f"   Shared Memory Samples: {r['data_quality']['shmem_samples']}\n"
        )

    def create_visualizations(self):
        if len(self.flight_data) < 10:
            return None

        df = pd.DataFrame(
            [
                {
                    "timestamp": m.timestamp,
                    "altitude": m.altitude,
                    "airspeed": m.airspeed,
                    "fuel_flow": m.fuel_flow,
                    "efficiency_score": m.efficiency_score,
                    "source": m.source_interface,
                }
                for m in self.flight_data
            ]
        )

        fig = make_subplots(
            rows=3,
            cols=2,
            subplot_titles=[
                "Altitude Profile",
                "Airspeed vs Time",
                "Fuel Flow Analysis",
                "Efficiency Score",
                "Interface Performance",
                "Data Distribution",
            ],
            specs=[[{"secondary_y": False}, {"secondary_y": False}],
                   [{"secondary_y": False}, {"secondary_y": False}],
                   [{"secondary_y": False}, {"type": "pie"}]],
        )

        fig.add_trace(go.Scatter(x=df["timestamp"], y=df["altitude"], name="Altitude", line=dict(color="blue")), row=1, col=1)
        fig.add_trace(go.Scatter(x=df["timestamp"], y=df["airspeed"], name="Airspeed", line=dict(color="green")), row=1, col=2)
        fig.add_trace(go.Scatter(x=df["timestamp"], y=df["fuel_flow"], name="Fuel Flow", line=dict(color="red")), row=2, col=1)
        fig.add_trace(go.Scatter(x=df["timestamp"], y=df["efficiency_score"], name="Efficiency", line=dict(color="orange")), row=2, col=2)

        # Bar for interface rates
        rates = self.analysis_results.get("interface_performance", {"tcp_rate": 0, "ws_rate": 0, "shmem_rate": 0})
        labels = list(rates.keys())
        values = [rates[k] for k in labels]
        fig.add_trace(go.Bar(x=labels, y=values, name="Update Rate"), row=3, col=1)

        # Pie for source distribution
        source_counts = df["source"].value_counts()
        fig.add_trace(go.Pie(labels=source_counts.index, values=source_counts.values, name="Data Sources"), row=3, col=2)

        fig.update_layout(height=900, title_text="Flight Performance Analysis Dashboard", showlegend=False)
        return fig


# ------------------------------- Dash App ------------------------------------


def create_dash_app(analyzer: FlightPerformanceAnalyzer):
    app = dash.Dash(__name__)

    app.layout = html.Div(
        [
            html.H1("🛩️ Flight Performance Analyzer", style={"textAlign": "center", "color": "#2c3e50"}),
            html.Div(
                [
                    html.Button("Start Analysis", id="start-btn", n_clicks=0, style={"backgroundColor": "#27ae60", "color": "white", "margin": "10px"}),
                    html.Button("Stop Analysis", id="stop-btn", n_clicks=0, style={"backgroundColor": "#e74c3c", "color": "white", "margin": "10px"}),
                    html.Button("Generate Report", id="report-btn", n_clicks=0, style={"backgroundColor": "#3498db", "color": "white", "margin": "10px"}),
                ],
                style={"textAlign": "center"},
            ),
            html.Div(id="status-display", style={"textAlign": "center", "margin": "20px"}),
            dcc.Interval(id="interval-component", interval=2000, n_intervals=0),
            dcc.Graph(id="performance-chart"),
            html.Pre(id="report-output", style={"backgroundColor": "#2c3e50", "color": "#ecf0f1", "padding": "20px", "margin": "20px"}),
        ]
    )

    @app.callback(
        Output("status-display", "children"),
        Output("performance-chart", "figure"),
        Output("report-output", "children"),
        Input("interval-component", "n_intervals"),
        Input("start-btn", "n_clicks"),
        Input("stop-btn", "n_clicks"),
        Input("report-btn", "n_clicks"),
        State("start-btn", "n_clicks"),
        State("stop-btn", "n_clicks"),
    )
    def update_dashboard(n_intervals, start_clicks, stop_clicks, report_clicks, start_state, stop_state):  # noqa: D401
        # Button handling
        ctx = dash.callback_context
        if ctx.triggered:
            button_id = ctx.triggered[0]["prop_id"].split(".")[0]
            if button_id == "start-btn" and (start_clicks or 0) > 0:
                if not analyzer.running:
                    analyzer.start_analysis()
            elif button_id == "stop-btn" and (stop_clicks or 0) > 0:
                if analyzer.running:
                    analyzer.stop_analysis()

        # Status panel
        if analyzer.running:
            status = html.Div([html.H3("✅ Analysis Running", style={"color": "#27ae60"}), html.P(f"Data Points: {len(analyzer.flight_data)}")])
        else:
            status = html.Div([html.H3("⏹️ Analysis Stopped", style={"color": "#e74c3c"}), html.P("Click 'Start Analysis' to begin")])

        # Chart
        fig = analyzer.create_visualizations()
        if fig is None:
            fig = go.Figure().add_annotation(text="No data available yet...", xref="paper", yref="paper", x=0.5, y=0.5, showarrow=False)

        # Report
        report = analyzer.generate_report() if (report_clicks or 0) > 0 else ""

        return status, fig, report

    return app


# ---------------------------------- Main -------------------------------------


def main() -> None:
    print("🛩️ Flight Performance Analyzer")
    print("📊 Multi-Interface Data Science Tool")
    print("=" * 50)

    analyzer = FlightPerformanceAnalyzer()
    app = create_dash_app(analyzer)

    print("🌐 Starting web dashboard...")
    print("📱 Open http://localhost:8050 in your browser")
    try:
        if hasattr(app, "run"):
            app.run(debug=False, host="0.0.0.0", port=8050)
        else:
            app.run_server(debug=False, host="0.0.0.0", port=8050)
    except KeyboardInterrupt:
        print("\n🛑 Shutting down...")
        analyzer.stop_analysis()


if __name__ == "__main__":
    main()


