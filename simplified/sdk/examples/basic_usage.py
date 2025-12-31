#!/usr/bin/env python3
"""
Aerofly Reader SDK - Basic Usage Example

Demonstrates the simplest way to connect and read flight data.
"""

from aerofly_reader import stream


def main():
    print("Aerofly Reader SDK - Basic Example")
    print("=" * 50)
    print("Connecting to Aerofly FS4...")
    print("Press Ctrl+C to exit\n")

    try:
        for flight in stream():
            # Clear screen
            print("\033[H\033[J", end="")

            print("=" * 50)
            print("        FLIGHT DATA")
            print("=" * 50)

            # Aircraft info
            print(f"\n  Aircraft: {flight.aircraft_name}")
            print(f"  Near: {flight.aircraft.nearest_airport_id}")

            # Position (automatically converted to degrees)
            print(f"\n  POSITION")
            print(f"    Lat: {flight.position.latitude:+.6f}°")
            print(f"    Lon: {flight.position.longitude:+.6f}°")
            print(f"    Alt: {flight.position.altitude_ft:,.0f} ft MSL")
            print(f"    AGL: {flight.position.height_agl_ft:,.0f} ft")

            # Speeds (automatically converted to knots/fpm)
            print(f"\n  SPEEDS")
            print(f"    IAS: {flight.speeds.indicated_airspeed:.0f} kts")
            print(f"    GS:  {flight.speeds.ground_speed:.0f} kts")
            print(f"    VS:  {flight.speeds.vertical_speed:+.0f} fpm")
            print(f"    Mach: {flight.speeds.mach_number:.3f}")

            # Orientation (automatically converted to degrees)
            print(f"\n  ORIENTATION")
            print(f"    Heading: {flight.orientation.heading:03.0f}°")
            print(f"    Pitch:   {flight.orientation.pitch:+.1f}°")
            print(f"    Bank:    {flight.orientation.bank:+.1f}°")

            # State
            status = "ON GROUND" if flight.on_ground else "IN FLIGHT"
            print(f"\n  STATE: {status}")
            print(f"    Gear:     {flight.gear_percent:.0f}%")
            print(f"    Flaps:    {flight.flaps_percent:.0f}%")
            print(f"    Throttle: {flight.throttle_percent:.0f}%")

            # Footer
            print("\n" + "-" * 50)
            valid = "✓" if flight.data_valid else "✗"
            print(f"  Update #{flight.update_counter} | Valid: {valid}")
            print("  Press Ctrl+C to exit")
            print("=" * 50)

    except KeyboardInterrupt:
        print("\n\nExiting...")


if __name__ == "__main__":
    main()
