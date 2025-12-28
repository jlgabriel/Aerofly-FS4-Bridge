#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Simple Example

Este script se conecta al AeroflyReader DLL via TCP y muestra
los datos del vuelo en tiempo real.

Uso:
    python simple_reader.py

Requisitos:
    - AeroflyReader.dll instalado en Aerofly FS 4
    - Aerofly FS 4 corriendo con un vuelo activo
"""

import socket
import json
import sys
import time
from datetime import datetime


def connect_to_aerofly(host: str = 'localhost', port: int = 12345) -> socket.socket:
    """Conecta al servidor TCP del AeroflyReader."""
    print(f"Conectando a {host}:{port}...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)

    try:
        sock.connect((host, port))
        print("Conectado!")
        return sock
    except socket.timeout:
        print("Error: Timeout al conectar. Verifica que Aerofly esté corriendo.")
        sys.exit(1)
    except ConnectionRefusedError:
        print("Error: Conexión rechazada. Verifica que AeroflyReader.dll esté cargado.")
        sys.exit(1)


def format_heading(radians: float) -> str:
    """Convierte radianes a grados de rumbo (000-360)."""
    import math
    degrees = math.degrees(radians) % 360
    return f"{degrees:03.0f}°"


def format_speed_kts(ms: float) -> str:
    """Convierte m/s a nudos."""
    kts = ms * 1.94384
    return f"{kts:.0f} kts"


def format_altitude(feet: float) -> str:
    """Formatea altitud en pies."""
    return f"{feet:,.0f} ft"


def format_vs(ms: float) -> str:
    """Convierte velocidad vertical de m/s a ft/min."""
    ftmin = ms * 196.85
    sign = "+" if ftmin > 0 else ""
    return f"{sign}{ftmin:.0f} fpm"


def display_flight_data(data: dict):
    """Muestra los datos del vuelo de forma legible."""

    # Limpiar pantalla (opcional, comenta si no funciona en tu terminal)
    print("\033[H\033[J", end="")

    print("=" * 60)
    print("         AEROFLY FS4 READER - Flight Data")
    print("=" * 60)
    print()

    # Avión e información básica
    print(f"  Avión: {data.get('aircraft_name', 'Unknown')}")
    print(f"  Aeropuerto cercano: {data.get('nearest_airport_id', '----')} - {data.get('nearest_airport_name', 'Unknown')}")
    print()

    # Posición
    lat = data.get('latitude', 0)
    lon = data.get('longitude', 0)
    print("  POSICIÓN")
    print(f"    Lat: {lat:+.6f}°")
    print(f"    Lon: {lon:+.6f}°")
    print()

    # Altitudes y velocidades
    print("  ALTITUD & VELOCIDAD")
    print(f"    Altitud MSL: {format_altitude(data.get('altitude', 0))}")
    print(f"    Altura AGL:  {format_altitude(data.get('height', 0))}")
    print(f"    IAS:         {format_speed_kts(data.get('indicated_airspeed', 0))}")
    print(f"    GS:          {format_speed_kts(data.get('ground_speed', 0))}")
    print(f"    VS:          {format_vs(data.get('vertical_speed', 0))}")
    print()

    # Orientación
    print("  ORIENTACIÓN")
    print(f"    Rumbo Mag:   {format_heading(data.get('magnetic_heading', 0))}")
    print(f"    Rumbo True:  {format_heading(data.get('true_heading', 0))}")
    import math
    pitch_deg = math.degrees(data.get('pitch', 0))
    bank_deg = math.degrees(data.get('bank', 0))
    print(f"    Pitch:       {pitch_deg:+.1f}°")
    print(f"    Bank:        {bank_deg:+.1f}°")
    print()

    # Estado
    print("  ESTADO")
    on_ground = "EN TIERRA" if data.get('on_ground', 0) > 0.5 else "EN VUELO"
    gear = "ABAJO" if data.get('gear', 0) > 0.5 else "ARRIBA"
    flaps = f"{data.get('flaps', 0) * 100:.0f}%"
    throttle = f"{data.get('throttle', 0) * 100:.0f}%"
    print(f"    {on_ground}")
    print(f"    Gear:     {gear}")
    print(f"    Flaps:    {flaps}")
    print(f"    Throttle: {throttle}")
    print()

    # Motores
    eng1 = "ON" if data.get('engine_running_1', 0) > 0.5 else "OFF"
    eng2 = "ON" if data.get('engine_running_2', 0) > 0.5 else "OFF"
    print("  MOTORES")
    print(f"    Motor 1: {eng1} ({data.get('engine_throttle_1', 0) * 100:.0f}%)")
    print(f"    Motor 2: {eng2} ({data.get('engine_throttle_2', 0) * 100:.0f}%)")
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
    print("  Presiona Ctrl+C para salir")
    print("=" * 60)


def main():
    """Función principal."""
    print()
    print("  Aerofly FS4 Reader - Simple Example")
    print("  ------------------------------------")
    print()

    sock = connect_to_aerofly()
    buffer = ""

    try:
        while True:
            # Recibir datos
            try:
                data = sock.recv(4096).decode('utf-8')
                if not data:
                    print("Conexión cerrada por el servidor.")
                    break
                buffer += data
            except socket.timeout:
                continue

            # Procesar líneas JSON completas
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

            # Pequeña pausa para no saturar la CPU
            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\n\nDesconectando...")
    finally:
        sock.close()
        print("Conexión cerrada.")


if __name__ == "__main__":
    main()
