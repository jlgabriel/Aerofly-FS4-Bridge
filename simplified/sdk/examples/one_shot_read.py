#!/usr/bin/env python3
"""
Aerofly Reader SDK - One-Shot Read Example

Demonstrates reading a single frame of data.
Useful for scripts that need quick access to current state.
"""

from aerofly_reader import read_once


def main():
    print("Aerofly Reader SDK - One-Shot Read")
    print("=" * 50)

    try:
        # Read single frame (connects and disconnects automatically)
        flight = read_once()

        print(f"\nAircraft: {flight.aircraft_name}")
        print(f"Near: {flight.aircraft.nearest_airport_id} - {flight.aircraft.nearest_airport_name}")

        print(f"\nPosition:")
        print(f"  Lat: {flight.position.latitude:.6f}°")
        print(f"  Lon: {flight.position.longitude:.6f}°")
        print(f"  Alt: {flight.position.altitude_ft:,.0f} ft MSL")

        print(f"\nSpeeds:")
        print(f"  IAS: {flight.speeds.indicated_airspeed:.0f} kts")
        print(f"  GS:  {flight.speeds.ground_speed:.0f} kts")
        print(f"  VS:  {flight.speeds.vertical_speed:+.0f} fpm")

        print(f"\nOrientation:")
        print(f"  HDG: {flight.orientation.heading:03.0f}°")
        print(f"  Pitch: {flight.orientation.pitch:+.1f}°")
        print(f"  Bank: {flight.orientation.bank:+.1f}°")

        print(f"\nState:")
        print(f"  On ground: {'Yes' if flight.on_ground else 'No'}")
        print(f"  Gear: {flight.gear_percent:.0f}%")
        print(f"  Flaps: {flight.flaps_percent:.0f}%")
        print(f"  Throttle: {flight.throttle_percent:.0f}%")

        print(f"\nEngines:")
        print(f"  Engine 1: {'Running' if flight.engines.engine1.running else 'Off'}")
        print(f"  Engine 2: {'Running' if flight.engines.engine2.running else 'Off'}")

        if flight.autopilot.master:
            print(f"\nAutopilot: ON")
            print(f"  HDG: {flight.autopilot.heading:03.0f}°")
            print(f"  ALT: {flight.autopilot.altitude:,.0f} ft")
        else:
            print(f"\nAutopilot: OFF")

        print(f"\nV-Speeds:")
        print(f"  VS0: {flight.vspeeds.vs0:.0f} kts")
        print(f"  VS1: {flight.vspeeds.vs1:.0f} kts")
        print(f"  VFE: {flight.vspeeds.vfe:.0f} kts")
        print(f"  VNO: {flight.vspeeds.vno:.0f} kts")
        print(f"  VNE: {flight.vspeeds.vne:.0f} kts")

        print("\n" + "=" * 50)
        print(f"Data valid: {'Yes' if flight.data_valid else 'No'}")
        print(f"Update counter: {flight.update_counter}")

    except Exception as e:
        print(f"\nError: {e}")
        print("Make sure Aerofly FS4 is running with AeroflyReader.dll loaded.")


if __name__ == "__main__":
    main()
