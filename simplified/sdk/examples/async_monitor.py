#!/usr/bin/env python3
"""
Aerofly Reader SDK - Async Monitor Example

Demonstrates async/await usage with the SDK.
"""

import asyncio

from aerofly_reader import AsyncAeroflyClient


async def display_flight_data(flight):
    """Display flight data asynchronously."""
    print("\033[H\033[J", end="")  # Clear screen

    print("=" * 50)
    print("    ASYNC FLIGHT MONITOR")
    print("=" * 50)

    print(f"\n  Aircraft: {flight.aircraft_name}")

    # Position
    print(f"\n  Position: {flight.position.latitude:.4f}°, {flight.position.longitude:.4f}°")
    print(f"  Altitude: {flight.position.altitude_ft:,.0f} ft")

    # Speeds
    print(f"\n  IAS: {flight.speeds.ias:.0f} kts")
    print(f"  GS:  {flight.speeds.gs:.0f} kts")
    print(f"  VS:  {flight.speeds.vs:+.0f} fpm")

    # Status
    status = "ON GROUND" if flight.on_ground else "AIRBORNE"
    print(f"\n  Status: {status}")
    print(f"  Heading: {flight.orientation.heading:03.0f}°")

    # Autopilot
    if flight.autopilot.master:
        print(f"\n  AP: ON")
        print(f"    HDG: {flight.autopilot.heading:03.0f}°")
        print(f"    ALT: {flight.autopilot.altitude:,.0f} ft")

    print("\n" + "-" * 50)
    print(f"  Update: {flight.update_counter} @ {flight.update_hz:.1f} Hz")
    print("  Press Ctrl+C to exit")


async def main():
    print("Aerofly Reader SDK - Async Example")
    print("Connecting...\n")

    try:
        async with AsyncAeroflyClient() as client:
            async for flight in client.stream():
                await display_flight_data(flight)
                # Small delay to reduce CPU usage
                await asyncio.sleep(0.1)

    except KeyboardInterrupt:
        print("\n\nExiting...")


if __name__ == "__main__":
    asyncio.run(main())
