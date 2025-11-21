# Variables Reference

**Complete reference of all 358 variables available through Aerofly FS4 Bridge**

Copyright (c) 2025 Juan Luis Gabriel

This document provides comprehensive information about every variable accessible through the bridge interfaces. Variables are organized by functional systems for easy navigation.

## 📋 Quick Reference

| Category | Variables | Description |
|----------|-----------|-------------|
| **[Aircraft](#-aircraft-variables-0-94)** | 95 variables | Core flight data, physics, systems |
| **[Performance](#-performance-speeds-95-104)** | 10 variables | Aircraft performance envelopes |
| **[Configuration](#-configuration-105-106)** | 2 variables | Flap and configuration settings |
| **[Flight Management](#-flight-management-system-107)** | 1 variable | FMS data |
| **[Navigation](#-navigation-systems-108-141)** | 34 variables | NAV, DME, ILS radio systems |
| **[Communication](#-communication-142-152)** | 11 variables | COM radios, transponder |
| **[Autopilot](#-autopilot-systems-153-180)** | 28 variables | Autopilot and flight director |
| **[Flight Director](#-flight-director-181-183)** | 3 variables | FD command bars |
| **[Copilot](#-copilot-184-191)** | 8 variables | Copilot controls |
| **[Controls](#-flight-controls-192-260)** | 69 variables | Primary flight controls |
| **[Pressurization](#-pressurization-261-262)** | 2 variables | Cabin pressurization |
| **[Warnings](#-warning-systems-263-272)** | 10 variables | Caution and warning systems |
| **[View/Camera](#-view-camera-273-302)** | 30 variables | Camera and view controls |
| **[Simulation](#-simulation-303-320)** | 18 variables | Sim environment controls |
| **[Command Interface](#-command-321-330)** | 10 variables | Command and control interface |
| **[Reserved](#-reserved-331-338)** | 8 variables | Reserved for internal use |
| **[Cessna 172](#-cessna-172-specific-340-357)** | 18 variables | C172-specific systems |

**Total: 358 variables** (indices 0-357, note: index 339 is intentionally skipped)

---

## 🛩 Aircraft Variables (0-94)

**Core aircraft state, physics, and primary systems**

### Basic Flight Data

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 0 | `Aircraft.UniversalTime` | Double | Seconds | 0 - 86400 | UTC time of day |
| 1 | `Aircraft.Altitude` | Double | Meters | -1000 - 50000 | Barometric altitude |
| 2 | `Aircraft.VerticalSpeed` | Double | m/s | -50 - 50 | Rate of climb/descent |
| 3 | `Aircraft.Pitch` | Double | Radians | -π/2 - π/2 | Pitch attitude |
| 4 | `Aircraft.Bank` | Double | Radians | -π - π | Bank/roll angle |
| 5 | `Aircraft.IndicatedAirspeed` | Double | m/s | 0 - 500 | IAS from pitot system |
| 6 | `Aircraft.IndicatedAirspeedTrend` | Double | m/s² | -10 - 10 | Airspeed acceleration |
| 7 | `Aircraft.GroundSpeed` | Double | m/s | 0 - 500 | Speed over ground |
| 8 | `Aircraft.MagneticHeading` | Double | Radians | 0 - 2π | Magnetic compass heading |
| 9 | `Aircraft.TrueHeading` | Double | Radians | 0 - 2π | True heading |
| 10 | `Aircraft.Latitude` | Double | Radians | -π/2 - π/2 | Geographic latitude |
| 11 | `Aircraft.Longitude` | Double | Radians | -π - π | Geographic longitude |

### Position and Physics

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 12 | `Aircraft.Height` | Double | Meters | 0 - 50000 | Height above terrain |
| 13 | `Aircraft.Position` | Vector3d | Meters | - | 3D world position |
| 14 | `Aircraft.Orientation` | Double | Radians | 0 - 2π | Aircraft orientation |
| 15 | `Aircraft.Velocity` | Vector3d | m/s | - | 3D velocity vector |
| 16 | `Aircraft.AngularVelocity` | Vector3d | rad/s | - | Rotation rates (p,q,r) |
| 17 | `Aircraft.Acceleration` | Vector3d | m/s² | - | 3D acceleration |
| 18 | `Aircraft.Gravity` | Vector3d | m/s² | - | Gravity vector |
| 19 | `Aircraft.Wind` | Vector3d | m/s | - | Wind velocity vector |
| 20 | `Aircraft.RateOfTurn` | Double | rad/s | -2 - 2 | Turn rate |
| 21 | `Aircraft.MachNumber` | Double | Mach | 0 - 5 | Mach number |
| 22 | `Aircraft.AngleOfAttack` | Double | Radians | -π/4 - π/4 | Angle of attack |
| 23 | `Aircraft.AngleOfAttackLimit` | Double | Radians | 0 - π/4 | AOA limit |
| 24 | `Aircraft.AccelerationLimit` | Double | m/s² | 0 - 100 | G-force limit |

### Aircraft Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 25 | `Aircraft.Gear` | Double | Position | 0.0 - 1.0 | Landing gear position |
| 26 | `Aircraft.Flaps` | Double | Position | 0.0 - 1.0 | Flap position |
| 27 | `Aircraft.Slats` | Double | Position | 0.0 - 1.0 | Slat position |
| 28 | `Aircraft.Throttle` | Double | Position | 0.0 - 1.0 | Throttle position |
| 29 | `Aircraft.AirBrake` | Double | Position | 0.0 - 1.0 | Air brake/spoiler |
| 30 | `Aircraft.GroundSpoilersArmed` | Double | Boolean | 0 or 1 | Ground spoilers armed |
| 31 | `Aircraft.GroundSpoilersExtended` | Double | Boolean | 0 or 1 | Ground spoilers deployed |
| 32 | `Aircraft.ParkingBrake` | Double | Boolean | 0 or 1 | Parking brake engaged |
| 33 | `Aircraft.AutoBrakeSetting` | Double | Setting | 0 - 5 | Auto brake level |
| 34 | `Aircraft.AutoBrakeEngaged` | Double | Boolean | 0 or 1 | Auto brake active |
| 35 | `Aircraft.AutoBrakeRejectedTakeOff` | Double | Boolean | 0 or 1 | RTO brake mode |

### Additional Aircraft Data

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 36 | `Aircraft.RadarAltitude` | Double | Meters | 0 - 2500 | Radio altitude |
| 37 | `Aircraft.Name` | String | - | - | Aircraft type name |
| 38 | `Aircraft.NearestAirportIdentifier` | String | - | - | Nearest airport ICAO |
| 39 | `Aircraft.NearestAirportName` | String | - | - | Nearest airport name |
| 40 | `Aircraft.NearestAirportLocation` | Vector2d | Degrees | - | Airport lat/lon |
| 41 | `Aircraft.NearestAirportElevation` | Double | Meters | - | Airport elevation |
| 42 | `Aircraft.BestAirportIdentifier` | String | - | - | Best airport ICAO |
| 43 | `Aircraft.BestAirportName` | String | - | - | Best airport name |
| 44 | `Aircraft.BestAirportLocation` | Vector2d | Degrees | - | Airport lat/lon |
| 45 | `Aircraft.BestAirportElevation` | Double | Meters | - | Airport elevation |
| 46 | `Aircraft.BestRunwayIdentifier` | String | - | - | Best runway ID |
| 47 | `Aircraft.BestRunwayElevation` | Double | Meters | - | Runway elevation |
| 48 | `Aircraft.BestRunwayThreshold` | Vector3d | Meters | - | Runway threshold |
| 49 | `Aircraft.BestRunwayEnd` | Vector3d | Meters | - | Runway end |

### Aircraft Status

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 50 | `Aircraft.Category.Jet` | Double | Boolean | 0 or 1 | Is jet aircraft |
| 51 | `Aircraft.Category.Glider` | Double | Boolean | 0 or 1 | Is glider |
| 52 | `Aircraft.OnGround` | Double | Boolean | 0 or 1 | On ground contact |
| 53 | `Aircraft.OnRunway` | Double | Boolean | 0 or 1 | On runway surface |
| 54 | `Aircraft.Crashed` | Double | Boolean | 0 or 1 | Aircraft crashed |
| 55 | `Aircraft.Power` | Double | Percent | 0.0 - 1.0 | Total power setting |

### Power and Engine Management

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 56 | `Aircraft.NormalizedPower` | Double | Percent | 0.0 - 1.0 | Normalized power |
| 57 | `Aircraft.NormalizedPowerTarget` | Double | Percent | 0.0 - 1.0 | Target power |
| 75-94 | `Aircraft.Engine*` | Various | Various | - | *See Engine Systems section* |

**Usage Example (JSON canonical variables):**
```javascript
// Read basic flight data from canonical variables map
const altitude_m = data.variables["Aircraft.Altitude"];     // meters MSL
const ias_mps    = data.variables["Aircraft.IndicatedAirspeed"]; // m/s
const onGround   = data.variables["Aircraft.OnGround"] === 1;

// Access additional variables by canonical name
const throttle = data.variables["Controls.Throttle"];

// Set throttle via command
{
  "variable": "Controls.Throttle",
  "value": 0.75
}
```

> Note on heading normalization
>
> - Prefer `Aircraft.MagneticHeading` for display; fall back to `Aircraft.TrueHeading` if magnetic is unavailable.
> - Canonical units are radians in mathematical convention (0° = East, CCW positive). If you need compass heading (0° = North, clockwise), convert as follows:
>
> ```javascript
> const src = (v["Aircraft.MagneticHeading"] ?? v["Aircraft.TrueHeading"] ?? 0);
> const RAD_TO_DEG = 180 / Math.PI;
> const hdgMathDeg = (Math.abs(src) <= 6.5) ? (src * RAD_TO_DEG) % 360 : (src % 360);
> let heading_deg = (90 - hdgMathDeg) % 360; if (heading_deg < 0) heading_deg += 360;
> ```

---

## 🎯 Performance Speeds (95-104)

**Aircraft performance envelope and limitation speeds**

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 95 | `Performance.Speed.VS0` | Double | m/s | 10 - 100 | Stall speed landing config |
| 96 | `Performance.Speed.VS1` | Double | m/s | 10 - 100 | Stall speed clean config |
| 97 | `Performance.Speed.VFE` | Double | m/s | 20 - 150 | Max flap extended speed |
| 98 | `Performance.Speed.VNO` | Double | m/s | 50 - 200 | Normal operating speed |
| 99 | `Performance.Speed.VNE` | Double | m/s | 100 - 300 | Never exceed speed |
| 100 | `Performance.Speed.VAPP` | Double | m/s | 20 - 150 | Approach speed |
| 101 | `Performance.Speed.Minimum` | Double | m/s | 10 - 50 | Minimum flying speed |
| 102 | `Performance.Speed.Maximum` | Double | m/s | 100 - 300 | Maximum design speed |
| 103 | `Performance.Speed.MinimumFlapRetraction` | Double | m/s | 15 - 80 | Min speed flap retract |
| 104 | `Performance.Speed.MaximumFlapExtension` | Double | m/s | 30 - 120 | Max speed flap extend |

**Aircraft-specific examples:**
- **Cessna 172**: VS0 ≈ 25 m/s (48 kts), VNE ≈ 84 m/s (163 kts)
- **Airbus A320**: VS0 ≈ 51 m/s (99 kts), VNE ≈ 205 m/s (400 kts)

**Usage Example:**
```python
# Check if current airspeed is within safe envelope
current_ias = bridge.get_variable('Aircraft.IndicatedAirspeed')
vno = bridge.get_variable('Performance.Speed.VNO')
vne = bridge.get_variable('Performance.Speed.VNE')

if current_ias > vno:
    print("⚠️  Above normal operating speed")
if current_ias > vne:
    print("🚨 OVERSPEED! Above VNE")
```

---

## ⚙️ Configuration (105-106)

**Aircraft configuration settings**

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 105 | `Configuration.SelectedTakeOffFlaps` | Double | Position | 0.0 - 1.0 | Takeoff flap setting |
| 106 | `Configuration.SelectedLandingFlaps` | Double | Position | 0.0 - 1.0 | Landing flap setting |

**Typical flap positions:**
- **0.0**: Clean/retracted
- **0.33**: Flaps 1 or 10°
- **0.67**: Flaps 2 or 20°
- **1.0**: Full flaps (landing)

---

## 📋 Flight Management System (107)

**FMS and flight planning data**

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 107 | `FlightManagementSystem.FlightNumber` | String | - | - | Flight number identifier |

---

## 🧭 Navigation Systems (108-141)

**Complete radio navigation suite**

### NAV Radio Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 108 | `Navigation.SelectedCourse1` | Double | Degrees | 0 - 360 | NAV1 selected course |
| 109 | `Navigation.SelectedCourse2` | Double | Degrees | 0 - 360 | NAV2 selected course |
| 110 | `Navigation.NAV1Identifier` | String | - | - | NAV1 station ID |
| 111 | `Navigation.NAV1Frequency` | Double | MHz | 108.0 - 118.0 | NAV1 active frequency |
| 112 | `Navigation.NAV1StandbyFrequency` | Double | MHz | 108.0 - 118.0 | NAV1 standby frequency |
| 113 | `Navigation.NAV1FrequencySwap` | Double | Boolean | 0 or 1 | Swap NAV1 frequencies |
| 114 | `Navigation.NAV2Identifier` | String | - | - | NAV2 station ID |
| 115 | `Navigation.NAV2Frequency` | Double | MHz | 108.0 - 118.0 | NAV2 active frequency |
| 116 | `Navigation.NAV2StandbyFrequency` | Double | MHz | 108.0 - 118.0 | NAV2 standby frequency |
| 117 | `Navigation.NAV2FrequencySwap` | Double | Boolean | 0 or 1 | Swap NAV2 frequencies |

### DME Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 118 | `Navigation.DME1Frequency` | Double | MHz | 960 - 1215 | DME1 frequency |
| 119 | `Navigation.DME1Distance` | Double | NM | 0 - 200 | DME1 distance |
| 120 | `Navigation.DME1Speed` | Double | m/s | 0 - 999 | DME1 ground speed |
| 121 | `Navigation.DME1Time` | Double | Seconds | 0 - 5940 | DME1 time to station |
| 122 | `Navigation.DME2Frequency` | Double | MHz | 960 - 1215 | DME2 frequency |
| 123 | `Navigation.DME2Distance` | Double | NM | 0 - 200 | DME2 distance |
| 124 | `Navigation.DME2Speed` | Double | m/s | 0 - 999 | DME2 ground speed |
| 125 | `Navigation.DME2Time` | Double | Seconds | 0 - 5940 | DME2 time to station |

### ILS Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 126 | `Navigation.ILS1Frequency` | Double | MHz | 108.1 - 111.95 | ILS1 frequency |
| 127 | `Navigation.ILS1FrequencySwap` | Double | Boolean | 0 or 1 | Swap ILS1 frequency |
| 128 | `Navigation.ILS1Identifier` | String | - | - | ILS1 station ID |
| 129 | `Navigation.ILS2Frequency` | Double | MHz | 108.1 - 111.95 | ILS2 frequency |
| 130 | `Navigation.ILS2FrequencySwap` | Double | Boolean | 0 or 1 | Swap ILS2 frequency |
| 131 | `Navigation.ILS2Identifier` | String | - | - | ILS2 station ID |

### ADF Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 132 | `Navigation.ADF1Frequency` | Double | kHz | 190 - 1750 | ADF1 frequency |
| 133 | `Navigation.ADF1StandbyFrequency` | Double | kHz | 190 - 1750 | ADF1 standby frequency |
| 134 | `Navigation.ADF1FrequencySwap` | Double | Boolean | 0 or 1 | Swap ADF1 frequencies |
| 135 | `Navigation.ADF2Frequency` | Double | kHz | 190 - 1750 | ADF2 frequency |
| 136 | `Navigation.ADF2StandbyFrequency` | Double | kHz | 190 - 1750 | ADF2 standby frequency |
| 137 | `Navigation.ADF2FrequencySwap` | Double | Boolean | 0 or 1 | Swap ADF2 frequencies |

### Navigation Data

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 138 | `Navigation.NAV1Data` | Complex | - | - | NAV1 complete data |
| 139 | `Navigation.NAV2Data` | Complex | - | - | NAV2 complete data |
| 140 | `Navigation.ILS1Data` | Complex | - | - | ILS1 complete data |
| 141 | `Navigation.ILS2Data` | Complex | - | - | ILS2 complete data |

**Usage Example:**
```javascript
// Tune NAV1 to a VOR frequency
{
  "variable": "Navigation.NAV1StandbyFrequency", 
  "value": 114.8  // 114.80 MHz
}
// Then swap to active
{
  "variable": "Navigation.NAV1FrequencySwap",
  "value": 1
}
```

---

## 📻 Communication (142-152)

**COM radios and transponder systems**

### COM Radio Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 142 | `Communication.COM1Frequency` | Double | MHz | 118.0 - 137.0 | COM1 active frequency |
| 143 | `Communication.COM1StandbyFrequency` | Double | MHz | 118.0 - 137.0 | COM1 standby frequency |
| 144 | `Communication.COM1FrequencySwap` | Double | Boolean | 0 or 1 | Swap COM1 frequencies |
| 145 | `Communication.COM2Frequency` | Double | MHz | 118.0 - 137.0 | COM2 active frequency |
| 146 | `Communication.COM2StandbyFrequency` | Double | MHz | 118.0 - 137.0 | COM2 standby frequency |
| 147 | `Communication.COM2FrequencySwap` | Double | Boolean | 0 or 1 | Swap COM2 frequencies |
| 148 | `Communication.COM3Frequency` | Double | MHz | 118.0 - 137.0 | COM3 active frequency |
| 149 | `Communication.COM3StandbyFrequency` | Double | MHz | 118.0 - 137.0 | COM3 standby frequency |
| 150 | `Communication.COM3FrequencySwap` | Double | Boolean | 0 or 1 | Swap COM3 frequencies |

### Transponder

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 151 | `Communication.TransponderCode` | Double | Code | 0000 - 7777 | Squawk code |
| 152 | `Communication.TransponderCursor` | Double | Position | 0 - 3 | Code digit cursor |

**Common frequencies:**
- **Ground**: 121.6 - 121.9 MHz
- **Tower**: 118.1 - 120.9 MHz  
- **Approach**: 119.0 - 127.0 MHz
- **Emergency**: 121.5 MHz

---

## 🤖 Autopilot Systems (153-180)

**Complete autopilot and autothrottle systems**

### Basic Autopilot Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 153 | `Autopilot.Master` | Double | Boolean | 0 or 1 | Autopilot master switch |
| 154 | `Autopilot.Disengage` | Double | Boolean | 0 or 1 | Disengage autopilot |
| 155 | `Autopilot.Heading` | Double | Boolean | 0 or 1 | Heading hold mode |
| 156 | `Autopilot.VerticalSpeed` | Double | Boolean | 0 or 1 | V/S hold mode |
| 165 | `Autopilot.Engaged` | Double | Boolean | 0 or 1 | Autopilot engaged |

### Selected Values

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 157 | `Autopilot.SelectedSpeed` | Double | m/s | 20 - 300 | Selected airspeed |
| 158 | `Autopilot.SelectedAirspeed` | Double | m/s | 20 - 300 | Selected IAS |
| 159 | `Autopilot.SelectedHeading` | Double | Degrees | 0 - 360 | Selected heading |
| 160 | `Autopilot.SelectedAltitude` | Double | Meters | 0 - 15000 | Selected altitude |
| 161 | `Autopilot.SelectedVerticalSpeed` | Double | m/s | -25 - 25 | Selected V/S |
| 162 | `Autopilot.SelectedAltitudeScale` | Double | Scale | 0.1 - 1000 | Altitude increment |

### Mode Strings

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 163 | `Autopilot.ActiveLateralMode` | String | - | - | Active lateral mode |
| 164 | `Autopilot.ArmedLateralMode` | String | - | - | Armed lateral mode |
| 166 | `Autopilot.ActiveVerticalMode` | String | - | - | Active vertical mode |
| 167 | `Autopilot.ArmedVerticalMode` | String | - | - | Armed vertical mode |
| 168 | `Autopilot.ArmedApproachMode` | String | - | - | Armed approach mode |
| 169 | `Autopilot.ActiveAutoThrottleMode` | String | - | - | Active A/T mode |
| 170 | `Autopilot.ActiveCollectiveMode` | String | - | - | Active collective mode |
| 171 | `Autopilot.ArmedCollectiveMode` | String | - | - | Armed collective mode |
| 172 | `Autopilot.Type` | String | - | - | Autopilot system type |

### Advanced Settings

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 173 | `Autopilot.UseMachNumber` | Double | Boolean | 0 or 1 | Use Mach instead of IAS |
| 174 | `Autopilot.SpeedManaged` | Double | Boolean | 0 or 1 | Managed speed mode |
| 175 | `Autopilot.TargetAirspeed` | Double | m/s | 20 - 300 | Target airspeed |
| 176 | `Autopilot.Aileron` | Double | Position | -1.0 - 1.0 | AP aileron output |
| 177 | `Autopilot.Elevator` | Double | Position | -1.0 - 1.0 | AP elevator output |

### Autothrottle

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 178 | `AutoThrottle.Type` | String | - | - | A/T system type |
| 179 | `Autopilot.ThrottleEngaged` | Double | Boolean | 0 or 1 | A/T engaged |
| 180 | `Autopilot.ThrottleCommand` | Double | Position | 0.0 - 1.0 | A/T throttle command |

**Common autopilot modes:**
- **Lateral**: HDG, NAV, LOC, LAND
- **Vertical**: ALT, V/S, GLIDESLOPE, FLARE

**Usage Example:**
```python
# Engage autopilot with heading hold
commands = [
    {"variable": "Autopilot.SelectedHeading", "value": 90},  # 090°
    {"variable": "Autopilot.Heading", "value": 1},           # HDG mode
    {"variable": "Autopilot.Master", "value": 1}             # Engage
]
```

---

## 📐 Flight Director (181-183)

**Flight director command bars**

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 181 | `FlightDirector.Pitch` | Double | Radians | -π/2 - π/2 | FD pitch command |
| 182 | `FlightDirector.Bank` | Double | Radians | -π/2 - π/2 | FD bank command |
| 183 | `FlightDirector.Yaw` | Double | Radians | -π/4 - π/4 | FD yaw command |

---

## 👥 Copilot (184-191)

**Copilot-side controls and instruments**

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 184 | `Copilot.Heading` | Double | Degrees | 0 - 360 | Copilot heading bug |
| 185 | `Copilot.Altitude` | Double | Meters | 0 - 15000 | Copilot altitude select |
| 186 | `Copilot.Airspeed` | Double | m/s | 20 - 300 | Copilot airspeed select |
| 187 | `Copilot.VerticalSpeed` | Double | m/s | -25 - 25 | Copilot V/S select |
| 188 | `Copilot.Aileron` | Double | Position | -1.0 - 1.0 | Copilot aileron input |
| 189 | `Copilot.Elevator` | Double | Position | -1.0 - 1.0 | Copilot elevator input |
| 190 | `Copilot.Rudder` | Double | Position | -1.0 - 1.0 | Copilot rudder input |
| 191 | `Copilot.Throttle` | Double | Position | 0.0 - 1.0 | Copilot throttle |

---

## 🕹 Flight Controls (192-230)

**Primary flight controls and trim systems**

### Primary Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 192 | `Controls.Pitch.Input` | Double | Position | -1.0 - 1.0 | Elevator input |
| 193 | `Controls.Roll.Input` | Double | Position | -1.0 - 1.0 | Aileron input |
| 194 | `Controls.Yaw.Input` | Double | Position | -1.0 - 1.0 | Rudder input |
| 195 | `Controls.Collective.Input` | Double | Position | 0.0 - 1.0 | Collective (helicopters) |

### Trim Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 196 | `Controls.AileronTrim` | Double | Position | -1.0 - 1.0 | Aileron trim |
| 197 | `Controls.RudderTrim` | Double | Position | -1.0 - 1.0 | Rudder trim |
| 198 | `Aircraft.Trim` | Double | Position | -1.0 - 1.0 | General trim |
| 199 | `Aircraft.PitchTrim` | Double | Position | -1.0 - 1.0 | Elevator trim |
| 200 | `Aircraft.PitchTrimScaling` | Double | Scale | 0.1 - 10.0 | Trim sensitivity |
| 201 | `Aircraft.PitchTrimOffset` | Double | Position | -1.0 - 1.0 | Trim offset |
| 202 | `Aircraft.RudderTrim` | Double | Position | -1.0 - 1.0 | Rudder trim |

### Advanced Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 203 | `Controls.Tiller` | Double | Position | -1.0 - 1.0 | Nose wheel tiller |
| 204 | `Controls.NoseWheelSteering` | Double | Position | -1.0 - 1.0 | NWS input |
| 205 | `Controls.PedalsDisconnect` | Double | Boolean | 0 or 1 | Disconnect rudder pedals |
| 206 | `Controls.AutoPitchTrim` | Double | Boolean | 0 or 1 | Auto pitch trim |

### Throttle and Power

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 207 | `Controls.Throttle` | Double | Position | 0.0 - 1.0 | Primary throttle |
| 208 | `Controls.ThrottleLimit` | Double | Position | 0.0 - 1.0 | Throttle limit |
| 209 | `Aircraft.ThrottleLimit` | Double | Position | 0.0 - 1.0 | Aircraft throttle limit |
| 210 | `Aircraft.Reverse` | Double | Boolean | 0 or 1 | Reverse thrust |

### Glider-Specific

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 230 | `Controls.GliderAirBrake` | Double | Position | 0.0 - 1.0 | Glider air brake |

**Usage Example:**
```javascript
// Set elevator trim
{
  "variable": "Aircraft.PitchTrim",
  "value": 0.1  // Slight nose-up trim
}
```

---

## 🔥 Engine Systems (231-265)

**Complete engine management and control**

### Engine Start/Stop

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 231 | `Aircraft.Starter` | Double | Boolean | 0 or 1 | General starter |
| 232 | `Aircraft.Starter1` | Double | Boolean | 0 or 1 | Engine 1 starter |
| 233 | `Aircraft.Starter2` | Double | Boolean | 0 or 1 | Engine 2 starter |
| 234 | `Aircraft.Starter3` | Double | Boolean | 0 or 1 | Engine 3 starter |
| 235 | `Aircraft.Starter4` | Double | Boolean | 0 or 1 | Engine 4 starter |

### Ignition Systems

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 236 | `Aircraft.Ignition` | Double | Boolean | 0 or 1 | General ignition |
| 237 | `Aircraft.Ignition1` | Double | Boolean | 0 or 1 | Engine 1 ignition |
| 238 | `Aircraft.Ignition2` | Double | Boolean | 0 or 1 | Engine 2 ignition |
| 239 | `Aircraft.Ignition3` | Double | Boolean | 0 or 1 | Engine 3 ignition |
| 240 | `Aircraft.Ignition4` | Double | Boolean | 0 or 1 | Engine 4 ignition |

### Engine Parameters

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 241 | `Aircraft.EngineRotationSpeed1` | Double | RPM | 0 - 8000 | Engine 1 RPM |
| 242 | `Aircraft.EngineRotationSpeed2` | Double | RPM | 0 - 8000 | Engine 2 RPM |
| 243 | `Aircraft.EngineRotationSpeed3` | Double | RPM | 0 - 8000 | Engine 3 RPM |
| 244 | `Aircraft.EngineRotationSpeed4` | Double | RPM | 0 - 8000 | Engine 4 RPM |
| 245 | `Aircraft.EngineRunning1` | Double | Boolean | 0 or 1 | Engine 1 running |
| 246 | `Aircraft.EngineRunning2` | Double | Boolean | 0 or 1 | Engine 2 running |
| 247 | `Aircraft.EngineRunning3` | Double | Boolean | 0 or 1 | Engine 3 running |
| 248 | `Aircraft.EngineRunning4` | Double | Boolean | 0 or 1 | Engine 4 running |

### Engine Master Switches

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 249 | `Aircraft.EngineMaster1` | Double | Boolean | 0 or 1 | Engine 1 master |
| 250 | `Aircraft.EngineMaster2` | Double | Boolean | 0 or 1 | Engine 2 master |
| 251 | `Aircraft.EngineMaster3` | Double | Boolean | 0 or 1 | Engine 3 master |
| 252 | `Aircraft.EngineMaster4` | Double | Boolean | 0 or 1 | Engine 4 master |

### APU (Auxiliary Power Unit)

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 265 | `Aircraft.APUAvailable` | Double | Boolean | 0 or 1 | APU available |

**Engine start sequence example:**
```python
# Start Engine 1
commands = [
    {"variable": "Aircraft.EngineMaster1", "value": 1},    # Master ON
    {"variable": "Aircraft.Ignition1", "value": 1},       # Ignition ON  
    {"variable": "Aircraft.Starter1", "value": 1},        # Starter ON
    # Wait for engine start, then:
    {"variable": "Aircraft.Starter1", "value": 0},        # Starter OFF
]
```

---

## ⚠️ Warning Systems (266-275)

**Caution and warning system indicators**

### Master Warnings

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 266 | `Warnings.MasterWarning` | Double | Boolean | 0 or 1 | Master warning active |
| 267 | `Warnings.MasterCaution` | Double | Boolean | 0 or 1 | Master caution active |

### Engine Warnings

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 268 | `Warnings.LowOilPressure` | Double | Boolean | 0 or 1 | Low oil pressure |
| 269 | `Warnings.LowFuelPressure` | Double | Boolean | 0 or 1 | Low fuel pressure |
| 270 | `Warnings.EngineOverheat` | Double | Boolean | 0 or 1 | Engine overheat |
| 271 | `Warnings.EngineOverspeed` | Double | Boolean | 0 or 1 | Engine overspeed |
| 272 | `Warnings.EngineFire` | Double | Boolean | 0 or 1 | Engine fire |

### System Warnings

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 273 | `Warnings.LowFuel` | Double | Boolean | 0 or 1 | Low fuel quantity |
| 274 | `Warnings.GearWarning` | Double | Boolean | 0 or 1 | Landing gear warning |
| 275 | `Warnings.StallWarning` | Double | Boolean | 0 or 1 | Stall warning |

---

## 📹 View/Camera (276-295)

**Camera and view system controls**

### View Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 276 | `View.PilotHeadPosition` | Double | Position | -1.0 - 1.0 | Pilot head position |
| 277 | `View.PilotHeadRotation` | Double | Degrees | -180 - 180 | Head rotation |
| 278-295 | `View.*` | Various | Various | - | Additional view controls |

---

## 🎮 Simulation (296-315)

**Simulation environment and system controls**

### Environment Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 296 | `Simulation.Paused` | Double | Boolean | 0 or 1 | Simulation paused |
| 297 | `Simulation.Sound` | Double | Boolean | 0 or 1 | Sound enabled |
| 298 | `Simulation.LiftUp` | Double | Boolean | 0 or 1 | Lift aircraft up |
| 299 | `Simulation.FlightInformation` | Double | Boolean | 0 or 1 | Show flight info |
| 300 | `Simulation.MovingMap` | Double | Boolean | 0 or 1 | Moving map display |
| 301 | `Simulation.UseMouseControl` | Double | Boolean | 0 or 1 | Mouse control enabled |
| 302 | `Simulation.TimeChange` | Double | Multiplier | 0.1 - 100 | Time acceleration |
| 303 | `Simulation.Visibility` | Double | Meters | 100 - 50000 | Visibility distance |

### Playback Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 304 | `Simulation.PlaybackStart` | Double | Boolean | 0 or 1 | Start playback |
| 305 | `Simulation.PlaybackStop` | Double | Boolean | 0 or 1 | Stop playback |
| 306-315 | `Simulation.*` | Various | Various | - | Additional sim controls |

---

## ✈️ Cessna 172 Specific (316-338)

**Aircraft-specific controls for Cessna 172**

### Fuel System

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 316 | `Controls.FuelSelector` | Double | Position | 0 - 3 | Fuel selector valve |
| 317 | `Controls.FuelShutOff` | Double | Boolean | 0 or 1 | Fuel shut-off |

### Interior Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 318 | `Controls.HideYoke.Left` | Double | Boolean | 0 or 1 | Hide left yoke |
| 319 | `Controls.HideYoke.Right` | Double | Boolean | 0 or 1 | Hide right yoke |
| 320 | `Controls.LeftSunBlocker` | Double | Position | 0.0 - 1.0 | Left sun visor |
| 321 | `Controls.RightSunBlocker` | Double | Position | 0.0 - 1.0 | Right sun visor |

### Lighting

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 322 | `Controls.Lighting.LeftCabinOverheadLight` | Double | Boolean | 0 or 1 | Left cabin light |
| 323 | `Controls.Lighting.RightCabinOverheadLight` | Double | Boolean | 0 or 1 | Right cabin light |

### Engine Controls

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 324 | `Controls.Magnetos1` | Double | Position | 0 - 3 | Magneto switch |
| 325 | `Controls.ParkingBrakeHandle` | Double | Position | 0.0 - 1.0 | Parking brake handle |
| 326 | `Controls.TrimWheel` | Double | Position | -1.0 - 1.0 | Trim wheel |
| 327 | `LeftYoke.Button` | Double | Boolean | 0 or 1 | Left yoke button |

### Doors and Windows

| Index | Variable Name | Type | Unit | Range | Description |
|-------|---------------|------|------|-------|-------------|
| 328 | `Doors.Left` | Double | Position | 0.0 - 1.0 | Left door position |
| 329 | `Doors.LeftHandle` | Double | Position | 0.0 - 1.0 | Left door handle |
| 330 | `Doors.Right` | Double | Position | 0.0 - 1.0 | Right door position |
| 331 | `Doors.RightHandle` | Double | Position | 0.0 - 1.0 | Right door handle |
| 332 | `Windows.Left` | Double | Position | 0.0 - 1.0 | Left window position |
| 333 | `Windows.Right` | Double | Position | 0.0 - 1.0 | Right window position |

**Cessna 172 startup example:**
```javascript
// Basic C172 startup sequence
const c172_startup = [
  {"variable": "Controls.FuelSelector", "value": 2},        // BOTH tanks
  {"variable": "Controls.Magnetos1", "value": 2},           // BOTH magnetos
  {"variable": "Aircraft.EngineMaster1", "value": 1},       // Master ON
  {"variable": "Aircraft.Starter1", "value": 1},            // Start engine
];
```

---

## 🔧 Data Types and Access Methods

### Variable Data Types

| Type | Description | Example | Access Method |
|------|-------------|---------|---------------|
| **Double** | Floating point number | `3500.5` | Direct value |
| **Boolean** | 0 or 1 value | `1` (true) | Check for 1 |
| **String** | Text identifier | `"ILS28L"` | String field |
| **Vector2d** | Lat/Lon coordinates | `[47.6, -122.3]` | Coordinate pair |
| **Vector3d** | 3D position/velocity | `[x, y, z]` | 3-component vector |

### Access Examples

**Shared Memory (C++):**
```cpp
double altitude = pData->aircraft_altitude;
bool onGround = (pData->aircraft_on_ground == 1.0);
const char* navId = pData->navigation_nav1_identifier;
```

**TCP/WebSocket (JSON):**
```javascript
const altitude_m = data.variables["Aircraft.Altitude"];
const onGround = (data.variables["Aircraft.OnGround"] === 1);
const throttle = data.variables["Controls.Throttle"];
```

**Python via offsets:**
```python
altitude = bridge.get_variable('Aircraft.Altitude')
onGround = bridge.get_variable('Aircraft.OnGround') == 1
throttle = bridge.get_variable('Controls.Throttle')
```

---

## 📊 Variable Categories Summary

| Category | Count | Read/Write | Primary Use Cases |
|----------|-------|------------|-------------------|
| **Aircraft** | 95 | Both | Flight displays, physics simulation |
| **Performance** | 10 | Read | Speed bugs, envelope protection |
| **Navigation** | 34 | Both | Radio management, course setting |
| **Communication** | 11 | Both | Frequency management, ATC |
| **Autopilot** | 28 | Both | Automated flight control |
| **Controls** | 39 | Both | Flight control surfaces, trim |
| **Engines** | 35 | Both | Engine management, start/stop |
| **Warnings** | 10 | Read | Alert systems, safety monitoring |
| **Simulation** | 20 | Both | Environment control, replay |
| **C172 Specific** | 23 | Both | Aircraft-specific features |

---

## 🚀 Quick Access Reference

**Most commonly used variables:**

```javascript
// Essential flight data (by canonical name - recommended)
const essentials = {
  altitude: data.variables["Aircraft.Altitude"],
  airspeed: data.variables["Aircraft.IndicatedAirspeed"],
  heading: data.variables["Aircraft.TrueHeading"],
  pitch: data.variables["Aircraft.Pitch"],
  bank: data.variables["Aircraft.Bank"],
  throttle: data.variables["Controls.Throttle"],
  flaps: data.variables["Aircraft.Flaps"],
  gear: data.variables["Aircraft.Gear"],
  onGround: data.variables["Aircraft.OnGround"],
};
```

**Alternative access by variable name:**
```python
# Python example using canonical names
essentials = {
    'altitude': bridge.get_variable('Aircraft.Altitude'),
    'airspeed': bridge.get_variable('Aircraft.IndicatedAirspeed'),
    'heading': bridge.get_variable('Aircraft.TrueHeading'),
    'pitch': bridge.get_variable('Aircraft.Pitch'),
    'bank': bridge.get_variable('Aircraft.Bank'),
    'throttle': bridge.get_variable('Controls.Throttle'),
    'flaps': bridge.get_variable('Aircraft.Flaps'),
    'gear': bridge.get_variable('Aircraft.Gear'),
    'onGround': bridge.get_variable('Aircraft.OnGround'),
}
```

**Most commonly controlled variables:**
- `Controls.Throttle` - Primary throttle control
- `Controls.Pitch.Input` - Elevator input
- `Controls.Roll.Input` - Aileron input
- `Controls.Yaw.Input` - Rudder input
- `Aircraft.Flaps` - Flap position
- `Aircraft.Gear` - Landing gear position

---

<!-- Removed cross-document links to avoid broken references in public repo -->