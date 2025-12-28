#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Flight Logger

Este script graba los datos del vuelo en un archivo CSV para
análisis posterior. Ideal para:
- Revisión de vuelos de entrenamiento
- Análisis de rendimiento
- Reconstrucción de trayectorias

Uso:
    python flight_logger.py [nombre_archivo.csv]

El archivo CSV incluye timestamp y todas las variables principales.
"""

import socket
import json
import csv
import sys
import time
import math
from datetime import datetime
from pathlib import Path


# Columnas del CSV
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
    """Conecta al servidor TCP del AeroflyReader."""
    print(f"Conectando a {host}:{port}...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)

    try:
        sock.connect((host, port))
        print("Conectado!")
        return sock
    except socket.timeout:
        print("Error: Timeout al conectar.")
        sys.exit(1)
    except ConnectionRefusedError:
        print("Error: Conexión rechazada.")
        sys.exit(1)


def ms_to_kts(ms: float) -> float:
    """Convierte m/s a nudos."""
    return ms * 1.94384


def ms_to_fpm(ms: float) -> float:
    """Convierte m/s a ft/min."""
    return ms * 196.85


def rad_to_deg(rad: float) -> float:
    """Convierte radianes a grados."""
    return math.degrees(rad)


def process_data(data: dict, start_time: float) -> dict:
    """Procesa los datos crudos y los convierte a unidades estándar."""
    elapsed = time.time() - start_time

    return {
        'timestamp': datetime.now().isoformat(),
        'elapsed_seconds': round(elapsed, 2),
        'latitude': round(data.get('latitude', 0), 6),
        'longitude': round(data.get('longitude', 0), 6),
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
    """Función principal."""
    print()
    print("  Aerofly FS4 Reader - Flight Logger")
    print("  -----------------------------------")
    print()

    # Determinar nombre del archivo
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"flight_log_{timestamp}.csv"

    filepath = Path(filename)
    print(f"  Archivo de log: {filepath.absolute()}")
    print()

    sock = connect_to_aerofly()
    buffer = ""
    start_time = time.time()
    record_count = 0
    last_log_time = 0
    log_interval = 1.0  # Grabar cada 1 segundo

    # Crear archivo CSV
    with open(filepath, 'w', newline='', encoding='utf-8') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=CSV_COLUMNS)
        writer.writeheader()

        print("  Grabando datos... (Ctrl+C para detener)")
        print()

        try:
            while True:
                # Recibir datos
                try:
                    data = sock.recv(4096).decode('utf-8')
                    if not data:
                        print("Conexión cerrada.")
                        break
                    buffer += data
                except socket.timeout:
                    continue

                # Procesar líneas JSON
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()

                    if not line:
                        continue

                    try:
                        flight_data = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    # Grabar según intervalo
                    current_time = time.time()
                    if current_time - last_log_time >= log_interval:
                        row = process_data(flight_data, start_time)
                        writer.writerow(row)
                        csvfile.flush()

                        record_count += 1
                        last_log_time = current_time

                        # Mostrar progreso
                        elapsed = row['elapsed_seconds']
                        alt = row['altitude_ft']
                        spd = row['indicated_airspeed_kts']
                        apt = row['nearest_airport']
                        print(f"\r  [{elapsed:>8.1f}s] Alt: {alt:>7.0f} ft | IAS: {spd:>5.0f} kts | Near: {apt} | Records: {record_count}", end="")

                time.sleep(0.02)

        except KeyboardInterrupt:
            print("\n")
            print("  Deteniendo grabación...")

    # Resumen final
    print()
    print("  " + "=" * 50)
    print(f"  Grabación completada!")
    print(f"  Archivo: {filepath.absolute()}")
    print(f"  Registros: {record_count}")
    print(f"  Duración: {time.time() - start_time:.1f} segundos")
    print("  " + "=" * 50)

    sock.close()


if __name__ == "__main__":
    main()
