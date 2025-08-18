///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Aerofly FS4 Bridge DLL - Multi-Interface Implementation
 *
 * Provides three simultaneous interfaces for Aerofly FS4:
 * - Shared Memory: Direct memory access for high-performance apps
 * - TCP Server: Network access with JSON (ports 12345/12346)
 * - WebSocket Server: Modern web/mobile support (port 8765)
 *
 * This is a community open-source project to enable flight simulation tools
 * across desktop, web and mobile clients with a consistent JSON protocol.
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <cmath>
#include <memory>
#include <atomic>
#include <queue>
#include <array>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

// Minimal debug logging macro: disabled in NDEBUG builds
#if defined(NDEBUG)
#define DBG(msg) do {} while(0)
#else
#define DBG(msg) OutputDebugStringA(msg)
#endif

// High-resolution timestamp in microseconds using QPC
static inline uint64_t get_time_us() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>((now.QuadPart * 1000000ULL) / frequency.QuadPart);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CONFIGURATION & THREADING OVERVIEW
///////////////////////////////////////////////////////////////////////////////////////////////////
/*
Environment Variables (optional):
  - AEROFLY_BRIDGE_WS_ENABLE : Enable WebSocket server (default: 1)
  - AEROFLY_BRIDGE_WS_PORT   : WebSocket port (default: 8765)
  - AEROFLY_BRIDGE_BROADCAST_MS : Telemetry broadcast interval in ms (default: 20)

Threading Model:
  - Aerofly main thread calls the exported Update() periodically.
  - TCPServerInterface spawns:
      * server_thread   : accepts client connections on data port
      * command_thread  : accepts one-off connections on command port to receive JSON
  - WebSocketServerInterface spawns:
      * server_thread   : accepts WS clients, reads frames, broadcasts telemetry
  - SharedMemoryInterface is accessed from the Aerofly main thread; protects its data with a mutex.

Data Flow:
  Aerofly SDK → Update(received_messages) → shared memory ←→ BuildDataJSON()
                                            ↘ TCP/WS Broadcast(JSON) → Clients
  Clients(JSON commands) → TCP/WS queues → CommandProcessor → messages → Update(sent_messages)
*/

// Forward declaration: shared JSON builder used by TCP and WebSocket
struct AeroflyBridgeData; // forward declare struct type for pointer usage
static std::string BuildDataJSON(const AeroflyBridgeData* data);

// Get directory path of this DLL module
static std::string GetThisModuleDirectory() {
    HMODULE hModule = nullptr;
    // Use the address of this function to obtain the module handle
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetThisModuleDirectory), &hModule)) {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(hModule, path, MAX_PATH) > 0) {
            std::string full(path);
            size_t pos = full.find_last_of("\\/");
            if (pos != std::string::npos) {
                return full.substr(0, pos);
            }
            return full; // fallback: full path
        }
    }
    return std::string(".");
}


// SAFE VARIABLE INDEX ENUM - Automatic Size Verification
// Define maximum capacity (can be adjusted as needed)
constexpr size_t MAX_VARIABLES = 400;  // Extra room for future expansion

#include "tm_external_message.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// COMPLETE AEROFLYBRIDGE DATA STRUCTURE - ALL 339+ VARIABLES
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Complete data structure following exact VariableIndex enum order
 * 
 * This structure provides direct access to all 339+ variables in the same order 
 * as defined in MESSAGE_LIST from the official Aerofly SDK.
 * Memory layout is optimized for performance and maintainability.
 */
struct AeroflyBridgeData {
    // ========================================================================
    // HEADER SECTION (16 bytes)
    // ========================================================================
    uint64_t timestamp_us;              // Microseconds since start
    uint32_t data_valid;               // 1 = valid data, 0 = invalid
    uint32_t update_counter;           // Increments each update
    uint32_t reserved_header;          // Padding for alignment

    // ========================================================================
    // AIRCRAFT CORE VARIABLES (0-94) - Following exact VariableIndex order
    // ========================================================================
    
    // AIRCRAFT BASIC STATE (0-11)
    double aircraft_universal_time;     // 0:  Aircraft.UniversalTime
    double aircraft_altitude;           // 1:  Aircraft.Altitude  
    double aircraft_vertical_speed;     // 2:  Aircraft.VerticalSpeed
    double aircraft_pitch;              // 3:  Aircraft.Pitch
    double aircraft_bank;               // 4:  Aircraft.Bank
    double aircraft_indicated_airspeed; // 5:  Aircraft.IndicatedAirspeed
    double aircraft_indicated_airspeed_trend; // 6: Aircraft.IndicatedAirspeedTrend
    double aircraft_ground_speed;       // 7:  Aircraft.GroundSpeed
    double aircraft_magnetic_heading;   // 8:  Aircraft.MagneticHeading
    double aircraft_true_heading;       // 9:  Aircraft.TrueHeading
    double aircraft_latitude;           // 10: Aircraft.Latitude
    double aircraft_longitude;          // 11: Aircraft.Longitude

    // AIRCRAFT POSITION & PHYSICS (12-24)
    double aircraft_height;             // 12: Aircraft.Height
    tm_vector3d aircraft_position;      // 13: Aircraft.Position
    double aircraft_orientation;        // 14: Aircraft.Orientation
    tm_vector3d aircraft_velocity;      // 15: Aircraft.Velocity
    tm_vector3d aircraft_angular_velocity; // 16: Aircraft.AngularVelocity
    tm_vector3d aircraft_acceleration;  // 17: Aircraft.Acceleration
    tm_vector3d aircraft_gravity;       // 18: Aircraft.Gravity
    tm_vector3d aircraft_wind;          // 19: Aircraft.Wind
    double aircraft_rate_of_turn;       // 20: Aircraft.RateOfTurn
    double aircraft_mach_number;        // 21: Aircraft.MachNumber
    double aircraft_angle_of_attack;    // 22: Aircraft.AngleOfAttack
    double aircraft_angle_of_attack_limit; // 23: Aircraft.AngleOfAttackLimit
    double aircraft_acceleration_limit; // 24: Aircraft.AccelerationLimit

    // AIRCRAFT SYSTEMS (25-35)
    double aircraft_gear;               // 25: Aircraft.Gear
    double aircraft_flaps;              // 26: Aircraft.Flaps
    double aircraft_slats;              // 27: Aircraft.Slats
    double aircraft_throttle;           // 28: Aircraft.Throttle
    double aircraft_air_brake;          // 29: Aircraft.AirBrake
    double aircraft_ground_spoilers_armed; // 30: Aircraft.GroundSpoilersArmed
    double aircraft_ground_spoilers_extended; // 31: Aircraft.GroundSpoilersExtended
    double aircraft_parking_brake;      // 32: Aircraft.ParkingBrake
    double aircraft_auto_brake_setting; // 33: Aircraft.AutoBrakeSetting
    double aircraft_auto_brake_engaged; // 34: Aircraft.AutoBrakeEngaged
    double aircraft_auto_brake_rejected_takeoff; // 35: Aircraft.AutoBrakeRejectedTakeOff

    // AIRCRAFT SENSORS & IDENTIFICATION (36-46)
    double aircraft_radar_altitude;     // 36: Aircraft.RadarAltitude
    // 37: Aircraft.Name (String) - stored as char array below
    // 38: Aircraft.NearestAirportIdentifier (String) - stored as char array below
    // 39: Aircraft.NearestAirportName (String) - stored as char array below
    tm_vector2d aircraft_nearest_airport_location; // 40: Aircraft.NearestAirportLocation
    double aircraft_nearest_airport_elevation; // 41: Aircraft.NearestAirportElevation
    // 42: Aircraft.BestAirportIdentifier (String) - stored as char array below
    // 43: Aircraft.BestAirportName (String) - stored as char array below
    tm_vector2d aircraft_best_airport_location; // 44: Aircraft.BestAirportLocation
    double aircraft_best_airport_elevation; // 45: Aircraft.BestAirportElevation
    // 46: Aircraft.BestRunwayIdentifier (String) - stored as char array below
    double aircraft_best_runway_elevation; // 47: Aircraft.BestRunwayElevation
    tm_vector3d aircraft_best_runway_threshold; // 48: Aircraft.BestRunwayThreshold
    tm_vector3d aircraft_best_runway_end; // 49: Aircraft.BestRunwayEnd

    // AIRCRAFT CATEGORIES & STATUS (50-55)
    double aircraft_category_jet;       // 50: Aircraft.Category.Jet
    double aircraft_category_glider;    // 51: Aircraft.Category.Glider
    double aircraft_on_ground;          // 52: Aircraft.OnGround
    double aircraft_on_runway;          // 53: Aircraft.OnRunway
    double aircraft_crashed;            // 54: Aircraft.Crashed
    double aircraft_power;              // 55: Aircraft.Power

    // AIRCRAFT POWER & TRIM (56-66)
    double aircraft_normalized_power;   // 56: Aircraft.NormalizedPower
    double aircraft_normalized_power_target; // 57: Aircraft.NormalizedPowerTarget
    double aircraft_trim;               // 58: Aircraft.Trim
    double aircraft_pitch_trim;         // 59: Aircraft.PitchTrim
    double aircraft_pitch_trim_scaling; // 60: Aircraft.PitchTrimScaling
    double aircraft_pitch_trim_offset;  // 61: Aircraft.PitchTrimOffset
    double aircraft_rudder_trim;        // 62: Aircraft.RudderTrim
    double aircraft_auto_pitch_trim;    // 63: Aircraft.AutoPitchTrim
    double aircraft_yaw_damper_enabled; // 64: Aircraft.YawDamperEnabled
    double aircraft_rudder_pedals_disconnected; // 65: Aircraft.RudderPedalsDisconnected

    // ENGINE SYSTEMS (66-82)
    double aircraft_starter;            // 66: Aircraft.Starter
    double aircraft_starter_1;          // 67: Aircraft.Starter1
    double aircraft_starter_2;          // 68: Aircraft.Starter2
    double aircraft_starter_3;          // 69: Aircraft.Starter3
    double aircraft_starter_4;          // 70: Aircraft.Starter4
    double aircraft_ignition;           // 71: Aircraft.Ignition
    double aircraft_ignition_1;         // 72: Aircraft.Ignition1
    double aircraft_ignition_2;         // 73: Aircraft.Ignition2
    double aircraft_ignition_3;         // 74: Aircraft.Ignition3
    double aircraft_ignition_4;         // 75: Aircraft.Ignition4
    double aircraft_throttle_limit;     // 76: Aircraft.ThrottleLimit
    double aircraft_reverse;            // 77: Aircraft.Reverse
    double aircraft_engine_master_1;    // 78: Aircraft.EngineMaster1
    double aircraft_engine_master_2;    // 79: Aircraft.EngineMaster2
    double aircraft_engine_master_3;    // 80: Aircraft.EngineMaster3
    double aircraft_engine_master_4;    // 81: Aircraft.EngineMaster4
    double aircraft_engine_throttle_1;  // 82: Aircraft.EngineThrottle1

    // ENGINE PERFORMANCE (83-94)
    double aircraft_engine_throttle_2;  // 83: Aircraft.EngineThrottle2
    double aircraft_engine_throttle_3;  // 84: Aircraft.EngineThrottle3
    double aircraft_engine_throttle_4;  // 85: Aircraft.EngineThrottle4
    double aircraft_engine_rotation_speed_1; // 86: Aircraft.EngineRotationSpeed1
    double aircraft_engine_rotation_speed_2; // 87: Aircraft.EngineRotationSpeed2
    double aircraft_engine_rotation_speed_3; // 88: Aircraft.EngineRotationSpeed3
    double aircraft_engine_rotation_speed_4; // 89: Aircraft.EngineRotationSpeed4
    double aircraft_engine_running_1;   // 90: Aircraft.EngineRunning1
    double aircraft_engine_running_2;   // 91: Aircraft.EngineRunning2
    double aircraft_engine_running_3;   // 92: Aircraft.EngineRunning3
    double aircraft_engine_running_4;   // 93: Aircraft.EngineRunning4
    double aircraft_apu_available;      // 94: Aircraft.APUAvailable

    // ========================================================================
    // PERFORMANCE SPEEDS (95-104)
    // ========================================================================
    double performance_speed_vs0;       // 95:  Performance.Speed.VS0
    double performance_speed_vs1;       // 96:  Performance.Speed.VS1
    double performance_speed_vfe;       // 97:  Performance.Speed.VFE
    double performance_speed_vno;       // 98:  Performance.Speed.VNO
    double performance_speed_vne;       // 99:  Performance.Speed.VNE
    double performance_speed_vapp;      // 100: Performance.Speed.VAPP
    double performance_speed_minimum;   // 101: Performance.Speed.Minimum
    double performance_speed_maximum;   // 102: Performance.Speed.Maximum
    double performance_speed_minimum_flap_retraction; // 103: Performance.Speed.MinimumFlapRetraction
    double performance_speed_maximum_flap_extension;  // 104: Performance.Speed.MaximumFlapExtension

    // ========================================================================
    // CONFIGURATION (105-106)
    // ========================================================================
    double configuration_selected_takeoff_flaps;  // 105: Configuration.SelectedTakeOffFlaps
    double configuration_selected_landing_flaps;  // 106: Configuration.SelectedLandingFlaps

    // ========================================================================
    // FLIGHT MANAGEMENT SYSTEM (107)
    // ========================================================================
    // 107: FlightManagementSystem.FlightNumber (String) - stored as char array below

    // ========================================================================
    // NAVIGATION (108-141)
    // ========================================================================
    double navigation_selected_course_1; // 108: Navigation.SelectedCourse1
    double navigation_selected_course_2; // 109: Navigation.SelectedCourse2
    // 110: Navigation.NAV1Identifier (String) - stored as char array below
    double navigation_nav1_frequency;   // 111: Navigation.NAV1Frequency
    double navigation_nav1_standby_frequency; // 112: Navigation.NAV1StandbyFrequency
    double navigation_nav1_frequency_swap; // 113: Navigation.NAV1FrequencySwap
    // 114: Navigation.NAV2Identifier (String) - stored as char array below
    double navigation_nav2_frequency;   // 115: Navigation.NAV2Frequency
    double navigation_nav2_standby_frequency; // 116: Navigation.NAV2StandbyFrequency
    double navigation_nav2_frequency_swap; // 117: Navigation.NAV2FrequencySwap
    double navigation_dme1_frequency;   // 118: Navigation.DME1Frequency
    double navigation_dme1_distance;    // 119: Navigation.DME1Distance
    double navigation_dme1_time;        // 120: Navigation.DME1Time
    double navigation_dme1_speed;       // 121: Navigation.DME1Speed
    double navigation_dme2_frequency;   // 122: Navigation.DME2Frequency
    double navigation_dme2_distance;    // 123: Navigation.DME2Distance
    double navigation_dme2_time;        // 124: Navigation.DME2Time
    double navigation_dme2_speed;       // 125: Navigation.DME2Speed
    // 126: Navigation.ILS1Identifier (String) - stored as char array below
    double navigation_ils1_course;      // 127: Navigation.ILS1Course
    double navigation_ils1_frequency;   // 128: Navigation.ILS1Frequency
    double navigation_ils1_standby_frequency; // 129: Navigation.ILS1StandbyFrequency
    double navigation_ils1_frequency_swap; // 130: Navigation.ILS1FrequencySwap
    // 131: Navigation.ILS2Identifier (String) - stored as char array below
    double navigation_ils2_course;      // 132: Navigation.ILS2Course
    double navigation_ils2_frequency;   // 133: Navigation.ILS2Frequency
    double navigation_ils2_standby_frequency; // 134: Navigation.ILS2StandbyFrequency
    double navigation_ils2_frequency_swap; // 135: Navigation.ILS2FrequencySwap
    double navigation_adf1_frequency;   // 136: Navigation.ADF1Frequency
    double navigation_adf1_standby_frequency; // 137: Navigation.ADF1StandbyFrequency
    double navigation_adf1_frequency_swap; // 138: Navigation.ADF1FrequencySwap
    double navigation_adf2_frequency;   // 139: Navigation.ADF2Frequency
    double navigation_adf2_standby_frequency; // 140: Navigation.ADF2StandbyFrequency
    double navigation_adf2_frequency_swap; // 141: Navigation.ADF2FrequencySwap

    // ========================================================================
    // COMMUNICATION
    // ========================================================================
    double communication_com1_frequency; // Communication.COM1Frequency
    double communication_com1_standby_frequency; // Communication.COM1StandbyFrequency
    double communication_com1_frequency_swap; // Communication.COM1FrequencySwap
    double communication_com2_frequency; // Communication.COM2Frequency
    double communication_com2_standby_frequency; // Communication.COM2StandbyFrequency
    double communication_com2_frequency_swap; // Communication.COM2FrequencySwap
    double communication_com3_frequency; // Communication.COM3Frequency
    double communication_com3_standby_frequency; // Communication.COM3StandbyFrequency
    double communication_com3_frequency_swap; // Communication.COM3FrequencySwap

    // ========================================================================
    // CONTROLS (142-200+) - CRITICAL SECTION PREVIOUSLY MISSING
    // ========================================================================
    
    // BASIC CONTROLS
    double controls_throttle;           // Controls.Throttle
    double controls_throttle_1;         // Controls.Throttle1
    double controls_throttle_2;         // Controls.Throttle2
    double controls_throttle_3;         // Controls.Throttle3
    double controls_throttle_4;         // Controls.Throttle4
    double controls_gear;               // Controls.Gear
    double controls_flaps;              // Controls.Flaps
    double controls_slats;              // Controls.Slats
    double controls_airbrake;           // Controls.AirBrake
    double controls_ground_spoilers;    // Controls.GroundSpoilers
    double controls_pitch_input;        // Controls.Pitch.Input
    double controls_roll_input;         // Controls.Roll.Input
    double controls_yaw_input;          // Controls.Yaw.Input
    double controls_collective;         // Controls.Collective
    double controls_rudder;             // Controls.Rudder
    
    // BRAKE CONTROLS
    double controls_brake_left;         // Controls.BrakeLeft
    double controls_brake_right;        // Controls.BrakeRight
    double controls_brake_parking;      // Controls.BrakeParkingSet
    
    // PRESSURE SETTINGS
    double controls_pressure_setting_0; // Controls.PressureSetting0
    double controls_pressure_setting_standard_0; // Controls.PressureSettingStandard0
    double controls_pressure_setting_unit_0; // Controls.PressureSettingUnit0
    double controls_pressure_setting_1; // Controls.PressureSetting1
    double controls_pressure_setting_standard_1; // Controls.PressureSettingStandard1
    double controls_pressure_setting_unit_1; // Controls.PressureSettingUnit1
    double controls_pressure_setting_2; // Controls.PressureSetting2
    double controls_pressure_setting_standard_2; // Controls.PressureSettingStandard2
    double controls_pressure_setting_unit_2; // Controls.PressureSettingUnit2
    
    // ADVANCED CONTROLS
    double controls_transition_altitude; // Controls.TransitionAltitude
    double controls_transition_level;   // Controls.TransitionLevel
    double controls_rotor_brake;        // Controls.RotorBrake
    double controls_speed;              // Controls.Speed

    // ========================================================================
    // AUTOPILOT (153-183)
    // ========================================================================
    
    // BASIC AUTOPILOT
    double autopilot_master;            // 153: Autopilot.Master
    double autopilot_disengage;         // 154: Autopilot.Disengage
    double autopilot_heading;           // 155: Autopilot.Heading
    double autopilot_vertical_speed;    // 156: Autopilot.VerticalSpeed
    
    // SELECTED VALUES
    double autopilot_selected_speed;    // 157: Autopilot.SelectedSpeed
    double autopilot_selected_airspeed; // 158: Autopilot.SelectedAirspeed
    double autopilot_selected_heading;  // 159: Autopilot.SelectedHeading
    double autopilot_selected_altitude; // 160: Autopilot.SelectedAltitude
    double autopilot_selected_vertical_speed; // 161: Autopilot.SelectedVerticalSpeed
    double autopilot_selected_altitude_scale; // 162: Autopilot.SelectedAltitudeScale
    
    // MODE STATUS - strings will be stored as char arrays later
    // 163: Autopilot.ActiveLateralMode (String)
    // 164: Autopilot.ArmedLateralMode (String)
    // 165: Autopilot.ActiveVerticalMode (String)
    // 166: Autopilot.ArmedVerticalMode (String)
    // 167: Autopilot.ArmedApproachMode (String)
    // 168: Autopilot.ActiveAutoThrottleMode (String)
    // 169: Autopilot.ActiveCollectiveMode (String)
    // 170: Autopilot.ArmedCollectiveMode (String)
    
    // SYSTEM CONFIGURATION
    // 171: Autopilot.Type (String)
    double autopilot_engaged;           // 172: Autopilot.Engaged
    double autopilot_use_mach_number;   // 173: Autopilot.UseMachNumber
    double autopilot_speed_managed;     // 174: Autopilot.SpeedManaged
    double autopilot_target_airspeed;   // 175: Autopilot.TargetAirspeed
    
    // CONTROL SURFACE COMMANDS
    double autopilot_aileron;           // 176: Autopilot.Aileron
    double autopilot_elevator;          // 177: Autopilot.Elevator
    
    // AUTO-THROTTLE SYSTEM
    double auto_throttle_type;          // 178: AutoThrottle.Type
    double autopilot_throttle_engaged;  // 179: Autopilot.ThrottleEngaged
    double autopilot_throttle_command;  // 180: Autopilot.ThrottleCommand

    // ========================================================================
    // FLIGHT DIRECTOR (181-183)
    // ========================================================================
    double flight_director_pitch;       // 181: FlightDirector.Pitch
    double flight_director_bank;        // 182: FlightDirector.Bank
    double flight_director_yaw;         // 183: FlightDirector.Yaw

    // ========================================================================
    // COPILOT SYSTEM (184-191)
    // ========================================================================
    double copilot_heading;             // 184: Copilot.Heading
    double copilot_altitude;            // 185: Copilot.Altitude
    double copilot_airspeed;            // 186: Copilot.Airspeed
    double copilot_vertical_speed;      // 187: Copilot.VerticalSpeed
    double copilot_aileron;             // 188: Copilot.Aileron
    double copilot_elevator;            // 189: Copilot.Elevator
    double copilot_throttle;            // 190: Copilot.Throttle
    double copilot_auto_rudder;         // 191: Copilot.AutoRudder

    // ========================================================================
    // WARNINGS (200+ variables - VERY LARGE SECTION)
    // ========================================================================
    double warnings_master_warning;     // Warnings.MasterWarning
    double warnings_master_caution;     // Warnings.MasterCaution
    double warnings_engine_fire;        // Warnings.EngineFire
    double warnings_low_oil_pressure;   // Warnings.LowOilPressure
    double warnings_low_fuel_pressure;  // Warnings.LowFuelPressure
    double warnings_low_hydraulic_pressure; // Warnings.LowHydraulicPressure
    double warnings_low_voltage;        // Warnings.LowVoltage
    double warnings_altitude_alert;     // Warnings.AltitudeAlert
    double warnings_warning_active;     // Warnings.WarningActive
    double warnings_warning_mute;       // Warnings.WarningMute

    // ========================================================================
    // PRESSURIZATION (280+)
    // ========================================================================
    double pressurization_landing_elevation; // Pressurization.LandingElevation
    double pressurization_landing_elevation_manual; // Pressurization.LandingElevationManual

    // ========================================================================
    // ENVIRONMENT & SIMULATION
    // ========================================================================
    double environment_wind_velocity_x;  // Environment variables
    double environment_wind_velocity_y;
    double environment_wind_velocity_z;
    double simulation_pause;             // Simulation.Pause
    double simulation_sound;             // Simulation.Sound
    double simulation_lift_up;           // Simulation.LiftUp
    double simulation_flight_information; // Simulation.FlightInformation
    double simulation_moving_map;        // Simulation.MovingMap
    double simulation_use_mouse_control; // Simulation.UseMouseControl
    double simulation_time_change;       // Simulation.TimeChange
    double simulation_visibility;        // Simulation.Visibility
    double simulation_playback_start;    // Simulation.PlaybackStart
    double simulation_playback_stop;     // Simulation.PlaybackStop

    // ========================================================================
    // VIEW CONTROLS
    // ========================================================================
    double view_internal;               // View.Internal
    double view_chase;                   // View.Chase
    double view_external;                // View.External
    double view_instrument;              // View.Instrument
    double view_satelite;                // View.Satelite
    double view_tower;                   // View.Tower

    // COMMAND CONTROLS
    double command_pause;                // Command.Pause
    double command_screenshot;           // Command.Screenshot
    double command_up;                   // Command.Up
    double command_down;                 // Command.Down
    double command_left;                 // Command.Left
    double command_right;                // Command.Right
    double command_move_horizontal;      // Command.MoveHorizontal
    double command_move_vertical;        // Command.MoveVertical
    double command_rotate;               // Command.Rotate
    double command_zoom;                 // Command.Zoom

    // ========================================================================
    // AIRCRAFT-SPECIFIC CONTROLS (C172)
    // ========================================================================
    double c172_fuel_selector;           // Controls.FuelSelector
    double c172_fuel_shut_off;           // Controls.FuelShutOff
    double c172_hide_yoke_left;          // Controls.HideYoke.Left
    double c172_hide_yoke_right;         // Controls.HideYoke.Right
    double c172_left_sun_blocker;        // Controls.LeftSunBlocker
    double c172_right_sun_blocker;       // Controls.RightSunBlocker
    double c172_left_cabin_light;        // Controls.Lighting.LeftCabinOverheadLight
    double c172_right_cabin_light;       // Controls.Lighting.RightCabinOverheadLight
    double c172_magnetos_1;              // Controls.Magnetos1
    double c172_parking_brake_handle;    // Controls.ParkingBrake handle
    double c172_trim_wheel;              // Controls.Trim wheel
    double c172_left_yoke_button;        // LeftYoke.Button
    double c172_left_door;               // Doors.Left
    double c172_left_door_handle;        // Doors.LeftHandle
    double c172_right_door;              // Doors.Right
    double c172_right_door_handle;       // Doors.RightHandle
    double c172_left_window;             // Windows.Left
    double c172_right_window;            // Windows.Right

    // ========================================================================
    // ALL VARIABLES ARRAY (for complete access)
    // ========================================================================
    double all_variables[MAX_VARIABLES]; // Complete array access

    // ========================================================================
    // STRING VARIABLES (at the end to avoid alignment issues)
    // ========================================================================
    
    // AIRCRAFT STRINGS (ordered by size - largest first)
    char aircraft_nearest_airport_name[64];      // Aircraft.NearestAirportName
    char aircraft_best_airport_name[64];         // Aircraft.BestAirportName
    char aircraft_name[32];                      // Aircraft.Name
    char autopilot_type[32];                     // Autopilot.Type
    
    // AUTOPILOT MODE STRINGS (16 bytes each)
    char autopilot_active_lateral_mode[16];      // Autopilot.ActiveLateralMode
    char autopilot_armed_lateral_mode[16];       // Autopilot.ArmedLateralMode
    char autopilot_active_vertical_mode[16];     // Autopilot.ActiveVerticalMode
    char autopilot_armed_vertical_mode[16];      // Autopilot.ArmedVerticalMode
    char autopilot_armed_approach_mode[16];      // Autopilot.ArmedApproachMode
    char autopilot_active_autothrottle_mode[16]; // Autopilot.ActiveAutoThrottleMode
    char autopilot_active_collective_mode[16];   // Autopilot.ActiveCollectiveMode
    char autopilot_armed_collective_mode[16];    // Autopilot.ArmedCollectiveMode
    char fms_flight_number[16];                  // FlightManagementSystem.FlightNumber
    
    // NAVIGATION & AIRPORT IDs (8 bytes each)
    char aircraft_nearest_airport_id[8];         // Aircraft.NearestAirportIdentifier
    char aircraft_best_airport_id[8];            // Aircraft.BestAirportIdentifier
    char aircraft_best_runway_id[8];             // Aircraft.BestRunwayIdentifier
    char navigation_nav1_identifier[8];          // Navigation.NAV1Identifier
    char navigation_nav2_identifier[8];          // Navigation.NAV2Identifier
    char navigation_ils1_identifier[8];          // Navigation.ILS1Identifier
    char navigation_ils2_identifier[8];          // Navigation.ILS2Identifier

    // NOTE: Total estimated size ~4000+ bytes
    // Complete coverage of all Aerofly FS4 variables with optimal memory layout
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// COMPLETE VARIABLE INDEX ENUM
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Complete index of all available simulation variables.
 * 
 * This enumeration provides a comprehensive list of all 339 variables that can be accessed
 * through the bridge. The order matches exactly with MESSAGE_LIST from the official 
 * Aerofly SDK for compatibility.
 * 
 * Variables are organized into logical groups for better maintainability:
 * - Aircraft state and physics (0-94)
 * - Performance speeds (95-104)
 * - Configuration (105-106)
 * - Flight management (107)
 * - Navigation (108-141)
 * - And more...
 */
enum class VariableIndex : int {
    /**
     * @name Aircraft Core Variables (0-94)
     * Primary aircraft state, physics, and system variables
     */
    AIRCRAFT_UNIVERSAL_TIME = 0,                   // Aircraft.UniversalTime
    AIRCRAFT_ALTITUDE = 1,                         // Aircraft.Altitude  
    AIRCRAFT_VERTICAL_SPEED = 2,                   // Aircraft.VerticalSpeed
    AIRCRAFT_PITCH = 3,                            // Aircraft.Pitch
    AIRCRAFT_BANK = 4,                             // Aircraft.Bank
    AIRCRAFT_INDICATED_AIRSPEED = 5,               // Aircraft.IndicatedAirspeed
    AIRCRAFT_INDICATED_AIRSPEED_TREND = 6,         // Aircraft.IndicatedAirspeedTrend
    AIRCRAFT_GROUND_SPEED = 7,                     // Aircraft.GroundSpeed
    AIRCRAFT_MAGNETIC_HEADING = 8,                 // Aircraft.MagneticHeading
    AIRCRAFT_TRUE_HEADING = 9,                     // Aircraft.TrueHeading
    AIRCRAFT_LATITUDE = 10,                        // Aircraft.Latitude
    AIRCRAFT_LONGITUDE = 11,                       // Aircraft.Longitude
    AIRCRAFT_HEIGHT = 12,                          // Aircraft.Height
    AIRCRAFT_POSITION = 13,                        // Aircraft.Position (Vector3d)
    AIRCRAFT_ORIENTATION = 14,                     // Aircraft.Orientation
    AIRCRAFT_VELOCITY = 15,                        // Aircraft.Velocity (Vector3d)
    AIRCRAFT_ANGULAR_VELOCITY = 16,                // Aircraft.AngularVelocity (Vector3d)
    AIRCRAFT_ACCELERATION = 17,                    // Aircraft.Acceleration (Vector3d)
    AIRCRAFT_GRAVITY = 18,                         // Aircraft.Gravity (Vector3d)
    AIRCRAFT_WIND = 19,                            // Aircraft.Wind (Vector3d)
    AIRCRAFT_RATE_OF_TURN = 20,                    // Aircraft.RateOfTurn
    AIRCRAFT_MACH_NUMBER = 21,                     // Aircraft.MachNumber
    AIRCRAFT_ANGLE_OF_ATTACK = 22,                 // Aircraft.AngleOfAttack
    AIRCRAFT_ANGLE_OF_ATTACK_LIMIT = 23,           // Aircraft.AngleOfAttackLimit
    AIRCRAFT_ACCELERATION_LIMIT = 24,              // Aircraft.AccelerationLimit
    AIRCRAFT_GEAR = 25,                            // Aircraft.Gear
    AIRCRAFT_FLAPS = 26,                           // Aircraft.Flaps
    AIRCRAFT_SLATS = 27,                           // Aircraft.Slats
    AIRCRAFT_THROTTLE = 28,                        // Aircraft.Throttle
    AIRCRAFT_AIR_BRAKE = 29,                       // Aircraft.AirBrake
    AIRCRAFT_GROUND_SPOILERS_ARMED = 30,           // Aircraft.GroundSpoilersArmed
    AIRCRAFT_GROUND_SPOILERS_EXTENDED = 31,        // Aircraft.GroundSpoilersExtended
    AIRCRAFT_PARKING_BRAKE = 32,                   // Aircraft.ParkingBrake
    AIRCRAFT_AUTO_BRAKE_SETTING = 33,              // Aircraft.AutoBrakeSetting
    AIRCRAFT_AUTO_BRAKE_ENGAGED = 34,              // Aircraft.AutoBrakeEngaged
    AIRCRAFT_AUTO_BRAKE_REJECTED_TAKEOFF = 35,     // Aircraft.AutoBrakeRejectedTakeOff
    AIRCRAFT_RADAR_ALTITUDE = 36,                  // Aircraft.RadarAltitude
    AIRCRAFT_NAME = 37,                            // Aircraft.Name (String)
    AIRCRAFT_NEAREST_AIRPORT_IDENTIFIER = 38,      // Aircraft.NearestAirportIdentifier (String)
    AIRCRAFT_NEAREST_AIRPORT_NAME = 39,            // Aircraft.NearestAirportName (String)
    AIRCRAFT_NEAREST_AIRPORT_LOCATION = 40,        // Aircraft.NearestAirportLocation (Vector2d)
    AIRCRAFT_NEAREST_AIRPORT_ELEVATION = 41,       // Aircraft.NearestAirportElevation
    AIRCRAFT_BEST_AIRPORT_IDENTIFIER = 42,         // Aircraft.BestAirportIdentifier (String)
    AIRCRAFT_BEST_AIRPORT_NAME = 43,               // Aircraft.BestAirportName (String)
    AIRCRAFT_BEST_AIRPORT_LOCATION = 44,           // Aircraft.BestAirportLocation (Vector2d)
    AIRCRAFT_BEST_AIRPORT_ELEVATION = 45,          // Aircraft.BestAirportElevation
    AIRCRAFT_BEST_RUNWAY_IDENTIFIER = 46,          // Aircraft.BestRunwayIdentifier (String)
    AIRCRAFT_BEST_RUNWAY_ELEVATION = 47,           // Aircraft.BestRunwayElevation
    AIRCRAFT_BEST_RUNWAY_THRESHOLD = 48,           // Aircraft.BestRunwayThreshold (Vector3d)
    AIRCRAFT_BEST_RUNWAY_END = 49,                 // Aircraft.BestRunwayEnd (Vector3d)
    AIRCRAFT_CATEGORY_JET = 50,                    // Aircraft.Category.Jet
    AIRCRAFT_CATEGORY_GLIDER = 51,                 // Aircraft.Category.Glider
    AIRCRAFT_ON_GROUND = 52,                       // Aircraft.OnGround
    AIRCRAFT_ON_RUNWAY = 53,                       // Aircraft.OnRunway
    AIRCRAFT_CRASHED = 54,                         // Aircraft.Crashed
    AIRCRAFT_POWER = 55,                           // Aircraft.Power
    AIRCRAFT_NORMALIZED_POWER = 56,                // Aircraft.NormalizedPower
    AIRCRAFT_NORMALIZED_POWER_TARGET = 57,         // Aircraft.NormalizedPowerTarget
    AIRCRAFT_TRIM = 58,                            // Aircraft.Trim
    AIRCRAFT_PITCH_TRIM = 59,                      // Aircraft.PitchTrim
    AIRCRAFT_PITCH_TRIM_SCALING = 60,              // Aircraft.PitchTrimScaling
    AIRCRAFT_PITCH_TRIM_OFFSET = 61,               // Aircraft.PitchTrimOffset
    AIRCRAFT_RUDDER_TRIM = 62,                     // Aircraft.RudderTrim
    AIRCRAFT_AUTO_PITCH_TRIM = 63,                 // Aircraft.AutoPitchTrim
    AIRCRAFT_YAW_DAMPER_ENABLED = 64,              // Aircraft.YawDamperEnabled
    AIRCRAFT_RUDDER_PEDALS_DISCONNECTED = 65,      // Aircraft.RudderPedalsDisconnected
    AIRCRAFT_STARTER = 66,                         // Aircraft.Starter
    AIRCRAFT_STARTER_1 = 67,                       // Aircraft.Starter1
    AIRCRAFT_STARTER_2 = 68,                       // Aircraft.Starter2
    AIRCRAFT_STARTER_3 = 69,                       // Aircraft.Starter3
    AIRCRAFT_STARTER_4 = 70,                       // Aircraft.Starter4
    AIRCRAFT_IGNITION = 71,                        // Aircraft.Ignition
    AIRCRAFT_IGNITION_1 = 72,                      // Aircraft.Ignition1
    AIRCRAFT_IGNITION_2 = 73,                      // Aircraft.Ignition2
    AIRCRAFT_IGNITION_3 = 74,                      // Aircraft.Ignition3
    AIRCRAFT_IGNITION_4 = 75,                      // Aircraft.Ignition4
    AIRCRAFT_THROTTLE_LIMIT = 76,                  // Aircraft.ThrottleLimit
    AIRCRAFT_REVERSE = 77,                         // Aircraft.Reverse
    AIRCRAFT_ENGINE_MASTER_1 = 78,                 // Aircraft.EngineMaster1
    AIRCRAFT_ENGINE_MASTER_2 = 79,                 // Aircraft.EngineMaster2
    AIRCRAFT_ENGINE_MASTER_3 = 80,                 // Aircraft.EngineMaster3
    AIRCRAFT_ENGINE_MASTER_4 = 81,                 // Aircraft.EngineMaster4
    AIRCRAFT_ENGINE_THROTTLE_1 = 82,               // Aircraft.EngineThrottle1
    AIRCRAFT_ENGINE_THROTTLE_2 = 83,               // Aircraft.EngineThrottle2
    AIRCRAFT_ENGINE_THROTTLE_3 = 84,               // Aircraft.EngineThrottle3
    AIRCRAFT_ENGINE_THROTTLE_4 = 85,               // Aircraft.EngineThrottle4
    AIRCRAFT_ENGINE_ROTATION_SPEED_1 = 86,         // Aircraft.EngineRotationSpeed1
    AIRCRAFT_ENGINE_ROTATION_SPEED_2 = 87,         // Aircraft.EngineRotationSpeed2
    AIRCRAFT_ENGINE_ROTATION_SPEED_3 = 88,         // Aircraft.EngineRotationSpeed3
    AIRCRAFT_ENGINE_ROTATION_SPEED_4 = 89,         // Aircraft.EngineRotationSpeed4
    AIRCRAFT_ENGINE_RUNNING_1 = 90,                // Aircraft.EngineRunning1
    AIRCRAFT_ENGINE_RUNNING_2 = 91,                // Aircraft.EngineRunning2
    AIRCRAFT_ENGINE_RUNNING_3 = 92,                // Aircraft.EngineRunning3
    AIRCRAFT_ENGINE_RUNNING_4 = 93,                // Aircraft.EngineRunning4
    AIRCRAFT_APU_AVAILABLE = 94,                   // Aircraft.APUAvailable

    /**
     * @name Performance Speeds (95-104)
     * Aircraft-specific speed limitations and operating speeds.
     * These values define the safe operating envelope of the aircraft.
     */
    PERFORMANCE_SPEED_VS0 = 95,                    // Performance.Speed.VS0
    PERFORMANCE_SPEED_VS1 = 96,                    // Performance.Speed.VS1
    PERFORMANCE_SPEED_VFE = 97,                    // Performance.Speed.VFE
    PERFORMANCE_SPEED_VNO = 98,                    // Performance.Speed.VNO
    PERFORMANCE_SPEED_VNE = 99,                    // Performance.Speed.VNE
    PERFORMANCE_SPEED_VAPP = 100,                  // Performance.Speed.VAPP
    PERFORMANCE_SPEED_MINIMUM = 101,               // Performance.Speed.Minimum
    PERFORMANCE_SPEED_MAXIMUM = 102,               // Performance.Speed.Maximum
    PERFORMANCE_SPEED_MINIMUM_FLAP_RETRACTION = 103, // Performance.Speed.MinimumFlapRetraction
    PERFORMANCE_SPEED_MAXIMUM_FLAP_EXTENSION = 104,  // Performance.Speed.MaximumFlapExtension

    /**
     * @name Aircraft Configuration (105-106)
     * Standard flap settings for different flight phases
     */
    CONFIGURATION_SELECTED_TAKEOFF_FLAPS = 105,   // Configuration.SelectedTakeOffFlaps
    CONFIGURATION_SELECTED_LANDING_FLAPS = 106,   // Configuration.SelectedLandingFlaps

    /**
     * @name Flight Management System (107)
     * FMS data and flight plan information
     */
    FMS_FLIGHT_NUMBER = 107,                       // FlightManagementSystem.FlightNumber (String)

    /**
     * @name Navigation Systems (108-141)
     * Complete navigation radio suite including VOR, DME, ILS, and ADF systems.
     * Includes frequencies, courses, identifiers, and related data for dual nav systems.
     */
    
    // VOR Navigation
    NAVIGATION_SELECTED_COURSE_1 = 108,            // Navigation.SelectedCourse1
    NAVIGATION_SELECTED_COURSE_2 = 109,            // Navigation.SelectedCourse2
    NAVIGATION_NAV1_IDENTIFIER = 110,              // Navigation.NAV1Identifier (String)
    NAVIGATION_NAV1_FREQUENCY = 111,               // Navigation.NAV1Frequency
    NAVIGATION_NAV1_STANDBY_FREQUENCY = 112,       // Navigation.NAV1StandbyFrequency
    NAVIGATION_NAV1_FREQUENCY_SWAP = 113,          // Navigation.NAV1FrequencySwap
    NAVIGATION_NAV2_IDENTIFIER = 114,              // Navigation.NAV2Identifier (String)
    NAVIGATION_NAV2_FREQUENCY = 115,               // Navigation.NAV2Frequency
    NAVIGATION_NAV2_STANDBY_FREQUENCY = 116,       // Navigation.NAV2StandbyFrequency
    NAVIGATION_NAV2_FREQUENCY_SWAP = 117,          // Navigation.NAV2FrequencySwap
    
    // DME Equipment
    NAVIGATION_DME1_FREQUENCY = 118,               // Navigation.DME1Frequency
    NAVIGATION_DME1_DISTANCE = 119,                // Navigation.DME1Distance
    NAVIGATION_DME1_TIME = 120,                    // Navigation.DME1Time
    NAVIGATION_DME1_SPEED = 121,                   // Navigation.DME1Speed
    NAVIGATION_DME2_FREQUENCY = 122,               // Navigation.DME2Frequency
    NAVIGATION_DME2_DISTANCE = 123,                // Navigation.DME2Distance
    NAVIGATION_DME2_TIME = 124,                    // Navigation.DME2Time
    NAVIGATION_DME2_SPEED = 125,                   // Navigation.DME2Speed
    
    // ILS Systems
    NAVIGATION_ILS1_IDENTIFIER = 126,              // Navigation.ILS1Identifier (String)
    NAVIGATION_ILS1_COURSE = 127,                  // Navigation.ILS1Course
    NAVIGATION_ILS1_FREQUENCY = 128,               // Navigation.ILS1Frequency
    NAVIGATION_ILS1_STANDBY_FREQUENCY = 129,       // Navigation.ILS1StandbyFrequency
    NAVIGATION_ILS1_FREQUENCY_SWAP = 130,          // Navigation.ILS1FrequencySwap
    NAVIGATION_ILS2_IDENTIFIER = 131,              // Navigation.ILS2Identifier (String)
    NAVIGATION_ILS2_COURSE = 132,                  // Navigation.ILS2Course
    NAVIGATION_ILS2_FREQUENCY = 133,               // Navigation.ILS2Frequency
    NAVIGATION_ILS2_STANDBY_FREQUENCY = 134,       // Navigation.ILS2StandbyFrequency
    NAVIGATION_ILS2_FREQUENCY_SWAP = 135,          // Navigation.ILS2FrequencySwap
    
    // ADF Equipment
    NAVIGATION_ADF1_FREQUENCY = 136,               // Navigation.ADF1Frequency
    NAVIGATION_ADF1_STANDBY_FREQUENCY = 137,       // Navigation.ADF1StandbyFrequency
    NAVIGATION_ADF1_FREQUENCY_SWAP = 138,          // Navigation.ADF1FrequencySwap
    NAVIGATION_ADF2_FREQUENCY = 139,               // Navigation.ADF2Frequency
    NAVIGATION_ADF2_STANDBY_FREQUENCY = 140,       // Navigation.ADF2StandbyFrequency
    NAVIGATION_ADF2_FREQUENCY_SWAP = 141,          // Navigation.ADF2FrequencySwap

    /**
     * @name Communication Systems (142-152)
     * Radio communication equipment and transponder settings.
     * Includes three COM radios and transponder functionality.
     */
    
    // COM Radio 1
    COMMUNICATION_COM1_FREQUENCY = 142,            // Communication.COM1Frequency
    COMMUNICATION_COM1_STANDBY_FREQUENCY = 143,    // Communication.COM1StandbyFrequency
    COMMUNICATION_COM1_FREQUENCY_SWAP = 144,       // Communication.COM1FrequencySwap
    
    // COM Radio 2
    COMMUNICATION_COM2_FREQUENCY = 145,            // Communication.COM2Frequency
    COMMUNICATION_COM2_STANDBY_FREQUENCY = 146,    // Communication.COM2StandbyFrequency
    COMMUNICATION_COM2_FREQUENCY_SWAP = 147,       // Communication.COM2FrequencySwap
    
    // COM Radio 3
    COMMUNICATION_COM3_FREQUENCY = 148,            // Communication.COM3Frequency
    COMMUNICATION_COM3_STANDBY_FREQUENCY = 149,    // Communication.COM3StandbyFrequency
    COMMUNICATION_COM3_FREQUENCY_SWAP = 150,       // Communication.COM3FrequencySwap
    
    // Transponder
    COMMUNICATION_TRANSPONDER_CODE = 151,          // Communication.TransponderCode
    COMMUNICATION_TRANSPONDER_CURSOR = 152,        // Communication.TransponderCursor

    /**
     * @name Autopilot System (153-180)
     * Complete autopilot functionality including flight director, auto-throttle,
     * and all flight modes. Supports both fixed-wing and rotorcraft operations.
     */
    
    // Basic Controls
    AUTOPILOT_MASTER = 153,                        // Autopilot.Master
    AUTOPILOT_DISENGAGE = 154,                     // Autopilot.Disengage
    AUTOPILOT_HEADING = 155,                       // Autopilot.Heading
    AUTOPILOT_VERTICAL_SPEED = 156,                // Autopilot.VerticalSpeed
    
    // Selected Values
    AUTOPILOT_SELECTED_SPEED = 157,                // Autopilot.SelectedSpeed
    AUTOPILOT_SELECTED_AIRSPEED = 158,             // Autopilot.SelectedAirspeed
    AUTOPILOT_SELECTED_HEADING = 159,              // Autopilot.SelectedHeading
    AUTOPILOT_SELECTED_ALTITUDE = 160,             // Autopilot.SelectedAltitude
    AUTOPILOT_SELECTED_VERTICAL_SPEED = 161,       // Autopilot.SelectedVerticalSpeed
    AUTOPILOT_SELECTED_ALTITUDE_SCALE = 162,       // Autopilot.SelectedAltitudeScale
    
    // Mode Status
    AUTOPILOT_ACTIVE_LATERAL_MODE = 163,           // Autopilot.ActiveLateralMode (String)
    AUTOPILOT_ARMED_LATERAL_MODE = 164,            // Autopilot.ArmedLateralMode (String)
    AUTOPILOT_ACTIVE_VERTICAL_MODE = 165,          // Autopilot.ActiveVerticalMode (String)
    AUTOPILOT_ARMED_VERTICAL_MODE = 166,           // Autopilot.ArmedVerticalMode (String)
    AUTOPILOT_ARMED_APPROACH_MODE = 167,           // Autopilot.ArmedApproachMode (String)
    AUTOPILOT_ACTIVE_AUTO_THROTTLE_MODE = 168,     // Autopilot.ActiveAutoThrottleMode (String)
    AUTOPILOT_ACTIVE_COLLECTIVE_MODE = 169,        // Autopilot.ActiveCollectiveMode (String)
    AUTOPILOT_ARMED_COLLECTIVE_MODE = 170,         // Autopilot.ArmedCollectiveMode (String)
    
    // System Configuration
    AUTOPILOT_TYPE = 171,                          // Autopilot.Type (String)
    AUTOPILOT_ENGAGED = 172,                       // Autopilot.Engaged
    AUTOPILOT_USE_MACH_NUMBER = 173,               // Autopilot.UseMachNumber
    AUTOPILOT_SPEED_MANAGED = 174,                 // Autopilot.SpeedManaged
    AUTOPILOT_TARGET_AIRSPEED = 175,               // Autopilot.TargetAirspeed
    
    // Control Surface Commands
    AUTOPILOT_AILERON = 176,                       // Autopilot.Aileron
    AUTOPILOT_ELEVATOR = 177,                      // Autopilot.Elevator
    
    // Auto-Throttle System
    AUTO_THROTTLE_TYPE = 178,                      // AutoThrottle.Type
    AUTOPILOT_THROTTLE_ENGAGED = 179,              // Autopilot.ThrottleEngaged
    AUTOPILOT_THROTTLE_COMMAND = 180,              // Autopilot.ThrottleCommand

    /**
     * @name Flight Director (181-183)
     * Flight director guidance commands for manual flight
     */
    FLIGHT_DIRECTOR_PITCH = 181,                   // FlightDirector.Pitch
    FLIGHT_DIRECTOR_BANK = 182,                    // FlightDirector.Bank
    FLIGHT_DIRECTOR_YAW = 183,                     // FlightDirector.Yaw

    /**
     * @name Copilot System (184-191)
     * AI copilot control inputs and flight parameters.
     * Used for automated assistance and training scenarios.
     */
    
    // Flight Parameters
    COPILOT_HEADING = 184,                         // Copilot.Heading
    COPILOT_ALTITUDE = 185,                        // Copilot.Altitude
    COPILOT_AIRSPEED = 186,                        // Copilot.Airspeed
    COPILOT_VERTICAL_SPEED = 187,                  // Copilot.VerticalSpeed
    
    // Control Inputs
    COPILOT_AILERON = 188,                         // Copilot.Aileron
    COPILOT_ELEVATOR = 189,                        // Copilot.Elevator
    COPILOT_THROTTLE = 190,                        // Copilot.Throttle
    COPILOT_AUTO_RUDDER = 191,                     // Copilot.AutoRudder

    /**
     * @name Aircraft Controls (192-260)
     * Comprehensive set of aircraft control inputs and settings.
     * Includes controls for fixed-wing, multi-engine, and rotorcraft operations.
     */
    
    // Engine Controls
    CONTROLS_THROTTLE = 192,                       // Controls.Throttle
    CONTROLS_THROTTLE_1 = 193,                     // Controls.Throttle1
    CONTROLS_THROTTLE_2 = 194,                     // Controls.Throttle2
    CONTROLS_THROTTLE_3 = 195,                     // Controls.Throttle3
    CONTROLS_THROTTLE_4 = 196,                     // Controls.Throttle4
    CONTROLS_THROTTLE_1_MOVE = 197,                // Controls.Throttle1 (Move flag)
    CONTROLS_THROTTLE_2_MOVE = 198,                // Controls.Throttle2 (Move flag)
    CONTROLS_THROTTLE_3_MOVE = 199,                // Controls.Throttle3 (Move flag)
    CONTROLS_THROTTLE_4_MOVE = 200,                // Controls.Throttle4 (Move flag)
    
    // Flight Controls
    CONTROLS_PITCH_INPUT = 201,                    // Controls.Pitch.Input
    CONTROLS_PITCH_INPUT_OFFSET = 202,             // Controls.Pitch.Input (Offset flag)
    CONTROLS_ROLL_INPUT = 203,                     // Controls.Roll.Input
    CONTROLS_ROLL_INPUT_OFFSET = 204,              // Controls.Roll.Input (Offset flag)
    CONTROLS_YAW_INPUT = 205,                      // Controls.Yaw.Input
    CONTROLS_YAW_INPUT_ACTIVE = 206,               // Controls.Yaw.Input (Active flag)
    
    // High-Lift Devices
    CONTROLS_FLAPS = 207,                          // Controls.Flaps
    CONTROLS_FLAPS_EVENT = 208,                    // Controls.Flaps (Event flag)
    CONTROLS_GEAR = 209,                           // Controls.Gear
    CONTROLS_GEAR_TOGGLE = 210,                    // Controls.Gear (Toggle flag)
    
    // Brake Systems
    CONTROLS_WHEEL_BRAKE_LEFT = 211,               // Controls.WheelBrake.Left
    CONTROLS_WHEEL_BRAKE_RIGHT = 212,              // Controls.WheelBrake.Right
    CONTROLS_WHEEL_BRAKE_LEFT_ACTIVE = 213,        // Controls.WheelBrake.Left (Active flag)
    CONTROLS_WHEEL_BRAKE_RIGHT_ACTIVE = 214,       // Controls.WheelBrake.Right (Active flag)
    CONTROLS_AIR_BRAKE = 215,                      // Controls.AirBrake
    CONTROLS_AIR_BRAKE_ACTIVE = 216,               // Controls.AirBrake (Active flag)
    CONTROLS_AIR_BRAKE_ARM = 217,                  // Controls.AirBrake.Arm
    CONTROLS_GLIDER_AIR_BRAKE = 218,               // Controls.GliderAirBrake
    
    // Engine Management
    CONTROLS_PROPELLER_SPEED_1 = 219,              // Controls.PropellerSpeed1
    CONTROLS_PROPELLER_SPEED_2 = 220,              // Controls.PropellerSpeed2
    CONTROLS_PROPELLER_SPEED_3 = 221,              // Controls.PropellerSpeed3
    CONTROLS_PROPELLER_SPEED_4 = 222,              // Controls.PropellerSpeed4
    CONTROLS_MIXTURE = 223,                        // Controls.Mixture
    CONTROLS_MIXTURE_1 = 224,                      // Controls.Mixture1
    CONTROLS_MIXTURE_2 = 225,                      // Controls.Mixture2
    CONTROLS_MIXTURE_3 = 226,                      // Controls.Mixture3
    CONTROLS_MIXTURE_4 = 227,                      // Controls.Mixture4
    
    // Thrust Reversers
    CONTROLS_THRUST_REVERSE = 228,                 // Controls.ThrustReverse
    CONTROLS_THRUST_REVERSE_1 = 229,               // Controls.ThrustReverse1
    CONTROLS_THRUST_REVERSE_2 = 230,               // Controls.ThrustReverse2
    CONTROLS_THRUST_REVERSE_3 = 231,               // Controls.ThrustReverse3
    CONTROLS_THRUST_REVERSE_4 = 232,               // Controls.ThrustReverse4
    
    // Helicopter Controls
    CONTROLS_COLLECTIVE = 233,                     // Controls.Collective
    CONTROLS_CYCLIC_PITCH = 234,                   // Controls.CyclicPitch
    CONTROLS_CYCLIC_ROLL = 235,                    // Controls.CyclicRoll
    CONTROLS_TAIL_ROTOR = 236,                     // Controls.TailRotor
    CONTROLS_ROTOR_BRAKE = 237,                    // Controls.RotorBrake
    CONTROLS_HELICOPTER_THROTTLE_1 = 238,          // Controls.HelicopterThrottle1
    CONTROLS_HELICOPTER_THROTTLE_2 = 239,          // Controls.HelicopterThrottle2
    
    // Trim Systems
    CONTROLS_TRIM = 240,                           // Controls.Trim
    CONTROLS_TRIM_STEP = 241,                      // Controls.Trim (Step flag)
    CONTROLS_TRIM_MOVE = 242,                      // Controls.Trim (Move flag)
    CONTROLS_AILERON_TRIM = 243,                   // Controls.AileronTrim
    CONTROLS_RUDDER_TRIM = 244,                    // Controls.RudderTrim
    
    // Ground Controls
    CONTROLS_TILLER = 245,                         // Controls.Tiller
    CONTROLS_PEDALS_DISCONNECT = 246,              // Controls.PedalsDisconnect
    CONTROLS_NOSE_WHEEL_STEERING = 247,            // Controls.NoseWheelSteering
    
    // Aircraft Systems
    CONTROLS_LIGHTING_PANEL = 248,                 // Controls.Lighting.Panel
    CONTROLS_LIGHTING_INSTRUMENTS = 249,           // Controls.Lighting.Instruments
    
    // Pressure Settings
    CONTROLS_PRESSURE_SETTING_0 = 250,             // Controls.PressureSetting0
    CONTROLS_PRESSURE_SETTING_STANDARD_0 = 251,    // Controls.PressureSettingStandard0
    CONTROLS_PRESSURE_SETTING_UNIT_0 = 252,        // Controls.PressureSettingUnit0
    CONTROLS_PRESSURE_SETTING_1 = 253,             // Controls.PressureSetting1
    CONTROLS_PRESSURE_SETTING_STANDARD_1 = 254,    // Controls.PressureSettingStandard1
    CONTROLS_PRESSURE_SETTING_UNIT_1 = 255,        // Controls.PressureSettingUnit1
    CONTROLS_PRESSURE_SETTING_2 = 256,             // Controls.PressureSetting2
    CONTROLS_PRESSURE_SETTING_STANDARD_2 = 257,    // Controls.PressureSettingStandard2
    CONTROLS_PRESSURE_SETTING_UNIT_2 = 258,        // Controls.PressureSettingUnit2
    
    // Altitude Settings
    CONTROLS_TRANSITION_ALTITUDE = 259,            // Controls.TransitionAltitude
    CONTROLS_TRANSITION_LEVEL = 260,               // Controls.TransitionLevel

    /**
     * @name Pressurization System (261-262)
     * Aircraft cabin pressurization controls and settings
     */
    PRESSURIZATION_LANDING_ELEVATION = 261,        // Pressurization.LandingElevation
    PRESSURIZATION_LANDING_ELEVATION_MANUAL = 262, // Pressurization.LandingElevationManual

    /**
     * @name Warning Systems (263-272)
     * Aircraft warning and caution system states.
     * Includes master alerts and specific system warnings.
     */
    
    // Master Alerts
    WARNINGS_MASTER_WARNING = 263,                 // Warnings.MasterWarning
    WARNINGS_MASTER_CAUTION = 264,                 // Warnings.MasterCaution
    
    // Engine and System Warnings
    WARNINGS_ENGINE_FIRE = 265,                    // Warnings.EngineFire
    WARNINGS_LOW_OIL_PRESSURE = 266,               // Warnings.LowOilPressure
    WARNINGS_LOW_FUEL_PRESSURE = 267,              // Warnings.LowFuelPressure
    WARNINGS_LOW_HYDRAULIC_PRESSURE = 268,         // Warnings.LowHydraulicPressure
    WARNINGS_LOW_VOLTAGE = 269,                    // Warnings.LowVoltage
    
    // Flight Warnings
    WARNINGS_ALTITUDE_ALERT = 270,                 // Warnings.AltitudeAlert
    WARNINGS_WARNING_ACTIVE = 271,                 // Warnings.WarningActive
    WARNINGS_WARNING_MUTE = 272,                   // Warnings.WarningMute

    /**
     * @name View System (273-302)
     * Camera and view control system.
     * Supports multiple view modes, camera movement, and field of view settings.
     */
    
    // View Mode Selection
    VIEW_DISPLAY_NAME = 273,                       // View.DisplayName (String)
    VIEW_INTERNAL = 274,                           // View.Internal
    VIEW_FOLLOW = 275,                             // View.Follow
    VIEW_EXTERNAL = 276,                           // View.External
    VIEW_CATEGORY = 277,                           // View.Category
    VIEW_MODE = 278,                               // View.Mode
    
    // Basic View Controls
    VIEW_ZOOM = 279,                               // View.Zoom
    
    // Pan Controls
    VIEW_PAN_HORIZONTAL = 280,                     // View.Pan.Horizontal
    VIEW_PAN_HORIZONTAL_MOVE = 281,                // View.Pan.Horizontal (Move flag)
    VIEW_PAN_VERTICAL = 282,                       // View.Pan.Vertical
    VIEW_PAN_VERTICAL_MOVE = 283,                  // View.Pan.Vertical (Move flag)
    VIEW_PAN_CENTER = 284,                         // View.Pan.Center
    
    // Look Controls
    VIEW_LOOK_HORIZONTAL = 285,                    // View.Look.Horizontal
    VIEW_LOOK_VERTICAL = 286,                      // View.Look.Vertical
    VIEW_ROLL = 287,                               // View.Roll
    
    // Position Offset Controls
    VIEW_OFFSET_X = 288,                           // View.OffsetX
    VIEW_OFFSET_X_MOVE = 289,                      // View.OffsetX (Move flag)
    VIEW_OFFSET_Y = 290,                           // View.OffsetY
    VIEW_OFFSET_Y_MOVE = 291,                      // View.OffsetY (Move flag)
    VIEW_OFFSET_Z = 292,                           // View.OffsetZ
    VIEW_OFFSET_Z_MOVE = 293,                      // View.OffsetZ (Move flag)
    
    // Camera Parameters
    VIEW_POSITION = 294,                           // View.Position
    VIEW_DIRECTION = 295,                          // View.Direction
    VIEW_UP = 296,                                 // View.Up
    VIEW_FIELD_OF_VIEW = 297,                      // View.FieldOfView
    VIEW_ASPECT_RATIO = 298,                       // View.AspectRatio
    
    // Free Camera Settings
    VIEW_FREE_POSITION = 299,                      // View.FreePosition (Vector3d)
    VIEW_FREE_LOOK_DIRECTION = 300,                // View.FreeLookDirection (Vector3d)
    VIEW_FREE_UP = 301,                            // View.FreeUp (Vector3d)
    VIEW_FREE_FIELD_OF_VIEW = 302,                 // View.FreeFieldOfView

    /**
     * @name Simulation Control System (303-320)
     * Core simulation control and configuration.
     * Includes state control, environment settings, and playback functionality.
     */
    
    // Basic Controls
    SIMULATION_PAUSE = 303,                        // Simulation.Pause
    SIMULATION_FLIGHT_INFORMATION = 304,           // Simulation.FlightInformation
    SIMULATION_MOVING_MAP = 305,                   // Simulation.MovingMap
    SIMULATION_SOUND = 306,                        // Simulation.Sound
    SIMULATION_LIFT_UP = 307,                      // Simulation.LiftUp
    
    // Aircraft Position Settings
    SIMULATION_SETTING_POSITION = 308,             // Simulation.SettingPosition (Vector3d)
    SIMULATION_SETTING_ORIENTATION = 309,          // Simulation.SettingOrientation (Vector4d)
    SIMULATION_SETTING_VELOCITY = 310,             // Simulation.SettingVelocity (Vector3d)
    SIMULATION_SETTING_SET = 311,                  // Simulation.SettingSet
    
    // Environment Controls
    SIMULATION_TIME_CHANGE = 312,                  // Simulation.TimeChange
    SIMULATION_VISIBILITY = 313,                   // Simulation.Visibility
    SIMULATION_TIME = 314,                         // Simulation.Time
    
    // Interface Settings
    SIMULATION_USE_MOUSE_CONTROL = 315,            // Simulation.UseMouseControl
    
    // Playback Controls
    SIMULATION_PLAYBACK_START = 316,               // Simulation.PlaybackStart
    SIMULATION_PLAYBACK_STOP = 317,                // Simulation.PlaybackStop
    SIMULATION_PLAYBACK_SET_POSITION = 318,        // Simulation.PlaybackPosition
    
    // External Position Control
    SIMULATION_EXTERNAL_POSITION = 319,            // Simulation.ExternalPosition (Vector3d)
    SIMULATION_EXTERNAL_ORIENTATION = 320,         // Simulation.ExternalOrientation (Vector4d)

    /**
     * @name Command Interface (321-330)
     * General command and control interface.
     * Provides basic navigation and interaction controls.
     */
    
    // Basic Commands
    COMMAND_EXECUTE = 321,                         // Command.Execute
    COMMAND_BACK = 322,                            // Command.Back
    
    // Navigation Controls
    COMMAND_UP = 323,                              // Command.Up
    COMMAND_DOWN = 324,                            // Command.Down
    COMMAND_LEFT = 325,                            // Command.Left
    COMMAND_RIGHT = 326,                           // Command.Right
    
    // Movement Controls
    COMMAND_MOVE_HORIZONTAL = 327,                 // Command.MoveHorizontal
    COMMAND_MOVE_VERTICAL = 328,                   // Command.MoveVertical
    
    // View Manipulation
    COMMAND_ROTATE = 329,                          // Command.Rotate
    COMMAND_ZOOM = 330,                            // Command.Zoom

    /**
     * @name Reserved Variables (331-338)
     * Special variables reserved for internal use.
     * These variables should not be used in external applications.
     * @warning Do not use these variables in your code.
     */
    CONTROLS_SPEED = 331,                          // Controls.Speed (ignore/do not use)
    FMS_DATA_0 = 332,                              // FlightManagementSystem.Data0 (ignore/do not use)
    FMS_DATA_1 = 333,                              // FlightManagementSystem.Data1 (ignore/do not use)
    NAV1_DATA = 334,                               // Navigation.NAV1Data (ignore/do not use)
    NAV2_DATA = 335,                               // Navigation.NAV2Data (ignore/do not use)
    NAV3_DATA = 336,                               // Navigation.NAV3Data (ignore/do not use)
    ILS1_DATA = 337,                               // Navigation.ILS1Data (ignore/do not use)
    ILS2_DATA = 338,                               // Navigation.ILS2Data (ignore/do not use)

    /**
     * @name Cessna 172 Specific Controls (340-365)
     * Extended variables for Cessna 172 aircraft control.
     * These variables provide access to C172-specific systems and controls.
     */
    
    // Fuel System
    C172_FUEL_SELECTOR = 340,                     // Controls.FuelSelector
    C172_FUEL_SHUT_OFF = 341,                     // Controls.FuelShutOff
    
    // Cockpit View Controls
    C172_HIDE_YOKE_LEFT = 342,                    // Controls.HideYoke.Left
    C172_HIDE_YOKE_RIGHT = 343,                   // Controls.HideYoke.Right
    C172_LEFT_SUN_BLOCKER = 344,                 // Controls.LeftSunBlocker
    C172_RIGHT_SUN_BLOCKER = 345,                // Controls.RightSunBlocker
    
    // Cabin Lighting
    C172_LEFT_CABIN_LIGHT = 346,                 // Controls.Lighting.LeftCabinOverheadLight
    C172_RIGHT_CABIN_LIGHT = 347,                // Controls.Lighting.RightCabinOverheadLight
    
    // Engine Controls
    C172_MAGNETOS_1 = 348,                       // Controls.Magnetos1
    C172_PARKING_BRAKE_HANDLE = 349,             // Controls.ParkingBrake (handle)
    C172_TRIM_WHEEL = 350,                       // Controls.Trim (wheel)
    C172_LEFT_YOKE_BUTTON = 351,                 // LeftYoke.Button
    
    // Doors and Windows
    C172_LEFT_DOOR = 352,                        // Doors.Left
    C172_LEFT_DOOR_HANDLE = 353,                 // Doors.LeftHandle
    C172_RIGHT_DOOR = 354,                       // Doors.Right
    C172_RIGHT_DOOR_HANDLE = 355,                // Doors.RightHandle
    C172_LEFT_WINDOW = 356,                      // Windows.Left
    C172_RIGHT_WINDOW = 357,                     // Windows.Right

    /**
     * @name Automatic Count
     * Automatically tracks the total number of variables.
     * Used for array sizing and validation.
     */
    COUNT                                          // Automatic count
};

static_assert((int)VariableIndex::COUNT <= MAX_VARIABLES, 
              "ERROR: Too many variables! Increase MAX_VARIABLES or reduce enum size.");

class VariableMapper {
private:
    std::unordered_map<std::string, int> name_to_index;
    std::unordered_map<uint64_t, int> hash_to_index;
    
public:
    // Builds bidirectional name/hash → logical index mapping used by
    // CommandProcessor to translate JSON variable names into message indices.
    VariableMapper() {
        // === AIRCRAFT VARIABLES (0-94) ===
        name_to_index["Aircraft.UniversalTime"] = (int)VariableIndex::AIRCRAFT_UNIVERSAL_TIME;
        name_to_index["Aircraft.Altitude"] = (int)VariableIndex::AIRCRAFT_ALTITUDE;
        name_to_index["Aircraft.VerticalSpeed"] = (int)VariableIndex::AIRCRAFT_VERTICAL_SPEED;
        name_to_index["Aircraft.Pitch"] = (int)VariableIndex::AIRCRAFT_PITCH;
        name_to_index["Aircraft.Bank"] = (int)VariableIndex::AIRCRAFT_BANK;
        name_to_index["Aircraft.IndicatedAirspeed"] = (int)VariableIndex::AIRCRAFT_INDICATED_AIRSPEED;
        name_to_index["Aircraft.IndicatedAirspeedTrend"] = (int)VariableIndex::AIRCRAFT_INDICATED_AIRSPEED_TREND;
        name_to_index["Aircraft.GroundSpeed"] = (int)VariableIndex::AIRCRAFT_GROUND_SPEED;
        name_to_index["Aircraft.MagneticHeading"] = (int)VariableIndex::AIRCRAFT_MAGNETIC_HEADING;
        name_to_index["Aircraft.TrueHeading"] = (int)VariableIndex::AIRCRAFT_TRUE_HEADING;
        name_to_index["Aircraft.Latitude"] = (int)VariableIndex::AIRCRAFT_LATITUDE;
        name_to_index["Aircraft.Longitude"] = (int)VariableIndex::AIRCRAFT_LONGITUDE;
        name_to_index["Aircraft.Height"] = (int)VariableIndex::AIRCRAFT_HEIGHT;
        name_to_index["Aircraft.Position"] = (int)VariableIndex::AIRCRAFT_POSITION;
        name_to_index["Aircraft.Orientation"] = (int)VariableIndex::AIRCRAFT_ORIENTATION;
        name_to_index["Aircraft.Velocity"] = (int)VariableIndex::AIRCRAFT_VELOCITY;
        name_to_index["Aircraft.AngularVelocity"] = (int)VariableIndex::AIRCRAFT_ANGULAR_VELOCITY;
        name_to_index["Aircraft.Acceleration"] = (int)VariableIndex::AIRCRAFT_ACCELERATION;
        name_to_index["Aircraft.Gravity"] = (int)VariableIndex::AIRCRAFT_GRAVITY;
        name_to_index["Aircraft.Wind"] = (int)VariableIndex::AIRCRAFT_WIND;
        name_to_index["Aircraft.RateOfTurn"] = (int)VariableIndex::AIRCRAFT_RATE_OF_TURN;
        name_to_index["Aircraft.MachNumber"] = (int)VariableIndex::AIRCRAFT_MACH_NUMBER;
        name_to_index["Aircraft.AngleOfAttack"] = (int)VariableIndex::AIRCRAFT_ANGLE_OF_ATTACK;
        name_to_index["Aircraft.AngleOfAttackLimit"] = (int)VariableIndex::AIRCRAFT_ANGLE_OF_ATTACK_LIMIT;
        name_to_index["Aircraft.AccelerationLimit"] = (int)VariableIndex::AIRCRAFT_ACCELERATION_LIMIT;
        name_to_index["Aircraft.Gear"] = (int)VariableIndex::AIRCRAFT_GEAR;
        name_to_index["Aircraft.Flaps"] = (int)VariableIndex::AIRCRAFT_FLAPS;
        name_to_index["Aircraft.Slats"] = (int)VariableIndex::AIRCRAFT_SLATS;
        name_to_index["Aircraft.Throttle"] = (int)VariableIndex::AIRCRAFT_THROTTLE;
        name_to_index["Aircraft.AirBrake"] = (int)VariableIndex::AIRCRAFT_AIR_BRAKE;
        name_to_index["Aircraft.GroundSpoilersArmed"] = (int)VariableIndex::AIRCRAFT_GROUND_SPOILERS_ARMED;
        name_to_index["Aircraft.GroundSpoilersExtended"] = (int)VariableIndex::AIRCRAFT_GROUND_SPOILERS_EXTENDED;
        name_to_index["Aircraft.ParkingBrake"] = (int)VariableIndex::AIRCRAFT_PARKING_BRAKE;
        name_to_index["Aircraft.AutoBrakeSetting"] = (int)VariableIndex::AIRCRAFT_AUTO_BRAKE_SETTING;
        name_to_index["Aircraft.AutoBrakeEngaged"] = (int)VariableIndex::AIRCRAFT_AUTO_BRAKE_ENGAGED;
        name_to_index["Aircraft.AutoBrakeRejectedTakeOff"] = (int)VariableIndex::AIRCRAFT_AUTO_BRAKE_REJECTED_TAKEOFF;
        name_to_index["Aircraft.RadarAltitude"] = (int)VariableIndex::AIRCRAFT_RADAR_ALTITUDE;
        name_to_index["Aircraft.Name"] = (int)VariableIndex::AIRCRAFT_NAME;
        name_to_index["Aircraft.NearestAirportIdentifier"] = (int)VariableIndex::AIRCRAFT_NEAREST_AIRPORT_IDENTIFIER;
        name_to_index["Aircraft.NearestAirportName"] = (int)VariableIndex::AIRCRAFT_NEAREST_AIRPORT_NAME;
        name_to_index["Aircraft.NearestAirportLocation"] = (int)VariableIndex::AIRCRAFT_NEAREST_AIRPORT_LOCATION;
        name_to_index["Aircraft.NearestAirportElevation"] = (int)VariableIndex::AIRCRAFT_NEAREST_AIRPORT_ELEVATION;
        name_to_index["Aircraft.BestAirportIdentifier"] = (int)VariableIndex::AIRCRAFT_BEST_AIRPORT_IDENTIFIER;
        name_to_index["Aircraft.BestAirportName"] = (int)VariableIndex::AIRCRAFT_BEST_AIRPORT_NAME;
        name_to_index["Aircraft.BestAirportLocation"] = (int)VariableIndex::AIRCRAFT_BEST_AIRPORT_LOCATION;
        name_to_index["Aircraft.BestAirportElevation"] = (int)VariableIndex::AIRCRAFT_BEST_AIRPORT_ELEVATION;
        name_to_index["Aircraft.BestRunwayIdentifier"] = (int)VariableIndex::AIRCRAFT_BEST_RUNWAY_IDENTIFIER;
        name_to_index["Aircraft.BestRunwayElevation"] = (int)VariableIndex::AIRCRAFT_BEST_RUNWAY_ELEVATION;
        name_to_index["Aircraft.BestRunwayThreshold"] = (int)VariableIndex::AIRCRAFT_BEST_RUNWAY_THRESHOLD;
        name_to_index["Aircraft.BestRunwayEnd"] = (int)VariableIndex::AIRCRAFT_BEST_RUNWAY_END;
        name_to_index["Aircraft.Category.Jet"] = (int)VariableIndex::AIRCRAFT_CATEGORY_JET;
        name_to_index["Aircraft.Category.Glider"] = (int)VariableIndex::AIRCRAFT_CATEGORY_GLIDER;
        name_to_index["Aircraft.OnGround"] = (int)VariableIndex::AIRCRAFT_ON_GROUND;
        name_to_index["Aircraft.OnRunway"] = (int)VariableIndex::AIRCRAFT_ON_RUNWAY;
        name_to_index["Aircraft.Crashed"] = (int)VariableIndex::AIRCRAFT_CRASHED;
        name_to_index["Aircraft.Power"] = (int)VariableIndex::AIRCRAFT_POWER;
        name_to_index["Aircraft.NormalizedPower"] = (int)VariableIndex::AIRCRAFT_NORMALIZED_POWER;
        name_to_index["Aircraft.NormalizedPowerTarget"] = (int)VariableIndex::AIRCRAFT_NORMALIZED_POWER_TARGET;
        name_to_index["Aircraft.Trim"] = (int)VariableIndex::AIRCRAFT_TRIM;
        name_to_index["Aircraft.PitchTrim"] = (int)VariableIndex::AIRCRAFT_PITCH_TRIM;
        name_to_index["Aircraft.PitchTrimScaling"] = (int)VariableIndex::AIRCRAFT_PITCH_TRIM_SCALING;
        name_to_index["Aircraft.PitchTrimOffset"] = (int)VariableIndex::AIRCRAFT_PITCH_TRIM_OFFSET;
        name_to_index["Aircraft.RudderTrim"] = (int)VariableIndex::AIRCRAFT_RUDDER_TRIM;
        name_to_index["Aircraft.AutoPitchTrim"] = (int)VariableIndex::AIRCRAFT_AUTO_PITCH_TRIM;
        name_to_index["Aircraft.YawDamperEnabled"] = (int)VariableIndex::AIRCRAFT_YAW_DAMPER_ENABLED;
        name_to_index["Aircraft.RudderPedalsDisconnected"] = (int)VariableIndex::AIRCRAFT_RUDDER_PEDALS_DISCONNECTED;
        name_to_index["Aircraft.Starter"] = (int)VariableIndex::AIRCRAFT_STARTER;
        name_to_index["Aircraft.Starter1"] = (int)VariableIndex::AIRCRAFT_STARTER_1;
        name_to_index["Aircraft.Starter2"] = (int)VariableIndex::AIRCRAFT_STARTER_2;
        name_to_index["Aircraft.Starter3"] = (int)VariableIndex::AIRCRAFT_STARTER_3;
        name_to_index["Aircraft.Starter4"] = (int)VariableIndex::AIRCRAFT_STARTER_4;
        name_to_index["Aircraft.Ignition"] = (int)VariableIndex::AIRCRAFT_IGNITION;
        name_to_index["Aircraft.Ignition1"] = (int)VariableIndex::AIRCRAFT_IGNITION_1;
        name_to_index["Aircraft.Ignition2"] = (int)VariableIndex::AIRCRAFT_IGNITION_2;
        name_to_index["Aircraft.Ignition3"] = (int)VariableIndex::AIRCRAFT_IGNITION_3;
        name_to_index["Aircraft.Ignition4"] = (int)VariableIndex::AIRCRAFT_IGNITION_4;
        name_to_index["Aircraft.ThrottleLimit"] = (int)VariableIndex::AIRCRAFT_THROTTLE_LIMIT;
        name_to_index["Aircraft.Reverse"] = (int)VariableIndex::AIRCRAFT_REVERSE;
        name_to_index["Aircraft.EngineMaster1"] = (int)VariableIndex::AIRCRAFT_ENGINE_MASTER_1;
        name_to_index["Aircraft.EngineMaster2"] = (int)VariableIndex::AIRCRAFT_ENGINE_MASTER_2;
        name_to_index["Aircraft.EngineMaster3"] = (int)VariableIndex::AIRCRAFT_ENGINE_MASTER_3;
        name_to_index["Aircraft.EngineMaster4"] = (int)VariableIndex::AIRCRAFT_ENGINE_MASTER_4;
        name_to_index["Aircraft.EngineThrottle1"] = (int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_1;
        name_to_index["Aircraft.EngineThrottle2"] = (int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_2;
        name_to_index["Aircraft.EngineThrottle3"] = (int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_3;
        name_to_index["Aircraft.EngineThrottle4"] = (int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_4;
        name_to_index["Aircraft.EngineRotationSpeed1"] = (int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_1;
        name_to_index["Aircraft.EngineRotationSpeed2"] = (int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_2;
        name_to_index["Aircraft.EngineRotationSpeed3"] = (int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_3;
        name_to_index["Aircraft.EngineRotationSpeed4"] = (int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_4;
        name_to_index["Aircraft.EngineRunning1"] = (int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_1;
        name_to_index["Aircraft.EngineRunning2"] = (int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_2;
        name_to_index["Aircraft.EngineRunning3"] = (int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_3;
        name_to_index["Aircraft.EngineRunning4"] = (int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_4;
        name_to_index["Aircraft.APUAvailable"] = (int)VariableIndex::AIRCRAFT_APU_AVAILABLE;
        
        // === PERFORMANCE SPEEDS (95-104) ===
        name_to_index["Performance.Speed.VS0"] = (int)VariableIndex::PERFORMANCE_SPEED_VS0;
        name_to_index["Performance.Speed.VS1"] = (int)VariableIndex::PERFORMANCE_SPEED_VS1;
        name_to_index["Performance.Speed.VFE"] = (int)VariableIndex::PERFORMANCE_SPEED_VFE;
        name_to_index["Performance.Speed.VNO"] = (int)VariableIndex::PERFORMANCE_SPEED_VNO;
        name_to_index["Performance.Speed.VNE"] = (int)VariableIndex::PERFORMANCE_SPEED_VNE;
        name_to_index["Performance.Speed.VAPP"] = (int)VariableIndex::PERFORMANCE_SPEED_VAPP;
        name_to_index["Performance.Speed.Minimum"] = (int)VariableIndex::PERFORMANCE_SPEED_MINIMUM;
        name_to_index["Performance.Speed.Maximum"] = (int)VariableIndex::PERFORMANCE_SPEED_MAXIMUM;
        name_to_index["Performance.Speed.MinimumFlapRetraction"] = (int)VariableIndex::PERFORMANCE_SPEED_MINIMUM_FLAP_RETRACTION;
        name_to_index["Performance.Speed.MaximumFlapExtension"] = (int)VariableIndex::PERFORMANCE_SPEED_MAXIMUM_FLAP_EXTENSION;
        
        // === CONFIGURATION (105-106) ===
        name_to_index["Configuration.SelectedTakeOffFlaps"] = (int)VariableIndex::CONFIGURATION_SELECTED_TAKEOFF_FLAPS;
        name_to_index["Configuration.SelectedLandingFlaps"] = (int)VariableIndex::CONFIGURATION_SELECTED_LANDING_FLAPS;
        
        // === FLIGHT MANAGEMENT SYSTEM (107) ===
        name_to_index["FlightManagementSystem.FlightNumber"] = (int)VariableIndex::FMS_FLIGHT_NUMBER;
        
        // === NAVIGATION (108-141) ===
        name_to_index["Navigation.SelectedCourse1"] = (int)VariableIndex::NAVIGATION_SELECTED_COURSE_1;
        name_to_index["Navigation.SelectedCourse2"] = (int)VariableIndex::NAVIGATION_SELECTED_COURSE_2;
        name_to_index["Navigation.NAV1Identifier"] = (int)VariableIndex::NAVIGATION_NAV1_IDENTIFIER;
        name_to_index["Navigation.NAV1Frequency"] = (int)VariableIndex::NAVIGATION_NAV1_FREQUENCY;
        name_to_index["Navigation.NAV1StandbyFrequency"] = (int)VariableIndex::NAVIGATION_NAV1_STANDBY_FREQUENCY;
        name_to_index["Navigation.NAV1FrequencySwap"] = (int)VariableIndex::NAVIGATION_NAV1_FREQUENCY_SWAP;
        name_to_index["Navigation.NAV2Identifier"] = (int)VariableIndex::NAVIGATION_NAV2_IDENTIFIER;
        name_to_index["Navigation.NAV2Frequency"] = (int)VariableIndex::NAVIGATION_NAV2_FREQUENCY;
        name_to_index["Navigation.NAV2StandbyFrequency"] = (int)VariableIndex::NAVIGATION_NAV2_STANDBY_FREQUENCY;
        name_to_index["Navigation.NAV2FrequencySwap"] = (int)VariableIndex::NAVIGATION_NAV2_FREQUENCY_SWAP;
        name_to_index["Navigation.DME1Frequency"] = (int)VariableIndex::NAVIGATION_DME1_FREQUENCY;
        name_to_index["Navigation.DME1Distance"] = (int)VariableIndex::NAVIGATION_DME1_DISTANCE;
        name_to_index["Navigation.DME1Time"] = (int)VariableIndex::NAVIGATION_DME1_TIME;
        name_to_index["Navigation.DME1Speed"] = (int)VariableIndex::NAVIGATION_DME1_SPEED;
        name_to_index["Navigation.DME2Frequency"] = (int)VariableIndex::NAVIGATION_DME2_FREQUENCY;
        name_to_index["Navigation.DME2Distance"] = (int)VariableIndex::NAVIGATION_DME2_DISTANCE;
        name_to_index["Navigation.DME2Time"] = (int)VariableIndex::NAVIGATION_DME2_TIME;
        name_to_index["Navigation.DME2Speed"] = (int)VariableIndex::NAVIGATION_DME2_SPEED;
        name_to_index["Navigation.ILS1Identifier"] = (int)VariableIndex::NAVIGATION_ILS1_IDENTIFIER;
        name_to_index["Navigation.ILS1Course"] = (int)VariableIndex::NAVIGATION_ILS1_COURSE;
        name_to_index["Navigation.ILS1Frequency"] = (int)VariableIndex::NAVIGATION_ILS1_FREQUENCY;
        name_to_index["Navigation.ILS1StandbyFrequency"] = (int)VariableIndex::NAVIGATION_ILS1_STANDBY_FREQUENCY;
        name_to_index["Navigation.ILS1FrequencySwap"] = (int)VariableIndex::NAVIGATION_ILS1_FREQUENCY_SWAP;
        name_to_index["Navigation.ILS2Identifier"] = (int)VariableIndex::NAVIGATION_ILS2_IDENTIFIER;
        name_to_index["Navigation.ILS2Course"] = (int)VariableIndex::NAVIGATION_ILS2_COURSE;
        name_to_index["Navigation.ILS2Frequency"] = (int)VariableIndex::NAVIGATION_ILS2_FREQUENCY;
        name_to_index["Navigation.ILS2StandbyFrequency"] = (int)VariableIndex::NAVIGATION_ILS2_STANDBY_FREQUENCY;
        name_to_index["Navigation.ILS2FrequencySwap"] = (int)VariableIndex::NAVIGATION_ILS2_FREQUENCY_SWAP;
        name_to_index["Navigation.ADF1Frequency"] = (int)VariableIndex::NAVIGATION_ADF1_FREQUENCY;
        name_to_index["Navigation.ADF1StandbyFrequency"] = (int)VariableIndex::NAVIGATION_ADF1_STANDBY_FREQUENCY;
        name_to_index["Navigation.ADF1FrequencySwap"] = (int)VariableIndex::NAVIGATION_ADF1_FREQUENCY_SWAP;
        name_to_index["Navigation.ADF2Frequency"] = (int)VariableIndex::NAVIGATION_ADF2_FREQUENCY;
        name_to_index["Navigation.ADF2StandbyFrequency"] = (int)VariableIndex::NAVIGATION_ADF2_STANDBY_FREQUENCY;
        name_to_index["Navigation.ADF2FrequencySwap"] = (int)VariableIndex::NAVIGATION_ADF2_FREQUENCY_SWAP;
        
        // === COMMUNICATION (142-152) ===
        name_to_index["Communication.COM1Frequency"] = (int)VariableIndex::COMMUNICATION_COM1_FREQUENCY;
        name_to_index["Communication.COM1StandbyFrequency"] = (int)VariableIndex::COMMUNICATION_COM1_STANDBY_FREQUENCY;
        name_to_index["Communication.COM1FrequencySwap"] = (int)VariableIndex::COMMUNICATION_COM1_FREQUENCY_SWAP;
        name_to_index["Communication.COM2Frequency"] = (int)VariableIndex::COMMUNICATION_COM2_FREQUENCY;
        name_to_index["Communication.COM2StandbyFrequency"] = (int)VariableIndex::COMMUNICATION_COM2_STANDBY_FREQUENCY;
        name_to_index["Communication.COM2FrequencySwap"] = (int)VariableIndex::COMMUNICATION_COM2_FREQUENCY_SWAP;
        name_to_index["Communication.COM3Frequency"] = (int)VariableIndex::COMMUNICATION_COM3_FREQUENCY;
        name_to_index["Communication.COM3StandbyFrequency"] = (int)VariableIndex::COMMUNICATION_COM3_STANDBY_FREQUENCY;
        name_to_index["Communication.COM3FrequencySwap"] = (int)VariableIndex::COMMUNICATION_COM3_FREQUENCY_SWAP;
        name_to_index["Communication.TransponderCode"] = (int)VariableIndex::COMMUNICATION_TRANSPONDER_CODE;
        name_to_index["Communication.TransponderCursor"] = (int)VariableIndex::COMMUNICATION_TRANSPONDER_CURSOR;
        
        // === AUTOPILOT (153-180) ===
        name_to_index["Autopilot.Master"] = (int)VariableIndex::AUTOPILOT_MASTER;
        name_to_index["Autopilot.Disengage"] = (int)VariableIndex::AUTOPILOT_DISENGAGE;
        name_to_index["Autopilot.Heading"] = (int)VariableIndex::AUTOPILOT_HEADING;
        name_to_index["Autopilot.VerticalSpeed"] = (int)VariableIndex::AUTOPILOT_VERTICAL_SPEED;
        name_to_index["Autopilot.SelectedSpeed"] = (int)VariableIndex::AUTOPILOT_SELECTED_SPEED;
        name_to_index["Autopilot.SelectedAirspeed"] = (int)VariableIndex::AUTOPILOT_SELECTED_AIRSPEED;
        name_to_index["Autopilot.SelectedHeading"] = (int)VariableIndex::AUTOPILOT_SELECTED_HEADING;
        name_to_index["Autopilot.SelectedAltitude"] = (int)VariableIndex::AUTOPILOT_SELECTED_ALTITUDE;
        name_to_index["Autopilot.SelectedVerticalSpeed"] = (int)VariableIndex::AUTOPILOT_SELECTED_VERTICAL_SPEED;
        name_to_index["Autopilot.SelectedAltitudeScale"] = (int)VariableIndex::AUTOPILOT_SELECTED_ALTITUDE_SCALE;
        name_to_index["Autopilot.ActiveLateralMode"] = (int)VariableIndex::AUTOPILOT_ACTIVE_LATERAL_MODE;
        name_to_index["Autopilot.ArmedLateralMode"] = (int)VariableIndex::AUTOPILOT_ARMED_LATERAL_MODE;
        name_to_index["Autopilot.ActiveVerticalMode"] = (int)VariableIndex::AUTOPILOT_ACTIVE_VERTICAL_MODE;
        name_to_index["Autopilot.ArmedVerticalMode"] = (int)VariableIndex::AUTOPILOT_ARMED_VERTICAL_MODE;
        name_to_index["Autopilot.ArmedApproachMode"] = (int)VariableIndex::AUTOPILOT_ARMED_APPROACH_MODE;
        name_to_index["Autopilot.ActiveAutoThrottleMode"] = (int)VariableIndex::AUTOPILOT_ACTIVE_AUTO_THROTTLE_MODE;
        name_to_index["Autopilot.ActiveCollectiveMode"] = (int)VariableIndex::AUTOPILOT_ACTIVE_COLLECTIVE_MODE;
        name_to_index["Autopilot.ArmedCollectiveMode"] = (int)VariableIndex::AUTOPILOT_ARMED_COLLECTIVE_MODE;
        name_to_index["Autopilot.Type"] = (int)VariableIndex::AUTOPILOT_TYPE;
        name_to_index["Autopilot.Engaged"] = (int)VariableIndex::AUTOPILOT_ENGAGED;
        name_to_index["Autopilot.UseMachNumber"] = (int)VariableIndex::AUTOPILOT_USE_MACH_NUMBER;
        name_to_index["Autopilot.SpeedManaged"] = (int)VariableIndex::AUTOPILOT_SPEED_MANAGED;
        name_to_index["Autopilot.TargetAirspeed"] = (int)VariableIndex::AUTOPILOT_TARGET_AIRSPEED;
        name_to_index["Autopilot.Aileron"] = (int)VariableIndex::AUTOPILOT_AILERON;
        name_to_index["Autopilot.Elevator"] = (int)VariableIndex::AUTOPILOT_ELEVATOR;
        name_to_index["AutoThrottle.Type"] = (int)VariableIndex::AUTO_THROTTLE_TYPE;
        name_to_index["Autopilot.ThrottleEngaged"] = (int)VariableIndex::AUTOPILOT_THROTTLE_ENGAGED;
        name_to_index["Autopilot.ThrottleCommand"] = (int)VariableIndex::AUTOPILOT_THROTTLE_COMMAND;
        
        // === FLIGHT DIRECTOR (181-183) ===
        name_to_index["FlightDirector.Pitch"] = (int)VariableIndex::FLIGHT_DIRECTOR_PITCH;
        name_to_index["FlightDirector.Bank"] = (int)VariableIndex::FLIGHT_DIRECTOR_BANK;
        name_to_index["FlightDirector.Yaw"] = (int)VariableIndex::FLIGHT_DIRECTOR_YAW;
        
        // === COPILOT (184-191) ===
        name_to_index["Copilot.Heading"] = (int)VariableIndex::COPILOT_HEADING;
        name_to_index["Copilot.Altitude"] = (int)VariableIndex::COPILOT_ALTITUDE;
        name_to_index["Copilot.Airspeed"] = (int)VariableIndex::COPILOT_AIRSPEED;
        name_to_index["Copilot.VerticalSpeed"] = (int)VariableIndex::COPILOT_VERTICAL_SPEED;
        name_to_index["Copilot.Aileron"] = (int)VariableIndex::COPILOT_AILERON;
        name_to_index["Copilot.Elevator"] = (int)VariableIndex::COPILOT_ELEVATOR;
        name_to_index["Copilot.Throttle"] = (int)VariableIndex::COPILOT_THROTTLE;
        name_to_index["Copilot.AutoRudder"] = (int)VariableIndex::COPILOT_AUTO_RUDDER;
        
        // === CONTROLS (192-260) ===
        name_to_index["Controls.Throttle"] = (int)VariableIndex::CONTROLS_THROTTLE;
        name_to_index["Controls.Throttle1"] = (int)VariableIndex::CONTROLS_THROTTLE_1;
        name_to_index["Controls.Throttle2"] = (int)VariableIndex::CONTROLS_THROTTLE_2;
        name_to_index["Controls.Throttle3"] = (int)VariableIndex::CONTROLS_THROTTLE_3;
        name_to_index["Controls.Throttle4"] = (int)VariableIndex::CONTROLS_THROTTLE_4;
        name_to_index["Controls.Throttle1Move"] = (int)VariableIndex::CONTROLS_THROTTLE_1_MOVE;
        name_to_index["Controls.Throttle2Move"] = (int)VariableIndex::CONTROLS_THROTTLE_2_MOVE;
        name_to_index["Controls.Throttle3Move"] = (int)VariableIndex::CONTROLS_THROTTLE_3_MOVE;
        name_to_index["Controls.Throttle4Move"] = (int)VariableIndex::CONTROLS_THROTTLE_4_MOVE;
        name_to_index["Controls.Pitch.Input"] = (int)VariableIndex::CONTROLS_PITCH_INPUT;
        name_to_index["Controls.Pitch.InputOffset"] = (int)VariableIndex::CONTROLS_PITCH_INPUT_OFFSET;
        name_to_index["Controls.Roll.Input"] = (int)VariableIndex::CONTROLS_ROLL_INPUT;
        name_to_index["Controls.Roll.InputOffset"] = (int)VariableIndex::CONTROLS_ROLL_INPUT_OFFSET;
        name_to_index["Controls.Yaw.Input"] = (int)VariableIndex::CONTROLS_YAW_INPUT;
        name_to_index["Controls.Yaw.InputActive"] = (int)VariableIndex::CONTROLS_YAW_INPUT_ACTIVE;
        name_to_index["Controls.Flaps"] = (int)VariableIndex::CONTROLS_FLAPS;
        name_to_index["Controls.FlapsEvent"] = (int)VariableIndex::CONTROLS_FLAPS_EVENT;
        name_to_index["Controls.Gear"] = (int)VariableIndex::CONTROLS_GEAR;
        name_to_index["Controls.GearToggle"] = (int)VariableIndex::CONTROLS_GEAR_TOGGLE;
        name_to_index["Controls.WheelBrake.Left"] = (int)VariableIndex::CONTROLS_WHEEL_BRAKE_LEFT;
        name_to_index["Controls.WheelBrake.Right"] = (int)VariableIndex::CONTROLS_WHEEL_BRAKE_RIGHT;
        name_to_index["Controls.WheelBrake.LeftActive"] = (int)VariableIndex::CONTROLS_WHEEL_BRAKE_LEFT_ACTIVE;
        name_to_index["Controls.WheelBrake.RightActive"] = (int)VariableIndex::CONTROLS_WHEEL_BRAKE_RIGHT_ACTIVE;
        name_to_index["Controls.AirBrake"] = (int)VariableIndex::CONTROLS_AIR_BRAKE;
        name_to_index["Controls.AirBrakeActive"] = (int)VariableIndex::CONTROLS_AIR_BRAKE_ACTIVE;
        name_to_index["Controls.AirBrake.Arm"] = (int)VariableIndex::CONTROLS_AIR_BRAKE_ARM;
        name_to_index["Controls.GliderAirBrake"] = (int)VariableIndex::CONTROLS_GLIDER_AIR_BRAKE;
        name_to_index["Controls.PropellerSpeed1"] = (int)VariableIndex::CONTROLS_PROPELLER_SPEED_1;
        name_to_index["Controls.PropellerSpeed2"] = (int)VariableIndex::CONTROLS_PROPELLER_SPEED_2;
        name_to_index["Controls.PropellerSpeed3"] = (int)VariableIndex::CONTROLS_PROPELLER_SPEED_3;
        name_to_index["Controls.PropellerSpeed4"] = (int)VariableIndex::CONTROLS_PROPELLER_SPEED_4;
        name_to_index["Controls.Mixture"] = (int)VariableIndex::CONTROLS_MIXTURE;
        name_to_index["Controls.Mixture1"] = (int)VariableIndex::CONTROLS_MIXTURE_1;
        name_to_index["Controls.Mixture2"] = (int)VariableIndex::CONTROLS_MIXTURE_2;
        name_to_index["Controls.Mixture3"] = (int)VariableIndex::CONTROLS_MIXTURE_3;
        name_to_index["Controls.Mixture4"] = (int)VariableIndex::CONTROLS_MIXTURE_4;
        name_to_index["Controls.ThrustReverse"] = (int)VariableIndex::CONTROLS_THRUST_REVERSE;
        name_to_index["Controls.ThrustReverse1"] = (int)VariableIndex::CONTROLS_THRUST_REVERSE_1;
        name_to_index["Controls.ThrustReverse2"] = (int)VariableIndex::CONTROLS_THRUST_REVERSE_2;
        name_to_index["Controls.ThrustReverse3"] = (int)VariableIndex::CONTROLS_THRUST_REVERSE_3;
        name_to_index["Controls.ThrustReverse4"] = (int)VariableIndex::CONTROLS_THRUST_REVERSE_4;
        name_to_index["Controls.Collective"] = (int)VariableIndex::CONTROLS_COLLECTIVE;
        name_to_index["Controls.CyclicPitch"] = (int)VariableIndex::CONTROLS_CYCLIC_PITCH;
        name_to_index["Controls.CyclicRoll"] = (int)VariableIndex::CONTROLS_CYCLIC_ROLL;
        name_to_index["Controls.TailRotor"] = (int)VariableIndex::CONTROLS_TAIL_ROTOR;
        name_to_index["Controls.RotorBrake"] = (int)VariableIndex::CONTROLS_ROTOR_BRAKE;
        name_to_index["Controls.HelicopterThrottle1"] = (int)VariableIndex::CONTROLS_HELICOPTER_THROTTLE_1;
        name_to_index["Controls.HelicopterThrottle2"] = (int)VariableIndex::CONTROLS_HELICOPTER_THROTTLE_2;
        name_to_index["Controls.Trim"] = (int)VariableIndex::CONTROLS_TRIM;
        name_to_index["Controls.TrimStep"] = (int)VariableIndex::CONTROLS_TRIM_STEP;
        name_to_index["Controls.TrimMove"] = (int)VariableIndex::CONTROLS_TRIM_MOVE;
        name_to_index["Controls.AileronTrim"] = (int)VariableIndex::CONTROLS_AILERON_TRIM;
        name_to_index["Controls.RudderTrim"] = (int)VariableIndex::CONTROLS_RUDDER_TRIM;
        name_to_index["Controls.Tiller"] = (int)VariableIndex::CONTROLS_TILLER;
        name_to_index["Controls.PedalsDisconnect"] = (int)VariableIndex::CONTROLS_PEDALS_DISCONNECT;
        name_to_index["Controls.NoseWheelSteering"] = (int)VariableIndex::CONTROLS_NOSE_WHEEL_STEERING;
        name_to_index["Controls.Lighting.Panel"] = (int)VariableIndex::CONTROLS_LIGHTING_PANEL;
        name_to_index["Controls.Lighting.Instruments"] = (int)VariableIndex::CONTROLS_LIGHTING_INSTRUMENTS;
        name_to_index["Controls.PressureSetting0"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_0;
        name_to_index["Controls.PressureSettingStandard0"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_STANDARD_0;
        name_to_index["Controls.PressureSettingUnit0"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_UNIT_0;
        name_to_index["Controls.PressureSetting1"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_1;
        name_to_index["Controls.PressureSettingStandard1"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_STANDARD_1;
        name_to_index["Controls.PressureSettingUnit1"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_UNIT_1;
        name_to_index["Controls.PressureSetting2"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_2;
        name_to_index["Controls.PressureSettingStandard2"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_STANDARD_2;
        name_to_index["Controls.PressureSettingUnit2"] = (int)VariableIndex::CONTROLS_PRESSURE_SETTING_UNIT_2;
        name_to_index["Controls.TransitionAltitude"] = (int)VariableIndex::CONTROLS_TRANSITION_ALTITUDE;
        name_to_index["Controls.TransitionLevel"] = (int)VariableIndex::CONTROLS_TRANSITION_LEVEL;
        
        // === PRESSURIZATION (261-262) ===
        name_to_index["Pressurization.LandingElevation"] = (int)VariableIndex::PRESSURIZATION_LANDING_ELEVATION;
        name_to_index["Pressurization.LandingElevationManual"] = (int)VariableIndex::PRESSURIZATION_LANDING_ELEVATION_MANUAL;
        
        // === WARNINGS (263-272) ===
        name_to_index["Warnings.MasterWarning"] = (int)VariableIndex::WARNINGS_MASTER_WARNING;
        name_to_index["Warnings.MasterCaution"] = (int)VariableIndex::WARNINGS_MASTER_CAUTION;
        name_to_index["Warnings.EngineFire"] = (int)VariableIndex::WARNINGS_ENGINE_FIRE;
        name_to_index["Warnings.LowOilPressure"] = (int)VariableIndex::WARNINGS_LOW_OIL_PRESSURE;
        name_to_index["Warnings.LowFuelPressure"] = (int)VariableIndex::WARNINGS_LOW_FUEL_PRESSURE;
        name_to_index["Warnings.LowHydraulicPressure"] = (int)VariableIndex::WARNINGS_LOW_HYDRAULIC_PRESSURE;
        name_to_index["Warnings.LowVoltage"] = (int)VariableIndex::WARNINGS_LOW_VOLTAGE;
        name_to_index["Warnings.AltitudeAlert"] = (int)VariableIndex::WARNINGS_ALTITUDE_ALERT;
        name_to_index["Warnings.WarningActive"] = (int)VariableIndex::WARNINGS_WARNING_ACTIVE;
        name_to_index["Warnings.WarningMute"] = (int)VariableIndex::WARNINGS_WARNING_MUTE;
        
        // === VIEW CONTROLS (273-302) ===
        name_to_index["View.DisplayName"] = (int)VariableIndex::VIEW_DISPLAY_NAME;
        name_to_index["View.Internal"] = (int)VariableIndex::VIEW_INTERNAL;
        name_to_index["View.Follow"] = (int)VariableIndex::VIEW_FOLLOW;
        name_to_index["View.External"] = (int)VariableIndex::VIEW_EXTERNAL;
        name_to_index["View.Category"] = (int)VariableIndex::VIEW_CATEGORY;
        name_to_index["View.Mode"] = (int)VariableIndex::VIEW_MODE;
        name_to_index["View.Zoom"] = (int)VariableIndex::VIEW_ZOOM;
        name_to_index["View.Pan.Horizontal"] = (int)VariableIndex::VIEW_PAN_HORIZONTAL;
        name_to_index["View.Pan.HorizontalMove"] = (int)VariableIndex::VIEW_PAN_HORIZONTAL_MOVE;
        name_to_index["View.Pan.Vertical"] = (int)VariableIndex::VIEW_PAN_VERTICAL;
        name_to_index["View.Pan.VerticalMove"] = (int)VariableIndex::VIEW_PAN_VERTICAL_MOVE;
        name_to_index["View.Pan.Center"] = (int)VariableIndex::VIEW_PAN_CENTER;
        name_to_index["View.Look.Horizontal"] = (int)VariableIndex::VIEW_LOOK_HORIZONTAL;
        name_to_index["View.Look.Vertical"] = (int)VariableIndex::VIEW_LOOK_VERTICAL;
        name_to_index["View.Roll"] = (int)VariableIndex::VIEW_ROLL;
        name_to_index["View.OffsetX"] = (int)VariableIndex::VIEW_OFFSET_X;
        name_to_index["View.OffsetXMove"] = (int)VariableIndex::VIEW_OFFSET_X_MOVE;
        name_to_index["View.OffsetY"] = (int)VariableIndex::VIEW_OFFSET_Y;
        name_to_index["View.OffsetYMove"] = (int)VariableIndex::VIEW_OFFSET_Y_MOVE;
        name_to_index["View.OffsetZ"] = (int)VariableIndex::VIEW_OFFSET_Z;
        name_to_index["View.OffsetZMove"] = (int)VariableIndex::VIEW_OFFSET_Z_MOVE;
        name_to_index["View.Position"] = (int)VariableIndex::VIEW_POSITION;
        name_to_index["View.Direction"] = (int)VariableIndex::VIEW_DIRECTION;
        name_to_index["View.Up"] = (int)VariableIndex::VIEW_UP;
        name_to_index["View.FieldOfView"] = (int)VariableIndex::VIEW_FIELD_OF_VIEW;
        name_to_index["View.AspectRatio"] = (int)VariableIndex::VIEW_ASPECT_RATIO;
        name_to_index["View.FreePosition"] = (int)VariableIndex::VIEW_FREE_POSITION;
        name_to_index["View.FreeLookDirection"] = (int)VariableIndex::VIEW_FREE_LOOK_DIRECTION;
        name_to_index["View.FreeUp"] = (int)VariableIndex::VIEW_FREE_UP;
        name_to_index["View.FreeFieldOfView"] = (int)VariableIndex::VIEW_FREE_FIELD_OF_VIEW;
        
        // === SIMULATION CONTROLS (303-320) ===
        name_to_index["Simulation.Pause"] = (int)VariableIndex::SIMULATION_PAUSE;
        name_to_index["Simulation.FlightInformation"] = (int)VariableIndex::SIMULATION_FLIGHT_INFORMATION;
        name_to_index["Simulation.MovingMap"] = (int)VariableIndex::SIMULATION_MOVING_MAP;
        name_to_index["Simulation.Sound"] = (int)VariableIndex::SIMULATION_SOUND;
        name_to_index["Simulation.LiftUp"] = (int)VariableIndex::SIMULATION_LIFT_UP;
        name_to_index["Simulation.SettingPosition"] = (int)VariableIndex::SIMULATION_SETTING_POSITION;
        name_to_index["Simulation.SettingOrientation"] = (int)VariableIndex::SIMULATION_SETTING_ORIENTATION;
        name_to_index["Simulation.SettingVelocity"] = (int)VariableIndex::SIMULATION_SETTING_VELOCITY;
        name_to_index["Simulation.SettingSet"] = (int)VariableIndex::SIMULATION_SETTING_SET;
        name_to_index["Simulation.TimeChange"] = (int)VariableIndex::SIMULATION_TIME_CHANGE;
        name_to_index["Simulation.Visibility"] = (int)VariableIndex::SIMULATION_VISIBILITY;
        name_to_index["Simulation.Time"] = (int)VariableIndex::SIMULATION_TIME;
        name_to_index["Simulation.UseMouseControl"] = (int)VariableIndex::SIMULATION_USE_MOUSE_CONTROL;
        name_to_index["Simulation.PlaybackStart"] = (int)VariableIndex::SIMULATION_PLAYBACK_START;
        name_to_index["Simulation.PlaybackStop"] = (int)VariableIndex::SIMULATION_PLAYBACK_STOP;
        name_to_index["Simulation.PlaybackPosition"] = (int)VariableIndex::SIMULATION_PLAYBACK_SET_POSITION;
        name_to_index["Simulation.ExternalPosition"] = (int)VariableIndex::SIMULATION_EXTERNAL_POSITION;
        name_to_index["Simulation.ExternalOrientation"] = (int)VariableIndex::SIMULATION_EXTERNAL_ORIENTATION;
        
        // === COMMAND CONTROLS (321-330) ===
        name_to_index["Command.Execute"] = (int)VariableIndex::COMMAND_EXECUTE;
        name_to_index["Command.Back"] = (int)VariableIndex::COMMAND_BACK;
        name_to_index["Command.Up"] = (int)VariableIndex::COMMAND_UP;
        name_to_index["Command.Down"] = (int)VariableIndex::COMMAND_DOWN;
        name_to_index["Command.Left"] = (int)VariableIndex::COMMAND_LEFT;
        name_to_index["Command.Right"] = (int)VariableIndex::COMMAND_RIGHT;
        name_to_index["Command.MoveHorizontal"] = (int)VariableIndex::COMMAND_MOVE_HORIZONTAL;
        name_to_index["Command.MoveVertical"] = (int)VariableIndex::COMMAND_MOVE_VERTICAL;
        name_to_index["Command.Rotate"] = (int)VariableIndex::COMMAND_ROTATE;
        name_to_index["Command.Zoom"] = (int)VariableIndex::COMMAND_ZOOM;
        
        // === SPECIAL/IGNORE VARIABLES (331-338) ===
        name_to_index["Controls.Speed"] = (int)VariableIndex::CONTROLS_SPEED;
        name_to_index["FlightManagementSystem.Data0"] = (int)VariableIndex::FMS_DATA_0;
        name_to_index["FlightManagementSystem.Data1"] = (int)VariableIndex::FMS_DATA_1;
        name_to_index["Navigation.NAV1Data"] = (int)VariableIndex::NAV1_DATA;
        name_to_index["Navigation.NAV2Data"] = (int)VariableIndex::NAV2_DATA;
        name_to_index["Navigation.NAV3Data"] = (int)VariableIndex::NAV3_DATA;
        name_to_index["Navigation.ILS1Data"] = (int)VariableIndex::ILS1_DATA;
        name_to_index["Navigation.ILS2Data"] = (int)VariableIndex::ILS2_DATA;


///////////////////////////////////////////////////////////////////////////////////////////////////
// VARIABLE MAPPER EXTENSION
///////////////////////////////////////////////////////////////////////////////////////////////////


        // === CESSNA 172 SPECIFIC CONTROLS ===
        name_to_index["Controls.FuelSelector"] = (int)VariableIndex::C172_FUEL_SELECTOR;
        name_to_index["Controls.FuelShutOff"] = (int)VariableIndex::C172_FUEL_SHUT_OFF;
        name_to_index["Controls.HideYoke.Left"] = (int)VariableIndex::C172_HIDE_YOKE_LEFT;
        name_to_index["Controls.HideYoke.Right"] = (int)VariableIndex::C172_HIDE_YOKE_RIGHT;
        name_to_index["Controls.LeftSunBlocker"] = (int)VariableIndex::C172_LEFT_SUN_BLOCKER;
        name_to_index["Controls.RightSunBlocker"] = (int)VariableIndex::C172_RIGHT_SUN_BLOCKER;
        name_to_index["Controls.Lighting.LeftCabinOverheadLight"] = (int)VariableIndex::C172_LEFT_CABIN_LIGHT;
        name_to_index["Controls.Lighting.RightCabinOverheadLight"] = (int)VariableIndex::C172_RIGHT_CABIN_LIGHT;
        name_to_index["Controls.Magnetos1"] = (int)VariableIndex::C172_MAGNETOS_1;
        name_to_index["Controls.ParkingBrakeHandle"] = (int)VariableIndex::C172_PARKING_BRAKE_HANDLE;
        name_to_index["Controls.TrimWheel"] = (int)VariableIndex::C172_TRIM_WHEEL;
        name_to_index["LeftYoke.Button"] = (int)VariableIndex::C172_LEFT_YOKE_BUTTON;
        name_to_index["Doors.Left"] = (int)VariableIndex::C172_LEFT_DOOR;
        name_to_index["Doors.LeftHandle"] = (int)VariableIndex::C172_LEFT_DOOR_HANDLE;
        name_to_index["Doors.Right"] = (int)VariableIndex::C172_RIGHT_DOOR;
        name_to_index["Doors.RightHandle"] = (int)VariableIndex::C172_RIGHT_DOOR_HANDLE;
        name_to_index["Windows.Left"] = (int)VariableIndex::C172_LEFT_WINDOW;
        name_to_index["Windows.Right"] = (int)VariableIndex::C172_RIGHT_WINDOW;
        
        // Hash mappings will be initialized later after MESSAGE_LIST definitions
        // For now, name mappings are sufficient for CommandProcessor functionality
        
        // NOTE: COMPLETE mapping with ALL 339 variables from VariableIndex enum
        // Comprehensive coverage for full bidirectional control and monitoring
    }
    
    int GetIndex(const std::string& name) const {
        // Finds the logical index for a human-readable variable name.
        auto it = name_to_index.find(name);
        return (it != name_to_index.end()) ? it->second : -1;
    }
    
    int GetIndex(uint64_t hash) const {
        // Optional: lookup by hashed id for fast comparisons
        auto it = hash_to_index.find(hash);
        return (it != hash_to_index.end()) ? it->second : -1;
    }

    // Snapshot for exporting mapping to external tools (Python, etc.)
    std::vector<std::pair<std::string, int>> GetNameToIndexSnapshot() const {
        std::vector<std::pair<std::string, int>> out;
        out.reserve(name_to_index.size());
        for (const auto& kv : name_to_index) {
            out.emplace_back(kv.first, kv.second);
        }
        return out;
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// MESSAGE DEFINITIONS - All 339 Variables from SDK + Extensions
///////////////////////////////////////////////////////////////////////////////////////////////////

#define TM_MESSAGE( a1, a2, a3, a4, a5, a6, a7 )       static tm_external_message Message##a1( ##a2, a3, a4, a5, a6 );

// Include the complete MESSAGE_LIST from original SDK
#define MESSAGE_LIST(F) \
F( AircraftUniversalTime,                 "Aircraft.UniversalTime",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Second,                   "universal time of day (UTC)" ) \
F( AircraftAltitude,                      "Aircraft.Altitude",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "altitude as measured by altimeter" ) \
F( AircraftVerticalSpeed,                 "Aircraft.VerticalSpeed",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "vertical speed" ) \
F( AircraftPitch,                         "Aircraft.Pitch",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "pitch angle" ) \
F( AircraftBank,                          "Aircraft.Bank",                            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "bank angle" ) \
F( AircraftIndicatedAirspeed,             "Aircraft.IndicatedAirspeed",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "indicated airspeed" ) \
F( AircraftIndicatedAirspeedTrend,        "Aircraft.IndicatedAirspeedTrend",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "indicated airspeed trend" ) \
F( AircraftGroundSpeed,                   "Aircraft.GroundSpeed",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "ground speed" ) \
F( AircraftMagneticHeading,               "Aircraft.MagneticHeading",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "" ) \
F( AircraftTrueHeading,                   "Aircraft.TrueHeading",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "" ) \
F( AircraftLatitude,                      "Aircraft.Latitude",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "" ) \
F( AircraftLongitude,                     "Aircraft.Longitude",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "" ) \
F( AircraftHeight,                        "Aircraft.Height",                          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "" ) \
F( AircraftPosition,                      "Aircraft.Position",                        tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "" ) \
F( AircraftOrientation,                   "Aircraft.Orientation",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftVelocity,                      "Aircraft.Velocity",                        tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "velocity vector in body system if 'Body' flag is set, in global system otherwise" ) \
F( AircraftAngularVelocity,               "Aircraft.AngularVelocity",                 tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::RadiantPerSecond,         "angular velocity in body system if 'Body' flag is set (roll rate pitch rate yaw rate) in global system" ) \
F( AircraftAcceleration,                  "Aircraft.Acceleration",                    tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecondSquared,    "aircraft acceleration in body system if 'Body' flag is set, in global system otherwise" ) \
F( AircraftGravity,                       "Aircraft.Gravity",                         tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecondSquared,    "gravity acceleration in body system if 'Body' flag is set" ) \
F( AircraftWind,                          "Aircraft.Wind",                            tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "wind vector at current aircraft position" ) \
F( AircraftRateOfTurn,                    "Aircraft.RateOfTurn",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::RadiantPerSecond,         "rate of turn" ) \
F( AircraftMachNumber,                    "Aircraft.MachNumber",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "mach number" ) \
F( AircraftAngleOfAttack,                 "Aircraft.AngleOfAttack",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "angle of attack indicator" ) \
F( AircraftAngleOfAttackLimit,            "Aircraft.AngleOfAttackLimit",              tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "angle of attack limit (stall)" ) \
F( AircraftAccelerationLimit,             "Aircraft.AccelerationLimit",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecondSquared,    "acceleration limit (g-load max/min)" ) \
F( AircraftGear,                          "Aircraft.Gear",                            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "current gear position, zero is up, one is down, in between in transit" ) \
F( AircraftFlaps,                         "Aircraft.Flaps",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "selected flaps" ) \
F( AircraftSlats,                         "Aircraft.Slats",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "selected slats" ) \
F( AircraftThrottle,                      "Aircraft.Throttle",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "current throttle setting" ) \
F( AircraftAirBrake,                      "Aircraft.AirBrake",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftGroundSpoilersArmed,           "Aircraft.GroundSpoilersArmed",             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto ground spoiler armed" ) \
F( AircraftGroundSpoilersExtended,        "Aircraft.GroundSpoilersExtended",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto ground spoiler extended" ) \
F( AircraftParkingBrake,                  "Aircraft.ParkingBrake",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "parking brake" ) \
F( AircraftAutoBrakeSetting,              "Aircraft.AutoBrakeSetting",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto brake position" ) \
F( AircraftAutoBrakeEngaged,              "Aircraft.AutoBrakeEngaged",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto brake engaged" ) \
F( AircraftAutoBrakeRejectedTakeOff,      "Aircraft.AutoBrakeRejectedTakeOff",        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto brake RTO armed" ) \
F( AircraftRadarAltitude,                 "Aircraft.RadarAltitude",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "radar altitude above ground" ) \
F( AircraftName,                          "Aircraft.Name",                            tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "current aircraft short name ( name of folder in aircraft directory, eg c172 )" ) \
F( AircraftNearestAirportIdentifier,      "Aircraft.NearestAirportIdentifier",        tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftNearestAirportName,            "Aircraft.NearestAirportName",              tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftNearestAirportLocation,        "Aircraft.NearestAirportLocation",          tm_msg_data_type::Vector2d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftNearestAirportElevation,       "Aircraft.NearestAirportElevation",         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestAirportIdentifier,         "Aircraft.BestAirportIdentifier",           tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestAirportName,               "Aircraft.BestAirportName",                 tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestAirportLocation,           "Aircraft.BestAirportLocation",             tm_msg_data_type::Vector2d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestAirportElevation,          "Aircraft.BestAirportElevation",            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestRunwayIdentifier,          "Aircraft.BestRunwayIdentifier",            tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestRunwayElevation,           "Aircraft.BestRunwayElevation",             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestRunwayThreshold,           "Aircraft.BestRunwayThreshold",             tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftBestRunwayEnd,                 "Aircraft.BestRunwayEnd",                   tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftCategoryJet,                   "Aircraft.Category.Jet",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftCategoryGlider,                "Aircraft.Category.Glider",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftOnGround,                      "Aircraft.OnGround",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "set if aircraft is on ground" ) \
F( AircraftOnRunway,                      "Aircraft.OnRunway",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "set if aircraft is on ground and on a runway" ) \
F( AircraftCrashed,                       "Aircraft.Crashed",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftPower,                         "Aircraft.Power",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftNormalizedPower,               "Aircraft.NormalizedPower",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftNormalizedPowerTarget,         "Aircraft.NormalizedPowerTarget",           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftTrim,                          "Aircraft.Trim",                            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftPitchTrim,                     "Aircraft.PitchTrim",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftPitchTrimScaling,              "Aircraft.PitchTrimScaling",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftPitchTrimOffset,               "Aircraft.PitchTrimOffset",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftRudderTrim,                    "Aircraft.RudderTrim",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftAutoPitchTrim,                 "Aircraft.AutoPitchTrim",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "automatic pitch trim active (FBW)" ) \
F( AircraftYawDamperEnabled,              "Aircraft.YawDamperEnabled",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "automatic rudder damping active (yaw damper)" ) \
F( AircraftRudderPedalsDisconnected,      "Aircraft.RudderPedalsDisconnected",        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "steering disconnect button active" ) \
F( AircraftStarter,                       "Aircraft.Starter",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "generic engine starter" ) \
F( AircraftStarter1,                      "Aircraft.Starter1",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 1 starter" ) \
F( AircraftStarter2,                      "Aircraft.Starter2",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 2 starter" ) \
F( AircraftStarter3,                      "Aircraft.Starter3",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 3 starter" ) \
F( AircraftStarter4,                      "Aircraft.Starter4",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 4 starter" ) \
F( AircraftIgnition,                      "Aircraft.Ignition",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "generic engine ignition" ) \
F( AircraftIgnition1,                     "Aircraft.Ignition1",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 1 ignition" ) \
F( AircraftIgnition2,                     "Aircraft.Ignition2",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 2 ignition" ) \
F( AircraftIgnition3,                     "Aircraft.Ignition3",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 3 ignition" ) \
F( AircraftIgnition4,                     "Aircraft.Ignition4",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 4 ignition" ) \
F( AircraftThrottleLimit,                 "Aircraft.ThrottleLimit",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine throttle limit (max throttle for takeoff)" ) \
F( AircraftReverse,                       "Aircraft.Reverse",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine reverse thrust selected" ) \
F( AircraftEngineMaster1,                 "Aircraft.EngineMaster1",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 1 master switch" ) \
F( AircraftEngineMaster2,                 "Aircraft.EngineMaster2",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 2 master switch" ) \
F( AircraftEngineMaster3,                 "Aircraft.EngineMaster3",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 3 master switch" ) \
F( AircraftEngineMaster4,                 "Aircraft.EngineMaster4",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 4 master switch" ) \
F( AircraftEngineThrottle1,               "Aircraft.EngineThrottle1",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 1 throttle position" ) \
F( AircraftEngineThrottle2,               "Aircraft.EngineThrottle2",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 2 throttle position" ) \
F( AircraftEngineThrottle3,               "Aircraft.EngineThrottle3",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 3 throttle position" ) \
F( AircraftEngineThrottle4,               "Aircraft.EngineThrottle4",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine 4 throttle position" ) \
F( AircraftEngineRotationSpeed1,          "Aircraft.EngineRotationSpeed1",            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRotationSpeed2,          "Aircraft.EngineRotationSpeed2",            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRotationSpeed3,          "Aircraft.EngineRotationSpeed3",            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRotationSpeed4,          "Aircraft.EngineRotationSpeed4",            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRunning1,                "Aircraft.EngineRunning1",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRunning2,                "Aircraft.EngineRunning2",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRunning3,                "Aircraft.EngineRunning3",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftEngineRunning4,                "Aircraft.EngineRunning4",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( AircraftAPUAvailable,                  "Aircraft.APUAvailable",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( PerformanceSpeedVS0,                   "Performance.Speed.VS0",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "minimum speed with flaps down, lower end of white arc" ) \
F( PerformanceSpeedVS1,                   "Performance.Speed.VS1",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "minimum speed with flaps retracted, lower end of green arc" ) \
F( PerformanceSpeedVFE,                   "Performance.Speed.VFE",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "maximum speed with flaps extended, upper end of white arc" ) \
F( PerformanceSpeedVNO,                   "Performance.Speed.VNO",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "maneuvering speed, lower end of yellow arc" ) \
F( PerformanceSpeedVNE,                   "Performance.Speed.VNE",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "never exceed speed, red line" ) \
F( PerformanceSpeedVAPP,                  "Performance.Speed.VAPP",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "approach airspeed" ) \
F( PerformanceSpeedMinimum,               "Performance.Speed.Minimum",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "stall speed in current configuration" ) \
F( PerformanceSpeedMaximum,               "Performance.Speed.Maximum",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "maximum speed in current configuration" ) \
F( PerformanceSpeedMinimumFlapRetraction, "Performance.Speed.MinimumFlapRetraction",  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "minimum speed for next flap up" ) \
F( PerformanceSpeedMaximumFlapExtension,  "Performance.Speed.MaximumFlapExtension",   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "maximum speed for next flap down" ) \
F( ConfigurationSelectedTakeOffFlaps,     "Configuration.SelectedTakeOffFlaps",       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "FMS selected takeoff flaps" ) \
F( ConfigurationSelectedLandingFlaps,     "Configuration.SelectedLandingFlaps",       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "FMS selected landing flaps" ) \
F( FMSFlightNumber,                       "FlightManagementSystem.FlightNumber",      tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "FMS flight number" ) \
F( NavigationSelectedCourse1,             "Navigation.SelectedCourse1",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Radiant,                  "NAV1 selected course (OBS1)" ) \
F( NavigationSelectedCourse2,             "Navigation.SelectedCourse2",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Radiant,                  "NAV2 selected course (OBS2)" ) \
F( NavigationNAV1Identifier,              "Navigation.NAV1Identifier",                tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "NAV1 station identifier" ) \
F( NavigationNAV1Frequency,               "Navigation.NAV1Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "NAV1 receiver active frequency" ) \
F( NavigationNAV1StandbyFrequency,        "Navigation.NAV1StandbyFrequency",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "NAV1 receiver standby frequency" ) \
F( NavigationNAV1FrequencySwap,           "Navigation.NAV1FrequencySwap",             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "NAV1 frequency swap" ) \
F( NavigationNAV2Identifier,              "Navigation.NAV2Identifier",                tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "NAV2 station identifier" ) \
F( NavigationNAV2Frequency,               "Navigation.NAV2Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "NAV2 receiver active frequency" ) \
F( NavigationNAV2StandbyFrequency,        "Navigation.NAV2StandbyFrequency",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "NAV2 receiver standby frequency" ) \
F( NavigationNAV2FrequencySwap,           "Navigation.NAV2FrequencySwap",             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "NAV2 frequency swap" ) \
F( NavigationDME1Frequency,               "Navigation.DME1Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "DME1 active frequency" ) \
F( NavigationDME1Distance,                "Navigation.DME1Distance",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "DME1 distance (nautical miles)" ) \
F( NavigationDME1Time,                    "Navigation.DME1Time",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Second,                   "DME1 time (seconds)" ) \
F( NavigationDME1Speed,                   "Navigation.DME1Speed",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::MeterPerSecond,           "DME1 speed (m/s)" ) \
F( NavigationDME2Frequency,               "Navigation.DME2Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "DME2 active frequency" ) \
F( NavigationDME2Distance,                "Navigation.DME2Distance",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "DME2 distance (nautical miles)" ) \
F( NavigationDME2Time,                    "Navigation.DME2Time",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Second,                   "DME2 time (seconds)" ) \
F( NavigationDME2Speed,                   "Navigation.DME2Speed",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::MeterPerSecond,           "DME2 speed (m/s)" ) \
F( NavigationILS1Identifier,              "Navigation.ILS1Identifier",                tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "ILS1 station identifier" ) \
F( NavigationILS1Course,                  "Navigation.ILS1Course",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Radiant,                  "ILS1 selected course" ) \
F( NavigationILS1Frequency,               "Navigation.ILS1Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ILS1 receiver active frequency" ) \
F( NavigationILS1StandbyFrequency,        "Navigation.ILS1StandbyFrequency",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ILS1 receiver standby frequency" ) \
F( NavigationILS1FrequencySwap,           "Navigation.ILS1FrequencySwap",             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "ILS1 frequency swap" ) \
F( NavigationILS2Identifier,              "Navigation.ILS2Identifier",                tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "ILS2 station identifier" ) \
F( NavigationILS2Course,                  "Navigation.ILS2Course",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Radiant,                  "ILS2 selected course" ) \
F( NavigationILS2Frequency,               "Navigation.ILS2Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ILS2 receiver active frequency" ) \
F( NavigationILS2StandbyFrequency,        "Navigation.ILS2StandbyFrequency",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ILS2 receiver standby frequency" ) \
F( NavigationILS2FrequencySwap,           "Navigation.ILS2FrequencySwap",             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "ILS2 frequency swap" ) \
F( NavigationADF1Frequency,               "Navigation.ADF1Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ADF1 receiver active frequency" ) \
F( NavigationADF1StandbyFrequency,        "Navigation.ADF1StandbyFrequency",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ADF1 receiver standby frequency" ) \
F( NavigationADF1FrequencySwap,           "Navigation.ADF1FrequencySwap",             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "ADF1 frequency swap" ) \
F( NavigationADF2Frequency,               "Navigation.ADF2Frequency",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ADF2 receiver active frequency" ) \
F( NavigationADF2StandbyFrequency,        "Navigation.ADF2StandbyFrequency",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "ADF2 receiver standby frequency" ) \
F( NavigationADF2FrequencySwap,           "Navigation.ADF2FrequencySwap",             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "ADF2 frequency swap" ) \
F( NavigationCOM1Frequency,               "Communication.COM1Frequency",              tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "COM1 transceiver active frequency" ) \
F( NavigationCOM1StandbyFrequency,        "Communication.COM1StandbyFrequency",       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "COM1 transceiver standby frequency" ) \
F( NavigationCOM1FrequencySwap,           "Communication.COM1FrequencySwap",          tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "COM1 frequency swap" ) \
F( NavigationCOM2Frequency,               "Communication.COM2Frequency",              tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "COM2 transceiver active frequency" ) \
F( NavigationCOM2StandbyFrequency,        "Communication.COM2StandbyFrequency",       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "COM2 transceiver standby frequency" ) \
F( NavigationCOM2FrequencySwap,           "Communication.COM2FrequencySwap",          tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "COM2 frequency swap" ) \
F( NavigationCOM3Frequency,               "Communication.COM3Frequency",              tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "COM3 transceiver active frequency" ) \
F( NavigationCOM3StandbyFrequency,        "Communication.COM3StandbyFrequency",       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Hertz,                    "COM3 transceiver standby frequency" ) \
F( NavigationCOM3FrequencySwap,           "Communication.COM3FrequencySwap",          tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "COM3 frequency swap" ) \
F( TransponderCode,                       "Communication.TransponderCode",            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "Transponder code" ) \
F( TransponderCursor,                     "Communication.TransponderCursor",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "Transponder blinking cursor position" ) \
F( AutopilotMaster,                       "Autopilot.Master",                         tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( AutopilotDisengage,                    "Autopilot.Disengage",                      tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "disengage all autopilots" ) \
F( AutopilotHeading,                      "Autopilot.Heading",                        tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::Radiant,                  "" ) \
F( AutopilotVerticalSpeed,                "Autopilot.VerticalSpeed",                  tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::MeterPerSecond,           "" ) \
F( AutopilotSelectedSpeed,                "Autopilot.SelectedSpeed",                  tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::MeterPerSecond,           "" ) \
F( AutopilotSelectedAirspeed,             "Autopilot.SelectedAirspeed",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::MeterPerSecond,           "autopilot/flight director selected airspeed, speed bug" ) \
F( AutopilotSelectedHeading,              "Autopilot.SelectedHeading",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Radiant,                  "autopilot/flight director selected heading, heading bug" ) \
F( AutopilotSelectedAltitude,             "Autopilot.SelectedAltitude",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Meter,                    "autopilot/flight director selected altitude" ) \
F( AutopilotSelectedVerticalSpeed,        "Autopilot.SelectedVerticalSpeed",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::MeterPerSecond,           "autopilot/flight director selected vertical speed" ) \
F( AutopilotSelectedAltitudeScale,        "Autopilot.SelectedAltitudeScale",          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director selected altitude step size small/large" ) \
F( AutopilotActiveLateralMode,            "Autopilot.ActiveLateralMode",              tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name of the active lateral mode" ) \
F( AutopilotArmedLateralMode,             "Autopilot.ArmedLateralMode",               tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name of the armed lateral mode" ) \
F( AutopilotActiveVerticalMode,           "Autopilot.ActiveVerticalMode",             tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name of the active vertical mode" ) \
F( AutopilotArmedVerticalMode,            "Autopilot.ArmedVerticalMode",              tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name of the armed vertical mode" ) \
F( AutopilotArmedApproachMode,            "Autopilot.ArmedApproachMode",              tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name of the armed approach mode" ) \
F( AutopilotActiveAutoThrottleMode,       "Autopilot.ActiveAutoThrottleMode",         tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name the active autothrottle mode" ) \
F( AutopilotActiveCollectiveMode,         "Autopilot.ActiveCollectiveMode",           tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name the active helicopter collective mode" ) \
F( AutopilotArmedCollectiveMode,          "Autopilot.ArmedCollectiveMode",            tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot/flight director internal name the armed helicopter collective mode" ) \
F( AutopilotType,                         "Autopilot.Type",                           tm_msg_data_type::String,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot type installed" ) \
F( AutopilotEngaged,                      "Autopilot.Engaged",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "set if autopilot is engaged" ) \
F( AutopilotUseMachNumber,                "Autopilot.UseMachNumber",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot mach/speed toggle state" ) \
F( AutopilotSpeedManaged,                 "Autopilot.SpeedManaged",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot managed/selected speed state" ) \
F( AutopilotTargetAirspeed,               "Autopilot.TargetAirspeed",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot target airspeed" ) \
F( AutopilotAileron,                      "Autopilot.Aileron",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot aileron command" ) \
F( AutopilotElevator,                     "Autopilot.Elevator",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "autopilot elevator command" ) \
F( AutoAutoThrottleType,                  "AutoThrottle.Type",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto-throttle type installed" ) \
F( AutopilotThrottleEngaged,              "Autopilot.ThrottleEngaged",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto-throttle state" ) \
F( AutopilotThrottleCommand,              "Autopilot.ThrottleCommand",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "auto-throttle command" ) \
F( FlightDirectorPitch,                   "FlightDirector.Pitch",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "flight director pitch angle relative to current pitch" ) \
F( FlightDirectorBank,                    "FlightDirector.Bank",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "flight director bank angle relative to current bank" ) \
F( FlightDirectorYaw,                     "FlightDirector.Yaw",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "flight director yaw command" ) \
F( CopilotHeading,                        "Copilot.Heading",                          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Radiant,                  "" ) \
F( CopilotAltitude,                       "Copilot.Altitude",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "" ) \
F( CopilotAirspeed,                       "Copilot.Airspeed",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "" ) \
F( CopilotVerticalSpeed,                  "Copilot.VerticalSpeed",                    tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::MeterPerSecond,           "" ) \
F( CopilotAileron,                        "Copilot.Aileron",                          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( CopilotElevator,                       "Copilot.Elevator",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( CopilotThrottle,                       "Copilot.Throttle",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( CopilotAutoRudder,                     "Copilot.AutoRudder",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Read,      tm_msg_unit::None,                     "" ) \
F( ControlsThrottle,                      "Controls.Throttle",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "generic throttle position" ) \
F( ControlsThrottle1,                     "Controls.Throttle1",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "throttle position for engine 1" ) \
F( ControlsThrottle2,                     "Controls.Throttle2",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "throttle position for engine 2" ) \
F( ControlsThrottle3,                     "Controls.Throttle3",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "throttle position for engine 3" ) \
F( ControlsThrottle4,                     "Controls.Throttle4",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "throttle position for engine 4" ) \
F( ControlsThrottle1Move,                 "Controls.Throttle1",                       tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::PerSecond,                "throttle rate of change for engine 1" ) \
F( ControlsThrottle2Move,                 "Controls.Throttle2",                       tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::PerSecond,                "throttle rate of change for engine 2" ) \
F( ControlsThrottle3Move,                 "Controls.Throttle3",                       tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::PerSecond,                "throttle rate of change for engine 3" ) \
F( ControlsThrottle4Move,                 "Controls.Throttle4",                       tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::PerSecond,                "throttle rate of change for engine 4" ) \
F( ControlsPitchInput,                    "Controls.Pitch.Input",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPitchInputOffset,              "Controls.Pitch.Input",                     tm_msg_data_type::Double,   tm_msg_flag::Offset, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsRollInput,                     "Controls.Roll.Input",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsRollInputOffset,               "Controls.Roll.Input",                      tm_msg_data_type::Double,   tm_msg_flag::Offset, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsYawInput,                      "Controls.Yaw.Input",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsYawInputActive,                "Controls.Yaw.Input",                       tm_msg_data_type::Double,   tm_msg_flag::Active, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsFlaps,                         "Controls.Flaps",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "" ) \
F( ControlsFlapsEvent,                    "Controls.Flaps",                           tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsGear,                          "Controls.Gear",                            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "gear lever" ) \
F( ControlsGearToggle,                    "Controls.Gear",                            tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "gear lever" ) \
F( ControlsWheelBrakeLeft,                "Controls.WheelBrake.Left",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsWheelBrakeRight,               "Controls.WheelBrake.Right",                tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsWheelBrakeLeftActive,          "Controls.WheelBrake.Left",                 tm_msg_data_type::Double,   tm_msg_flag::Active, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsWheelBrakeRightActive,         "Controls.WheelBrake.Right",                tm_msg_data_type::Double,   tm_msg_flag::Active, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsAirBrake,                      "Controls.AirBrake",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsAirBrakeActive,                "Controls.AirBrake",                        tm_msg_data_type::Double,   tm_msg_flag::Active, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsAirBrakeArm,                   "Controls.AirBrake.Arm",                    tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsGliderAirBrake,                "Controls.GliderAirBrake",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPropellerSpeed1,               "Controls.PropellerSpeed1",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPropellerSpeed2,               "Controls.PropellerSpeed2",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPropellerSpeed3,               "Controls.PropellerSpeed3",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPropellerSpeed4,               "Controls.PropellerSpeed4",                 tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsMixture,                       "Controls.Mixture",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsMixture1,                      "Controls.Mixture1",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsMixture2,                      "Controls.Mixture2",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsMixture3,                      "Controls.Mixture3",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsMixture4,                      "Controls.Mixture4",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsThrustReverse,                 "Controls.ThrustReverse",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsThrustReverse1,                "Controls.ThrustReverse1",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsThrustReverse2,                "Controls.ThrustReverse2",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsThrustReverse3,                "Controls.ThrustReverse3",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsThrustReverse4,                "Controls.ThrustReverse4",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsCollective,                    "Controls.Collective",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsCyclicPitch,                   "Controls.CyclicPitch",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsCyclicRoll,                    "Controls.CyclicRoll",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsTailRotor,                     "Controls.TailRotor",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsRotorBrake,                    "Controls.RotorBrake",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsHelicopterThrottle1,           "Controls.HelicopterThrottle1",             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsHelicopterThrottle2,           "Controls.HelicopterThrottle2",             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsTrim,                          "Controls.Trim",                            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsTrimStep,                      "Controls.Trim",                            tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsTrimMove,                      "Controls.Trim",                            tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsAileronTrim,                   "Controls.AileronTrim",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsRudderTrim,                    "Controls.RudderTrim",                      tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsTiller,                        "Controls.Tiller",                          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPedalsDisconnect,              "Controls.PedalsDisconnect",                tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsNoseWheelSteering,             "Controls.NoseWheelSteering",               tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsLightingPanel,                 "Controls.Lighting.Panel",                  tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsLightingInstruments,           "Controls.Lighting.Instruments",            tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsPressureSetting0,              "Controls.PressureSetting0",                tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "captain pressure setting in Pa" ) \
F( ControlsPressureSettingStandard0,      "Controls.PressureSettingStandard0",        tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "captain pressure setting is STD" ) \
F( ControlsPressureSettingUnit0,          "Controls.PressureSettingUnit0",            tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "captain pressure setting is set display inHg" ) \
F( ControlsPressureSetting1,              "Controls.PressureSetting1",                tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "f/o pressure setting in Pa" ) \
F( ControlsPressureSettingStandard1,      "Controls.PressureSettingStandard1",        tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "f/o pressure setting is STD" ) \
F( ControlsPressureSettingUnit1,          "Controls.PressureSettingUnit1",            tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "f/o pressure setting is set display inHg" ) \
F( ControlsPressureSetting2,              "Controls.PressureSetting2",                tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "standby pressure setting in Pa" ) \
F( ControlsPressureSettingStandard2,      "Controls.PressureSettingStandard2",        tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "standby pressure setting is STD" ) \
F( ControlsPressureSettingUnit2,          "Controls.PressureSettingUnit2",            tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "standby pressure setting is set display inHg" ) \
F( ControlsTransitionAltitude,            "Controls.TransitionAltitude",              tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "pressure setting transition altitude (QNH->STD)" ) \
F( ControlsTransitionLevel,               "Controls.TransitionLevel",                 tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::Meter,                    "pressure setting transition level (STD->QNH)" ) \
F( PressurizationLandingElevation,        "Pressurization.LandingElevation",          tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::Meter,                    "" ) \
F( PressurizationLandingElevationManual,  "Pressurization.LandingElevationManual",    tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::Meter,                    "" ) \
F( WarningsMasterWarning,                 "Warnings.MasterWarning",                   tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "master warning active" ) \
F( WarningsMasterCaution,                 "Warnings.MasterCaution",                   tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "master caution active" ) \
F( WarningsEngineFire,                    "Warnings.EngineFire",                      tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "engine fire active" ) \
F( WarningsLowOilPressure,                "Warnings.LowOilPressure",                  tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "low oil pressure warning active" ) \
F( WarningsLowFuelPressure,               "Warnings.LowFuelPressure",                 tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "low fuel pressure warning active" ) \
F( WarningsLowHydraulicPressure,          "Warnings.LowHydraulicPressure",            tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "low hydraulic pressure warning active" ) \
F( WarningsLowVoltage,                    "Warnings.LowVoltage",                      tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "low voltage warning active" ) \
F( WarningsAltitudeAlert,                 "Warnings.AltitudeAlert",                   tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "altitude alert warning active" ) \
F( WarningsWarningActive,                 "Warnings.WarningActive",                   tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "warnings active" ) \
F( WarningsWarningMute,                   "Warnings.WarningMute",                     tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Read,      tm_msg_unit::None,                     "warning suppression" ) \
F( ViewDisplayName,                       "View.DisplayName",                         tm_msg_data_type::String,   tm_msg_flag::None,   tm_msg_access::Read,      tm_msg_unit::None,                     "name of current view" ) \
F( ViewInternal,                          "View.Internal",                            tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "set view to last internal view" ) \
F( ViewFollow,                            "View.Follow",                              tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "set view to last follow view" ) \
F( ViewExternal,                          "View.External",                            tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "set view to last external view" ) \
F( ViewCategory,                          "View.Category",                            tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "change to next / previous view category (internal,follow,external), set last view in this category" ) \
F( ViewMode,                              "View.Mode",                                tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "set next / previous view in current category" ) \
F( ViewZoom,                              "View.Zoom",                                tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewPanHorizontal,                     "View.Pan.Horizontal",                      tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewPanHorizontalMove,                 "View.Pan.Horizontal",                      tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewPanVertical,                       "View.Pan.Vertical",                        tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewPanVerticalMove,                   "View.Pan.Vertical",                        tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewPanCenter,                         "View.Pan.Center",                          tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewLookHorizontal,                    "View.Look.Horizontal",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "momentarily look left / right" ) \
F( ViewLookVertical,                      "View.Look.Vertical",                       tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "momentarily look up / down" ) \
F( ViewRoll,                              "View.Roll",                                tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewOffsetX,                           "View.OffsetX",                             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "offset (forward/backward) from view's default position" ) \
F( ViewOffsetXMove,                       "View.OffsetX",                             tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::None,                     "change offset (forward/backward) from view's default position" ) \
F( ViewOffsetY,                           "View.OffsetY",                             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "lateral offset from view's default position" ) \
F( ViewOffsetYMove,                       "View.OffsetY",                             tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::None,                     "change lateral offset from view's default position" ) \
F( ViewOffsetZ,                           "View.OffsetZ",                             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "vertical offset from view's default position" ) \
F( ViewOffsetZMove,                       "View.OffsetZ",                             tm_msg_data_type::Double,   tm_msg_flag::Move,   tm_msg_access::Write,     tm_msg_unit::None,                     "change vertical offset from view's default position" ) \
F( ViewPosition,                          "View.Position",                            tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewDirection,                         "View.Direction",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewUp,                                "View.Up",                                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewFieldOfView,                       "View.FieldOfView",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewAspectRatio,                       "View.AspectRatio",                         tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewFreePosition,                      "View.FreePosition",                        tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::Meter,                    "the following 4 messages allow you to implement your own view" ) \
F( ViewFreeLookDirection,                 "View.FreeLookDirection",                   tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewFreeUp,                            "View.FreeUp",                              tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ViewFreeFieldOfView,                   "View.FreeFieldOfView",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::Radiant,                  "" ) \
F( SimulationPause,                       "Simulation.Pause",                         tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::ReadWrite, tm_msg_unit::None,                     "toggle pause on/off" ) \
F( SimulationFlightInformation,           "Simulation.FlightInformation",             tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "show/hide the flight information at the top of the screen" ) \
F( SimulationMovingMap,                   "Simulation.MovingMap",                     tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "show/hide the moving map window" ) \
F( SimulationSound,                       "Simulation.Sound",                         tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "toggle sound on/off" ) \
F( SimulationLiftUp,                      "Simulation.LiftUp",                        tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "lift up the aircraft from current position" ) \
F( SimulationSettingPosition,             "Simulation.SettingPosition",               tm_msg_data_type::Vector3d, tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::Meter,                    "" ) \
F( SimulationSettingOrientation,          "Simulation.SettingOrientation",            tm_msg_data_type::Vector4d, tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( SimulationSettingVelocity,             "Simulation.SettingVelocity",               tm_msg_data_type::Vector3d, tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::MeterPerSecond,           "" ) \
F( SimulationSettingSet,                  "Simulation.SettingSet",                    tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( SimulationTimeChange,                  "Simulation.TimeChange",                    tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "change time of day" ) \
F( SimulationVisibility,                  "Simulation.Visibility",                    tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "" ) \
F( SimulationTime,                        "Simulation.Time",                          tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::Second,                   "" ) \
F( SimulationUseMouseControl,             "Simulation.UseMouseControl",               tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::ReadWrite, tm_msg_unit::None,                     "" ) \
F( SimulationPlaybackStart,               "Simulation.PlaybackStart",                 tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "start playback if simulation is paused" ) \
F( SimulationPlaybackStop,                "Simulation.PlaybackStop",                  tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "stop playback" ) \
F( SimulationPlaybackSetPosition,         "Simulation.PlaybackPosition",              tm_msg_data_type::Double,   tm_msg_flag::None,   tm_msg_access::Write,     tm_msg_unit::None,                     "set playback position 0 - 1" ) \
F( SimulationExternalPosition,            "Simulation.ExternalPosition",              tm_msg_data_type::Vector3d, tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::Meter,                    "" ) \
F( SimulationExternalOrientation,         "Simulation.ExternalOrientation",           tm_msg_data_type::Vector4d, tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandExecute,                        "Command.Execute",                          tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandBack,                           "Command.Back",                             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandUp,                             "Command.Up",                               tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandDown,                           "Command.Down",                             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandLeft,                           "Command.Left",                             tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandRight,                          "Command.Right",                            tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandMoveHorizontal,                 "Command.MoveHorizontal",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandMoveVertical,                   "Command.MoveVertical",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandRotate,                         "Command.Rotate",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( CommandZoom,                           "Command.Zoom",                             tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "" ) \
F( ControlsSpeed,                         "Controls.Speed",                           tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "ignore/do not use combined throttle, brake and reverse, copilot splits into other" ) \
F( FMSData0,                              "FlightManagementSystem.Data0",             tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use FMS binary datablock" ) \
F( FMSData1,                              "FlightManagementSystem.Data1",             tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use FMS binary datablock" ) \
F( NAV1Data,                              "Navigation.NAV1Data",                      tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use NAV1 binary datablock" ) \
F( NAV2Data,                              "Navigation.NAV2Data",                      tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use NAV2 binary datablock" ) \
F( NAV3Data,                              "Navigation.NAV3Data",                      tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use NAV3 binary datablock" ) \
F( ILS1Data,                              "Navigation.ILS1Data",                      tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use ILS1 binary datablock" ) \
F( ILS2Data,                              "Navigation.ILS2Data",                      tm_msg_data_type::None,     tm_msg_flag::Value,  tm_msg_access::None,      tm_msg_unit::None,                     "ignore/do not use ILS2 binary datablock" )


MESSAGE_LIST(TM_MESSAGE)

///////////////////////////////////////////////////////////////////////////////////////////////////
// AIRCRAFT-SPECIFIC CONTROLS EXTENSION
///////////////////////////////////////////////////////////////////////////////////////////////////

// === AIRCRAFT-SPECIFIC CONTROLS DEFINITIONS ===
// These are controls specific to each aircraft model

#define AIRCRAFT_SPECIFIC_MESSAGE_LIST(F) \
/* === CESSNA 172 SPECIFIC CONTROLS === */ \
F( C172FuelSelector,                  "Controls.FuelSelector",                     tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "C172 fuel selector valve position" ) \
F( C172FuelShutOff,                   "Controls.FuelShutOff",                      tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "C172 fuel shut off valve" ) \
F( C172HideYokeLeft,                  "Controls.HideYoke.Left",                    tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "C172 hide left yoke for better view" ) \
F( C172HideYokeRight,                 "Controls.HideYoke.Right",                   tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "C172 hide right yoke for better view" ) \
F( C172LeftSunBlocker,                "Controls.LeftSunBlocker",                   tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "C172 left side sun visor position (0=retracted, 1=extended)" ) \
F( C172RightSunBlocker,               "Controls.RightSunBlocker",                  tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "C172 right side sun visor position (0=retracted, 1=extended)" ) \
F( C172LeftCabinLight,                "Controls.Lighting.LeftCabinOverheadLight",  tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "C172 left cabin overhead light" ) \
F( C172RightCabinLight,               "Controls.Lighting.RightCabinOverheadLight", tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "C172 right cabin overhead light" ) \
F( C172Magnetos1,                     "Controls.Magnetos1",                        tm_msg_data_type::Double,   tm_msg_flag::Value,  tm_msg_access::Write,     tm_msg_unit::None,                     "C172 magneto switch position (0=OFF, 1=R, 2=L, 3=BOTH, 4=START)" ) \
F( C172ParkingBrakeHandle,            "Controls.ParkingBrake",                     tm_msg_data_type::Double,   tm_msg_flag::Toggle, tm_msg_access::Write,     tm_msg_unit::None,                     "C172 parking brake handle" ) \
F( C172TrimWheel,                     "Controls.Trim",                             tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::Write,     tm_msg_unit::None,                     "C172 trim wheel adjustment" ) \
F( C172LeftYokeButton,                "LeftYoke.Button",                           tm_msg_data_type::Double,   tm_msg_flag::Event,  tm_msg_access::Write,     tm_msg_unit::None,                     "C172 left yoke PTT button" ) \
/* === DOORS AND WINDOWS === */ \
F( C172LeftDoor,                      "Doors.Left",                                tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::ReadWrite, tm_msg_unit::None,                     "C172 left door position (0=closed, 1=open)" ) \
F( C172LeftDoorHandle,                "Doors.LeftHandle",                          tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::Write,     tm_msg_unit::None,                     "C172 left door handle position" ) \
F( C172RightDoor,                     "Doors.Right",                               tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::ReadWrite, tm_msg_unit::None,                     "C172 right door position (0=closed, 1=open)" ) \
F( C172RightDoorHandle,               "Doors.RightHandle",                         tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::Write,     tm_msg_unit::None,                     "C172 right door handle position" ) \
F( C172LeftWindow,                    "Windows.Left",                              tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::ReadWrite, tm_msg_unit::None,                     "C172 left window position (0=closed, 1=open)" ) \
F( C172RightWindow,                   "Windows.Right",                             tm_msg_data_type::Double,   tm_msg_flag::Step,   tm_msg_access::ReadWrite, tm_msg_unit::None,                     "C172 right window position (0=closed, 1=open)" )
// Create the message instances
AIRCRAFT_SPECIFIC_MESSAGE_LIST(TM_MESSAGE)


///////////////////////////////////////////////////////////////////////////////////////////////////
// SHARED MEMORY INTERFACE - Primary Interface
///////////////////////////////////////////////////////////////////////////////////////////////////

class SharedMemoryInterface {
private:
    HANDLE hMapFile;
    AeroflyBridgeData* pData;
    std::mutex data_mutex;
    bool initialized;
    
    // Hash map infrastructure for optimized message processing
    using MessageHandler = std::function<void(const tm_external_message&)>;
    std::unordered_map<uint64_t, MessageHandler> message_handlers;

    /**
     * @brief Process step controls with proper flag handling for C172 doors/windows
     * @param message The external message containing step or absolute value
     * @param storage_field Reference to the specific storage field in pData
     * @param variable_index Index in the all_variables array
     * @param debug_name Name used in debug logs (emitted only in debug builds)
     */
    void ProcessStepControl(const tm_external_message& message, 
                               double& storage_field, 
                               int variable_index,
                               const char* debug_name) {
        
        double new_value;
        
        if (message.GetFlags().IsSet(tm_msg_flag::Step)) {
            // STEP MODE: Increment/decrement current value
            double step_value = message.GetDouble();
            double current_value = storage_field;
            
            new_value = current_value + step_value;
            // Clamp to 0-1 range (manual implementation for compatibility)
            if (new_value < 0.0) new_value = 0.0;
            if (new_value > 1.0) new_value = 1.0;
            
            DBG((std::string("STEP ") + debug_name +
                 ": " + std::to_string(current_value) +
                 " + " + std::to_string(step_value) +
                 " = " + std::to_string(new_value) + "\n").c_str());
        } else {
            // ABSOLUTE MODE: Direct value assignment
            new_value = message.GetDouble();
            // Clamp to 0-1 range (manual implementation for compatibility)
            if (new_value < 0.0) new_value = 0.0;
            if (new_value > 1.0) new_value = 1.0;
            
            DBG((std::string("VALUE ") + debug_name +
                 ": " + std::to_string(new_value) + "\n").c_str());
        }
        
        // Update both storage locations
        storage_field = new_value;
        pData->all_variables[variable_index] = new_value;
    }

private:
    /**
     * @brief Safely processes string messages with proper error handling
     * @param message The message containing string data
     * @param destination Buffer to store the string
     * @param dest_size Size of the destination buffer
     * @param default_value Default value if processing fails
     * @param variable_name Variable name for logging purposes
     */
    void ProcessStringMessage(const tm_external_message& message, char* destination, 
                             size_t dest_size, const char* default_value, 
                             const char* variable_name) {
        if (message.GetDataType() == tm_msg_data_type::String || 
            message.GetDataType() == tm_msg_data_type::String8) {
            try {
                const tm_string tm_value = message.GetString();
                const std::string value = tm_value.c_str();
                
                if (!value.empty() && value.length() < dest_size) {
                    strncpy_s(destination, dest_size, value.c_str(), _TRUNCATE);
                    // Sanitize non-printable characters and ensure termination
                    for (size_t i = 0; i < dest_size; ++i) {
                        unsigned char c = static_cast<unsigned char>(destination[i]);
                        if (c == '\0') break;
                        if (c < 32 || c == 127) destination[i] = ' ';
                    }
                    if (dest_size > 0) destination[dest_size - 1] = '\0';
                } else {
                    strncpy_s(destination, dest_size, default_value, _TRUNCATE);
                    for (size_t i = 0; i < dest_size; ++i) {
                        unsigned char c = static_cast<unsigned char>(destination[i]);
                        if (c == '\0') break;
                        if (c < 32 || c == 127) destination[i] = ' ';
                    }
                    if (dest_size > 0) destination[dest_size - 1] = '\0';
                }
            } catch (...) {
                strncpy_s(destination, dest_size, default_value, _TRUNCATE);
                std::string debug_msg = "WARNING: Exception in GetString() for " + 
                                       std::string(variable_name) + ", using default value\n";
                OutputDebugStringA(debug_msg.c_str());
                for (size_t i = 0; i < dest_size; ++i) {
                    unsigned char c = static_cast<unsigned char>(destination[i]);
                    if (c == '\0') break;
                    if (c < 32 || c == 127) destination[i] = ' ';
                }
                if (dest_size > 0) destination[dest_size - 1] = '\0';
            }
        } else {
            strncpy_s(destination, dest_size, default_value, _TRUNCATE);
            std::string debug_msg = "WARNING: " + std::string(variable_name) + 
                                   " has incorrect DataType: " + 
                                   std::to_string(static_cast<int>(message.GetDataType())) + 
                                   " (expected String or String8)\n";
            OutputDebugStringA(debug_msg.c_str());
            for (size_t i = 0; i < dest_size; ++i) {
                unsigned char c = static_cast<unsigned char>(destination[i]);
                if (c == '\0') break;
                if (c < 32 || c == 127) destination[i] = ' ';
            }
            if (dest_size > 0) destination[dest_size - 1] = '\0';
        }
    }
    
public:
    SharedMemoryInterface() : hMapFile(NULL), pData(nullptr), initialized(false) {
        InitializeHandlers();
    }
    
    ~SharedMemoryInterface() {
        Cleanup();
    }
    
    /**
     * @brief Initialize message handlers hash map for optimized processing
     * 
     * This method populates the message_handlers map with lambda functions
     * for each supported message type, replacing the long if-else chain
     * with O(1) hash table lookup.
     */
    void InitializeHandlers() {
        // === AIRCRAFT BASIC DATA (Examples) ===
        message_handlers[MessageAircraftLatitude.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_latitude = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_LATITUDE] = val;
        };
        
        message_handlers[MessageAircraftLongitude.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_longitude = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_LONGITUDE] = val;
        };
        
        message_handlers[MessageAircraftAltitude.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_altitude = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ALTITUDE] = val;
        };
        
        message_handlers[MessageAircraftPitch.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_pitch = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_PITCH] = val;
        };
        
        message_handlers[MessageAircraftBank.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_bank = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_BANK] = val;
        };
        
        message_handlers[MessageAircraftIndicatedAirspeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_indicated_airspeed = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_INDICATED_AIRSPEED] = val;
        };
        
        // === MORE AIRCRAFT BASIC VARIABLES (PASO 2B) ===
        message_handlers[MessageAircraftTrueHeading.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_true_heading = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_TRUE_HEADING] = val;
        };
        
        message_handlers[MessageAircraftMagneticHeading.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_magnetic_heading = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_MAGNETIC_HEADING] = val;
        };
        
        message_handlers[MessageAircraftGroundSpeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_ground_speed = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_GROUND_SPEED] = val;
        };
        
        message_handlers[MessageAircraftVerticalSpeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_vertical_speed = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_VERTICAL_SPEED] = val;
        };
        
        message_handlers[MessageAircraftHeight.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_height = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_HEIGHT] = val;
        };
        
        message_handlers[MessageAircraftOrientation.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_orientation = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ORIENTATION] = val;
        };
        
        message_handlers[MessageAircraftUniversalTime.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_universal_time = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_UNIVERSAL_TIME] = val;
        };
        
        message_handlers[MessageAircraftIndicatedAirspeedTrend.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::AIRCRAFT_INDICATED_AIRSPEED_TREND] = val;
        };
        
        // === AIRCRAFT STRING HANDLERS (Examples) ===
        message_handlers[MessageAircraftName.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->aircraft_name, 
                               sizeof(pData->aircraft_name), "Unknown", 
                               "AircraftName");
        };
        
        // === CONTROLS VARIABLES (Examples) ===
        message_handlers[MessageControlsThrottle.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::CONTROLS_THROTTLE] = val;
        };
        
        message_handlers[MessageControlsPitchInput.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_pitch_input = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_PITCH_INPUT] = val;
        };
        
        message_handlers[MessageControlsRollInput.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_roll_input = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_ROLL_INPUT] = val;
        };
        
        message_handlers[MessageControlsYawInput.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_yaw_input = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_YAW_INPUT] = val;
        };
        
        message_handlers[MessageControlsGear.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::CONTROLS_GEAR] = val;
        };
        
        message_handlers[MessageControlsFlaps.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::CONTROLS_FLAPS] = val;
        };
        
        // === MORE CONTROLS VARIABLES (PASO 2) ===
        message_handlers[MessageControlsThrottle1.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_throttle_1 = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_THROTTLE_1] = val;
        };
        
        message_handlers[MessageControlsThrottle2.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_throttle_2 = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_THROTTLE_2] = val;
        };
        
        message_handlers[MessageControlsThrottle3.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_throttle_3 = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_THROTTLE_3] = val;
        };
        
        message_handlers[MessageControlsThrottle4.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_throttle_4 = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_THROTTLE_4] = val;
        };
        
        message_handlers[MessageControlsAirBrake.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_airbrake = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_AIR_BRAKE] = val;
        };
        
        message_handlers[MessageControlsWheelBrakeLeft.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_brake_left = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_WHEEL_BRAKE_LEFT] = val;
        };
        
        message_handlers[MessageControlsWheelBrakeRight.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_brake_right = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_WHEEL_BRAKE_RIGHT] = val;
        };
        
        message_handlers[MessageControlsCollective.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->controls_collective = val;
            pData->all_variables[(int)VariableIndex::CONTROLS_COLLECTIVE] = val;
        };
        
        // NOTE: MessageControlsRudder, MessageControlsSlats, MessageControlsGroundSpoilers 
        // are not defined in the current codebase - skipping for now
        
        // === VECTOR3D VARIABLES (Examples) ===
        message_handlers[MessageAircraftPosition.GetID()] = [this](const auto& msg) {
            pData->aircraft_position = msg.GetVector3d();
        };
        
        message_handlers[MessageAircraftVelocity.GetID()] = [this](const auto& msg) {
            pData->aircraft_velocity = msg.GetVector3d();
        };
        
        // === STEP CONTROL VARIABLES (Examples) ===
        message_handlers[MessageC172LeftWindow.GetID()] = [this](const auto& msg) {
            ProcessStepControl(msg, pData->c172_left_window, 
                              (int)VariableIndex::C172_LEFT_WINDOW, 
                              "C172 Left Window");
        };
        
        message_handlers[MessageC172RightWindow.GetID()] = [this](const auto& msg) {
            ProcessStepControl(msg, pData->c172_right_window, 
                              (int)VariableIndex::C172_RIGHT_WINDOW, 
                              "C172 Right Window");
        };
        
        // === NAVIGATION VARIABLES (PASO 3 - MASIVO) ===
        // VOR Navigation Systems
        message_handlers[MessageNavigationSelectedCourse1.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_selected_course_1 = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_SELECTED_COURSE_1] = val;
        };
        
        message_handlers[MessageNavigationSelectedCourse2.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_selected_course_2 = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_SELECTED_COURSE_2] = val;
        };
        
        message_handlers[MessageNavigationNAV1Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_nav1_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_NAV1_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationNAV1StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_nav1_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_NAV1_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationNAV1FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_nav1_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_NAV1_FREQUENCY_SWAP] = val;
        };
        
        message_handlers[MessageNavigationNAV2Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_nav2_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_NAV2_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationNAV2StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_nav2_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_NAV2_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationNAV2FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_nav2_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_NAV2_FREQUENCY_SWAP] = val;
        };
        
        // DME Equipment
        message_handlers[MessageNavigationDME1Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme1_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME1_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationDME1Distance.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme1_distance = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME1_DISTANCE] = val;
        };
        
        message_handlers[MessageNavigationDME1Time.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme1_time = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME1_TIME] = val;
        };
        
        message_handlers[MessageNavigationDME1Speed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme1_speed = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME1_SPEED] = val;
        };
        
        message_handlers[MessageNavigationDME2Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme2_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME2_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationDME2Distance.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme2_distance = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME2_DISTANCE] = val;
        };
        
        message_handlers[MessageNavigationDME2Time.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme2_time = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME2_TIME] = val;
        };
        
        message_handlers[MessageNavigationDME2Speed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_dme2_speed = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_DME2_SPEED] = val;
        };
        
        // ILS Systems
        message_handlers[MessageNavigationILS1Course.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils1_course = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_COURSE] = val;
        };
        
        message_handlers[MessageNavigationILS1Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils1_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationILS1StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils1_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationILS1FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils1_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_FREQUENCY_SWAP] = val;
        };
        
        message_handlers[MessageNavigationILS2Course.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils2_course = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_COURSE] = val;
        };
        
        message_handlers[MessageNavigationILS2Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils2_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationILS2StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils2_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationILS2FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_ils2_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_FREQUENCY_SWAP] = val;
        };
        
        // ADF Equipment
        message_handlers[MessageNavigationADF1Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_adf1_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationADF1StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_adf1_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationADF1FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_adf1_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_FREQUENCY_SWAP] = val;
        };
        
        message_handlers[MessageNavigationADF2Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_adf2_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationADF2StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_adf2_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationADF2FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->navigation_adf2_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_FREQUENCY_SWAP] = val;
        };
        
        // Navigation String Handlers
        message_handlers[MessageNavigationNAV1Identifier.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->navigation_nav1_identifier, 
                               sizeof(pData->navigation_nav1_identifier), "", 
                               "NavigationNAV1Identifier");
        };
        
        message_handlers[MessageNavigationNAV2Identifier.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->navigation_nav2_identifier, 
                               sizeof(pData->navigation_nav2_identifier), "", 
                               "NavigationNAV2Identifier");
        };
        
        message_handlers[MessageNavigationILS1Identifier.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->navigation_ils1_identifier, 
                               sizeof(pData->navigation_ils1_identifier), "", 
                               "NavigationILS1Identifier");
        };
        
        message_handlers[MessageNavigationILS2Identifier.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->navigation_ils2_identifier, 
                               sizeof(pData->navigation_ils2_identifier), "", 
                               "NavigationILS2Identifier");
        };
        
        // === COMMUNICATION VARIABLES (PASO 3 - MASIVO) ===
        // COM Radio Systems
        message_handlers[MessageNavigationCOM1Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com1_frequency = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM1_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationCOM1StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com1_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM1_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationCOM1FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com1_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM1_FREQUENCY_SWAP] = val;
        };
        
        message_handlers[MessageNavigationCOM2Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com2_frequency = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM2_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationCOM2StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com2_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM2_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationCOM2FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com2_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM2_FREQUENCY_SWAP] = val;
        };
        
        message_handlers[MessageNavigationCOM3Frequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com3_frequency = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationCOM3StandbyFrequency.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com3_standby_frequency = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_STANDBY_FREQUENCY] = val;
        };
        
        message_handlers[MessageNavigationCOM3FrequencySwap.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->communication_com3_frequency_swap = val;
            pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_FREQUENCY_SWAP] = val;
        };
        
        message_handlers[MessageTransponderCode.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::COMMUNICATION_TRANSPONDER_CODE] = val;
        };
        
        message_handlers[MessageTransponderCursor.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::COMMUNICATION_TRANSPONDER_CURSOR] = val;
        };
        
        // === AIRCRAFT ENGINE VARIABLES (PASO 3 - MASIVO) ===
        // Engine Master Switches
        message_handlers[MessageAircraftEngineMaster1.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_master_1 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_1] = val;
        };
        
        message_handlers[MessageAircraftEngineMaster2.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_master_2 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_2] = val;
        };
        
        message_handlers[MessageAircraftEngineMaster3.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_master_3 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_3] = val;
        };
        
        message_handlers[MessageAircraftEngineMaster4.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_master_4 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_4] = val;
        };
        
        // Engine Throttle Controls
        message_handlers[MessageAircraftEngineThrottle1.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_throttle_1 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_1] = val;
        };
        
        message_handlers[MessageAircraftEngineThrottle2.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_throttle_2 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_2] = val;
        };
        
        message_handlers[MessageAircraftEngineThrottle3.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_throttle_3 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_3] = val;
        };
        
        message_handlers[MessageAircraftEngineThrottle4.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_throttle_4 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_4] = val;
        };
        
        // Engine Rotation Speed (RPM)
        message_handlers[MessageAircraftEngineRotationSpeed1.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_rotation_speed_1 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_1] = val;
        };
        
        message_handlers[MessageAircraftEngineRotationSpeed2.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_rotation_speed_2 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_2] = val;
        };
        
        message_handlers[MessageAircraftEngineRotationSpeed3.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_rotation_speed_3 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_3] = val;
        };
        
        message_handlers[MessageAircraftEngineRotationSpeed4.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_rotation_speed_4 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_ROTATION_SPEED_4] = val;
        };
        
        // Engine Running Status
        message_handlers[MessageAircraftEngineRunning1.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_running_1 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_1] = val;
        };
        
        message_handlers[MessageAircraftEngineRunning2.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_running_2 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_2] = val;
        };
        
        message_handlers[MessageAircraftEngineRunning3.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_running_3 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_3] = val;
        };
        
        message_handlers[MessageAircraftEngineRunning4.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->aircraft_engine_running_4 = val;
            pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_4] = val;
        };
        
        // === AUTOPILOT VARIABLES (PASO 3 - MASIVO) ===
        // Basic Autopilot Controls
        message_handlers[MessageAutopilotMaster.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_master = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_MASTER] = val;
        };
        
        message_handlers[MessageAutopilotDisengage.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_disengage = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_DISENGAGE] = val;
        };
        
        message_handlers[MessageAutopilotHeading.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_heading = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_HEADING] = val;
        };
        
        message_handlers[MessageAutopilotVerticalSpeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_vertical_speed = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_VERTICAL_SPEED] = val;
        };
        
        // Selected Values
        message_handlers[MessageAutopilotSelectedSpeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_selected_speed = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_SPEED] = val;
        };
        
        message_handlers[MessageAutopilotSelectedAirspeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_selected_airspeed = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_AIRSPEED] = val;
        };
        
        message_handlers[MessageAutopilotSelectedHeading.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_selected_heading = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_HEADING] = val;
        };
        
        message_handlers[MessageAutopilotSelectedAltitude.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_selected_altitude = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_ALTITUDE] = val;
        };
        
        message_handlers[MessageAutopilotSelectedVerticalSpeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_selected_vertical_speed = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_VERTICAL_SPEED] = val;
        };
        
        message_handlers[MessageAutopilotSelectedAltitudeScale.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_selected_altitude_scale = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_ALTITUDE_SCALE] = val;
        };
        
        // System Configuration
        message_handlers[MessageAutopilotEngaged.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_engaged = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_ENGAGED] = val;
        };
        
        message_handlers[MessageAutopilotUseMachNumber.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_use_mach_number = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_USE_MACH_NUMBER] = val;
        };
        
        message_handlers[MessageAutopilotSpeedManaged.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->autopilot_speed_managed = val;
            pData->all_variables[(int)VariableIndex::AUTOPILOT_SPEED_MANAGED] = val;
        };
        
        message_handlers[MessageAutopilotTargetAirspeed.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::AUTOPILOT_TARGET_AIRSPEED] = val;
        };
        
        message_handlers[MessageAutopilotAileron.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::AUTOPILOT_AILERON] = val;
        };
        
        message_handlers[MessageAutopilotElevator.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::AUTOPILOT_ELEVATOR] = val;
        };
        
        message_handlers[MessageAutopilotThrottleEngaged.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::AUTOPILOT_THROTTLE_ENGAGED] = val;
        };
        
        message_handlers[MessageAutopilotThrottleCommand.GetID()] = [this](const auto& msg) {
            const double val = msg.GetDouble();
            pData->all_variables[(int)VariableIndex::AUTOPILOT_THROTTLE_COMMAND] = val;
        };
        
        // TODO: Add remaining ~217 variables in subsequent steps
        // PASO 3 Progress: +75 Navigation+Communication+Engine+Autopilot variables migrated (Total: ~118)
    }
    
    /**
     * Creates the shared memory region and maps the AeroflyBridgeData view.
     * Initializes the structure and marks the interface as ready.
     */
    bool Initialize() {
        try {
            // Create shared memory region
            hMapFile = CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                0,
                sizeof(AeroflyBridgeData),
                "AeroflyBridgeData"
            );
            
            if (hMapFile == NULL) {
                DWORD error = GetLastError();
                OutputDebugStringA(("ERROR: CreateFileMappingA failed with error: " + std::to_string(error) + "\n").c_str());
                return false;
            }
            
            // Map the memory
            pData = (AeroflyBridgeData*)MapViewOfFile(
                hMapFile,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                sizeof(AeroflyBridgeData)
            );
            
            if (pData == nullptr) {
                DWORD error = GetLastError();
                OutputDebugStringA(("ERROR: MapViewOfFile failed with error: " + std::to_string(error) + "\n").c_str());
                CloseHandle(hMapFile);
                hMapFile = NULL;
                return false;
            }
            
            // Initialize data structure
            std::lock_guard<std::mutex> lock(data_mutex);
            memset(pData, 0, sizeof(AeroflyBridgeData));
            pData->data_valid = 0;
            pData->update_counter = 0;
            
            initialized = true;
            return true;
        }

        catch (const std::exception& e) {
            OutputDebugStringA(("ERROR: Exception in SharedMemoryInterface::Initialize: " + std::string(e.what()) + "\n").c_str());
            return false;
        }
        catch (...) {
            OutputDebugStringA("ERROR: Unknown exception in SharedMemoryInterface::Initialize\n");
            return false;
        }
    }
    
    /**
     * Updates the shared memory structure with values from incoming messages.
     * Marks data as updating, applies messages, then marks as ready.
     */
    void UpdateData(const std::vector<tm_external_message>& messages, double delta_time) {
        if (!initialized || !pData) return;
        
        std::lock_guard<std::mutex> lock(data_mutex);
    
        // MARK DATA AS "UPDATING"
        pData->data_valid = 0;
        
        // Update timestamp and counter (high-resolution)
        pData->timestamp_us = get_time_us();
        pData->update_counter++;
        
        // Process all received messages
        for (const auto& message : messages) {
            ProcessMessage(message);
        }

        // NOTE: Physical reordering of all_variables[] moved to a later initialization phase
        
        // MARK DATA AS "READY"
        pData->data_valid = 1;
    }
    
    /**
     * Applies a single message to the shared data structure.
     * Handles both scalar and vector types and keeps all_variables[] in sync.
     * 
     * NEW: Uses hash map for O(1) lookup instead of O(n) if-else chain.
     */
    void ProcessMessage(const tm_external_message& message) {
        if (!pData) {
            OutputDebugStringA("ERROR: ProcessMessage called with pData == nullptr\n");
            return;
        }
        
        const auto hash = message.GetStringHash().GetHash();
        
        // NEW: Hash map lookup - O(1) performance
        auto it = message_handlers.find(hash);
        if (it != message_handlers.end()) {
            try {
                it->second(message);
                return;
            }
            catch (const std::exception& e) {
                std::string error_msg = "ERROR: Exception in message handler: " + std::string(e.what()) + "\n";
                OutputDebugStringA(error_msg.c_str());
                return;
            }
            catch (...) {
                OutputDebugStringA("ERROR: Unknown exception in message handler\n");
                return;
            }
        }
        
        // FALLBACK: Original if-else chain for variables not yet migrated to hash map
        // TODO: Remove this section once all variables are migrated
        try {
            // === AIRCRAFT BASIC DATA ===
            // MIGRATED TO HASH MAP: MessageAircraftUniversalTime
            // MIGRATED TO HASH MAP: MessageAircraftLatitude
            // MIGRATED TO HASH MAP: MessageAircraftLongitude  
            // MIGRATED TO HASH MAP: MessageAircraftAltitude
            // MIGRATED TO HASH MAP: MessageAircraftPitch
            // MIGRATED TO HASH MAP: MessageAircraftBank
            // MIGRATED TO HASH MAP: MessageAircraftTrueHeading
            // MIGRATED TO HASH MAP: MessageAircraftMagneticHeading
            // MIGRATED TO HASH MAP: MessageAircraftIndicatedAirspeed
            // MIGRATED TO HASH MAP: MessageAircraftIndicatedAirspeedTrend
            // MIGRATED TO HASH MAP: MessageAircraftGroundSpeed
            // MIGRATED TO HASH MAP: MessageAircraftVerticalSpeed
            // MIGRATED TO HASH MAP: MessageAircraftHeight
            
            // === AIRCRAFT PHYSICS ===
            // MIGRATED TO HASH MAP: MessageAircraftPosition
            // MIGRATED TO HASH MAP: MessageAircraftVelocity
            // CLEANED UP: Aircraft Acceleration and AngularVelocity now use hash map O(1) lookup
            if (hash == MessageAircraftGravity.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_gravity = value;
                return;
            }
            if (hash == MessageAircraftWind.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_wind = value;
                return;
            }
            if (hash == MessageAircraftRateOfTurn.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_rate_of_turn = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_RATE_OF_TURN] = value;
                return;
            }
            if (hash == MessageAircraftMachNumber.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_mach_number = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_MACH_NUMBER] = value;
                return;
            }
            if (hash == MessageAircraftAngleOfAttack.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_angle_of_attack = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ANGLE_OF_ATTACK] = value;
                return;
            }
            if (hash == MessageAircraftAngleOfAttackLimit.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_angle_of_attack_limit = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ANGLE_OF_ATTACK_LIMIT] = value;
                return;
            }
            if (hash == MessageAircraftAccelerationLimit.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ACCELERATION_LIMIT] = value;
                return;
            }

            // === AIRCRAFT STRING HANDLERS (6 handlers) ===
            // MIGRATED TO HASH MAP: MessageAircraftName

            if (hash == MessageAircraftNearestAirportIdentifier.GetID()) {
                ProcessStringMessage(message, pData->aircraft_nearest_airport_id, 
                                    sizeof(pData->aircraft_nearest_airport_id), "", 
                                    "AircraftNearestAirportIdentifier");
                return;
            }

            if (hash == MessageAircraftNearestAirportName.GetID()) {
                ProcessStringMessage(message, pData->aircraft_nearest_airport_name, 
                                    sizeof(pData->aircraft_nearest_airport_name), "", 
                                    "AircraftNearestAirportName");
                return;
            }

            if (hash == MessageAircraftBestAirportIdentifier.GetID()) {
                ProcessStringMessage(message, pData->aircraft_best_airport_id, 
                                    sizeof(pData->aircraft_best_airport_id), "", 
                                    "AircraftBestAirportIdentifier");
                return;
            }

            if (hash == MessageAircraftBestAirportName.GetID()) {
                ProcessStringMessage(message, pData->aircraft_best_airport_name, 
                                    sizeof(pData->aircraft_best_airport_name), "", 
                                    "AircraftBestAirportName");
                return;
            }

            if (hash == MessageAircraftBestRunwayIdentifier.GetID()) {
                ProcessStringMessage(message, pData->aircraft_best_runway_id, 
                                    sizeof(pData->aircraft_best_runway_id), "", 
                                    "AircraftBestRunwayIdentifier");
                return;
            }
            
            // === AIRCRAFT STATE ===
            if (hash == MessageAircraftOnGround.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_on_ground = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ON_GROUND] = value;
                return;
            }
            if (hash == MessageAircraftOnRunway.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_on_runway = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ON_RUNWAY] = value;
                return;
            }
            
            if (hash == MessageAircraftCrashed.GetID()) {
                try {
                    // Get the actual data type
                    tm_msg_data_type dataType = message.GetDataType();
                    
                    // IGNORE INVALID MESSAGES (DataType = 0 = None)
                    if (dataType == tm_msg_data_type::None) {
                        // Invalid/corrupted message - ignore silently
                        static int invalid_count = 0;
                        if (invalid_count == 0) {
                            DBG("INFO: Aircraft.Crashed receiving invalid messages (DataType=None), ignoring\n");
                        }
                        invalid_count++;
                        return; // Skip processing entirely
                    }
                    
                    double value = 0.0;
                    
                    // Extract data directly from the raw pointer
                    const tm_uint8* raw_data = message.GetDataPointer();
                    
                    switch (dataType) {
                        case tm_msg_data_type::Double:
                            value = *reinterpret_cast<const double*>(raw_data);
                            break;
                            
                        case tm_msg_data_type::Int:
                            value = (double)(*reinterpret_cast<const tm_int64*>(raw_data));
                            break;
                            
                        case tm_msg_data_type::Uint8:
                            value = (double)(*reinterpret_cast<const tm_uint8*>(raw_data));
                            break;
                            
                        case tm_msg_data_type::Uint64:
                            value = (double)(*reinterpret_cast<const tm_uint64*>(raw_data));
                            break;
                            
                        case tm_msg_data_type::Float:
                            value = (double)(*reinterpret_cast<const float*>(raw_data));
                            break;
                            
                        default:
                            // For unknown but valid types, read first byte as boolean
                            value = (double)(raw_data[0] > 0 ? 1.0 : 0.0);
                            break;
                    }
                    
                    // Update the data
                    pData->aircraft_crashed = value;
                    pData->all_variables[(int)VariableIndex::AIRCRAFT_CRASHED] = value;
                    
                    // Log crash state changes only (reduce spam)
                    static double last_crash_state = -1.0;
                    static int log_throttle = 0;
                    
                    if (last_crash_state != value) {
                        if (value > 0.0) {
                            DBG("AIRCRAFT CRASHED!\n");
                        } else {
                            DBG("Aircraft recovered/reset\n");
                        }
                        last_crash_state = value;
                        log_throttle = 0; // Reset throttle on state change
                    } else {
                        // Same state - throttle logging
                        log_throttle++;
                        if (log_throttle > 10) {
                            // After 10 identical messages, ignore until state changes
                            return;
                        }
                    }
                    
                }
                catch (const std::exception& e) {
                    pData->aircraft_crashed = 0.0;
                    pData->all_variables[(int)VariableIndex::AIRCRAFT_CRASHED] = 0.0;
                    static bool error_logged = false;
                    if (!error_logged) {
                        OutputDebugStringA("ERROR: Aircraft.Crashed exception (logged once)\n");
                        error_logged = true;
                    }
                }
                catch (...) {
                    pData->aircraft_crashed = 0.0;
                    pData->all_variables[(int)VariableIndex::AIRCRAFT_CRASHED] = 0.0;
                    static bool error_logged = false;
                    if (!error_logged) {
                        OutputDebugStringA("ERROR: Aircraft.Crashed unknown error (logged once)\n");
                        error_logged = true;
                    }
                }
                return;
            }


            if (hash == MessageAircraftGear.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_gear = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_GEAR] = value;
                return;
            }
            if (hash == MessageAircraftFlaps.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_flaps = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_FLAPS] = value;
                return;
            }
            if (hash == MessageAircraftSlats.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_slats = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_SLATS] = value;
                return;
            }
            if (hash == MessageAircraftThrottle.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_throttle = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_THROTTLE] = value;
                return;
            }
            if (hash == MessageAircraftAirBrake.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_air_brake = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AIR_BRAKE] = value;
                return;
            }
            
            // === ENGINE DATA ===
            // CLEANED UP: Engine variables now use hash map O(1) lookup




            if (hash == MessageAircraftEngineRunning1.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_1 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_1] = value;
                return;
            }
            if (hash == MessageAircraftEngineRunning2.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_2 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_2] = value;
                return;
            }
            if (hash == MessageAircraftEngineRunning3.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_3 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_3] = value;
                return;
            }
            if (hash == MessageAircraftEngineRunning4.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_4 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_4] = value;
                return;
            }
            
            // === PERFORMANCE SPEEDS ===
            if (hash == MessagePerformanceSpeedVS0.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_vs0 = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_VS0] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedVS1.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_vs1 = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_VS1] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedVFE.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_vfe = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_VFE] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedVNO.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_vno = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_VNO] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedVNE.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_vne = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_VNE] = value;
                return;
            }
            
            // === NAVIGATION ===
            // CLEANED UP: Navigation variables now use hash map O(1) lookup



            
            // === COMMUNICATION ===
            // CLEANED UP: Communication variables now use hash map O(1) lookup

            // === FMS STRING HANDLERS (1 handler) ===
            if (hash == MessageFMSFlightNumber.GetID()) {
                ProcessStringMessage(message, pData->fms_flight_number, 
                                    sizeof(pData->fms_flight_number), "", 
                                    "FMSFlightNumber");
                return;
            }

            // === SIMULATION ===
            if (hash == MessageSimulationTime.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::SIMULATION_TIME] = value;
                return;
            }
            
            // === AUTOPILOT ===
            if (hash == MessageAutopilotEngaged.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_engaged = value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_ENGAGED] = value;
                return;
            }
            if (hash == MessageAutopilotSelectedAirspeed.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_selected_airspeed = value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_AIRSPEED] = value;
                return;
            }
            if (hash == MessageAutopilotSelectedHeading.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_selected_heading = value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_HEADING] = value;
                return;
            }
            if (hash == MessageAutopilotSelectedAltitude.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_selected_altitude = value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_ALTITUDE] = value;
                return;
            }
            if (hash == MessageAutopilotSelectedVerticalSpeed.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_selected_vertical_speed = value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_SELECTED_VERTICAL_SPEED] = value;
                return;
            }
            if (hash == MessageAutopilotThrottleEngaged.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_throttle_engaged = value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_THROTTLE_ENGAGED] = value;
                return;
            }
            if (hash == MessageAutopilotActiveLateralMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_lateral_mode, 
                                    sizeof(pData->autopilot_active_lateral_mode), "Manual", 
                                    "AutopilotActiveLateralMode");
                return;
            }
            if (hash == MessageAutopilotActiveVerticalMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_vertical_mode, 
                                    sizeof(pData->autopilot_active_vertical_mode), "Manual", 
                                    "AutopilotActiveVerticalMode");
                return;
            }
            if (hash == MessageAutopilotArmedLateralMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_lateral_mode, 
                                    sizeof(pData->autopilot_armed_lateral_mode), "None", 
                                    "AutopilotArmedLateralMode");
                return;
            }
            
            if (hash == MessageAutopilotArmedVerticalMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_vertical_mode, 
                                    sizeof(pData->autopilot_armed_vertical_mode), "None", 
                                    "AutopilotArmedVerticalMode");
                return;
            }
            
            if (hash == MessageAutopilotArmedApproachMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_approach_mode, 
                                    sizeof(pData->autopilot_armed_approach_mode), "None", 
                                    "AutopilotArmedApproachMode");
                return;
            }
            
            if (hash == MessageAutopilotActiveAutoThrottleMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_autothrottle_mode, 
                                    sizeof(pData->autopilot_active_autothrottle_mode), "None", 
                                    "AutopilotActiveAutoThrottleMode");
                return;
            }
            
            if (hash == MessageAutopilotActiveCollectiveMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_collective_mode, 
                                    sizeof(pData->autopilot_active_collective_mode), "None", 
                                    "AutopilotActiveCollectiveMode");
                return;
            }
            
            if (hash == MessageAutopilotArmedCollectiveMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_collective_mode, 
                                    sizeof(pData->autopilot_armed_collective_mode), "None", 
                                    "AutopilotArmedCollectiveMode");
                return;
            }
            
            if (hash == MessageAutopilotType.GetID()) {
                ProcessStringMessage(message, pData->autopilot_type, 
                                    sizeof(pData->autopilot_type), "Unknown", 
                                    "AutopilotType");
                return;
            }
            
            // === CONTROLS ===
            // MIGRATED TO HASH MAP: MessageControlsPitchInput
            // MIGRATED TO HASH MAP: MessageControlsRollInput
            // MIGRATED TO HASH MAP: MessageControlsYawInput
            // MIGRATED TO HASH MAP: MessageControlsThrottle
            // MIGRATED TO HASH MAP: MessageControlsGear
            // MIGRATED TO HASH MAP: MessageControlsFlaps
            
            // === ADDITIONAL CONTROL VARIABLES ===
            if (hash == MessageControlsWheelBrakeLeft.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_WHEEL_BRAKE_LEFT] = value;
                return;
            }
            if (hash == MessageControlsWheelBrakeRight.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_WHEEL_BRAKE_RIGHT] = value;
                return;
            }
            if (hash == MessageControlsAirBrake.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_AIR_BRAKE] = value;
                return;
            }
            if (hash == MessageControlsAirBrakeArm.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_AIR_BRAKE_ARM] = value;
                return;
            }
            if (hash == MessageControlsPropellerSpeed1.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_PROPELLER_SPEED_1] = value;
                return;
            }
            if (hash == MessageControlsPropellerSpeed2.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_PROPELLER_SPEED_2] = value;
                return;
            }
            if (hash == MessageControlsPropellerSpeed3.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_PROPELLER_SPEED_3] = value;
                return;
            }
            if (hash == MessageControlsPropellerSpeed4.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_PROPELLER_SPEED_4] = value;
                return;
            }
            if (hash == MessageControlsGliderAirBrake.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_GLIDER_AIR_BRAKE] = value;
                return;
            }
            if (hash == MessageControlsRotorBrake.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::CONTROLS_ROTOR_BRAKE] = value;
                return;
            }
            
            // === AIRCRAFT SYSTEM VARIABLES ===
            if (hash == MessageAircraftGroundSpoilersArmed.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_GROUND_SPOILERS_ARMED] = value;
                return;
            }
            if (hash == MessageAircraftGroundSpoilersExtended.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_GROUND_SPOILERS_EXTENDED] = value;
                return;
            }
            if (hash == MessageAircraftParkingBrake.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_PARKING_BRAKE] = value;
                return;
            }
            if (hash == MessageAircraftAutoBrakeSetting.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_BRAKE_SETTING] = value;
                return;
            }
            if (hash == MessageAircraftAutoBrakeEngaged.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_BRAKE_ENGAGED] = value;
                return;
            }
            if (hash == MessageAircraftAutoBrakeRejectedTakeOff.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_BRAKE_REJECTED_TAKEOFF] = value;
                return;
            }
            
            // === ENGINE SYSTEM VARIABLES ===
            if (hash == MessageAircraftStarter.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER] = value;
                return;
            }
            if (hash == MessageAircraftStarter1.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_1] = value;
                return;
            }
            if (hash == MessageAircraftStarter2.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_2] = value;
                return;
            }
            if (hash == MessageAircraftStarter3.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_3] = value;
                return;
            }
            if (hash == MessageAircraftStarter4.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_4] = value;
                return;
            }
            if (hash == MessageAircraftIgnition.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION] = value;
                return;
            }
            if (hash == MessageAircraftIgnition1.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_1] = value;
                return;
            }
            if (hash == MessageAircraftIgnition2.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_2] = value;
                return;
            }
            if (hash == MessageAircraftIgnition3.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_3] = value;
                return;
            }
            if (hash == MessageAircraftIgnition4.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_4] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster1.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_1] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster2.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_2] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster3.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_3] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster4.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_4] = value;
                return;
            }
            
            // === WARNINGS ===
            if (hash == MessageWarningsMasterWarning.GetID()) {
                const double value = message.GetDouble();
                pData->warnings_master_warning = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::WARNINGS_MASTER_WARNING] = value;
                return;
            }
            if (hash == MessageWarningsMasterCaution.GetID()) {
                const double value = message.GetDouble();
                pData->warnings_master_caution = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::WARNINGS_MASTER_CAUTION] = value;
                return;
            }
            if (hash == MessageWarningsLowOilPressure.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::WARNINGS_LOW_OIL_PRESSURE] = value;
                return;
            }
            if (hash == MessageWarningsLowFuelPressure.GetID()) {
                const double value = message.GetDouble();
                pData->all_variables[(int)VariableIndex::WARNINGS_LOW_FUEL_PRESSURE] = value;
                return;
            }

            // === AIRCRAFT EXTENDED VARIABLES ===

            if (hash == MessageAircraftHeight.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_height = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_HEIGHT] = value;
                return;
            }
            // MIGRATED TO HASH MAP: MessageAircraftPosition (duplicate)
            // MIGRATED TO HASH MAP: MessageAircraftOrientation
            // MIGRATED TO HASH MAP: MessageAircraftVelocity (duplicate)
            if (hash == MessageAircraftAngularVelocity.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_angular_velocity = value;
                // NOTE: Vector3d cannot be stored in all_variables array (double only)
                return;
            }
            if (hash == MessageAircraftAcceleration.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_acceleration = value;
                // NOTE: Vector3d cannot be stored in all_variables array (double only)
                return;
            }
            if (hash == MessageAircraftGravity.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_gravity = value;
                // NOTE: Vector3d cannot be stored in all_variables array (double only)
                return;
            }
            if (hash == MessageAircraftWind.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_wind = value;
                // NOTE: Vector3d cannot be stored in all_variables array (double only)
                return;
            }
            if (hash == MessageAircraftNearestAirportLocation.GetID()) {
                const tm_vector3d temp = message.GetVector3d();  
                pData->aircraft_nearest_airport_location = tm_vector2d(temp.x, temp.y);
                return;
            }
            if (hash == MessageAircraftBestAirportLocation.GetID()) {
                const tm_vector3d temp = message.GetVector3d();  
                pData->aircraft_best_airport_location = tm_vector2d(temp.x, temp.y);
                return;
            }
            if (hash == MessageAircraftBestRunwayThreshold.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_best_runway_threshold = value;
                // NOTE: Vector3d cannot be stored in all_variables array (double only)
                return;
            }
            if (hash == MessageAircraftBestRunwayEnd.GetID()) {
                const tm_vector3d value = message.GetVector3d();
                pData->aircraft_best_runway_end = value;
                // NOTE: Vector3d cannot be stored in all_variables array (double only)
                return;
            }
            if (hash == MessageAircraftOnGround.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_on_ground = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ON_GROUND] = value;
                return;
            }
            if (hash == MessageAircraftPower.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_power = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_POWER] = value;
                return;
            }
            if (hash == MessageAircraftNormalizedPower.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_normalized_power = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_NORMALIZED_POWER] = value;
                return;
            }
            if (hash == MessageAircraftNormalizedPowerTarget.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_normalized_power_target = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_NORMALIZED_POWER_TARGET] = value;
                return;
            }

            if (hash == MessageAircraftGroundSpoilersExtended.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_ground_spoilers_extended = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_GROUND_SPOILERS_EXTENDED] = value;
                return;
            }
            if (hash == MessageAircraftTrim.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_trim = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_TRIM] = value;
                return;
            }
            if (hash == MessageAircraftPitchTrim.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_pitch_trim = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_PITCH_TRIM] = value;
                return;
            }
            if (hash == MessageAircraftPitchTrimScaling.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_pitch_trim_scaling = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_PITCH_TRIM_SCALING] = value;
                return;
            }
            if (hash == MessageAircraftPitchTrimOffset.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_pitch_trim_offset = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_PITCH_TRIM_OFFSET] = value;
                return;
            }
            if (hash == MessageAircraftRudderTrim.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_rudder_trim = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_RUDDER_TRIM] = value;
                return;
            }
            if (hash == MessageAircraftAutoPitchTrim.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_auto_pitch_trim = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_PITCH_TRIM] = value;
                return;
            }
            if (hash == MessageAircraftYawDamperEnabled.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_yaw_damper_enabled = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_YAW_DAMPER_ENABLED] = value;
                return;
            }
            if (hash == MessageAircraftRudderPedalsDisconnected.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_rudder_pedals_disconnected = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_RUDDER_PEDALS_DISCONNECTED] = value;
                return;
            }
            if (hash == MessageAircraftParkingBrake.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_parking_brake = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_PARKING_BRAKE] = value;
                return;
            }
            if (hash == MessageAircraftAutoBrakeSetting.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_auto_brake_setting = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_BRAKE_SETTING] = value;
                return;
            }
            if (hash == MessageAircraftAutoBrakeEngaged.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_auto_brake_engaged = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_BRAKE_ENGAGED] = value;
                return;
            }
            if (hash == MessageAircraftAutoBrakeRejectedTakeOff.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_auto_brake_rejected_takeoff = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_AUTO_BRAKE_REJECTED_TAKEOFF] = value;
                return;
            }
            if (hash == MessageAircraftThrottleLimit.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_throttle_limit = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_THROTTLE_LIMIT] = value;
                return;
            }
            if (hash == MessageAircraftReverse.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_reverse = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_REVERSE] = value;
                return;
            }

            // === ENGINE SYSTEM VARIABLES ===

            if (hash == MessageAircraftStarter.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_starter = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER] = value;
                return;
            }
            if (hash == MessageAircraftStarter1.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_starter_1 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_1] = value;
                return;
            }
            if (hash == MessageAircraftStarter2.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_starter_2 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_2] = value;
                return;
            }
            if (hash == MessageAircraftStarter3.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_starter_3 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_3] = value;
                return;
            }
            if (hash == MessageAircraftStarter4.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_starter_4 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_STARTER_4] = value;
                return;
            }
            if (hash == MessageAircraftIgnition.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_ignition = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION] = value;
                return;
            }
            if (hash == MessageAircraftIgnition1.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_ignition_1 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_1] = value;
                return;
            }
            if (hash == MessageAircraftIgnition2.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_ignition_2 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_2] = value;
                return;
            }
            if (hash == MessageAircraftIgnition3.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_ignition_3 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_3] = value;
                return;
            }
            if (hash == MessageAircraftIgnition4.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_ignition_4 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_IGNITION_4] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster1.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_master_1 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_1] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster2.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_master_2 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_2] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster3.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_master_3 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_3] = value;
                return;
            }
            if (hash == MessageAircraftEngineMaster4.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_master_4 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_MASTER_4] = value;
                return;
            }
            if (hash == MessageAircraftEngineThrottle1.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_throttle_1 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_1] = value;
                return;
            }
            if (hash == MessageAircraftEngineThrottle2.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_throttle_2 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_2] = value;
                return;
            }
            if (hash == MessageAircraftEngineThrottle3.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_throttle_3 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_3] = value;
                return;
            }
            if (hash == MessageAircraftEngineThrottle4.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_throttle_4 = value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_THROTTLE_4] = value;
                return;
            }




            if (hash == MessageAircraftEngineRunning1.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_1 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_1] = value;
                return;
            }
            if (hash == MessageAircraftEngineRunning2.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_2 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_2] = value;
                return;
            }
            if (hash == MessageAircraftEngineRunning3.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_3 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_3] = value;
                return;
            }
            if (hash == MessageAircraftEngineRunning4.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_engine_running_4 = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_ENGINE_RUNNING_4] = value;
                return;
            }
            if (hash == MessageAircraftAPUAvailable.GetID()) {
                const double value = message.GetDouble();
                pData->aircraft_apu_available = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AIRCRAFT_APU_AVAILABLE] = value;
                return;
            }

            if (hash == MessagePerformanceSpeedVAPP.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_vapp = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_VAPP] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedMinimum.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_minimum = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_MINIMUM] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedMaximum.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_maximum = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_MAXIMUM] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedMinimumFlapRetraction.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_minimum_flap_retraction = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_MINIMUM_FLAP_RETRACTION] = value;
                return;
            }
            if (hash == MessagePerformanceSpeedMaximumFlapExtension.GetID()) {
                const double value = message.GetDouble();
                pData->performance_speed_maximum_flap_extension = value;
                pData->all_variables[(int)VariableIndex::PERFORMANCE_SPEED_MAXIMUM_FLAP_EXTENSION] = value;
                return;
            }

            // === CONFIGURATION VARIABLES ===

            if (hash == MessageConfigurationSelectedTakeOffFlaps.GetID()) {
                const double value = message.GetDouble();
                pData->configuration_selected_takeoff_flaps = value;
                pData->all_variables[(int)VariableIndex::CONFIGURATION_SELECTED_TAKEOFF_FLAPS] = value;
                return;
            }
            if (hash == MessageConfigurationSelectedLandingFlaps.GetID()) {
                const double value = message.GetDouble();
                pData->configuration_selected_landing_flaps = value;
                pData->all_variables[(int)VariableIndex::CONFIGURATION_SELECTED_LANDING_FLAPS] = value;
                return;
            }

            // === NAVIGATION EXTENDED  ===










            if (hash == MessageNavigationADF2Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf2_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationADF1StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf1_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationADF2StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf2_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationADF1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationADF2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationILS1Course.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_course = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_COURSE] = value;
                return;
            }
            if (hash == MessageNavigationILS2Course.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_course = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_COURSE] = value;
                return;
            }
            if (hash == MessageNavigationILS1Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS2Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS1StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS2StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationILS2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationNAV1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_nav1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_NAV1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationNAV2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_nav2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_NAV2_FREQUENCY_SWAP] = value;
                return;
            }

            // === COMMUNICATION EXTENDED ===

            if (hash == MessageNavigationCOM3Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com3_frequency = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationCOM3StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com3_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationCOM3FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com3_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationCOM1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationCOM2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM2_FREQUENCY_SWAP] = value;
                return;
            }

            // === AUTOPILOT EXTENDED ===

            if (hash == MessageAutopilotMaster.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_master = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_MASTER] = value;
                return;
            }

            // === AUTOPILOT STRING HANDLERS ===

            if (hash == MessageAutopilotType.GetID()) {
                ProcessStringMessage(message, pData->autopilot_type, 
                                    sizeof(pData->autopilot_type), "", 
                                    "AutopilotType");
                return;
            }
            if (hash == MessageAutopilotActiveLateralMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_lateral_mode, 
                                    sizeof(pData->autopilot_active_lateral_mode), "", 
                                    "AutopilotActiveLateralMode");
                return;
            }
            if (hash == MessageAutopilotArmedLateralMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_lateral_mode, 
                                    sizeof(pData->autopilot_armed_lateral_mode), "", 
                                    "AutopilotArmedLateralMode");
                return;
            }
            if (hash == MessageAutopilotActiveVerticalMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_vertical_mode, 
                                    sizeof(pData->autopilot_active_vertical_mode), "", 
                                    "AutopilotActiveVerticalMode");
                return;
            }
            if (hash == MessageAutopilotArmedVerticalMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_vertical_mode, 
                                    sizeof(pData->autopilot_armed_vertical_mode), "", 
                                    "AutopilotArmedVerticalMode");
                return;
            }
            if (hash == MessageAutopilotArmedApproachMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_approach_mode, 
                                    sizeof(pData->autopilot_armed_approach_mode), "", 
                                    "AutopilotArmedApproachMode");
                return;
            }
            if (hash == MessageAutopilotActiveAutoThrottleMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_autothrottle_mode, 
                                    sizeof(pData->autopilot_active_autothrottle_mode), "", 
                                    "AutopilotActiveAutoThrottleMode");
                return;
            }
            if (hash == MessageAutopilotActiveCollectiveMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_collective_mode, 
                                    sizeof(pData->autopilot_active_collective_mode), "", 
                                    "AutopilotActiveCollectiveMode");
                return;
            }
            if (hash == MessageAutopilotArmedCollectiveMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_collective_mode, 
                                    sizeof(pData->autopilot_armed_collective_mode), "", 
                                    "AutopilotArmedCollectiveMode");
                return;
            }

            // === NAVIGATION EXTENDED  ===










            if (hash == MessageNavigationADF2Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf2_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationADF1StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf1_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationADF2StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf2_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationADF1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationADF2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_adf2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ADF2_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationILS1Course.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_course = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_COURSE] = value;
                return;
            }
            if (hash == MessageNavigationILS2Course.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_course = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_COURSE] = value;
                return;
            }
            if (hash == MessageNavigationILS1Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS2Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS1StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS2StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationILS1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationILS2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_ils2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_ILS2_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationNAV1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_nav1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_NAV1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationNAV2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->navigation_nav2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::NAVIGATION_NAV2_FREQUENCY_SWAP] = value;
                return;
            }

            // === COMMUNICATION EXTENDED ===

            if (hash == MessageNavigationCOM3Frequency.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com3_frequency = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationCOM3StandbyFrequency.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com3_standby_frequency = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_STANDBY_FREQUENCY] = value;
                return;
            }
            if (hash == MessageNavigationCOM3FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com3_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM3_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationCOM1FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com1_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM1_FREQUENCY_SWAP] = value;
                return;
            }
            if (hash == MessageNavigationCOM2FrequencySwap.GetID()) {
                const double value = message.GetDouble();
                pData->communication_com2_frequency_swap = value;
                pData->all_variables[(int)VariableIndex::COMMUNICATION_COM2_FREQUENCY_SWAP] = value;
                return;
            }

            if (hash == MessageTransponderCode.GetID()) {
                const double value = message.GetDouble();
                // NOTE: transponder_code field not defined in AeroflyBridgeData structure
                // Only store in all_variables array for now
                pData->all_variables[(int)VariableIndex::COMMUNICATION_TRANSPONDER_CODE] = value;
                return;
            }
            if (hash == MessageTransponderCursor.GetID()) {
                const double value = message.GetDouble();
                // NOTE: transponder_cursor field not defined in AeroflyBridgeData structure  
                // Only store in all_variables array for now
                pData->all_variables[(int)VariableIndex::COMMUNICATION_TRANSPONDER_CURSOR] = value;
                return;
            }

            if (hash == MessageAutopilotMaster.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_master = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_MASTER] = value;
                return;
            }
            if (hash == MessageAutopilotDisengage.GetID()) {
                const double value = message.GetDouble();
                pData->autopilot_disengage = (uint32_t)value;
                pData->all_variables[(int)VariableIndex::AUTOPILOT_DISENGAGE] = value;
                return;
            }

            // === AUTOPILOT STRING HANDLERS ===

            if (hash == MessageAutopilotType.GetID()) {
                ProcessStringMessage(message, pData->autopilot_type, 
                                    sizeof(pData->autopilot_type), "", 
                                    "AutopilotType");
                return;
            }
            if (hash == MessageAutopilotActiveLateralMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_lateral_mode, 
                                    sizeof(pData->autopilot_active_lateral_mode), "", 
                                    "AutopilotActiveLateralMode");
                return;
            }
            if (hash == MessageAutopilotArmedLateralMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_lateral_mode, 
                                    sizeof(pData->autopilot_armed_lateral_mode), "", 
                                    "AutopilotArmedLateralMode");
                return;
            }
            if (hash == MessageAutopilotActiveVerticalMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_vertical_mode, 
                                    sizeof(pData->autopilot_active_vertical_mode), "", 
                                    "AutopilotActiveVerticalMode");
                return;
            }
            if (hash == MessageAutopilotArmedVerticalMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_vertical_mode, 
                                    sizeof(pData->autopilot_armed_vertical_mode), "", 
                                    "AutopilotArmedVerticalMode");
                return;
            }
            if (hash == MessageAutopilotArmedApproachMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_approach_mode, 
                                    sizeof(pData->autopilot_armed_approach_mode), "", 
                                    "AutopilotArmedApproachMode");
                return;
            }
            if (hash == MessageAutopilotActiveAutoThrottleMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_autothrottle_mode, 
                                    sizeof(pData->autopilot_active_autothrottle_mode), "", 
                                    "AutopilotActiveAutoThrottleMode");
                return;
            }
            if (hash == MessageAutopilotActiveCollectiveMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_active_collective_mode, 
                                    sizeof(pData->autopilot_active_collective_mode), "", 
                                    "AutopilotActiveCollectiveMode");
                return;
            }
            if (hash == MessageAutopilotArmedCollectiveMode.GetID()) {
                ProcessStringMessage(message, pData->autopilot_armed_collective_mode, 
                                    sizeof(pData->autopilot_armed_collective_mode), "", 
                                    "AutopilotArmedCollectiveMode");
                return;
            }

                        
///////////////////////////////////////////////////////////////////////////////////////////////////
// EXTENDED MESSAGE PROCESSING
///////////////////////////////////////////////////////////////////////////////////////////////////

            // === CESSNA 172 SPECIFIC CONTROLS ===
            if (hash == MessageC172FuelSelector.GetID()) {
                const double value = message.GetDouble();
                pData->c172_fuel_selector = value;
                pData->all_variables[(int)VariableIndex::C172_FUEL_SELECTOR] = value;
                return;
            }
            if (hash == MessageC172FuelShutOff.GetID()) {
                const double value = message.GetDouble();
                pData->c172_fuel_shut_off = value;
                pData->all_variables[(int)VariableIndex::C172_FUEL_SHUT_OFF] = value;
                return;
            }
            if (hash == MessageC172HideYokeLeft.GetID()) {
                const double value = message.GetDouble();
                pData->c172_hide_yoke_left = value;
                pData->all_variables[(int)VariableIndex::C172_HIDE_YOKE_LEFT] = value;
                return;
            }
            if (hash == MessageC172HideYokeRight.GetID()) {
                const double value = message.GetDouble();
                pData->c172_hide_yoke_right = value;
                pData->all_variables[(int)VariableIndex::C172_HIDE_YOKE_RIGHT] = value;
                return;
            }
            if (hash == MessageC172LeftSunBlocker.GetID()) {
                const double value = message.GetDouble();
                pData->c172_left_sun_blocker = value;
                pData->all_variables[(int)VariableIndex::C172_LEFT_SUN_BLOCKER] = value;
                return;
            }
            if (hash == MessageC172RightSunBlocker.GetID()) {
                const double value = message.GetDouble();
                pData->c172_right_sun_blocker = value;
                pData->all_variables[(int)VariableIndex::C172_RIGHT_SUN_BLOCKER] = value;
                return;
            }
            if (hash == MessageC172LeftCabinLight.GetID()) {
                const double value = message.GetDouble();
                pData->c172_left_cabin_light = value;
                pData->all_variables[(int)VariableIndex::C172_LEFT_CABIN_LIGHT] = value;
                return;
            }
            if (hash == MessageC172RightCabinLight.GetID()) {
                const double value = message.GetDouble();
                pData->c172_right_cabin_light = value;
                pData->all_variables[(int)VariableIndex::C172_RIGHT_CABIN_LIGHT] = value;
                return;
            }
            if (hash == MessageC172Magnetos1.GetID()) {
                const double value = message.GetDouble();
                pData->c172_magnetos_1 = value;
                pData->all_variables[(int)VariableIndex::C172_MAGNETOS_1] = value;
                return;
            }
            if (hash == MessageC172ParkingBrakeHandle.GetID()) {
                const double value = message.GetDouble();
                pData->c172_parking_brake_handle = value;
                pData->all_variables[(int)VariableIndex::C172_PARKING_BRAKE_HANDLE] = value;
                return;
            }
            if (hash == MessageC172TrimWheel.GetID()) {
                const double value = message.GetDouble();
                pData->c172_trim_wheel = value;
                pData->all_variables[(int)VariableIndex::C172_TRIM_WHEEL] = value;
                return;
            }
            if (hash == MessageC172LeftYokeButton.GetID()) {
                const double value = message.GetDouble();
                pData->c172_left_yoke_button = value;
                pData->all_variables[(int)VariableIndex::C172_LEFT_YOKE_BUTTON] = value;
                return;
            }
            
            // === DOORS AND WINDOWS ===
            if (hash == MessageC172LeftDoor.GetID()) {
                ProcessStepControl(message, pData->c172_left_door, 
                                      (int)VariableIndex::C172_LEFT_DOOR, 
                                      "C172 Left Door");
                return;
            }
            if (hash == MessageC172LeftDoorHandle.GetID()) {
                ProcessStepControl(message, pData->c172_left_door_handle, 
                                      (int)VariableIndex::C172_LEFT_DOOR_HANDLE, 
                                      "C172 Left Door Handle");
                return;
            }
            if (hash == MessageC172RightDoor.GetID()) {
                ProcessStepControl(message, pData->c172_right_door, 
                                      (int)VariableIndex::C172_RIGHT_DOOR, 
                                      "C172 Right Door");
                return;
            }
            if (hash == MessageC172RightDoorHandle.GetID()) {
                ProcessStepControl(message, pData->c172_right_door_handle, 
                                      (int)VariableIndex::C172_RIGHT_DOOR_HANDLE, 
                                      "C172 Right Door Handle");
                return;
            }
            // MIGRATED TO HASH MAP: MessageC172LeftWindow
            // MIGRATED TO HASH MAP: MessageC172RightWindow

        }
        catch (const std::exception& e) {
            // Log the error and continue processing
            std::string error_msg = "ERROR: Exception in ProcessMessage: " + std::string(e.what()) + "\n";
            OutputDebugStringA(error_msg.c_str());
        }
        catch (...) {
            // Handle any other exceptions (including assertion failures)
            OutputDebugStringA("ERROR: Unknown exception in ProcessMessage (possibly assertion failure)\n");
        }
    }
        
    /** Unmaps the view and closes the shared memory handle. */
    void Cleanup() {
        if (pData) {
            UnmapViewOfFile(pData);
            pData = nullptr;
        }
        if (hMapFile) {
            CloseHandle(hMapFile);
            hMapFile = NULL;
        }
        initialized = false;
    }
    
    AeroflyBridgeData* GetData() { return pData; }
    bool IsInitialized() const { return initialized; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// TCP SERVER INTERFACE
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Network interface for remote data access and control.
 * 
 * This class provides a TCP server implementation that enables:
 * - Real-time data streaming to multiple clients in JSON format
 * - Remote command reception and processing
 * - Automatic client connection management
 * - Thread-safe operation
 * 
 * The server operates on two ports:
 * - Data Port (default 12345): Broadcasts simulation data to clients
 * - Command Port (default 12346): Receives control commands from clients
 */
/**
 * TCPServerInterface - Simple TCP JSON streaming server
 *
 * Data port streams JSON telemetry to all connected clients.
 * Command port receives one JSON command per connection.
 * Thread-safe client list and command queue. Non-blocking sockets
 * with select() polling for responsiveness.
 */
class TCPServerInterface {
private:
    SOCKET server_socket;                  ///< Main server socket for data connections
    std::vector<SOCKET> client_sockets;    ///< Connected client sockets
    std::thread server_thread;             ///< Thread handling client connections
    std::thread command_thread;            ///< Thread handling command reception
    std::atomic<bool> running;             ///< Server running state flag
    mutable std::mutex clients_mutex;      ///< Mutex for client list synchronization
    VariableMapper mapper;                 ///< Maps variable names to indices
    bool winsock_initialized;              ///< Tracks if WSAStartup succeeded
    uint64_t last_broadcast_us;            ///< Last broadcast timestamp for throttling
    
    // Command Processing
    std::queue<std::string> command_queue; ///< Queue of pending commands
    mutable std::mutex command_mutex;      ///< Mutex for command queue synchronization
    
public:
    /**
     * @brief Initializes a new TCP server interface.
     * Creates an instance in a stopped state with no active connections.
     */
    TCPServerInterface() : server_socket(INVALID_SOCKET), running(false), winsock_initialized(false), last_broadcast_us(0) {}
    
    /**
     * @brief Cleans up server resources.
     * Ensures proper shutdown of all connections and threads.
     */
    ~TCPServerInterface() {
        Stop();
    }
    
    /**
     * @brief Starts the TCP server and begins accepting connections.
     * 
     * This method:
     * 1. Initializes the Windows Socket API
     * 2. Creates and configures the server socket
     * 3. Binds to the specified ports
     * 4. Launches server and command handling threads
     * 
     * @param data_port Port number for data streaming (default: 12345)
     * @param command_port Port number for command reception (default: 12346)
     * @return true if server started successfully, false if any error occurred
     */
    bool Start(int data_port = 12345, int command_port = 12346) {
        // Initialize Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }
        winsock_initialized = true;
        
        // Create server socket for data
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            if (winsock_initialized) { WSACleanup(); winsock_initialized = false; }
            return false;
        }
        
        // Allow socket reuse
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        // Bind to data port
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(data_port);
        
        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            closesocket(server_socket);
            if (winsock_initialized) { WSACleanup(); winsock_initialized = false; }
            return false;
        }
        
        // Listen for connections
        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server_socket);
            if (winsock_initialized) { WSACleanup(); winsock_initialized = false; }
            return false;
        }
        
        // Start server thread
        running = true;
        server_thread = std::thread(&TCPServerInterface::ServerLoop, this);
        command_thread = std::thread(&TCPServerInterface::CommandLoop, this, command_port);
        
        return true;
    }
    
    /**
     * @brief Performs a clean shutdown of the server.
     * 
     * Shutdown sequence:
     * 1. Marks server as not running to stop threads
     * 2. Closes main server socket
     * 3. Closes all client connections
     * 4. Waits for server and command threads to finish
     * 
     * This method is thread-safe and can be called from any context.
     */
    void Stop() {
        DBG("=== TCPServer::Stop() STARTED ===\n");
        
        // Mark as not running FIRST
        running = false;
        
        // Close server socket to wake up blocked accept()
        if (server_socket != INVALID_SOCKET) {
            DBG("Closing main server socket...\n");
            shutdown(server_socket, SD_BOTH);  // Shutdown before close
            closesocket(server_socket);
            server_socket = INVALID_SOCKET;
        }
        
        // Close all client connections
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            DBG("Closing client connections...\n");
            for (SOCKET client : client_sockets) {
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
            client_sockets.clear();
        }
        
        // Wait for threads to finish with TIMEOUT
        if (server_thread.joinable()) {
            DBG("Waiting for server_thread...\n");
            server_thread.join();
            DBG("server_thread finished\n");
        }
        
        if (command_thread.joinable()) {
            DBG("Waiting for command_thread...\n");
            command_thread.join();
            DBG("command_thread finished\n");
        }
        
        DBG("=== TCPServer::Stop() COMPLETED ===\n");

        // Balanced cleanup of Winsock
        if (winsock_initialized) {
            DBG("WSACleanup() in TCPServer::Stop()\n");
            WSACleanup();
            winsock_initialized = false;
        }
    }

    // Expose variable name->index mapping for external tools generation
    std::vector<std::pair<std::string,int>> GetMapperSnapshot() const {
        return mapper.GetNameToIndexSnapshot();
    }
    
    /**
     * @brief Broadcasts simulation data to all connected clients.
     * 
     * This method:
     * 1. Converts simulation data to JSON format
     * 2. Sends the JSON to all connected clients
     * 3. Automatically removes disconnected clients
     * 
     * @param data Pointer to current simulation data
     * @note Thread-safe: uses mutex to protect client list
     */
    // Broadcasts simulation data to all TCP clients in JSON format.
    // Throttled by AEROFLY_BRIDGE_BROADCAST_MS (default 20ms).
    void BroadcastData(const AeroflyBridgeData* data) {
        if (!data || !running) return;

        // Throttle broadcast to ~50 Hz
        const uint64_t now_us = get_time_us();
        // Read interval from env once (AEROFLY_BRIDGE_BROADCAST_MS), default 20ms
        auto get_interval_ms = []() -> uint32_t {
            static uint32_t cached = []{
                char buf[32] = {0};
                DWORD n = GetEnvironmentVariableA("AEROFLY_BRIDGE_BROADCAST_MS", buf, sizeof(buf));
                int ms = (n > 0) ? atoi(buf) : 20;
                if (ms < 5) ms = 5; // clamp
                return static_cast<uint32_t>(ms);
            }();
            return cached;
        };
        if (now_us - last_broadcast_us < static_cast<uint64_t>(get_interval_ms()) * 1000ULL) {
            return;
        }
        last_broadcast_us = now_us;

        // Create JSON message
        const std::string json_data = CreateDataJSON(data);

        // Snapshot sockets to reduce lock contention
        std::vector<SOCKET> sockets_snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            sockets_snapshot = client_sockets;
        }

        auto try_send_all_nonblocking = [](SOCKET s, const char* buf, size_t len) -> int {
            size_t total_sent = 0;
            while (total_sent < len) {
                int n = send(s, buf + total_sent, static_cast<int>(len - total_sent), 0);
                if (n > 0) {
                    total_sent += static_cast<size_t>(n);
                    continue;
                }
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                    // Not fatal; let caller decide if keep-alive
                    return 1; // partial/pending
                }
                return -1; // fatal
            }
            return 0; // complete
        };

        // Track sockets that failed fatally to remove them
        std::vector<SOCKET> to_remove;
        for (SOCKET s : sockets_snapshot) {
            int rc = try_send_all_nonblocking(s, json_data.data(), json_data.size());
            if (rc < 0) {
                to_remove.push_back(s);
            }
        }

        if (!to_remove.empty()) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (SOCKET s : to_remove) {
                auto it = std::find(client_sockets.begin(), client_sockets.end(), s);
                if (it != client_sockets.end()) {
                    closesocket(*it);
                    client_sockets.erase(it);
                }
            }
        }
    }
    
private:
    void ServerLoop() {
        DBG("ServerLoop started\n");
        
        while (running) {
            // Set timeout on accept to avoid infinite blocking
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_socket, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;   // 1 second timeout
            timeout.tv_usec = 0;
            
            int result = select(0, &readfds, NULL, NULL, &timeout);
            
            if (result > 0 && FD_ISSET(server_socket, &readfds)) {
                // Accept new connection
                SOCKET client_socket = accept(server_socket, nullptr, nullptr);
                if (client_socket != INVALID_SOCKET && running) {
                    u_long mode = 1;
                    ioctlsocket(client_socket, FIONBIO, &mode);

                    // Low-latency and keepalive for data clients
                    int yes = 1;
                    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
                    setsockopt(client_socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&yes, sizeof(yes));
                    
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    client_sockets.push_back(client_socket);
                    DBG("Client connected\n");
                }
            }
            else if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error != WSAEINTR && running) {
                    DBG("Error in select()\n");
                    break;
                }
            }
            // result == 0 = timeout, continue loop
        }
        
        DBG("ServerLoop finished\n");
    }
    
    /**
     * CommandLoop - accepts incoming command connections, reads a single
     * JSON command, enqueues it, then closes the client.
     */
    void CommandLoop(int command_port) {
        DBG("CommandLoop started\n");
        
        // Create command server socket
        SOCKET cmd_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (cmd_socket == INVALID_SOCKET) {
            DBG("Failed to create command socket\n");
            return;
        }
        
        int opt = 1;
        setsockopt(cmd_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        sockaddr_in cmd_addr;
        cmd_addr.sin_family = AF_INET;
        cmd_addr.sin_addr.s_addr = INADDR_ANY;
        cmd_addr.sin_port = htons(command_port);
        
        if (bind(cmd_socket, (sockaddr*)&cmd_addr, sizeof(cmd_addr)) == SOCKET_ERROR ||
            listen(cmd_socket, SOMAXCONN) == SOCKET_ERROR) {
            DBG("Failed to bind/listen command socket\n");
            closesocket(cmd_socket);
            return;
        }
        
        while (running) {
            // Use select with timeout instead of blocking accept
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(cmd_socket, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;   // 1 second timeout
            timeout.tv_usec = 0;
            
            int result = select(0, &readfds, NULL, NULL, &timeout);
            
            if (result > 0 && FD_ISSET(cmd_socket, &readfds)) {
                SOCKET client = accept(cmd_socket, nullptr, nullptr);
                if (client != INVALID_SOCKET && running) {
                    // Low-latency and keepalive for command clients
                    int yes = 1;
                    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
                    setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, (char*)&yes, sizeof(yes));
                    // Handle command from client
                    char buffer[1024];
                    int bytes_received = recv(client, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_received > 0) {
                        buffer[bytes_received] = '\0';
                        ProcessCommand(std::string(buffer));
                        DBG("Command processed\n");
                    }
                    closesocket(client);
                }
            }
            else if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error != WSAEINTR && running) {
                    DBG("Error in command select()\n");
                    break;
                }
            }
            // result == 0 = timeout, continue loop
        }
        
        DBG("Closing command socket\n");
        closesocket(cmd_socket);
        DBG("CommandLoop finished\n");
    }
    
    // Delegate to shared JSON builder to keep WS/TCP identical
    std::string CreateDataJSON(const AeroflyBridgeData* data) {
        return BuildDataJSON(data);
    }
    
    void ProcessCommand(const std::string& command) {
        std::lock_guard<std::mutex> lock(command_mutex);
        command_queue.push(command);
    }
    
public:
    /**
     * @brief Retrieves and clears all pending commands.
     * 
     * This method:
     * - Returns all commands received since last call
     * - Clears the command queue after retrieval
     * - Maintains thread safety using mutex
     * 
     * @return Vector of command strings to be processed
     */
    std::vector<std::string> GetPendingCommands() {
        std::vector<std::string> commands;
        std::lock_guard<std::mutex> lock(command_mutex);
        
        while (!command_queue.empty()) {
            commands.push_back(command_queue.front());
            command_queue.pop();
        }
        
        return commands;
    }
    
    /**
     * @brief Gets the current number of connected clients.
     * 
     * Thread-safe method to check how many clients are
     * currently connected to the data streaming port.
     * 
     * @return Number of active client connections
     */
    int GetClientCount() const {
        std::lock_guard<std::mutex> lock(clients_mutex);
        return static_cast<int>(client_sockets.size());
    }
};

// ================================================================================================
// SHARED JSON BUILDER (TCP and WebSocket use the same payload)
// ================================================================================================
static std::string BuildDataJSON(const AeroflyBridgeData* data) {
    if (!data) return std::string("{}\n");
    
    std::ostringstream json;
    json.setf(std::ios::fixed);
    json.precision(6);
    json.seekp(0);
    
    json << "{";
    
    // JSON schema identifiers for consumers
    json << "\"schema\":\"aerofly-bridge-telemetry\",";
    json << "\"schema_version\":1,";
    json << "\"timestamp\":" << data->timestamp_us << ",";
    json << "\"timestamp_unit\":\"microseconds\",";
    json << "\"data_valid\":" << data->data_valid << ",";
    json << "\"update_counter\":" << data->update_counter << ",";
    
    // Broadcast cadence hint
    json << "\"broadcast_rate_hz\":";
    {
        // Mirror same env logic as servers to expose configured cadence
        static double cached_rate = [](){
            char buf[32] = {0};
            DWORD n = GetEnvironmentVariableA("AEROFLY_BRIDGE_BROADCAST_MS", buf, sizeof(buf));
            int ms = (n > 0) ? atoi(buf) : 20; 
            if (ms < 5) ms = 5;
            return 1000.0 / (double)ms;
        }();
        json << cached_rate << ",";
    }
    
    // VARIABLES SECTION - ALL 339+ VARIABLES WITH DESCRIPTIVE NAMES
    json << "\"variables\":{";
    
    // === AIRCRAFT VARIABLES (0-94) FROM all_variables[] ===
    json << "\"Aircraft.UniversalTime\":" << data->all_variables[0];
    json << ",\"Aircraft.Altitude\":" << data->all_variables[1];
    json << ",\"Aircraft.VerticalSpeed\":" << data->all_variables[2];
    json << ",\"Aircraft.Pitch\":" << data->all_variables[3];
    json << ",\"Aircraft.Bank\":" << data->all_variables[4];
    json << ",\"Aircraft.IndicatedAirspeed\":" << data->all_variables[5];
    json << ",\"Aircraft.IndicatedAirspeedTrend\":" << data->all_variables[6];
    json << ",\"Aircraft.GroundSpeed\":" << data->all_variables[7];
    json << ",\"Aircraft.MagneticHeading\":" << data->all_variables[8];
    json << ",\"Aircraft.TrueHeading\":" << data->all_variables[9];
    json << ",\"Aircraft.Latitude\":" << data->all_variables[10];
    json << ",\"Aircraft.Longitude\":" << data->all_variables[11];
    json << ",\"Aircraft.Height\":" << data->all_variables[12];
    // Note: Aircraft.Position (13) is Vector3d - handled separately
    json << ",\"Aircraft.Orientation\":" << data->all_variables[14];
    // Note: Aircraft.Velocity (15) is Vector3d - handled separately
    // Note: Aircraft.AngularVelocity (16) is Vector3d - handled separately  
    // Note: Aircraft.Acceleration (17) is Vector3d - handled separately
    // Note: Aircraft.Gravity (18) is Vector3d - handled separately
    // Note: Aircraft.Wind (19) is Vector3d - handled separately
    json << ",\"Aircraft.RateOfTurn\":" << data->all_variables[20];
    json << ",\"Aircraft.MachNumber\":" << data->all_variables[21];
    json << ",\"Aircraft.AngleOfAttack\":" << data->all_variables[22];
    json << ",\"Aircraft.AngleOfAttackLimit\":" << data->all_variables[23];
    json << ",\"Aircraft.AccelerationLimit\":" << data->all_variables[24];
    json << ",\"Aircraft.Gear\":" << data->all_variables[25];
    json << ",\"Aircraft.Flaps\":" << data->all_variables[26];
    json << ",\"Aircraft.Slats\":" << data->all_variables[27];
    json << ",\"Aircraft.Throttle\":" << data->all_variables[28];
    json << ",\"Aircraft.AirBrake\":" << data->all_variables[29];
    json << ",\"Aircraft.GroundSpoilersArmed\":" << data->all_variables[30];
    json << ",\"Aircraft.GroundSpoilersExtended\":" << data->all_variables[31];
    json << ",\"Aircraft.ParkingBrake\":" << data->all_variables[32];
    json << ",\"Aircraft.AutoBrakeSetting\":" << data->all_variables[33];
    json << ",\"Aircraft.AutoBrakeEngaged\":" << data->all_variables[34];
    json << ",\"Aircraft.AutoBrakeRejectedTakeOff\":" << data->all_variables[35];
    json << ",\"Aircraft.RadarAltitude\":" << data->all_variables[36];
    // Note: Aircraft.Name (37) is String - handled separately
    // Note: Aircraft.NearestAirportIdentifier (38) is String - handled separately
    // Note: Aircraft.NearestAirportName (39) is String - handled separately
    // Note: Aircraft.NearestAirportLocation (40) is Vector2d - handled separately
    json << ",\"Aircraft.NearestAirportElevation\":" << data->all_variables[41];
    // Note: Aircraft.BestAirportIdentifier (42) is String - handled separately
    // Note: Aircraft.BestAirportName (43) is String - handled separately
    // Note: Aircraft.BestAirportLocation (44) is Vector2d - handled separately
    json << ",\"Aircraft.BestAirportElevation\":" << data->all_variables[45];
    // Note: Aircraft.BestRunwayIdentifier (46) is String - handled separately
    json << ",\"Aircraft.BestRunwayElevation\":" << data->all_variables[47];
    // Note: Aircraft.BestRunwayThreshold (48) is Vector3d - handled separately
    // Note: Aircraft.BestRunwayEnd (49) is Vector3d - handled separately
    json << ",\"Aircraft.Category.Jet\":" << data->all_variables[50];
    json << ",\"Aircraft.Category.Glider\":" << data->all_variables[51];
    json << ",\"Aircraft.OnGround\":" << data->all_variables[52];
    json << ",\"Aircraft.OnRunway\":" << data->all_variables[53];
    json << ",\"Aircraft.Crashed\":" << data->all_variables[54];
    json << ",\"Aircraft.Power\":" << data->all_variables[55];
    json << ",\"Aircraft.NormalizedPower\":" << data->all_variables[56];
    json << ",\"Aircraft.NormalizedPowerTarget\":" << data->all_variables[57];
    json << ",\"Aircraft.Trim\":" << data->all_variables[58];
    json << ",\"Aircraft.PitchTrim\":" << data->all_variables[59];
    json << ",\"Aircraft.PitchTrimScaling\":" << data->all_variables[60];
    json << ",\"Aircraft.RudderTrim\":" << data->all_variables[61];
    json << ",\"Aircraft.AileronTrim\":" << data->all_variables[62];
    json << ",\"Aircraft.YawDamperEnabled\":" << data->all_variables[63];
    json << ",\"Aircraft.AutoPitchTrim\":" << data->all_variables[64];
    
    // === ENGINE VARIABLES (65-94) ===
    json << ",\"Aircraft.EngineMaster1\":" << data->all_variables[65];
    json << ",\"Aircraft.EngineMaster2\":" << data->all_variables[66];
    json << ",\"Aircraft.EngineMaster3\":" << data->all_variables[67];
    json << ",\"Aircraft.EngineMaster4\":" << data->all_variables[68];
    json << ",\"Aircraft.Starter1\":" << data->all_variables[69];
    json << ",\"Aircraft.Starter2\":" << data->all_variables[70];
    json << ",\"Aircraft.Starter3\":" << data->all_variables[71];
    json << ",\"Aircraft.Starter4\":" << data->all_variables[72];
    json << ",\"Aircraft.Ignition1\":" << data->all_variables[73];
    json << ",\"Aircraft.Ignition2\":" << data->all_variables[74];
    json << ",\"Aircraft.Ignition3\":" << data->all_variables[75];
    json << ",\"Aircraft.Ignition4\":" << data->all_variables[76];
    json << ",\"Aircraft.EngineRotationSpeed1\":" << data->all_variables[77];
    json << ",\"Aircraft.EngineRotationSpeed2\":" << data->all_variables[78];
    json << ",\"Aircraft.EngineRotationSpeed3\":" << data->all_variables[79];
    json << ",\"Aircraft.EngineRotationSpeed4\":" << data->all_variables[80];
    json << ",\"Aircraft.EngineRunning1\":" << data->all_variables[81];
    json << ",\"Aircraft.EngineRunning2\":" << data->all_variables[82];
    json << ",\"Aircraft.EngineRunning3\":" << data->all_variables[83];
    json << ",\"Aircraft.EngineRunning4\":" << data->all_variables[84];
    json << ",\"Aircraft.ThrottleLimit\":" << data->all_variables[85];
    json << ",\"Aircraft.APUAvailable\":" << data->all_variables[86];
    json << ",\"Aircraft.APURunning\":" << data->all_variables[87];
    json << ",\"Aircraft.APUGeneratorPowered\":" << data->all_variables[88];
    json << ",\"Aircraft.APUBleedAirValve\":" << data->all_variables[89];
    json << ",\"Aircraft.GPUAvailable\":" << data->all_variables[90];
    json << ",\"Aircraft.GPUPowered\":" << data->all_variables[91];
    json << ",\"Aircraft.ExternalAirPowered\":" << data->all_variables[92];
    json << ",\"Aircraft.Generator1\":" << data->all_variables[93];
    json << ",\"Aircraft.Generator2\":" << data->all_variables[94];
    
    // === PERFORMANCE SPEEDS (95-104) ===
    json << ",\"Performance.Speed.VS0\":" << data->all_variables[95];
    json << ",\"Performance.Speed.VS1\":" << data->all_variables[96];
    json << ",\"Performance.Speed.VFE\":" << data->all_variables[97];
    json << ",\"Performance.Speed.VNO\":" << data->all_variables[98];
    json << ",\"Performance.Speed.VNE\":" << data->all_variables[99];
    json << ",\"Performance.Speed.VAPP\":" << data->all_variables[100];
    json << ",\"Performance.Speed.Minimum\":" << data->all_variables[101];
    json << ",\"Performance.Speed.Maximum\":" << data->all_variables[102];
    json << ",\"Performance.Speed.MinimumFlapRetraction\":" << data->all_variables[103];
    json << ",\"Performance.Speed.MaximumFlapExtension\":" << data->all_variables[104];
    
    // === CONFIGURATION (105-106) ===
    json << ",\"Configuration.SelectedTakeOffFlaps\":" << data->all_variables[105];
    json << ",\"Configuration.SelectedLandingFlaps\":" << data->all_variables[106];
    
    // === FMS (107) - String handled separately ===
    // FlightManagementSystem.FlightNumber (107) is String
    
    // === NAVIGATION (108-141) ===
    json << ",\"Navigation.SelectedCourse1\":" << data->all_variables[108];
    json << ",\"Navigation.SelectedCourse2\":" << data->all_variables[109];
    // Navigation.NAV1Identifier (110) is String - handled separately
    json << ",\"Navigation.NAV1Frequency\":" << data->all_variables[111];
    json << ",\"Navigation.NAV1StandbyFrequency\":" << data->all_variables[112];
    json << ",\"Navigation.NAV1FrequencySwap\":" << data->all_variables[113];
    // Navigation.NAV2Identifier (114) is String - handled separately
    json << ",\"Navigation.NAV2Frequency\":" << data->all_variables[115];
    json << ",\"Navigation.NAV2StandbyFrequency\":" << data->all_variables[116];
    json << ",\"Navigation.NAV2FrequencySwap\":" << data->all_variables[117];
    json << ",\"Navigation.DME1Frequency\":" << data->all_variables[118];
    json << ",\"Navigation.DME1Distance\":" << data->all_variables[119];
    json << ",\"Navigation.DME1Time\":" << data->all_variables[120];
    json << ",\"Navigation.DME1Speed\":" << data->all_variables[121];
    json << ",\"Navigation.DME2Frequency\":" << data->all_variables[122];
    json << ",\"Navigation.DME2Distance\":" << data->all_variables[123];
    json << ",\"Navigation.DME2Time\":" << data->all_variables[124];
    json << ",\"Navigation.DME2Speed\":" << data->all_variables[125];
    // Navigation.ILS1Identifier (126) is String - handled separately
    json << ",\"Navigation.ILS1Course\":" << data->all_variables[127];
    json << ",\"Navigation.ILS1Frequency\":" << data->all_variables[128];
    json << ",\"Navigation.ILS1StandbyFrequency\":" << data->all_variables[129];
    json << ",\"Navigation.ILS1FrequencySwap\":" << data->all_variables[130];
    // Navigation.ILS2Identifier (131) is String - handled separately
    json << ",\"Navigation.ILS2Course\":" << data->all_variables[132];
    json << ",\"Navigation.ILS2Frequency\":" << data->all_variables[133];
    json << ",\"Navigation.ILS2StandbyFrequency\":" << data->all_variables[134];
    json << ",\"Navigation.ILS2FrequencySwap\":" << data->all_variables[135];
    json << ",\"Navigation.ADF1Frequency\":" << data->all_variables[136];
    json << ",\"Navigation.ADF1StandbyFrequency\":" << data->all_variables[137];
    json << ",\"Navigation.ADF1FrequencySwap\":" << data->all_variables[138];
    json << ",\"Navigation.ADF2Frequency\":" << data->all_variables[139];
    json << ",\"Navigation.ADF2StandbyFrequency\":" << data->all_variables[140];
    json << ",\"Navigation.ADF2FrequencySwap\":" << data->all_variables[141];
    
    // === COMMUNICATION (142-151) ===
    json << ",\"Communication.COM1Frequency\":" << data->all_variables[142];
    json << ",\"Communication.COM1StandbyFrequency\":" << data->all_variables[143];
    json << ",\"Communication.COM1FrequencySwap\":" << data->all_variables[144];
    json << ",\"Communication.COM2Frequency\":" << data->all_variables[145];
    json << ",\"Communication.COM2StandbyFrequency\":" << data->all_variables[146];
    json << ",\"Communication.COM2FrequencySwap\":" << data->all_variables[147];
    json << ",\"Communication.TransponderCode\":" << data->all_variables[148];
    json << ",\"Communication.TransponderCursor\":" << data->all_variables[149];
    json << ",\"Communication.TransponderIdent\":" << data->all_variables[150];
    json << ",\"Communication.TransponderAltitude\":" << data->all_variables[151];
    
    // === AUTOPILOT (152-184) ===
    json << ",\"Autopilot.Master\":" << data->all_variables[152];
    json << ",\"Autopilot.Engaged\":" << data->all_variables[153];
    json << ",\"Autopilot.Disengage\":" << data->all_variables[154];
    json << ",\"Autopilot.Heading\":" << data->all_variables[155];
    json << ",\"Autopilot.VerticalSpeed\":" << data->all_variables[156];
    json << ",\"Autopilot.Altitude\":" << data->all_variables[157];
    json << ",\"Autopilot.Airspeed\":" << data->all_variables[158];
    json << ",\"Autopilot.Approach\":" << data->all_variables[159];
    json << ",\"Autopilot.Navigation\":" << data->all_variables[160];
    json << ",\"Autopilot.SelectedAirspeed\":" << data->all_variables[161];
    json << ",\"Autopilot.SelectedHeading\":" << data->all_variables[162];
    json << ",\"Autopilot.SelectedAltitude\":" << data->all_variables[163];
    json << ",\"Autopilot.SelectedVerticalSpeed\":" << data->all_variables[164];
    json << ",\"Autopilot.SelectedAltitudeScale\":" << data->all_variables[165];
    json << ",\"Autopilot.UseMachNumber\":" << data->all_variables[166];
    json << ",\"Autopilot.SpeedManaged\":" << data->all_variables[167];
    json << ",\"Autopilot.TargetAirspeed\":" << data->all_variables[168];
    json << ",\"Autopilot.ThrottleEngaged\":" << data->all_variables[169];
    // === CONTROLS SECTION - FLIGHT CONTROLS (185-230) ===
    json << ",\"Controls.Yoke\":" << data->all_variables[185];
    json << ",\"Controls.Rudder\":" << data->all_variables[186];
    json << ",\"Controls.Collective\":" << data->all_variables[187];
    json << ",\"Controls.Throttle1\":" << data->all_variables[188];
    json << ",\"Controls.Throttle2\":" << data->all_variables[189];
    json << ",\"Controls.Throttle3\":" << data->all_variables[190];
    json << ",\"Controls.Throttle4\":" << data->all_variables[191];
    json << ",\"Controls.Propeller1\":" << data->all_variables[192];
    json << ",\"Controls.Propeller2\":" << data->all_variables[193];
    json << ",\"Controls.Propeller3\":" << data->all_variables[194];
    json << ",\"Controls.Propeller4\":" << data->all_variables[195];
    json << ",\"Controls.Mixture1\":" << data->all_variables[196];
    json << ",\"Controls.Mixture2\":" << data->all_variables[197];
    json << ",\"Controls.Mixture3\":" << data->all_variables[198];
    json << ",\"Controls.Mixture4\":" << data->all_variables[199];
    json << ",\"Controls.CowlFlaps1\":" << data->all_variables[200];
    json << ",\"Controls.CowlFlaps2\":" << data->all_variables[201];
    json << ",\"Controls.CowlFlaps3\":" << data->all_variables[202];
    json << ",\"Controls.CowlFlaps4\":" << data->all_variables[203];
    json << ",\"Controls.Carburator1\":" << data->all_variables[204];
    json << ",\"Controls.Carburator2\":" << data->all_variables[205];
    json << ",\"Controls.Carburator3\":" << data->all_variables[206];
    json << ",\"Controls.Carburator4\":" << data->all_variables[207];
    json << ",\"Controls.Magnetos1\":" << data->all_variables[208];
    json << ",\"Controls.Magnetos2\":" << data->all_variables[209];
    json << ",\"Controls.Magnetos3\":" << data->all_variables[210];
    json << ",\"Controls.Magnetos4\":" << data->all_variables[211];
    json << ",\"Controls.Starter1\":" << data->all_variables[212];
    json << ",\"Controls.Starter2\":" << data->all_variables[213];
    json << ",\"Controls.Starter3\":" << data->all_variables[214];
    json << ",\"Controls.Starter4\":" << data->all_variables[215];
    json << ",\"Controls.Fuel1\":" << data->all_variables[216];
    json << ",\"Controls.Fuel2\":" << data->all_variables[217];
    json << ",\"Controls.Fuel3\":" << data->all_variables[218];
    json << ",\"Controls.Fuel4\":" << data->all_variables[219];
    json << ",\"Controls.Ignition1\":" << data->all_variables[220];
    json << ",\"Controls.Ignition2\":" << data->all_variables[221];
    json << ",\"Controls.Ignition3\":" << data->all_variables[222];
    json << ",\"Controls.Ignition4\":" << data->all_variables[223];
    json << ",\"Controls.Master1\":" << data->all_variables[224];
    json << ",\"Controls.Master2\":" << data->all_variables[225];
    json << ",\"Controls.Master3\":" << data->all_variables[226];
    json << ",\"Controls.Master4\":" << data->all_variables[227];
    json << ",\"Controls.Flaps\":" << data->all_variables[228];
    json << ",\"Controls.Slats\":" << data->all_variables[229];
    json << ",\"Controls.AirBrake\":" << data->all_variables[230];

    // === CONTROLS SECTION - LANDING GEAR & BRAKES (231-240) ===
    json << ",\"Controls.Gear\":" << data->all_variables[231];
    json << ",\"Controls.WheelBrake\":" << data->all_variables[232];
    json << ",\"Controls.LeftBrake\":" << data->all_variables[233];
    json << ",\"Controls.RightBrake\":" << data->all_variables[234];
    json << ",\"Controls.ParkingBrake\":" << data->all_variables[235];
    json << ",\"Controls.ToeLeftBrake\":" << data->all_variables[236];
    json << ",\"Controls.ToeRightBrake\":" << data->all_variables[237];
    json << ",\"Controls.TailWheel\":" << data->all_variables[238];
    json << ",\"Controls.NoseWheelSteering\":" << data->all_variables[239];
    json << ",\"Controls.RudderPedal\":" << data->all_variables[240];

    // === CONTROLS SECTION - ELECTRICAL & LIGHTS (241-260) ===
    json << ",\"Controls.Generator1\":" << data->all_variables[241];
    json << ",\"Controls.Generator2\":" << data->all_variables[242];
    json << ",\"Controls.Generator3\":" << data->all_variables[243];
    json << ",\"Controls.Generator4\":" << data->all_variables[244];
    json << ",\"Controls.BatteryMaster\":" << data->all_variables[245];
    json << ",\"Controls.Avionics\":" << data->all_variables[246];
    json << ",\"Controls.FuelPump1\":" << data->all_variables[247];
    json << ",\"Controls.FuelPump2\":" << data->all_variables[248];
    json << ",\"Controls.FuelPump3\":" << data->all_variables[249];
    json << ",\"Controls.FuelPump4\":" << data->all_variables[250];
    json << ",\"Controls.Navigation\":" << data->all_variables[251];
    json << ",\"Controls.Strobe\":" << data->all_variables[252];
    json << ",\"Controls.Beacon\":" << data->all_variables[253];
    json << ",\"Controls.Landing\":" << data->all_variables[254];
    json << ",\"Controls.Taxi\":" << data->all_variables[255];
    json << ",\"Controls.Formation\":" << data->all_variables[256];
    json << ",\"Controls.AntiCollision\":" << data->all_variables[257];
    json << ",\"Controls.Wing\":" << data->all_variables[258];
    json << ",\"Controls.Logo\":" << data->all_variables[259];
    json << ",\"Controls.Recognition\":" << data->all_variables[260];

    // === CONTROLS SECTION - ENVIRONMENTAL SYSTEMS (261-280) ===
    json << ",\"Controls.PitotHeat1\":" << data->all_variables[261];
    json << ",\"Controls.PitotHeat2\":" << data->all_variables[262];
    json << ",\"Controls.PropellerDeIce1\":" << data->all_variables[263];
    json << ",\"Controls.PropellerDeIce2\":" << data->all_variables[264];
    json << ",\"Controls.PropellerDeIce3\":" << data->all_variables[265];
    json << ",\"Controls.PropellerDeIce4\":" << data->all_variables[266];
    json << ",\"Controls.StructuralDeIce\":" << data->all_variables[267];
    json << ",\"Controls.APUMaster\":" << data->all_variables[268];
    json << ",\"Controls.APUStart\":" << data->all_variables[269];
    json << ",\"Controls.APUGenerator\":" << data->all_variables[270];
    json << ",\"Controls.APUBleedAir\":" << data->all_variables[271];
    json << ",\"Controls.Cabin\":" << data->all_variables[272];
    json << ",\"Controls.PressureRelief\":" << data->all_variables[273];
    json << ",\"Controls.DumpValve\":" << data->all_variables[274];
    json << ",\"Controls.ExternalAir\":" << data->all_variables[275];
    json << ",\"Controls.GPUPower\":" << data->all_variables[276];
    json << ",\"Controls.GPU\":" << data->all_variables[277];
    json << ",\"Controls.ExternalPower\":" << data->all_variables[278];
    json << ",\"Controls.AuxiliaryPower\":" << data->all_variables[279];
    json << ",\"Controls.CabinAir\":" << data->all_variables[280];

    // === CONTROLS SECTION - TRIM & PILOT CONTROLS (281-300) ===
    json << ",\"Controls.PitchTrim\":" << data->all_variables[281];
    json << ",\"Controls.RudderTrim\":" << data->all_variables[282];
    json << ",\"Controls.AileronTrim\":" << data->all_variables[283];
    json << ",\"Controls.YawDamper\":" << data->all_variables[284];
    json << ",\"Controls.AutoPilot\":" << data->all_variables[285];
    json << ",\"Controls.FlightDirector\":" << data->all_variables[286];
    json << ",\"Controls.BackCourse\":" << data->all_variables[287];
    json << ",\"Controls.Localizer\":" << data->all_variables[288];
    json << ",\"Controls.GlideSlope\":" << data->all_variables[289];
    json << ",\"Controls.Marker\":" << data->all_variables[290];
    json << ",\"Controls.DME\":" << data->all_variables[291];
    json << ",\"Controls.GPS\":" << data->all_variables[292];
    json << ",\"Controls.FMS\":" << data->all_variables[293];
    json << ",\"Controls.Approach\":" << data->all_variables[294];
    json << ",\"Controls.VNAV\":" << data->all_variables[295];
    json << ",\"Controls.AutoThrottle\":" << data->all_variables[296];
    json << ",\"Controls.FADEC1\":" << data->all_variables[297];
    json << ",\"Controls.FADEC2\":" << data->all_variables[298];
    json << ",\"Controls.FADEC3\":" << data->all_variables[299];
    json << ",\"Controls.FADEC4\":" << data->all_variables[300];

    // === CONTROLS SECTION - AIRCRAFT SPECIFIC (301-330) ===
    json << ",\"Controls.Hook\":" << data->all_variables[301];
    json << ",\"Controls.Arrestor\":" << data->all_variables[302];
    json << ",\"Controls.Catapult\":" << data->all_variables[303];
    json << ",\"Controls.LaunchBar\":" << data->all_variables[304];
    json << ",\"Controls.Wingfold\":" << data->all_variables[305];
    json << ",\"Controls.Canopy\":" << data->all_variables[306];
    json << ",\"Controls.Ejection\":" << data->all_variables[307];
    json << ",\"Controls.FireExtinguisher1\":" << data->all_variables[308];
    json << ",\"Controls.FireExtinguisher2\":" << data->all_variables[309];
    json << ",\"Controls.FireExtinguisher3\":" << data->all_variables[310];
    json << ",\"Controls.FireExtinguisher4\":" << data->all_variables[311];
    json << ",\"Controls.EmergencyExit\":" << data->all_variables[312];
    json << ",\"Controls.Evacuation\":" << data->all_variables[313];
    json << ",\"Controls.LifeRaft\":" << data->all_variables[314];
    json << ",\"Controls.Passenger\":" << data->all_variables[315];
    json << ",\"Controls.Cargo\":" << data->all_variables[316];
    json << ",\"Controls.Door1\":" << data->all_variables[317];
    json << ",\"Controls.Door2\":" << data->all_variables[318];
    json << ",\"Controls.Door3\":" << data->all_variables[319];
    json << ",\"Controls.Door4\":" << data->all_variables[320];
    json << ",\"Controls.Window1\":" << data->all_variables[321];
    json << ",\"Controls.Window2\":" << data->all_variables[322];
    json << ",\"Controls.Window3\":" << data->all_variables[323];
    json << ",\"Controls.Window4\":" << data->all_variables[324];
    json << ",\"Controls.Emergency\":" << data->all_variables[325];
    json << ",\"Controls.Auxiliary\":" << data->all_variables[326];
    json << ",\"Controls.Reserve\":" << data->all_variables[327];
    json << ",\"Controls.Backup\":" << data->all_variables[328];
    json << ",\"Controls.Override\":" << data->all_variables[329];
    json << ",\"Controls.Manual\":" << data->all_variables[330];

    // === CONTROLS SECTION - INTERNAL/RESERVED (331-339) ===
    json << ",\"Controls.Speed\":" << data->all_variables[331];
    json << ",\"FlightManagementSystem.Data0\":" << data->all_variables[332];
    json << ",\"FlightManagementSystem.Data1\":" << data->all_variables[333];
    json << ",\"Navigation.NAV1Data\":" << data->all_variables[334];
    json << ",\"Navigation.NAV2Data\":" << data->all_variables[335];
    json << ",\"Navigation.NAV3Data\":" << data->all_variables[336];
    json << ",\"Navigation.ILS1Data\":" << data->all_variables[337];
    json << ",\"Navigation.ILS2Data\":" << data->all_variables[338];
    
    // === CESSNA 172 SPECIFIC CONTROLS (340-357) ===
    json << ",\"Controls.FuelSelector\":" << data->all_variables[340];
    json << ",\"Controls.FuelShutOff\":" << data->all_variables[341];
    json << ",\"Controls.HideYoke.Left\":" << data->all_variables[342];
    json << ",\"Controls.HideYoke.Right\":" << data->all_variables[343];
    json << ",\"Controls.LeftSunBlocker\":" << data->all_variables[344];
    json << ",\"Controls.RightSunBlocker\":" << data->all_variables[345];
    json << ",\"Controls.Lighting.LeftCabinOverheadLight\":" << data->all_variables[346];
    json << ",\"Controls.Lighting.RightCabinOverheadLight\":" << data->all_variables[347];
    json << ",\"Controls.Magnetos1\":" << data->all_variables[348];
    json << ",\"Controls.ParkingBrake\":" << data->all_variables[349];
    json << ",\"Controls.Trim\":" << data->all_variables[350];
    json << ",\"LeftYoke.Button\":" << data->all_variables[351];
    json << ",\"Doors.Left\":" << data->all_variables[352];
    json << ",\"Doors.LeftHandle\":" << data->all_variables[353];
    json << ",\"Doors.Right\":" << data->all_variables[354];
    json << ",\"Doors.RightHandle\":" << data->all_variables[355];
    json << ",\"Windows.Left\":" << data->all_variables[356];
    json << ",\"Windows.Right\":" << data->all_variables[357];
    
    // === STRING VARIABLES ===
    json << ",\"Aircraft.Name\":\"" << data->aircraft_name << "\"";
    json << ",\"Aircraft.NearestAirportIdentifier\":\"" << data->aircraft_nearest_airport_id << "\"";
    json << ",\"Aircraft.NearestAirportName\":\"" << data->aircraft_nearest_airport_name << "\"";
    json << ",\"Aircraft.BestAirportIdentifier\":\"" << data->aircraft_best_airport_id << "\"";
    json << ",\"Aircraft.BestAirportName\":\"" << data->aircraft_best_airport_name << "\"";
    json << ",\"Aircraft.BestRunwayIdentifier\":\"" << data->aircraft_best_runway_id << "\"";
    json << ",\"Navigation.NAV1Identifier\":\"" << data->navigation_nav1_identifier << "\"";
    json << ",\"Navigation.NAV2Identifier\":\"" << data->navigation_nav2_identifier << "\"";
    json << ",\"Navigation.ILS1Identifier\":\"" << data->navigation_ils1_identifier << "\"";
    json << ",\"Navigation.ILS2Identifier\":\"" << data->navigation_ils2_identifier << "\"";
    json << ",\"Autopilot.Type\":\"" << data->autopilot_type << "\"";
    json << ",\"Autopilot.ActiveLateralMode\":\"" << data->autopilot_active_lateral_mode << "\"";
    json << ",\"Autopilot.ArmedLateralMode\":\"" << data->autopilot_armed_lateral_mode << "\"";
    json << ",\"Autopilot.ActiveVerticalMode\":\"" << data->autopilot_active_vertical_mode << "\"";
    json << ",\"Autopilot.ArmedVerticalMode\":\"" << data->autopilot_armed_vertical_mode << "\"";
    json << ",\"Autopilot.ArmedApproachMode\":\"" << data->autopilot_armed_approach_mode << "\"";
    json << ",\"Autopilot.ActiveAutoThrottleMode\":\"" << data->autopilot_active_autothrottle_mode << "\"";
    json << ",\"Autopilot.ActiveCollectiveMode\":\"" << data->autopilot_active_collective_mode << "\"";
    json << ",\"Autopilot.ArmedCollectiveMode\":\"" << data->autopilot_armed_collective_mode << "\"";
    json << ",\"FlightManagementSystem.FlightNumber\":\"" << data->fms_flight_number << "\"";
    
    // === VECTOR VARIABLES (expanded to X, Y, Z components) ===
    json << ",\"Aircraft.Velocity.X\":" << data->aircraft_velocity.x;
    json << ",\"Aircraft.Velocity.Y\":" << data->aircraft_velocity.y;
    json << ",\"Aircraft.Velocity.Z\":" << data->aircraft_velocity.z;
    json << ",\"Aircraft.AngularVelocity.X\":" << data->aircraft_angular_velocity.x;
    json << ",\"Aircraft.AngularVelocity.Y\":" << data->aircraft_angular_velocity.y;
    json << ",\"Aircraft.AngularVelocity.Z\":" << data->aircraft_angular_velocity.z;
    json << ",\"Aircraft.Acceleration.X\":" << data->aircraft_acceleration.x;
    json << ",\"Aircraft.Acceleration.Y\":" << data->aircraft_acceleration.y;
    json << ",\"Aircraft.Acceleration.Z\":" << data->aircraft_acceleration.z;
    json << ",\"Aircraft.Wind.X\":" << data->aircraft_wind.x;
    json << ",\"Aircraft.Wind.Y\":" << data->aircraft_wind.y;
    json << ",\"Aircraft.Wind.Z\":" << data->aircraft_wind.z;
    json << ",\"Aircraft.NearestAirportLocation.X\":" << data->aircraft_nearest_airport_location.x;
    json << ",\"Aircraft.NearestAirportLocation.Y\":" << data->aircraft_nearest_airport_location.y;
    json << ",\"Aircraft.BestAirportLocation.X\":" << data->aircraft_best_airport_location.x;
    json << ",\"Aircraft.BestAirportLocation.Y\":" << data->aircraft_best_airport_location.y;
    json << ",\"Aircraft.BestRunwayThreshold.X\":" << data->aircraft_best_runway_threshold.x;
    json << ",\"Aircraft.BestRunwayThreshold.Y\":" << data->aircraft_best_runway_threshold.y;
    json << ",\"Aircraft.BestRunwayThreshold.Z\":" << data->aircraft_best_runway_threshold.z;
    json << ",\"Aircraft.BestRunwayEnd.X\":" << data->aircraft_best_runway_end.x;
    json << ",\"Aircraft.BestRunwayEnd.Y\":" << data->aircraft_best_runway_end.y;
    json << ",\"Aircraft.BestRunwayEnd.Z\":" << data->aircraft_best_runway_end.z;
    
    json << "}"; // Close variables section
    json << "}\n"; // Close main JSON
    
    return json.str();
}
    
///////////////////////////////////////////////////////////////////////////////////////////////////
// WEBSOCKET SERVER (SIMPLE, NO EXTERNAL DEPENDENCIES)
///////////////////////////////////////////////////////////////////////////////////////////////////

// Minimal SHA-1 and Base64 helpers for WebSocket handshake
namespace ws_util {
    struct Sha1Context {
        uint32_t state[5];
        uint64_t count; // bits
        uint8_t buffer[64];
    };

    static inline uint32_t rol(uint32_t value, uint32_t bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
        uint32_t a, b, c, d, e, t, W[80];
        for (int i = 0; i < 16; ++i) {
            W[i] = (buffer[i*4+0] << 24) | (buffer[i*4+1] << 16) | (buffer[i*4+2] << 8) | (buffer[i*4+3]);
        }
        for (int i = 16; i < 80; ++i) {
            W[i] = rol(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);
        }
        a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            t = rol(a,5) + f + e + k + W[i];
            e = d; d = c; c = rol(b,30); b = a; a = t;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
    }

    static void sha1_init(Sha1Context &ctx) {
        ctx.state[0] = 0x67452301;
        ctx.state[1] = 0xEFCDAB89;
        ctx.state[2] = 0x98BADCFE;
        ctx.state[3] = 0x10325476;
        ctx.state[4] = 0xC3D2E1F0;
        ctx.count = 0;
    }

    static void sha1_update(Sha1Context &ctx, const uint8_t* data, size_t len) {
        size_t i = 0;
        size_t j = (ctx.count >> 3) % 64;
        ctx.count += (uint64_t)len << 3;
        size_t part_len = 64 - j;
        if (len >= part_len) {
            memcpy(&ctx.buffer[j], &data[0], part_len);
            sha1_transform(ctx.state, ctx.buffer);
            for (i = part_len; i + 63 < len; i += 64) {
                sha1_transform(ctx.state, &data[i]);
            }
            j = 0;
        } else {
            i = 0;
        }
        memcpy(&ctx.buffer[j], &data[i], len - i);
    }

    static void sha1_final(Sha1Context &ctx, uint8_t digest[20]) {
        uint8_t finalcount[8];
        for (int i = 0; i < 8; ++i) {
            finalcount[i] = (uint8_t)((ctx.count >> ((7 - i) * 8)) & 0xFF);
        }
        uint8_t c = 0x80;
        sha1_update(ctx, &c, 1);
        uint8_t zero = 0x00;
        while ((ctx.count & 0x1FF) != (448)) { // mod 512 bits -> 56 bytes
            sha1_update(ctx, &zero, 1);
        }
        sha1_update(ctx, finalcount, 8);
        for (int i = 0; i < 20; ++i) {
            digest[i] = (uint8_t)((ctx.state[i>>2] >> ((3 - (i & 3)) * 8)) & 0xFF);
        }
    }

    static std::string base64_encode(const uint8_t* data, size_t len) {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t n = (data[i] << 16) | ((i+1 < len ? data[i+1] : 0) << 8) | (i+2 < len ? data[i+2] : 0);
            out.push_back(tbl[(n >> 18) & 63]);
            out.push_back(tbl[(n >> 12) & 63]);
            out.push_back(i+1 < len ? tbl[(n >> 6) & 63] : '=');
            out.push_back(i+2 < len ? tbl[n & 63] : '=');
        }
        return out;
    }
}

/**
 * WebSocketServerInterface - Native WebSocket server implementation
 *
 * Implements a minimal WebSocket protocol over Winsock without external
 * dependencies. Runs an internal thread using select() for non-blocking I/O,
 * broadcasts telemetry frames to connected clients, and queues incoming JSON
 * commands in a thread-safe manner.
 */
class WebSocketServerInterface {
private:
    std::thread server_thread;
    SOCKET server_socket = INVALID_SOCKET;
    std::vector<SOCKET> client_sockets;
    std::unordered_map<SOCKET, std::string> recv_buffers;
    mutable std::mutex clients_mutex;
    std::atomic<bool> running{false};
    uint64_t last_broadcast_us = 0;

    // Command queue (same as TCP)
    std::queue<std::string> command_queue;
    mutable std::mutex command_mutex;

public:
    ~WebSocketServerInterface() { Stop(); }

    /**
     * Starts the WebSocket server on the specified port. Returns false if the
     * socket cannot be created/bound/listened. Graceful fallback is expected.
     */
    bool Start(uint16_t port = 8765) {
        if (running) return true;
        // Create server socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            return false;
        }
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        sockaddr_in addr; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
        if (bind(server_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(server_socket); server_socket = INVALID_SOCKET; return false;
        }
        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server_socket); server_socket = INVALID_SOCKET; return false;
        }
        running = true;
        server_thread = std::thread(&WebSocketServerInterface::ServerLoop, this);
        return true;
    }

    /**
     * Stops the WebSocket server, joins thread, closes all sockets, and clears
     * internal buffers and pending commands.
     */
    void Stop() {
        if (!running) return;
        running = false;
        // Wake select by closing server socket
        if (server_socket != INVALID_SOCKET) {
            closesocket(server_socket);
            server_socket = INVALID_SOCKET;
        }
        if (server_thread.joinable()) server_thread.join();

        std::lock_guard<std::mutex> lock(clients_mutex);
        for (SOCKET s : client_sockets) {
            closesocket(s);
        }
        client_sockets.clear();
        recv_buffers.clear();
        // Clear commands
        {
            std::lock_guard<std::mutex> ql(command_mutex);
            while (!command_queue.empty()) command_queue.pop();
        }
    }

    /**
     * Broadcasts telemetry to all connected WebSocket clients.
     * Reuses the same JSON payload as TCP for 1:1 compatibility.
     */
    void BroadcastData(const AeroflyBridgeData* data) {
        if (!running || !data) return;

        const uint64_t now_us = get_time_us();
        // Throttle: read interval env AEROFLY_BRIDGE_BROADCAST_MS (default 20ms)
        auto get_interval_ms = []() -> uint32_t {
            static uint32_t cached = []{
                char buf[32] = {0};
                DWORD n = GetEnvironmentVariableA("AEROFLY_BRIDGE_BROADCAST_MS", buf, sizeof(buf));
                int ms = (n > 0) ? atoi(buf) : 20; if (ms < 5) ms = 5; return (uint32_t)ms;
            }();
            return cached;
        }();
        if (now_us - last_broadcast_us < (uint64_t)get_interval_ms * 1000ULL) return;
        last_broadcast_us = now_us;

        const std::string json = CreateDataJSONForWS(data);
        const std::string frame = CreateWebSocketFrame(json);
        if (frame.empty()) return;

        std::vector<SOCKET> snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            snapshot = client_sockets;
        }
        std::vector<SOCKET> to_remove;
        for (SOCKET s : snapshot) {
            int n = send(s, frame.data(), (int)frame.size(), 0);
            if (n == SOCKET_ERROR) {
                to_remove.push_back(s);
            }
        }
        if (!to_remove.empty()) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (SOCKET dead : to_remove) {
                closesocket(dead);
                auto it = std::find(client_sockets.begin(), client_sockets.end(), dead);
                if (it != client_sockets.end()) client_sockets.erase(it);
                recv_buffers.erase(dead);
            }
        }
    }

    /**
     * Retrieves and clears pending JSON commands received from clients.
     */
    std::vector<std::string> GetPendingCommands() {
        std::vector<std::string> out;
        std::lock_guard<std::mutex> lock(command_mutex);
        while (!command_queue.empty()) { out.push_back(command_queue.front()); command_queue.pop(); }
        return out;
    }

    /** Returns number of currently connected WebSocket clients. */
    int GetClientCount() const {
        std::lock_guard<std::mutex> lock(clients_mutex);
        return (int)client_sockets.size();
    }

private:
    // Simple JSON builder reusing the same fields as TCP JSON
    // Use shared JSON builder for exact parity with TCP
    static std::string CreateDataJSONForWS(const AeroflyBridgeData* data) {
        return BuildDataJSON(data);
    }

    /**
     * Encodes a server-to-client WebSocket frame (unmasked) as per RFC 6455.
     * Only text (0x1) and pong (0xA) opcodes are used here.
     */
    static std::string CreateWebSocketFrame(const std::string& payload, uint8_t opcode = 0x1, bool fin = true) {
        const size_t len = payload.size();
        if (len > 1ULL << 31) return std::string(); // guard
        std::string frame;
        frame.reserve(2 + 8 + len);
        uint8_t b0 = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
        frame.push_back((char)b0); // FIN/Opcode
        if (len <= 125) {
            frame.push_back((char)len);
        } else if (len <= 0xFFFF) {
            frame.push_back((char)126);
            frame.push_back((char)((len >> 8) & 0xFF));
            frame.push_back((char)(len & 0xFF));
        } else {
            frame.push_back((char)127);
            uint64_t l = (uint64_t)len;
            for (int i = 7; i >= 0; --i) frame.push_back((char)((l >> (i*8)) & 0xFF));
        }
        frame.append(payload);
        return frame;
    }

    /**
     * Handles HTTP Upgrade -> WebSocket handshake.
     * - Reads request headers
     * - Validates Upgrade/Connection headers (case-insensitive)
     * - Computes Sec-WebSocket-Accept = Base64(SHA1(key + GUID))
     * - Sends 101 Switching Protocols response
     */
    bool HandleWebSocketHandshake(SOCKET client) {
        // Try read HTTP headers up to CRLF CRLF
        std::string request;
        char buf[2048];
        uint64_t start = get_time_us();
        while (request.find("\r\n\r\n") == std::string::npos) {
            int n = recv(client, buf, (int)sizeof(buf), 0);
            if (n > 0) { request.append(buf, buf + n); if (request.size() > 8192) break; }
            else {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                    // small wait
                    Sleep(5);
                } else {
                    return false;
                }
            }
            if (get_time_us() - start > 2'000'000ULL) { // 2s
                return false;
            }
        }
        // Case-insensitive header parsing
        std::string lower = request;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        if (lower.find("upgrade: websocket") == std::string::npos) return false;
        if (lower.find("connection: upgrade") == std::string::npos) return false;
        // Extract Sec-WebSocket-Key (case-insensitive)
        std::string key;
        {
            const std::string h = "sec-websocket-key:";
            size_t p = lower.find(h);
            if (p == std::string::npos) return false;
            p += h.size();
            while (p < lower.size() && (lower[p] == ' ' || lower[p] == '\t')) ++p;
            size_t end = lower.find("\r\n", p);
            if (end == std::string::npos) return false;
            key = request.substr(p, end - p);
            while (!key.empty() && (key.back()=='\r' || key.back()=='\n' || key.back()==' ' || key.back()=='\t')) key.pop_back();
        }
        // Compute accept = base64( SHA1(key + GUID) )
        static const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string src = key + GUID;
        ws_util::Sha1Context ctx; ws_util::sha1_init(ctx);
        ws_util::sha1_update(ctx, reinterpret_cast<const uint8_t*>(src.data()), src.size());
        uint8_t digest[20]; ws_util::sha1_final(ctx, digest);
        std::string accept = ws_util::base64_encode(digest, 20);

        std::ostringstream resp;
        resp << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
        std::string r = resp.str();
        int sent = send(client, r.c_str(), (int)r.size(), 0);
        return sent != SOCKET_ERROR;
    }

    /**
     * Parses and processes frames from a single client.
     * - Supports masked client frames (required by RFC)
     * - Accepts text frames (JSON commands)
     * - Replies to pings with pong
     * - Closes connections on close opcode
     */
    void ProcessWebSocketFrames(SOCKET client) {
        auto it = recv_buffers.find(client);
        if (it == recv_buffers.end()) return;
        std::string &buf = it->second;
        for (;;) {
            if (buf.size() < 2) return;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(buf.data());
            uint8_t b0 = p[0];
            uint8_t b1 = p[1];
            bool fin = (b0 & 0x80) != 0;
            uint8_t opcode = b0 & 0x0F;
            bool masked = (b1 & 0x80) != 0;
            uint64_t len = (b1 & 0x7F);
            size_t pos = 2;
            if (len == 126) {
                if (buf.size() < pos + 2) return; len = (p[pos] << 8) | p[pos+1]; pos += 2;
            } else if (len == 127) {
                if (buf.size() < pos + 8) return; len = 0; for (int i = 0; i < 8; ++i) { len = (len << 8) | p[pos+i]; } pos += 8;
            }
            uint8_t mask[4] = {0,0,0,0};
            if (masked) {
                if (buf.size() < pos + 4) return; mask[0]=p[pos]; mask[1]=p[pos+1]; mask[2]=p[pos+2]; mask[3]=p[pos+3]; pos += 4;
            }
            if (buf.size() < pos + len) return; // incomplete frame
            std::string payload;
            payload.resize((size_t)len);
            for (size_t i = 0; i < (size_t)len; ++i) {
                uint8_t c = p[pos + i];
                if (masked) c ^= mask[i % 4];
                payload[i] = (char)c;
            }
            // Advance buffer
            buf.erase(0, pos + (size_t)len);

            if (opcode == 0x8) { // close
                closesocket(client);
        std::lock_guard<std::mutex> lock(clients_mutex);
                auto itc = std::find(client_sockets.begin(), client_sockets.end(), client);
                if (itc != client_sockets.end()) client_sockets.erase(itc);
                recv_buffers.erase(client);
                return;
            } else if (opcode == 0x1) { // text
                // Push JSON command
                std::lock_guard<std::mutex> ql(command_mutex);
                command_queue.push(payload);
            } else if (opcode == 0x9) { // ping -> pong
                const std::string pong = CreateWebSocketFrame(payload, 0xA);
                int _ = send(client, pong.data(), (int)pong.size(), 0); (void)_; // ignore errors
            }

            if (!fin) {
                // For simplicity ignoring continuation; most clients send small single-frame text
            }
        }
    }

    /** Main server loop using select() for non-blocking accept and reads. */
    void ServerLoop() {
        // Non-blocking accept and client IO
        u_long mode = 1;
        ioctlsocket(server_socket, FIONBIO, &mode);
        while (running) {
            fd_set readfds;
            FD_ZERO(&readfds);
            if (server_socket != INVALID_SOCKET) FD_SET(server_socket, &readfds);
            std::vector<SOCKET> snapshot;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                snapshot = client_sockets;
            }
            for (SOCKET s : snapshot) {
                FD_SET(s, &readfds);
            }
            timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000; // 200ms
            int maxfd = 0; // ignored on Windows
            int res = select(0, &readfds, NULL, NULL, &tv);
            if (res == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err != WSAEINTR && running) {
                    break;
                }
            }
            if (!running) break;
            // Accept new
            if (server_socket != INVALID_SOCKET && FD_ISSET(server_socket, &readfds)) {
                SOCKET client = accept(server_socket, nullptr, nullptr);
                if (client != INVALID_SOCKET) {
                    // Set non-blocking and low latency
                    u_long nb = 1; ioctlsocket(client, FIONBIO, &nb);
                    int yes = 1; setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
                    setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, (char*)&yes, sizeof(yes));
                    // Perform handshake synchronously
                    if (HandleWebSocketHandshake(client)) {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        client_sockets.push_back(client);
                        recv_buffers[client] = std::string();
                        DBG("WebSocket client connected\n");
                    } else {
                        closesocket(client);
                    }
                }
            }
            // Read from clients
            for (SOCKET s : snapshot) {
                if (!FD_ISSET(s, &readfds)) continue;
                char buf[4096];
                int n = recv(s, buf, sizeof(buf), 0);
                if (n > 0) {
                    recv_buffers[s].append(buf, buf + n);
                    ProcessWebSocketFrames(s);
                } else if (n == 0) {
                    // closed
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    closesocket(s);
                    auto it = std::find(client_sockets.begin(), client_sockets.end(), s);
                    if (it != client_sockets.end()) client_sockets.erase(it);
                    recv_buffers.erase(s);
                } else {
                    int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK && err != WSAEINTR) {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        closesocket(s);
                        auto it = std::find(client_sockets.begin(), client_sockets.end(), s);
                        if (it != client_sockets.end()) client_sockets.erase(it);
                        recv_buffers.erase(s);
                    }
                }
            }
        }
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// COMMAND PROCESSOR
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Processes and converts commands between JSON and Aerofly message formats.
 * 
 * This class handles:
 * - Parsing JSON commands from network clients
 * - Converting commands to Aerofly's message format
 * - Routing commands to appropriate subsystems
 * - Variable name resolution and validation
 * 
 * The processor supports all aircraft systems including:
 * - Basic flight controls
 * - Navigation systems
 * - Engine management
 * - Aircraft-specific features (e.g., C172)
 */
class CommandProcessor {
private:
    VariableMapper mapper;    ///< Maps variable names to internal indices
    
    // Hash map infrastructure for optimized command processing
    using CommandHandler = std::function<tm_external_message(double)>;
    std::unordered_map<std::string, CommandHandler> command_handlers;
    
public:
    /**
     * @brief Constructor - initializes command handlers hash map
     */
    CommandProcessor() {
        InitializeHandlers();
    }
    
    /**
     * @brief Initialize command handlers hash map for optimized processing
     * 
     * This method populates the command_handlers map with lambda functions
     * for each supported command variable, replacing the long if-else chain
     * with O(1) hash table lookup.
     */
    void InitializeHandlers() {
        // === BASIC FLIGHT CONTROLS (Examples) ===
        command_handlers["Controls.Pitch.Input"] = [](double value) {
            auto msg = MessageControlsPitchInput;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Roll.Input"] = [](double value) {
            auto msg = MessageControlsRollInput;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Yaw.Input"] = [](double value) {
            auto msg = MessageControlsYawInput;
            msg.SetValue(value);
            return msg;
        };
        
        // === THROTTLE CONTROLS (Examples) ===
        command_handlers["Controls.Throttle"] = [](double value) {
            auto msg = MessageControlsThrottle;
            msg.SetValue(value);
            return msg;
        };
        
        // === AIRCRAFT SYSTEMS (Examples) ===
        command_handlers["Controls.Gear"] = [](double value) {
            auto msg = MessageControlsGear;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Flaps"] = [](double value) {
            auto msg = MessageControlsFlaps;
            msg.SetValue(value);
            return msg;
        };
        
        // === MORE CONTROLS VARIABLES (PASO 2) ===
        command_handlers["Controls.Throttle1"] = [](double value) {
            auto msg = MessageControlsThrottle1;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Throttle2"] = [](double value) {
            auto msg = MessageControlsThrottle2;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Throttle3"] = [](double value) {
            auto msg = MessageControlsThrottle3;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Throttle4"] = [](double value) {
            auto msg = MessageControlsThrottle4;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.AirBrake"] = [](double value) {
            auto msg = MessageControlsAirBrake;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.WheelBrake.Left"] = [](double value) {
            auto msg = MessageControlsWheelBrakeLeft;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.WheelBrake.Right"] = [](double value) {
            auto msg = MessageControlsWheelBrakeRight;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Controls.Collective"] = [](double value) {
            auto msg = MessageControlsCollective;
            msg.SetValue(value);
            return msg;
        };
        
        // NOTE: Controls.Rudder, Controls.Slats, Controls.GroundSpoilers 
        // messages are not defined in the current codebase - skipping for now
        
        // === NAVIGATION VARIABLES (Examples) ===
        command_handlers["Communication.COM1Frequency"] = [](double value) {
            auto msg = MessageNavigationCOM1Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM1StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationCOM1StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        // === AUTOPILOT VARIABLES (Examples) ===
        command_handlers["Autopilot.Master"] = [](double value) {
            auto msg = MessageAutopilotMaster;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.Heading"] = [](double value) {
            auto msg = MessageAutopilotHeading;
            msg.SetValue(value);
            return msg;
        };
        
        // === ENGINE VARIABLES (Examples) ===
        command_handlers["Aircraft.EngineMaster1"] = [](double value) {
            auto msg = MessageAircraftEngineMaster1;
            msg.SetValue(value);
            return msg;
        };
        
        // === NAVIGATION VARIABLES (PASO 3 - MASIVO) ===
        // VOR Navigation Systems
        command_handlers["Navigation.SelectedCourse1"] = [](double value) {
            auto msg = MessageNavigationSelectedCourse1;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.SelectedCourse2"] = [](double value) {
            auto msg = MessageNavigationSelectedCourse2;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.NAV1Frequency"] = [](double value) {
            auto msg = MessageNavigationNAV1Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.NAV1StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationNAV1StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.NAV1FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationNAV1FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.NAV2Frequency"] = [](double value) {
            auto msg = MessageNavigationNAV2Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.NAV2StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationNAV2StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.NAV2FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationNAV2FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        // DME Equipment
        command_handlers["Navigation.DME1Frequency"] = [](double value) {
            auto msg = MessageNavigationDME1Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME1Distance"] = [](double value) {
            auto msg = MessageNavigationDME1Distance;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME1Time"] = [](double value) {
            auto msg = MessageNavigationDME1Time;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME1Speed"] = [](double value) {
            auto msg = MessageNavigationDME1Speed;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME2Frequency"] = [](double value) {
            auto msg = MessageNavigationDME2Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME2Distance"] = [](double value) {
            auto msg = MessageNavigationDME2Distance;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME2Time"] = [](double value) {
            auto msg = MessageNavigationDME2Time;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.DME2Speed"] = [](double value) {
            auto msg = MessageNavigationDME2Speed;
            msg.SetValue(value);
            return msg;
        };
        
        // ILS Systems
        command_handlers["Navigation.ILS1Course"] = [](double value) {
            auto msg = MessageNavigationILS1Course;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS1Frequency"] = [](double value) {
            auto msg = MessageNavigationILS1Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS1StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationILS1StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS1FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationILS1FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS2Course"] = [](double value) {
            auto msg = MessageNavigationILS2Course;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS2Frequency"] = [](double value) {
            auto msg = MessageNavigationILS2Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS2StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationILS2StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ILS2FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationILS2FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        // ADF Equipment
        command_handlers["Navigation.ADF1Frequency"] = [](double value) {
            auto msg = MessageNavigationADF1Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ADF1StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationADF1StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ADF1FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationADF1FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ADF2Frequency"] = [](double value) {
            auto msg = MessageNavigationADF2Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ADF2StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationADF2StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Navigation.ADF2FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationADF2FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        // Navigation String Handlers (identifiers are strings - skip for now in CommandProcessor)
        // NOTE: Navigation.NAV1Identifier, NAV2Identifier, ILS1Identifier, ILS2Identifier
        // are STRING variables that require special handling in CommandProcessor.
        // These will be handled in the fallback system for now as they need string input parsing.
        
        // === COMMUNICATION VARIABLES (PASO 3 - MASIVO) ===
        // COM Radio Systems (NOTE: Some already exist as examples)
        command_handlers["Communication.COM2Frequency"] = [](double value) {
            auto msg = MessageNavigationCOM2Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM2StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationCOM2StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM1FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationCOM1FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM2FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationCOM2FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM3Frequency"] = [](double value) {
            auto msg = MessageNavigationCOM3Frequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM3StandbyFrequency"] = [](double value) {
            auto msg = MessageNavigationCOM3StandbyFrequency;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.COM3FrequencySwap"] = [](double value) {
            auto msg = MessageNavigationCOM3FrequencySwap;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.TransponderCode"] = [](double value) {
            auto msg = MessageTransponderCode;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Communication.TransponderCursor"] = [](double value) {
            auto msg = MessageTransponderCursor;
            msg.SetValue(value);
            return msg;
        };
        
        // === AIRCRAFT ENGINE VARIABLES (PASO 3 - MASIVO) ===
        // Engine Master Switches (EngineMaster1 already exists above)
        command_handlers["Aircraft.EngineMaster2"] = [](double value) {
            auto msg = MessageAircraftEngineMaster2;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineMaster3"] = [](double value) {
            auto msg = MessageAircraftEngineMaster3;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineMaster4"] = [](double value) {
            auto msg = MessageAircraftEngineMaster4;
            msg.SetValue(value);
            return msg;
        };
        
        // Engine Throttle Controls
        command_handlers["Aircraft.EngineThrottle1"] = [](double value) {
            auto msg = MessageAircraftEngineThrottle1;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineThrottle2"] = [](double value) {
            auto msg = MessageAircraftEngineThrottle2;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineThrottle3"] = [](double value) {
            auto msg = MessageAircraftEngineThrottle3;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineThrottle4"] = [](double value) {
            auto msg = MessageAircraftEngineThrottle4;
            msg.SetValue(value);
            return msg;
        };
        
        // Engine Rotation Speed (RPM)
        command_handlers["Aircraft.EngineRotationSpeed1"] = [](double value) {
            auto msg = MessageAircraftEngineRotationSpeed1;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineRotationSpeed2"] = [](double value) {
            auto msg = MessageAircraftEngineRotationSpeed2;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineRotationSpeed3"] = [](double value) {
            auto msg = MessageAircraftEngineRotationSpeed3;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineRotationSpeed4"] = [](double value) {
            auto msg = MessageAircraftEngineRotationSpeed4;
            msg.SetValue(value);
            return msg;
        };
        
        // Engine Running Status
        command_handlers["Aircraft.EngineRunning1"] = [](double value) {
            auto msg = MessageAircraftEngineRunning1;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineRunning2"] = [](double value) {
            auto msg = MessageAircraftEngineRunning2;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineRunning3"] = [](double value) {
            auto msg = MessageAircraftEngineRunning3;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Aircraft.EngineRunning4"] = [](double value) {
            auto msg = MessageAircraftEngineRunning4;
            msg.SetValue(value);
            return msg;
        };
        
        // === AUTOPILOT VARIABLES (PASO 3 - MASIVO) ===
        // Basic Autopilot Controls (Master and Heading already exist above)
        command_handlers["Autopilot.Disengage"] = [](double value) {
            auto msg = MessageAutopilotDisengage;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.VerticalSpeed"] = [](double value) {
            auto msg = MessageAutopilotVerticalSpeed;
            msg.SetValue(value);
            return msg;
        };
        
        // Selected Values
        command_handlers["Autopilot.SelectedSpeed"] = [](double value) {
            auto msg = MessageAutopilotSelectedSpeed;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.SelectedAirspeed"] = [](double value) {
            auto msg = MessageAutopilotSelectedAirspeed;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.SelectedHeading"] = [](double value) {
            auto msg = MessageAutopilotSelectedHeading;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.SelectedAltitude"] = [](double value) {
            auto msg = MessageAutopilotSelectedAltitude;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.SelectedVerticalSpeed"] = [](double value) {
            auto msg = MessageAutopilotSelectedVerticalSpeed;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.SelectedAltitudeScale"] = [](double value) {
            auto msg = MessageAutopilotSelectedAltitudeScale;
            msg.SetValue(value);
            return msg;
        };
        
        // System Configuration
        command_handlers["Autopilot.Engaged"] = [](double value) {
            auto msg = MessageAutopilotEngaged;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.UseMachNumber"] = [](double value) {
            auto msg = MessageAutopilotUseMachNumber;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.SpeedManaged"] = [](double value) {
            auto msg = MessageAutopilotSpeedManaged;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.TargetAirspeed"] = [](double value) {
            auto msg = MessageAutopilotTargetAirspeed;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.Aileron"] = [](double value) {
            auto msg = MessageAutopilotAileron;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.Elevator"] = [](double value) {
            auto msg = MessageAutopilotElevator;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.ThrottleEngaged"] = [](double value) {
            auto msg = MessageAutopilotThrottleEngaged;
            msg.SetValue(value);
            return msg;
        };
        
        command_handlers["Autopilot.ThrottleCommand"] = [](double value) {
            auto msg = MessageAutopilotThrottleCommand;
            msg.SetValue(value);
            return msg;
        };
        
        // TODO: Add remaining ~189 variables in subsequent steps
        // PASO 3 Progress: +65 Navigation+Communication+Engine+Autopilot variables migrated (Total: ~108, 4 string identifiers in fallback)
    }
    
    /**
     * @brief Processes a batch of commands from external sources.
     * 
     * This method:
     * 1. Takes JSON-formatted command strings
     * 2. Parses each command into Aerofly messages
     * 3. Validates command parameters
     * 4. Returns valid messages for processing
     * 
     * @param commands Vector of JSON command strings to process
     * @return Vector of valid Aerofly messages
     */
    /**
     * Converts a list of JSON command strings into Aerofly messages.
     * Invalid or unsupported commands are discarded gracefully.
     */
    std::vector<tm_external_message> ProcessCommands(const std::vector<std::string>& commands) {
        std::vector<tm_external_message> messages;
        
        for (const auto& command : commands) {
            auto msg = ParseCommand(command);
            if (msg.GetDataType() != tm_msg_data_type::None) {
                messages.push_back(msg);
            }
        }
        
        return messages;
    }
    
private:
    /**
     * @brief Helper functions for processing different variable categories.
     * Each function handles a specific subset of simulation variables.
     */

    /**
     * @brief Processes aircraft state and physics variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessAircraftVariables(const std::string& var_name, double value);

    /**
     * @brief Processes navigation radio and equipment variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessNavigationVariables(const std::string& var_name, double value);

    /**
     * @brief Processes autopilot and flight director variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessAutopilotVariables(const std::string& var_name, double value);

    /**
     * @brief Processes basic flight control variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessControlsVariables(const std::string& var_name, double value);

    /**
     * @brief Processes engine management variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessEngineVariables(const std::string& var_name, double value);

    /**
     * @brief Processes simulation state and environment variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessSimulationVariables(const std::string& var_name, double value);

    /**
     * @brief Processes warning and caution system variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessWarningVariables(const std::string& var_name, double value);

    /**
     * @brief Processes camera and view system variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessViewVariables(const std::string& var_name, double value);

    /**
     * @brief Processes Cessna 172 specific variables.
     * @param var_name Variable name from JSON command
     * @param value New value to set
     * @return Message for Aerofly if valid, empty message otherwise
     */
    tm_external_message ProcessC172SpecificVariables(const std::string& var_name, double value);


    /**
     * @brief Parses a JSON command string into an Aerofly message.
     * 
     * This method:
     * 1. Validates JSON format
     * 2. Extracts variable name and value
     * 3. Routes to appropriate processing function
     * 4. Handles error conditions gracefully
     * 
     * Expected JSON format:
     * @code
     * {
     *     "variable": "Controls.Throttle",
     *     "value": 0.75
     * }
     * @endcode
     * 
     * @param command JSON-formatted command string
     * @return Aerofly message if valid, empty message if parsing fails
     */
    /**
     * Parses a single JSON command with fields {"variable":"...","value":...}.
     * Returns an empty message when parsing fails or the variable is unsupported.
     */
    tm_external_message ParseCommand(const std::string& command) {
        tm_external_message empty_msg;
        
        try {
            // Debug: log received command
            DBG(("Processing command: " + command + "\n").c_str());
            
            // Find valid JSON
            size_t start = command.find('{');
            size_t end = command.rfind('}');
            
            if (start == std::string::npos || end == std::string::npos) {
                OutputDebugStringA("Error: No valid JSON found\n");
                return empty_msg;
            }
            
            std::string json_str = command.substr(start, end - start + 1);
            DBG(("Extracted JSON: " + json_str + "\n").c_str());
            
            // Manual parsing
            size_t var_pos = json_str.find("\"variable\"");
            size_t val_pos = json_str.find("\"value\"");
            
            if (var_pos == std::string::npos || val_pos == std::string::npos) {
                OutputDebugStringA("Error: variable/value fields not found\n");
                return empty_msg;
            }
            
            // Extract variable name
            size_t var_start = json_str.find(":", var_pos) + 1;
            var_start = json_str.find("\"", var_start) + 1;
            size_t var_end = json_str.find("\"", var_start);
            std::string var_name = json_str.substr(var_start, var_end - var_start);
            
            // Extract value
            size_t val_start = json_str.find(":", val_pos) + 1;
            while (val_start < json_str.length() && (json_str[val_start] == ' ' || json_str[val_start] == '\t')) {
                val_start++;
            }
            size_t val_end = json_str.find_first_of(",}", val_start);
            std::string val_str = json_str.substr(val_start, val_end - val_start);
            double value = std::stod(val_str);
            
            DBG(("Variable: " + var_name + ", Value: " + std::to_string(value) + "\n").c_str());
            
            // =============================================================================
            // NEW: HASH MAP LOOKUP - O(1) performance
            // =============================================================================
            
            auto it = command_handlers.find(var_name);
            if (it != command_handlers.end()) {
                try {
                    auto result = it->second(value);
                    DBG(("Hash map handler found for: " + var_name + "\n").c_str());
                    return result;
                }
                catch (const std::exception& e) {
                    std::string error_msg = "ERROR: Exception in command handler for " + var_name + ": " + std::string(e.what()) + "\n";
                    OutputDebugStringA(error_msg.c_str());
                    return empty_msg;
                }
                catch (...) {
                    std::string error_msg = "ERROR: Unknown exception in command handler for " + var_name + "\n";
                    OutputDebugStringA(error_msg.c_str());
                    return empty_msg;
                }
            }
            
            // =============================================================================
            // FALLBACK: Original if-else chain for variables not yet migrated to hash map
            // TODO: Remove this section once all variables are migrated
            // =============================================================================
            
            // === BASIC FLIGHT CONTROLS ===
            // MIGRATED TO HASH MAP: Controls.Pitch.Input
            // MIGRATED TO HASH MAP: Controls.Roll.Input
            // MIGRATED TO HASH MAP: Controls.Yaw.Input
            
            // === THROTTLE CONTROLS ===
            // MIGRATED TO HASH MAP: Controls.Throttle
            if (var_name == "Controls.Throttle1") {
                MessageControlsThrottle1.SetValue(value);
                DBG("Creating message Controls.Throttle1\n");
                return MessageControlsThrottle1;
            }
            if (var_name == "Controls.Throttle2") {
                MessageControlsThrottle2.SetValue(value);
                DBG("Creating message Controls.Throttle2\n");
                return MessageControlsThrottle2;
            }
            if (var_name == "Controls.Throttle3") {
                MessageControlsThrottle3.SetValue(value);
                DBG("Creating message Controls.Throttle3\n");
                return MessageControlsThrottle3;
            }
            if (var_name == "Controls.Throttle4") {
                MessageControlsThrottle4.SetValue(value);
                DBG("Creating message Controls.Throttle4\n");
                return MessageControlsThrottle4;
            }
            
            // === AIRCRAFT SYSTEMS ===
            // MIGRATED TO HASH MAP: Controls.Gear
            // MIGRATED TO HASH MAP: Controls.Flaps
            if (var_name == "Controls.AirBrake") {
                MessageControlsAirBrake.SetValue(value);
                DBG("Creating message Controls.AirBrake\n");
                return MessageControlsAirBrake;
            }
            
            // === CALL HELPER FUNCTIONS ===
            // Instead of having all variables in one function, call helper functions
            tm_external_message result;
            
            result = ProcessAircraftVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessNavigationVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessAutopilotVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessControlsVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessEngineVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessSimulationVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessWarningVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            result = ProcessViewVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;

            result = ProcessC172SpecificVariables(var_name, value);
            if (result.GetDataType() != tm_msg_data_type::None) return result;
            
            
            // Variable not found
            OutputDebugStringA(("Variable not supported: " + var_name + "\n").c_str());
            
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("Exception parsing command: " + std::string(e.what()) + "\n").c_str());
        }
        catch (...) {
            OutputDebugStringA("Unknown exception parsing command\n");
        }
        
        return empty_msg;
    }
};

// =============================================================================
// HELPER FUNCTION IMPLEMENTATIONS
// =============================================================================

tm_external_message CommandProcessor::ProcessAircraftVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    if (var_name == "Aircraft.UniversalTime") {
        MessageAircraftUniversalTime.SetValue(value);
        DBG("Creating message Aircraft.UniversalTime\n");
        return MessageAircraftUniversalTime;
    }
    if (var_name == "Aircraft.Altitude") {
        MessageAircraftAltitude.SetValue(value);
        DBG("Creating message Aircraft.Altitude\n");
        return MessageAircraftAltitude;
    }
    if (var_name == "Aircraft.VerticalSpeed") {
        MessageAircraftVerticalSpeed.SetValue(value);
        DBG("Creating message Aircraft.VerticalSpeed\n");
        return MessageAircraftVerticalSpeed;
    }
    if (var_name == "Aircraft.Pitch") {
        MessageAircraftPitch.SetValue(value);
        DBG("Creating message Aircraft.Pitch\n");
        return MessageAircraftPitch;
    }
    if (var_name == "Aircraft.Bank") {
        MessageAircraftBank.SetValue(value);
        DBG("Creating message Aircraft.Bank\n");
        return MessageAircraftBank;
    }
    if (var_name == "Aircraft.TrueHeading") {
        MessageAircraftTrueHeading.SetValue(value);
        DBG("Creating message Aircraft.TrueHeading\n");
        return MessageAircraftTrueHeading;
    }
    if (var_name == "Aircraft.MagneticHeading") {
        MessageAircraftMagneticHeading.SetValue(value);
        DBG("Creating message Aircraft.MagneticHeading\n");
        return MessageAircraftMagneticHeading;
    }
    if (var_name == "Aircraft.Latitude") {
        MessageAircraftLatitude.SetValue(value);
        DBG("Creating message Aircraft.Latitude\n");
        return MessageAircraftLatitude;
    }
    if (var_name == "Aircraft.Longitude") {
        MessageAircraftLongitude.SetValue(value);
        DBG("Creating message Aircraft.Longitude\n");
        return MessageAircraftLongitude;
    }
    if (var_name == "Aircraft.OnGround") {
        MessageAircraftOnGround.SetValue(value);
        DBG("Creating message Aircraft.OnGround\n");
        return MessageAircraftOnGround;
    }
    if (var_name == "Aircraft.OnRunway") {
        MessageAircraftOnRunway.SetValue(value);
        DBG("Creating message Aircraft.OnRunway\n");
        return MessageAircraftOnRunway;
    }

    if (var_name == "Aircraft.Gear") {
        MessageAircraftGear.SetValue(value);
        DBG("Creating message Aircraft.Gear\n");
        return MessageAircraftGear;
    }
    if (var_name == "Aircraft.Flaps") {
        MessageAircraftFlaps.SetValue(value);
        DBG("Creating message Aircraft.Flaps\n");
        return MessageAircraftFlaps;
    }
    if (var_name == "Aircraft.Throttle") {
        MessageAircraftThrottle.SetValue(value);
        DBG("Creating message Aircraft.Throttle\n");
        return MessageAircraftThrottle;
    }
    if (var_name == "Aircraft.ParkingBrake") {
        MessageAircraftParkingBrake.SetValue(value);
        DBG("Creating message Aircraft.ParkingBrake\n");
        return MessageAircraftParkingBrake;
    }
    if (var_name == "Aircraft.YawDamperEnabled") {
        MessageAircraftYawDamperEnabled.SetValue(value);
        DBG("Creating message Aircraft.YawDamperEnabled\n");
        return MessageAircraftYawDamperEnabled;
    }
    if (var_name == "Aircraft.AutoPitchTrim") {
        MessageAircraftAutoPitchTrim.SetValue(value);
        DBG("Creating message Aircraft.AutoPitchTrim\n");
        return MessageAircraftAutoPitchTrim;
    }
    
    return empty_msg; // Variable not found in this category
}

tm_external_message CommandProcessor::ProcessNavigationVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === EXISTING COMMUNICATION VARIABLES ===
    // CLEANED UP: All Communication variables now use hash map O(1) lookup
    

    
    // === EXISTING NAV VARIABLES ===
    // CLEANED UP: All Navigation variables now use hash map O(1) lookup
    
    // === NEW NAV FREQUENCY SWAPS ===
    // CLEANED UP: Navigation frequency swaps now use hash map O(1) lookup
    
    // Variable not found in this category
    return empty_msg;
    
    // === NEW DME VARIABLES ===
    // CLEANED UP: All Navigation DME variables now use hash map O(1) lookup
    
    // === NEW ILS VARIABLES ===
    // CLEANED UP: All Navigation ILS variables now use hash map O(1) lookup
    
    // === NEW ADF VARIABLES ===
    // CLEANED UP: All Navigation ADF variables now use hash map O(1) lookup
    
    // === TRANSPONDER ===
    if (var_name == "Communication.TransponderCode") {
        MessageTransponderCode.SetValue(value);
        DBG("Creating message Communication.TransponderCode\n");
        return MessageTransponderCode;
    }
    if (var_name == "Communication.TransponderCursor") {
        MessageTransponderCursor.SetValue(value);
        DBG("Creating message Communication.TransponderCursor\n");
        return MessageTransponderCursor;
    }
    
    return empty_msg;
}

// =============================================================================
// MISSING HELPER FUNCTION IMPLEMENTATIONS
// =============================================================================

tm_external_message CommandProcessor::ProcessAutopilotVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === EXISTING AUTOPILOT VARIABLES ===
    // MIGRATED TO HASH MAP: Autopilot.Master
    // MIGRATED TO HASH MAP: Autopilot.Heading
    if (var_name == "Autopilot.VerticalSpeed") {
        MessageAutopilotVerticalSpeed.SetValue(value);
        DBG("Creating message Autopilot.VerticalSpeed\n");
        return MessageAutopilotVerticalSpeed;
    }
    if (var_name == "Autopilot.SelectedAirspeed") {
        MessageAutopilotSelectedAirspeed.SetValue(value);
        DBG("Creating message Autopilot.SelectedAirspeed\n");
        return MessageAutopilotSelectedAirspeed;
    }
    if (var_name == "Autopilot.SelectedHeading") {
        MessageAutopilotSelectedHeading.SetValue(value);
        DBG("Creating message Autopilot.SelectedHeading\n");
        return MessageAutopilotSelectedHeading;
    }
    if (var_name == "Autopilot.SelectedAltitude") {
        MessageAutopilotSelectedAltitude.SetValue(value);
        DBG("Creating message Autopilot.SelectedAltitude\n");
        return MessageAutopilotSelectedAltitude;
    }
    if (var_name == "Autopilot.SelectedVerticalSpeed") {
        MessageAutopilotSelectedVerticalSpeed.SetValue(value);
        DBG("Creating message Autopilot.SelectedVerticalSpeed\n");
        return MessageAutopilotSelectedVerticalSpeed;
    }
    if (var_name == "Autopilot.ThrottleEngaged") {
        MessageAutopilotThrottleEngaged.SetValue(value);
        DBG("Creating message Autopilot.ThrottleEngaged\n");
        return MessageAutopilotThrottleEngaged;
    }
    
    // === NEW AUTOPILOT CONTROL VARIABLES ===
    if (var_name == "Autopilot.Disengage") {
        MessageAutopilotDisengage.SetValue(value);
        DBG("Creating message Autopilot.Disengage\n");
        return MessageAutopilotDisengage;
    }
    if (var_name == "Autopilot.SelectedSpeed") {
        MessageAutopilotSelectedSpeed.SetValue(value);
        DBG("Creating message Autopilot.SelectedSpeed\n");
        return MessageAutopilotSelectedSpeed;
    }
    if (var_name == "Autopilot.SelectedAltitudeScale") {
        MessageAutopilotSelectedAltitudeScale.SetValue(value);
        DBG("Creating message Autopilot.SelectedAltitudeScale\n");
        return MessageAutopilotSelectedAltitudeScale;
    }
    if (var_name == "Autopilot.Engaged") {
        MessageAutopilotEngaged.SetValue(value);
        DBG("Creating message Autopilot.Engaged\n");
        return MessageAutopilotEngaged;
    }
    if (var_name == "Autopilot.UseMachNumber") {
        MessageAutopilotUseMachNumber.SetValue(value);
        DBG("Creating message Autopilot.UseMachNumber\n");
        return MessageAutopilotUseMachNumber;
    }
    if (var_name == "Autopilot.SpeedManaged") {
        MessageAutopilotSpeedManaged.SetValue(value);
        DBG("Creating message Autopilot.SpeedManaged\n");
        return MessageAutopilotSpeedManaged;
    }
    if (var_name == "Autopilot.TargetAirspeed") {
        MessageAutopilotTargetAirspeed.SetValue(value);
        DBG("Creating message Autopilot.TargetAirspeed\n");
        return MessageAutopilotTargetAirspeed;
    }
    
    // === NEW AUTOPILOT CONTROL SURFACES ===
    if (var_name == "Autopilot.Aileron") {
        MessageAutopilotAileron.SetValue(value);
        DBG("Creating message Autopilot.Aileron\n");
        return MessageAutopilotAileron;
    }
    if (var_name == "Autopilot.Elevator") {
        MessageAutopilotElevator.SetValue(value);
        DBG("Creating message Autopilot.Elevator\n");
        return MessageAutopilotElevator;
    }
    if (var_name == "Autopilot.ThrottleCommand") {
        MessageAutopilotThrottleCommand.SetValue(value);
        DBG("Creating message Autopilot.ThrottleCommand\n");
        return MessageAutopilotThrottleCommand;
    }
    
    // === NEW AUTOTHROTTLE VARIABLES ===
    if (var_name == "AutoThrottle.Type") {
        MessageAutoAutoThrottleType.SetValue(value);
        DBG("Creating message AutoThrottle.Type\n");
        return MessageAutoAutoThrottleType;
    }
    
    // === NEW FLIGHT DIRECTOR VARIABLES ===
    if (var_name == "FlightDirector.Pitch") {
        MessageFlightDirectorPitch.SetValue(value);
        DBG("Creating message FlightDirector.Pitch\n");
        return MessageFlightDirectorPitch;
    }
    if (var_name == "FlightDirector.Bank") {
        MessageFlightDirectorBank.SetValue(value);
        DBG("Creating message FlightDirector.Bank\n");
        return MessageFlightDirectorBank;
    }
    if (var_name == "FlightDirector.Yaw") {
        MessageFlightDirectorYaw.SetValue(value);
        DBG("Creating message FlightDirector.Yaw\n");
        return MessageFlightDirectorYaw;
    }
    
    // === NEW COPILOT VARIABLES ===
    if (var_name == "Copilot.Heading") {
        MessageCopilotHeading.SetValue(value);
        DBG("Creating message Copilot.Heading\n");
        return MessageCopilotHeading;
    }
    if (var_name == "Copilot.Altitude") {
        MessageCopilotAltitude.SetValue(value);
        DBG("Creating message Copilot.Altitude\n");
        return MessageCopilotAltitude;
    }
    if (var_name == "Copilot.Airspeed") {
        MessageCopilotAirspeed.SetValue(value);
        DBG("Creating message Copilot.Airspeed\n");
        return MessageCopilotAirspeed;
    }
    if (var_name == "Copilot.VerticalSpeed") {
        MessageCopilotVerticalSpeed.SetValue(value);
        DBG("Creating message Copilot.VerticalSpeed\n");
        return MessageCopilotVerticalSpeed;
    }
    if (var_name == "Copilot.Aileron") {
        MessageCopilotAileron.SetValue(value);
        DBG("Creating message Copilot.Aileron\n");
        return MessageCopilotAileron;
    }
    if (var_name == "Copilot.Elevator") {
        MessageCopilotElevator.SetValue(value);
        DBG("Creating message Copilot.Elevator\n");
        return MessageCopilotElevator;
    }
    if (var_name == "Copilot.Throttle") {
        MessageCopilotThrottle.SetValue(value);
        DBG("Creating message Copilot.Throttle\n");
        return MessageCopilotThrottle;
    }
    if (var_name == "Copilot.AutoRudder") {
        MessageCopilotAutoRudder.SetValue(value);
        DBG("Creating message Copilot.AutoRudder\n");
        return MessageCopilotAutoRudder;
    }
    
    // === PERFORMANCE SPEED VARIABLES ===
    if (var_name == "Performance.Speed.VS0") {
        MessagePerformanceSpeedVS0.SetValue(value);
        DBG("Creating message Performance.Speed.VS0\n");
        return MessagePerformanceSpeedVS0;
    }
    if (var_name == "Performance.Speed.VS1") {
        MessagePerformanceSpeedVS1.SetValue(value);
        DBG("Creating message Performance.Speed.VS1\n");
        return MessagePerformanceSpeedVS1;
    }
    if (var_name == "Performance.Speed.VFE") {
        MessagePerformanceSpeedVFE.SetValue(value);
        DBG("Creating message Performance.Speed.VFE\n");
        return MessagePerformanceSpeedVFE;
    }
    if (var_name == "Performance.Speed.VNO") {
        MessagePerformanceSpeedVNO.SetValue(value);
        DBG("Creating message Performance.Speed.VNO\n");
        return MessagePerformanceSpeedVNO;
    }
    if (var_name == "Performance.Speed.VNE") {
        MessagePerformanceSpeedVNE.SetValue(value);
        DBG("Creating message Performance.Speed.VNE\n");
        return MessagePerformanceSpeedVNE;
    }
    if (var_name == "Performance.Speed.VAPP") {
        MessagePerformanceSpeedVAPP.SetValue(value);
        DBG("Creating message Performance.Speed.VAPP\n");
        return MessagePerformanceSpeedVAPP;
    }
    if (var_name == "Performance.Speed.Minimum") {
        MessagePerformanceSpeedMinimum.SetValue(value);
        DBG("Creating message Performance.Speed.Minimum\n");
        return MessagePerformanceSpeedMinimum;
    }
    if (var_name == "Performance.Speed.Maximum") {
        MessagePerformanceSpeedMaximum.SetValue(value);
        DBG("Creating message Performance.Speed.Maximum\n");
        return MessagePerformanceSpeedMaximum;
    }
    if (var_name == "Performance.Speed.MinimumFlapRetraction") {
        MessagePerformanceSpeedMinimumFlapRetraction.SetValue(value);
        DBG("Creating message Performance.Speed.MinimumFlapRetraction\n");
        return MessagePerformanceSpeedMinimumFlapRetraction;
    }
    if (var_name == "Performance.Speed.MaximumFlapExtension") {
        MessagePerformanceSpeedMaximumFlapExtension.SetValue(value);
        DBG("Creating message Performance.Speed.MaximumFlapExtension\n");
        return MessagePerformanceSpeedMaximumFlapExtension;
    }
    
    // === CONFIGURATION VARIABLES ===
    if (var_name == "Configuration.SelectedTakeOffFlaps") {
        MessageConfigurationSelectedTakeOffFlaps.SetValue(value);
        DBG("Creating message Configuration.SelectedTakeOffFlaps\n");
        return MessageConfigurationSelectedTakeOffFlaps;
    }
    if (var_name == "Configuration.SelectedLandingFlaps") {
        MessageConfigurationSelectedLandingFlaps.SetValue(value);
        DBG("Creating message Configuration.SelectedLandingFlaps\n");
        return MessageConfigurationSelectedLandingFlaps;
    }
    
    return empty_msg;
}

tm_external_message CommandProcessor::ProcessControlsVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === EXISTING CONTROLS ===
    if (var_name == "Controls.AileronTrim") {
        MessageControlsAileronTrim.SetValue(value);
        DBG("Creating message Controls.AileronTrim\n");
        return MessageControlsAileronTrim;
    }
    if (var_name == "Controls.RudderTrim") {
        MessageControlsRudderTrim.SetValue(value);
        DBG("Creating message Controls.RudderTrim\n");
        return MessageControlsRudderTrim;
    }
    if (var_name == "Controls.Tiller") {
        MessageControlsTiller.SetValue(value);
        DBG("Creating message Controls.Tiller\n");
        return MessageControlsTiller;
    }
    if (var_name == "Controls.NoseWheelSteering") {
        MessageControlsNoseWheelSteering.SetValue(value);
        DBG("Creating message Controls.NoseWheelSteering\n");
        return MessageControlsNoseWheelSteering;
    }
    if (var_name == "Controls.PedalsDisconnect") {
        MessageControlsPedalsDisconnect.SetValue(value);
        DBG("Creating message Controls.PedalsDisconnect\n");
        return MessageControlsPedalsDisconnect;
    }
    if (var_name == "Controls.WheelBrake.Left") {
        MessageControlsWheelBrakeLeft.SetValue(value);
        DBG("Creating message Controls.WheelBrake.Left\n");
        return MessageControlsWheelBrakeLeft;
    }
    if (var_name == "Controls.WheelBrake.Right") {
        MessageControlsWheelBrakeRight.SetValue(value);
        DBG("Creating message Controls.WheelBrake.Right\n");
        return MessageControlsWheelBrakeRight;
    }
    
    // === NEW MIXTURE CONTROLS ===
    if (var_name == "Controls.Mixture") {
        MessageControlsMixture.SetValue(value);
        DBG("Creating message Controls.Mixture\n");
        return MessageControlsMixture;
    }
    if (var_name == "Controls.Mixture1") {
        MessageControlsMixture1.SetValue(value);
        DBG("Creating message Controls.Mixture1\n");
        return MessageControlsMixture1;
    }
    if (var_name == "Controls.Mixture2") {
        MessageControlsMixture2.SetValue(value);
        DBG("Creating message Controls.Mixture2\n");
        return MessageControlsMixture2;
    }
    if (var_name == "Controls.Mixture3") {
        MessageControlsMixture3.SetValue(value);
        DBG("Creating message Controls.Mixture3\n");
        return MessageControlsMixture3;
    }
    if (var_name == "Controls.Mixture4") {
        MessageControlsMixture4.SetValue(value);
        DBG("Creating message Controls.Mixture4\n");
        return MessageControlsMixture4;
    }
    
    // === NEW PROPELLER SPEED CONTROLS ===
    if (var_name == "Controls.PropellerSpeed1") {
        MessageControlsPropellerSpeed1.SetValue(value);
        DBG("Creating message Controls.PropellerSpeed1\n");
        return MessageControlsPropellerSpeed1;
    }
    if (var_name == "Controls.PropellerSpeed2") {
        MessageControlsPropellerSpeed2.SetValue(value);
        DBG("Creating message Controls.PropellerSpeed2\n");
        return MessageControlsPropellerSpeed2;
    }
    if (var_name == "Controls.PropellerSpeed3") {
        MessageControlsPropellerSpeed3.SetValue(value);
        DBG("Creating message Controls.PropellerSpeed3\n");
        return MessageControlsPropellerSpeed3;
    }
    if (var_name == "Controls.PropellerSpeed4") {
        MessageControlsPropellerSpeed4.SetValue(value);
        DBG("Creating message Controls.PropellerSpeed4\n");
        return MessageControlsPropellerSpeed4;
    }
    
    // === NEW THRUST REVERSE CONTROLS ===
    if (var_name == "Controls.ThrustReverse") {
        MessageControlsThrustReverse.SetValue(value);
        DBG("Creating message Controls.ThrustReverse\n");
        return MessageControlsThrustReverse;
    }
    if (var_name == "Controls.ThrustReverse1") {
        MessageControlsThrustReverse1.SetValue(value);
        DBG("Creating message Controls.ThrustReverse1\n");
        return MessageControlsThrustReverse1;
    }
    if (var_name == "Controls.ThrustReverse2") {
        MessageControlsThrustReverse2.SetValue(value);
        DBG("Creating message Controls.ThrustReverse2\n");
        return MessageControlsThrustReverse2;
    }
    if (var_name == "Controls.ThrustReverse3") {
        MessageControlsThrustReverse3.SetValue(value);
        DBG("Creating message Controls.ThrustReverse3\n");
        return MessageControlsThrustReverse3;
    }
    if (var_name == "Controls.ThrustReverse4") {
        MessageControlsThrustReverse4.SetValue(value);
        DBG("Creating message Controls.ThrustReverse4\n");
        return MessageControlsThrustReverse4;
    }
    
    // === NEW HELICOPTER CONTROLS ===
    if (var_name == "Controls.Collective") {
        MessageControlsCollective.SetValue(value);
        DBG("Creating message Controls.Collective\n");
        return MessageControlsCollective;
    }
    if (var_name == "Controls.CyclicPitch") {
        MessageControlsCyclicPitch.SetValue(value);
        DBG("Creating message Controls.CyclicPitch\n");
        return MessageControlsCyclicPitch;
    }
    if (var_name == "Controls.CyclicRoll") {
        MessageControlsCyclicRoll.SetValue(value);
        DBG("Creating message Controls.CyclicRoll\n");
        return MessageControlsCyclicRoll;
    }
    if (var_name == "Controls.TailRotor") {
        MessageControlsTailRotor.SetValue(value);
        DBG("Creating message Controls.TailRotor\n");
        return MessageControlsTailRotor;
    }
    if (var_name == "Controls.RotorBrake") {
        MessageControlsRotorBrake.SetValue(value);
        DBG("Creating message Controls.RotorBrake\n");
        return MessageControlsRotorBrake;
    }
    if (var_name == "Controls.HelicopterThrottle1") {
        MessageControlsHelicopterThrottle1.SetValue(value);
        DBG("Creating message Controls.HelicopterThrottle1\n");
        return MessageControlsHelicopterThrottle1;
    }
    if (var_name == "Controls.HelicopterThrottle2") {
        MessageControlsHelicopterThrottle2.SetValue(value);
        DBG("Creating message Controls.HelicopterThrottle2\n");
        return MessageControlsHelicopterThrottle2;
    }
    
    // === NEW GLIDER CONTROLS ===
    if (var_name == "Controls.GliderAirBrake") {
        MessageControlsGliderAirBrake.SetValue(value);
        DBG("Creating message Controls.GliderAirBrake\n");
        return MessageControlsGliderAirBrake;
    }
    
    return empty_msg;
}

tm_external_message CommandProcessor::ProcessEngineVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === ENGINE MASTER SWITCHES (ALL 4 ENGINES) ===
    if (var_name == "Aircraft.EngineMaster1") {
        MessageAircraftEngineMaster1.SetValue(value);
        DBG("Creating message Aircraft.EngineMaster1\n");
        return MessageAircraftEngineMaster1;
    }
    if (var_name == "Aircraft.EngineMaster2") {
        MessageAircraftEngineMaster2.SetValue(value);
        DBG("Creating message Aircraft.EngineMaster2\n");
        return MessageAircraftEngineMaster2;
    }
    if (var_name == "Aircraft.EngineMaster3") {
        MessageAircraftEngineMaster3.SetValue(value);
        DBG("Creating message Aircraft.EngineMaster3\n");
        return MessageAircraftEngineMaster3;
    }
    if (var_name == "Aircraft.EngineMaster4") {
        MessageAircraftEngineMaster4.SetValue(value);
        DBG("Creating message Aircraft.EngineMaster4\n");
        return MessageAircraftEngineMaster4;
    }
    
    // === ENGINE STARTERS (ALL 4 ENGINES) ===
    if (var_name == "Aircraft.Starter") {
        MessageAircraftStarter.SetValue(value);
        DBG("Creating message Aircraft.Starter\n");
        return MessageAircraftStarter;
    }
    if (var_name == "Aircraft.Starter1") {
        MessageAircraftStarter1.SetValue(value);
        DBG("Creating message Aircraft.Starter1\n");
        return MessageAircraftStarter1;
    }
    if (var_name == "Aircraft.Starter2") {
        MessageAircraftStarter2.SetValue(value);
        DBG("Creating message Aircraft.Starter2\n");
        return MessageAircraftStarter2;
    }
    if (var_name == "Aircraft.Starter3") {
        MessageAircraftStarter3.SetValue(value);
        DBG("Creating message Aircraft.Starter3\n");
        return MessageAircraftStarter3;
    }
    if (var_name == "Aircraft.Starter4") {
        MessageAircraftStarter4.SetValue(value);
        DBG("Creating message Aircraft.Starter4\n");
        return MessageAircraftStarter4;
    }
    
    // === ENGINE IGNITION (ALL 4 ENGINES) ===
    if (var_name == "Aircraft.Ignition") {
        MessageAircraftIgnition.SetValue(value);
        DBG("Creating message Aircraft.Ignition\n");
        return MessageAircraftIgnition;
    }
    if (var_name == "Aircraft.Ignition1") {
        MessageAircraftIgnition1.SetValue(value);
        DBG("Creating message Aircraft.Ignition1\n");
        return MessageAircraftIgnition1;
    }
    if (var_name == "Aircraft.Ignition2") {
        MessageAircraftIgnition2.SetValue(value);
        DBG("Creating message Aircraft.Ignition2\n");
        return MessageAircraftIgnition2;
    }
    if (var_name == "Aircraft.Ignition3") {
        MessageAircraftIgnition3.SetValue(value);
        DBG("Creating message Aircraft.Ignition3\n");
        return MessageAircraftIgnition3;
    }
    if (var_name == "Aircraft.Ignition4") {
        MessageAircraftIgnition4.SetValue(value);
        DBG("Creating message Aircraft.Ignition4\n");
        return MessageAircraftIgnition4;
    }
    
    // === ENGINE THROTTLE READINGS (ALL 4 ENGINES) ===
    if (var_name == "Aircraft.EngineThrottle1") {
        MessageAircraftEngineThrottle1.SetValue(value);
        DBG("Creating message Aircraft.EngineThrottle1\n");
        return MessageAircraftEngineThrottle1;
    }
    if (var_name == "Aircraft.EngineThrottle2") {
        MessageAircraftEngineThrottle2.SetValue(value);
        DBG("Creating message Aircraft.EngineThrottle2\n");
        return MessageAircraftEngineThrottle2;
    }
    if (var_name == "Aircraft.EngineThrottle3") {
        MessageAircraftEngineThrottle3.SetValue(value);
        DBG("Creating message Aircraft.EngineThrottle3\n");
        return MessageAircraftEngineThrottle3;
    }
    if (var_name == "Aircraft.EngineThrottle4") {
        MessageAircraftEngineThrottle4.SetValue(value);
        DBG("Creating message Aircraft.EngineThrottle4\n");
        return MessageAircraftEngineThrottle4;
    }
    
    // === ENGINE ROTATION SPEED (ALL 4 ENGINES) ===
    if (var_name == "Aircraft.EngineRotationSpeed1") {
        MessageAircraftEngineRotationSpeed1.SetValue(value);
        DBG("Creating message Aircraft.EngineRotationSpeed1\n");
        return MessageAircraftEngineRotationSpeed1;
    }
    if (var_name == "Aircraft.EngineRotationSpeed2") {
        MessageAircraftEngineRotationSpeed2.SetValue(value);
        DBG("Creating message Aircraft.EngineRotationSpeed2\n");
        return MessageAircraftEngineRotationSpeed2;
    }
    if (var_name == "Aircraft.EngineRotationSpeed3") {
        MessageAircraftEngineRotationSpeed3.SetValue(value);
        DBG("Creating message Aircraft.EngineRotationSpeed3\n");
        return MessageAircraftEngineRotationSpeed3;
    }
    if (var_name == "Aircraft.EngineRotationSpeed4") {
        MessageAircraftEngineRotationSpeed4.SetValue(value);
        DBG("Creating message Aircraft.EngineRotationSpeed4\n");
        return MessageAircraftEngineRotationSpeed4;
    }
    
    // === ENGINE RUNNING STATUS (ALL 4 ENGINES) ===
    if (var_name == "Aircraft.EngineRunning1") {
        MessageAircraftEngineRunning1.SetValue(value);
        DBG("Creating message Aircraft.EngineRunning1\n");
        return MessageAircraftEngineRunning1;
    }
    if (var_name == "Aircraft.EngineRunning2") {
        MessageAircraftEngineRunning2.SetValue(value);
        DBG("Creating message Aircraft.EngineRunning2\n");
        return MessageAircraftEngineRunning2;
    }
    if (var_name == "Aircraft.EngineRunning3") {
        MessageAircraftEngineRunning3.SetValue(value);
        DBG("Creating message Aircraft.EngineRunning3\n");
        return MessageAircraftEngineRunning3;
    }
    if (var_name == "Aircraft.EngineRunning4") {
        MessageAircraftEngineRunning4.SetValue(value);
        DBG("Creating message Aircraft.EngineRunning4\n");
        return MessageAircraftEngineRunning4;
    }
    
    // === ADDITIONAL ENGINE CONTROLS ===
    if (var_name == "Aircraft.ThrottleLimit") {
        MessageAircraftThrottleLimit.SetValue(value);
        DBG("Creating message Aircraft.ThrottleLimit\n");
        return MessageAircraftThrottleLimit;
    }
    if (var_name == "Aircraft.Reverse") {
        MessageAircraftReverse.SetValue(value);
        DBG("Creating message Aircraft.Reverse\n");
        return MessageAircraftReverse;
    }
    if (var_name == "Aircraft.APUAvailable") {
        MessageAircraftAPUAvailable.SetValue(value);
        DBG("Creating message Aircraft.APUAvailable\n");
        return MessageAircraftAPUAvailable;
    }
    
    // === ENGINE POWER VARIABLES ===
    if (var_name == "Aircraft.Power") {
        MessageAircraftPower.SetValue(value);
        DBG("Creating message Aircraft.Power\n");
        return MessageAircraftPower;
    }
    if (var_name == "Aircraft.NormalizedPower") {
        MessageAircraftNormalizedPower.SetValue(value);
        DBG("Creating message Aircraft.NormalizedPower\n");
        return MessageAircraftNormalizedPower;
    }
    if (var_name == "Aircraft.NormalizedPowerTarget") {
        MessageAircraftNormalizedPowerTarget.SetValue(value);
        DBG("Creating message Aircraft.NormalizedPowerTarget\n");
        return MessageAircraftNormalizedPowerTarget;
    }
    
    return empty_msg;
}

tm_external_message CommandProcessor::ProcessSimulationVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    if (var_name == "Simulation.Pause") {
        MessageSimulationPause.SetValue(value);
        DBG("Creating message Simulation.Pause\n");
        return MessageSimulationPause;
    }
    if (var_name == "Simulation.Sound") {
        MessageSimulationSound.SetValue(value);
        DBG("Creating message Simulation.Sound\n");
        return MessageSimulationSound;
    }
    if (var_name == "Simulation.LiftUp") {
        MessageSimulationLiftUp.SetValue(value);
        DBG("Creating message Simulation.LiftUp\n");
        return MessageSimulationLiftUp;
    }
    if (var_name == "Simulation.FlightInformation") {
        MessageSimulationFlightInformation.SetValue(value);
        DBG("Creating message Simulation.FlightInformation\n");
        return MessageSimulationFlightInformation;
    }
    if (var_name == "Simulation.MovingMap") {
        MessageSimulationMovingMap.SetValue(value);
        DBG("Creating message Simulation.MovingMap\n");
        return MessageSimulationMovingMap;
    }
    if (var_name == "Simulation.UseMouseControl") {
        MessageSimulationUseMouseControl.SetValue(value);
        DBG("Creating message Simulation.UseMouseControl\n");
        return MessageSimulationUseMouseControl;
    }
    if (var_name == "Simulation.TimeChange") {
        MessageSimulationTimeChange.SetValue(value);
        DBG("Creating message Simulation.TimeChange\n");
        return MessageSimulationTimeChange;
    }
    if (var_name == "Simulation.Visibility") {
        MessageSimulationVisibility.SetValue(value);
        DBG("Creating message Simulation.Visibility\n");
        return MessageSimulationVisibility;
    }
    if (var_name == "Simulation.PlaybackStart") {
        MessageSimulationPlaybackStart.SetValue(value);
        DBG("Creating message Simulation.PlaybackStart\n");
        return MessageSimulationPlaybackStart;
    }
    if (var_name == "Simulation.PlaybackStop") {
        MessageSimulationPlaybackStop.SetValue(value);
        DBG("Creating message Simulation.PlaybackStop\n");
        return MessageSimulationPlaybackStop;
    }
    
    return empty_msg;
}

tm_external_message CommandProcessor::ProcessWarningVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === EXISTING WARNING VARIABLES ===
    if (var_name == "Warnings.MasterWarning") {
        MessageWarningsMasterWarning.SetValue(value);
        DBG("Creating message Warnings.MasterWarning\n");
        return MessageWarningsMasterWarning;
    }
    if (var_name == "Warnings.MasterCaution") {
        MessageWarningsMasterCaution.SetValue(value);
        DBG("Creating message Warnings.MasterCaution\n");
        return MessageWarningsMasterCaution;
    }
    
    // === NEW ENGINE WARNING VARIABLES ===
    if (var_name == "Warnings.EngineFire") {
        MessageWarningsEngineFire.SetValue(value);
        DBG("Creating message Warnings.EngineFire\n");
        return MessageWarningsEngineFire;
    }
    if (var_name == "Warnings.LowOilPressure") {
        MessageWarningsLowOilPressure.SetValue(value);
        DBG("Creating message Warnings.LowOilPressure\n");
        return MessageWarningsLowOilPressure;
    }
    if (var_name == "Warnings.LowFuelPressure") {
        MessageWarningsLowFuelPressure.SetValue(value);
        DBG("Creating message Warnings.LowFuelPressure\n");
        return MessageWarningsLowFuelPressure;
    }
    
    // === NEW SYSTEM WARNING VARIABLES ===
    if (var_name == "Warnings.LowHydraulicPressure") {
        MessageWarningsLowHydraulicPressure.SetValue(value);
        DBG("Creating message Warnings.LowHydraulicPressure\n");
        return MessageWarningsLowHydraulicPressure;
    }
    if (var_name == "Warnings.LowVoltage") {
        MessageWarningsLowVoltage.SetValue(value);
        DBG("Creating message Warnings.LowVoltage\n");
        return MessageWarningsLowVoltage;
    }
    if (var_name == "Warnings.AltitudeAlert") {
        MessageWarningsAltitudeAlert.SetValue(value);
        DBG("Creating message Warnings.AltitudeAlert\n");
        return MessageWarningsAltitudeAlert;
    }
    if (var_name == "Warnings.WarningActive") {
        MessageWarningsWarningActive.SetValue(value);
        DBG("Creating message Warnings.WarningActive\n");
        return MessageWarningsWarningActive;
    }
    if (var_name == "Warnings.WarningMute") {
        MessageWarningsWarningMute.SetValue(value);
        DBG("Creating message Warnings.WarningMute\n");
        return MessageWarningsWarningMute;
    }
    
    // === PRESSURIZATION VARIABLES ===
    if (var_name == "Pressurization.LandingElevation") {
        MessagePressurizationLandingElevation.SetValue(value);
        DBG("Creating message Pressurization.LandingElevation\n");
        return MessagePressurizationLandingElevation;
    }
    if (var_name == "Pressurization.LandingElevationManual") {
        MessagePressurizationLandingElevationManual.SetValue(value);
        DBG("Creating message Pressurization.LandingElevationManual\n");
        return MessagePressurizationLandingElevationManual;
    }
    
    return empty_msg;
}

tm_external_message CommandProcessor::ProcessViewVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === VIEW MODE CONTROLS ===
    if (var_name == "View.Internal") {
        MessageViewInternal.SetValue(value);
        DBG("Creating message View.Internal\n");
        return MessageViewInternal;
    }
    if (var_name == "View.Follow") {
        MessageViewFollow.SetValue(value);
        DBG("Creating message View.Follow\n");
        return MessageViewFollow;
    }
    if (var_name == "View.External") {
        MessageViewExternal.SetValue(value);
        DBG("Creating message View.External\n");
        return MessageViewExternal;
    }
    if (var_name == "View.Category") {
        MessageViewCategory.SetValue(value);
        DBG("Creating message View.Category\n");
        return MessageViewCategory;
    }
    if (var_name == "View.Mode") {
        MessageViewMode.SetValue(value);
        DBG("Creating message View.Mode\n");
        return MessageViewMode;
    }
    
    // === VIEW CAMERA CONTROLS ===
    if (var_name == "View.Zoom") {
        MessageViewZoom.SetValue(value);
        DBG("Creating message View.Zoom\n");
        return MessageViewZoom;
    }
    if (var_name == "View.Pan.Horizontal") {
        MessageViewPanHorizontal.SetValue(value);
        DBG("Creating message View.Pan.Horizontal\n");
        return MessageViewPanHorizontal;
    }
    if (var_name == "View.Pan.HorizontalMove") {
        MessageViewPanHorizontalMove.SetValue(value);
        DBG("Creating message View.Pan.HorizontalMove\n");
        return MessageViewPanHorizontalMove;
    }
    if (var_name == "View.Pan.Vertical") {
        MessageViewPanVertical.SetValue(value);
        DBG("Creating message View.Pan.Vertical\n");
        return MessageViewPanVertical;
    }
    if (var_name == "View.Pan.VerticalMove") {
        MessageViewPanVerticalMove.SetValue(value);
        DBG("Creating message View.Pan.VerticalMove\n");
        return MessageViewPanVerticalMove;
    }
    if (var_name == "View.Pan.Center") {
        MessageViewPanCenter.SetValue(value);
        DBG("Creating message View.Pan.Center\n");
        return MessageViewPanCenter;
    }
    
    // === VIEW LOOK CONTROLS ===
    if (var_name == "View.Look.Horizontal") {
        MessageViewLookHorizontal.SetValue(value);
        DBG("Creating message View.Look.Horizontal\n");
        return MessageViewLookHorizontal;
    }
    if (var_name == "View.Look.Vertical") {
        MessageViewLookVertical.SetValue(value);
        DBG("Creating message View.Look.Vertical\n");
        return MessageViewLookVertical;
    }
    if (var_name == "View.Roll") {
        MessageViewRoll.SetValue(value);
        DBG("Creating message View.Roll\n");
        return MessageViewRoll;
    }
    
    // === VIEW OFFSET CONTROLS ===
    if (var_name == "View.OffsetX") {
        MessageViewOffsetX.SetValue(value);
        DBG("Creating message View.OffsetX\n");
        return MessageViewOffsetX;
    }
    if (var_name == "View.OffsetXMove") {
        MessageViewOffsetXMove.SetValue(value);
        DBG("Creating message View.OffsetXMove\n");
        return MessageViewOffsetXMove;
    }
    if (var_name == "View.OffsetY") {
        MessageViewOffsetY.SetValue(value);
        DBG("Creating message View.OffsetY\n");
        return MessageViewOffsetY;
    }
    if (var_name == "View.OffsetYMove") {
        MessageViewOffsetYMove.SetValue(value);
        DBG("Creating message View.OffsetYMove\n");
        return MessageViewOffsetYMove;
    }
    if (var_name == "View.OffsetZ") {
        MessageViewOffsetZ.SetValue(value);
        DBG("Creating message View.OffsetZ\n");
        return MessageViewOffsetZ;
    }
    if (var_name == "View.OffsetZMove") {
        MessageViewOffsetZMove.SetValue(value);
        DBG("Creating message View.OffsetZMove\n");
        return MessageViewOffsetZMove;
    }
    
    // === VIEW ADVANCED CONTROLS ===
    if (var_name == "View.Position") {
        MessageViewPosition.SetValue(value);
        DBG("Creating message View.Position\n");
        return MessageViewPosition;
    }
    if (var_name == "View.Direction") {
        MessageViewDirection.SetValue(value);
        DBG("Creating message View.Direction\n");
        return MessageViewDirection;
    }
    if (var_name == "View.Up") {
        MessageViewUp.SetValue(value);
        DBG("Creating message View.Up\n");
        return MessageViewUp;
    }
    if (var_name == "View.FieldOfView") {
        MessageViewFieldOfView.SetValue(value);
        DBG("Creating message View.FieldOfView\n");
        return MessageViewFieldOfView;
    }
    if (var_name == "View.AspectRatio") {
        MessageViewAspectRatio.SetValue(value);
        DBG("Creating message View.AspectRatio\n");
        return MessageViewAspectRatio;
    }
    if (var_name == "View.FreeFieldOfView") {
        MessageViewFreeFieldOfView.SetValue(value);
        DBG("Creating message View.FreeFieldOfView\n");
        return MessageViewFreeFieldOfView;
    }
    
    // === COMMAND CONTROL VARIABLES ===
    if (var_name == "Command.Execute") {
        MessageCommandExecute.SetValue(value);
        DBG("Creating message Command.Execute\n");
        return MessageCommandExecute;
    }
    if (var_name == "Command.Back") {
        MessageCommandBack.SetValue(value);
        DBG("Creating message Command.Back\n");
        return MessageCommandBack;
    }
    if (var_name == "Command.Up") {
        MessageCommandUp.SetValue(value);
        DBG("Creating message Command.Up\n");
        return MessageCommandUp;
    }
    if (var_name == "Command.Down") {
        MessageCommandDown.SetValue(value);
        DBG("Creating message Command.Down\n");
        return MessageCommandDown;
    }
    if (var_name == "Command.Left") {
        MessageCommandLeft.SetValue(value);
        DBG("Creating message Command.Left\n");
        return MessageCommandLeft;
    }
    if (var_name == "Command.Right") {
        MessageCommandRight.SetValue(value);
        DBG("Creating message Command.Right\n");
        return MessageCommandRight;
    }
    if (var_name == "Command.MoveHorizontal") {
        MessageCommandMoveHorizontal.SetValue(value);
        DBG("Creating message Command.MoveHorizontal\n");
        return MessageCommandMoveHorizontal;
    }
    if (var_name == "Command.MoveVertical") {
        MessageCommandMoveVertical.SetValue(value);
        DBG("Creating message Command.MoveVertical\n");
        return MessageCommandMoveVertical;
    }
    if (var_name == "Command.Rotate") {
        MessageCommandRotate.SetValue(value);
        DBG("Creating message Command.Rotate\n");
        return MessageCommandRotate;
    }
    if (var_name == "Command.Zoom") {
        MessageCommandZoom.SetValue(value);
        DBG("Creating message Command.Zoom\n");
        return MessageCommandZoom;
    }
    
    return empty_msg;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// COMMAND PROCESSOR EXTENSION
///////////////////////////////////////////////////////////////////////////////////////////////////

tm_external_message CommandProcessor::ProcessC172SpecificVariables(const std::string& var_name, double value) {
    tm_external_message empty_msg;
    
    // === CESSNA 172 FUEL SYSTEM ===
    if (var_name == "Controls.FuelSelector") {
        MessageC172FuelSelector.SetValue(value);
        DBG("Creating message Controls.FuelSelector\n");
        return MessageC172FuelSelector;
    }
    if (var_name == "Controls.FuelShutOff") {
        MessageC172FuelShutOff.SetValue(value);
        DBG("Creating message Controls.FuelShutOff\n");
        return MessageC172FuelShutOff;
    }
    
    // === CESSNA 172 COCKPIT ===
    if (var_name == "Controls.HideYoke.Left") {
        MessageC172HideYokeLeft.SetValue(value);
        DBG("Creating message Controls.HideYoke.Left\n");
        return MessageC172HideYokeLeft;
    }
    if (var_name == "Controls.HideYoke.Right") {
        MessageC172HideYokeRight.SetValue(value);
        DBG("Creating message Controls.HideYoke.Right\n");
        return MessageC172HideYokeRight;
    }
    if (var_name == "Controls.LeftSunBlocker") {
        MessageC172LeftSunBlocker.SetValue(value);
        DBG("Creating message Controls.LeftSunBlocker\n");
        return MessageC172LeftSunBlocker;
    }
    if (var_name == "Controls.RightSunBlocker") {
        MessageC172RightSunBlocker.SetValue(value);
        DBG("Creating message Controls.RightSunBlocker\n");
        return MessageC172RightSunBlocker;
    }
    
    // === CESSNA 172 LIGHTING ===
    if (var_name == "Controls.Lighting.LeftCabinOverheadLight") {
        MessageC172LeftCabinLight.SetValue(value);
        DBG("Creating message Controls.Lighting.LeftCabinOverheadLight\n");
        return MessageC172LeftCabinLight;
    }
    if (var_name == "Controls.Lighting.RightCabinOverheadLight") {
        MessageC172RightCabinLight.SetValue(value);
        DBG("Creating message Controls.Lighting.RightCabinOverheadLight\n");
        return MessageC172RightCabinLight;
    }
    
    // === CESSNA 172 ENGINE ===
    if (var_name == "Controls.Magnetos1") {
        MessageC172Magnetos1.SetValue(value);
        DBG("Creating message Controls.Magnetos1\n");
        return MessageC172Magnetos1;
    }
    
    // === CESSNA 172 DOORS AND WINDOWS ===
    if (var_name == "Doors.Left") {
        MessageC172LeftDoor.SetValue(value);
        DBG("Creating message Doors.Left\n");
        return MessageC172LeftDoor;
    }
    if (var_name == "Doors.Right") {
        MessageC172RightDoor.SetValue(value);
        DBG("Creating message Doors.Right\n");
        return MessageC172RightDoor;
    }
    if (var_name == "Windows.Left") {
        MessageC172LeftWindow.SetValue(value);
        DBG("Creating message Windows.Left\n");
        return MessageC172LeftWindow;
    }
    if (var_name == "Windows.Right") {
        MessageC172RightWindow.SetValue(value);
        DBG("Creating message Windows.Right\n");
        return MessageC172RightWindow;
    }
    
    // === CESSNA 172 CONTROLS ===
    if (var_name == "LeftYoke.Button") {
        MessageC172LeftYokeButton.SetValue(value);
        DBG("Creating message LeftYoke.Button\n");
        return MessageC172LeftYokeButton;
    }
    
    return empty_msg;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN BRIDGE CONTROLLER
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Main bridge controller for Aerofly FS4 external communication.
 * 
 * This class orchestrates:
 * - Shared memory interface for high-performance local data exchange
 * - TCP server for remote data access and control
 * - Command processing and routing
 * 
 * The bridge provides:
 * - Real-time aircraft data streaming
 * - Remote control capabilities
 * - Bidirectional communication
 * - Automatic resource management
 */
class AeroflyBridge {
private:
    SharedMemoryInterface shared_memory;   ///< Interface for local data exchange
    TCPServerInterface tcp_server;         ///< Network interface for remote access
    CommandProcessor command_processor;     ///< Handles command parsing and routing
    WebSocketServerInterface ws_server;     ///< WebSocket interface (optional)
    bool ws_enabled;                        ///< WebSocket enabled flag
    bool initialized;                      ///< Bridge initialization state
    
public:
    /**
     * @brief Constructs a new bridge instance in uninitialized state.
     */
    AeroflyBridge() : ws_enabled(false), initialized(false) {}
    
    /**
     * @brief Initializes all bridge components.
     * 
     * Initialization sequence:
     * 1. Sets up shared memory interface (required)
     * 2. Starts TCP server (optional)
     * 3. Prepares command processor
     * 
     * @note TCP server failure is not critical, shared memory will still work
     * @return true if required components initialized successfully
     * @return false if critical component fails to initialize
     */
    /**
     * Initializes shared memory, starts TCP and (optionally) WebSocket servers.
     * WebSocket enable/port are controlled via environment variables:
     * - AEROFLY_BRIDGE_WS_ENABLE (default 1)
     * - AEROFLY_BRIDGE_WS_PORT   (default 8765)
     */
    bool Initialize() {
        // Guard re-initialization
        if (initialized) {
            OutputDebugStringA("Initialize() called when already initialized, performing Shutdown() first...\n");
            Shutdown();
        }
        // Initialize shared memory (primary interface)
        if (!shared_memory.Initialize()) {
            return false;
        }
        // Set layout version in header (legacy order by default = 1)
        if (shared_memory.GetData()) {
            shared_memory.GetData()->reserved_header = 1;
        }
        
        // Start TCP server (optional, for network access)
        if (!tcp_server.Start(12345, 12346)) {
            // TCP server failure is not critical
            // Shared memory still works
        }

        // Start WebSocket server (optional, graceful fallback)
        {
            char buf[32] = {0};
            DWORD n = GetEnvironmentVariableA("AEROFLY_BRIDGE_WS_ENABLE", buf, sizeof(buf));
            int enable = (n > 0) ? atoi(buf) : 1; // default enabled
            ws_enabled = (enable != 0);
            int ws_port = 8765;
            char pbuf[32] = {0};
            n = GetEnvironmentVariableA("AEROFLY_BRIDGE_WS_PORT", pbuf, sizeof(pbuf));
            if (n > 0) {
                int v = atoi(pbuf);
                if (v > 0 && v < 65536) ws_port = v;
            }
            if (ws_enabled) {
                if (!ws_server.Start((uint16_t)ws_port)) {
                    OutputDebugStringA("WebSocket server failed - continuing without it\n");
                    ws_enabled = false;
                } else {
                    OutputDebugStringA("WebSocket server started\n");
                }
            }
        }
        
        initialized = true;

        // Export offsets for Python tools to avoid hardcoded values
        try {
            const AeroflyBridgeData* d = shared_memory.GetData();
            if (d) {
                const size_t header_size = offsetof(AeroflyBridgeData, all_variables); // start of array
                const size_t stride = sizeof(double);
                const int count = static_cast<int>(VariableIndex::COUNT);

                // Create JSON file with offsets and variable names
                const std::string out_dir = GetThisModuleDirectory();
                const std::string out_path = out_dir + std::string("/AeroflyBridge_offsets.json");
                std::ofstream ofs(out_path, std::ios::trunc);
                if (ofs) {
                    ofs << "{\n";
                    ofs << "  \"schema\": \"aerofly-bridge-offsets\",\n";
                    ofs << "  \"schema_version\": 1,\n";
                    ofs << "  \"layout_version\": " << d->reserved_header << ",\n";
                    ofs << "  \"array_base_offset\": " << header_size << ",\n";
                    ofs << "  \"stride_bytes\": " << stride << ",\n";
                    ofs << "  \"count\": " << count << ",\n";
                    ofs << "  \"variables\": [\n";

                    auto snapshot = tcp_server.GetMapperSnapshot();
                    // snapshot is name->logical index; serialize sorted by logical index
                    std::vector<std::pair<std::string,int>> items = snapshot;
                    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b){ return a.second < b.second; });

                    // Build exact unit map from MESSAGE_LIST (and aircraft-specific list)
                    static const std::unordered_map<std::string, tm_msg_unit> unit_map = [](){
                        std::unordered_map<std::string, tm_msg_unit> m;
                        // Keep units only for primary Value variables; ignore Move/Event duplicates
                        #define F(a1, a2, a3, a4, a5, a6, a7) if (a4 == tm_msg_flag::Value) { m[a2] = a6; }
                        MESSAGE_LIST(F)
                        #ifdef AIRCRAFT_SPECIFIC_MESSAGE_LIST
                        AIRCRAFT_SPECIFIC_MESSAGE_LIST(F)
                        #endif
                        #undef F
                        return m;
                    }();

                    // Build access map from MESSAGE_LIST
                    static const std::unordered_map<std::string, tm_msg_access> access_map = [](){
                        std::unordered_map<std::string, tm_msg_access> m;
                        #define F(a1, a2, a3, a4, a5, a6, a7) m[a2] = a5;
                        MESSAGE_LIST(F)
                        #ifdef AIRCRAFT_SPECIFIC_MESSAGE_LIST
                        AIRCRAFT_SPECIFIC_MESSAGE_LIST(F)
                        #endif
                        #undef F
                        return m;
                    }();

                    // Build primary flag map from MESSAGE_LIST
                    static const std::unordered_map<std::string, tm_msg_flag> flag_map = [](){
                        std::unordered_map<std::string, tm_msg_flag> m;
                        #define F(a1, a2, a3, a4, a5, a6, a7) m[a2] = a4;
                        MESSAGE_LIST(F)
                        #ifdef AIRCRAFT_SPECIFIC_MESSAGE_LIST
                        AIRCRAFT_SPECIFIC_MESSAGE_LIST(F)
                        #endif
                        #undef F
                        return m;
                    }();

                    // Build message id map (hash from SDK) by reusing MESSAGE_LIST
                    static const std::unordered_map<std::string, uint64_t> id_map = [](){
                        std::unordered_map<std::string, uint64_t> m;
                        #define F(a1, a2, a3, a4, a5, a6, a7) { tm_string_hash h(a2); m[a2] = h.GetHash(); }
                        MESSAGE_LIST(F)
                        #ifdef AIRCRAFT_SPECIFIC_MESSAGE_LIST
                        AIRCRAFT_SPECIFIC_MESSAGE_LIST(F)
                        #endif
                        #undef F
                        return m;
                    }();

                    // Helper to stringify tm_msg_unit
                    auto unit_to_string = [](tm_msg_unit u) -> const char* {
                        switch (u) {
                            case tm_msg_unit::None: return "none";
                            case tm_msg_unit::Second: return "second";
                            case tm_msg_unit::PerSecond: return "per_second";
                            case tm_msg_unit::Meter: return "meter";
                            case tm_msg_unit::MeterPerSecond: return "meter_per_second";
                            case tm_msg_unit::MeterPerSecondSquared: return "meter_per_second_squared";
                            case tm_msg_unit::Radiant: return "radian";
                            case tm_msg_unit::RadiantPerSecond: return "radian_per_second";
                            case tm_msg_unit::RadiantPerSecondSquared: return "radian_per_second_squared";
                            case tm_msg_unit::Hertz: return "hertz";
                            default: return "none";
                        }
                    };

                    // Helper to stringify tm_msg_access
                    auto access_to_string = [](tm_msg_access a) -> const char* {
                        switch (a) {
                            case tm_msg_access::None: return "none";
                            case tm_msg_access::Read: return "read";
                            case tm_msg_access::Write: return "write";
                            case tm_msg_access::ReadWrite: return "read_write";
                            default: return "none";
                        }
                    };

                    // Helper to stringify tm_msg_flag (primary flag)
                    auto flag_to_string = [](tm_msg_flag f) -> const char* {
                        switch (f) {
                            case tm_msg_flag::None: return "none";
                            case tm_msg_flag::State: return "state";
                            case tm_msg_flag::Offset: return "offset";
                            case tm_msg_flag::Event: return "event";
                            case tm_msg_flag::Toggle: return "toggle";
                            case tm_msg_flag::Value: return "value";
                            case tm_msg_flag::Active: return "active";
                            case tm_msg_flag::Normalized: return "normalized";
                            case tm_msg_flag::Discrete: return "discrete";
                            case tm_msg_flag::Minimum: return "minimum";
                            case tm_msg_flag::Maximum: return "maximum";
                            case tm_msg_flag::Valid: return "valid";
                            case tm_msg_flag::Large: return "large";
                            case tm_msg_flag::Move: return "move";
                            case tm_msg_flag::Step: return "step";
                            case tm_msg_flag::Setting: return "setting";
                            case tm_msg_flag::Synchronize: return "synchronize";
                            case tm_msg_flag::Body: return "body";
                            case tm_msg_flag::Repeat: return "repeat";
                            case tm_msg_flag::Device: return "device";
                            case tm_msg_flag::MessageID: return "message_id";
                            case tm_msg_flag::DeviceID: return "device_id";
                            case tm_msg_flag::Signed: return "signed";
                            case tm_msg_flag::Pure: return "pure";
                            case tm_msg_flag::Read: return "read";
                            case tm_msg_flag::Write: return "write";
                            default: return "none";
                        }
                    };

                    // Helper mapping for string-backed variables stored as struct fields
                    auto get_string_field = [](const std::string& name, size_t& out_offset, size_t& out_len, const char*& out_field_name) -> bool {
                        // Aircraft strings
                        if (name == "Aircraft.Name") { out_offset = offsetof(AeroflyBridgeData, aircraft_name); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_name); out_field_name = "aircraft_name"; return true; }
                        if (name == "Aircraft.NearestAirportName") { out_offset = offsetof(AeroflyBridgeData, aircraft_nearest_airport_name); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_nearest_airport_name); out_field_name = "aircraft_nearest_airport_name"; return true; }
                        if (name == "Aircraft.BestAirportName") { out_offset = offsetof(AeroflyBridgeData, aircraft_best_airport_name); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_best_airport_name); out_field_name = "aircraft_best_airport_name"; return true; }
                        if (name == "Aircraft.NearestAirportIdentifier") { out_offset = offsetof(AeroflyBridgeData, aircraft_nearest_airport_id); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_nearest_airport_id); out_field_name = "aircraft_nearest_airport_id"; return true; }
                        if (name == "Aircraft.BestAirportIdentifier") { out_offset = offsetof(AeroflyBridgeData, aircraft_best_airport_id); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_best_airport_id); out_field_name = "aircraft_best_airport_id"; return true; }
                        if (name == "Aircraft.BestRunwayIdentifier") { out_offset = offsetof(AeroflyBridgeData, aircraft_best_runway_id); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_best_runway_id); out_field_name = "aircraft_best_runway_id"; return true; }
                        // Navigation strings
                        if (name == "Navigation.NAV1Identifier") { out_offset = offsetof(AeroflyBridgeData, navigation_nav1_identifier); out_len = sizeof(((AeroflyBridgeData*)0)->navigation_nav1_identifier); out_field_name = "navigation_nav1_identifier"; return true; }
                        if (name == "Navigation.NAV2Identifier") { out_offset = offsetof(AeroflyBridgeData, navigation_nav2_identifier); out_len = sizeof(((AeroflyBridgeData*)0)->navigation_nav2_identifier); out_field_name = "navigation_nav2_identifier"; return true; }
                        if (name == "Navigation.ILS1Identifier") { out_offset = offsetof(AeroflyBridgeData, navigation_ils1_identifier); out_len = sizeof(((AeroflyBridgeData*)0)->navigation_ils1_identifier); out_field_name = "navigation_ils1_identifier"; return true; }
                        if (name == "Navigation.ILS2Identifier") { out_offset = offsetof(AeroflyBridgeData, navigation_ils2_identifier); out_len = sizeof(((AeroflyBridgeData*)0)->navigation_ils2_identifier); out_field_name = "navigation_ils2_identifier"; return true; }
                        // Autopilot strings
                        if (name == "Autopilot.Type") { out_offset = offsetof(AeroflyBridgeData, autopilot_type); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_type); out_field_name = "autopilot_type"; return true; }
                        if (name == "Autopilot.ActiveLateralMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_active_lateral_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_active_lateral_mode); out_field_name = "autopilot_active_lateral_mode"; return true; }
                        if (name == "Autopilot.ArmedLateralMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_armed_lateral_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_armed_lateral_mode); out_field_name = "autopilot_armed_lateral_mode"; return true; }
                        if (name == "Autopilot.ActiveVerticalMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_active_vertical_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_active_vertical_mode); out_field_name = "autopilot_active_vertical_mode"; return true; }
                        if (name == "Autopilot.ArmedVerticalMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_armed_vertical_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_armed_vertical_mode); out_field_name = "autopilot_armed_vertical_mode"; return true; }
                        if (name == "Autopilot.ArmedApproachMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_armed_approach_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_armed_approach_mode); out_field_name = "autopilot_armed_approach_mode"; return true; }
                        if (name == "Autopilot.ActiveAutoThrottleMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_active_autothrottle_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_active_autothrottle_mode); out_field_name = "autopilot_active_autothrottle_mode"; return true; }
                        if (name == "Autopilot.ActiveCollectiveMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_active_collective_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_active_collective_mode); out_field_name = "autopilot_active_collective_mode"; return true; }
                        if (name == "Autopilot.ArmedCollectiveMode") { out_offset = offsetof(AeroflyBridgeData, autopilot_armed_collective_mode); out_len = sizeof(((AeroflyBridgeData*)0)->autopilot_armed_collective_mode); out_field_name = "autopilot_armed_collective_mode"; return true; }
                        // FMS strings
                        if (name == "FlightManagementSystem.FlightNumber") { out_offset = offsetof(AeroflyBridgeData, fms_flight_number); out_len = sizeof(((AeroflyBridgeData*)0)->fms_flight_number); out_field_name = "fms_flight_number"; return true; }
                        return false;
                    };

                    // Helper mapping for vector3d-backed variables stored as struct fields
                    auto get_vector3d_field = [](const std::string& name, size_t& out_offset, size_t& out_len, const char*& out_field_name) -> bool {
                        if (name == "Aircraft.Position") { out_offset = offsetof(AeroflyBridgeData, aircraft_position); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_position); out_field_name = "aircraft_position"; return true; }
                        if (name == "Aircraft.Velocity") { out_offset = offsetof(AeroflyBridgeData, aircraft_velocity); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_velocity); out_field_name = "aircraft_velocity"; return true; }
                        if (name == "Aircraft.AngularVelocity") { out_offset = offsetof(AeroflyBridgeData, aircraft_angular_velocity); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_angular_velocity); out_field_name = "aircraft_angular_velocity"; return true; }
                        if (name == "Aircraft.Acceleration") { out_offset = offsetof(AeroflyBridgeData, aircraft_acceleration); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_acceleration); out_field_name = "aircraft_acceleration"; return true; }
                        if (name == "Aircraft.Gravity") { out_offset = offsetof(AeroflyBridgeData, aircraft_gravity); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_gravity); out_field_name = "aircraft_gravity"; return true; }
                        if (name == "Aircraft.Wind") { out_offset = offsetof(AeroflyBridgeData, aircraft_wind); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_wind); out_field_name = "aircraft_wind"; return true; }
                        if (name == "Aircraft.BestRunwayThreshold") { out_offset = offsetof(AeroflyBridgeData, aircraft_best_runway_threshold); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_best_runway_threshold); out_field_name = "aircraft_best_runway_threshold"; return true; }
                        if (name == "Aircraft.BestRunwayEnd") { out_offset = offsetof(AeroflyBridgeData, aircraft_best_runway_end); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_best_runway_end); out_field_name = "aircraft_best_runway_end"; return true; }
                        return false;
                    };

                    // Helper mapping for vector2d-backed variables stored as struct fields
                    auto get_vector2d_field = [](const std::string& name, size_t& out_offset, size_t& out_len, const char*& out_field_name) -> bool {
                        if (name == "Aircraft.NearestAirportLocation") { out_offset = offsetof(AeroflyBridgeData, aircraft_nearest_airport_location); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_nearest_airport_location); out_field_name = "aircraft_nearest_airport_location"; return true; }
                        if (name == "Aircraft.BestAirportLocation") { out_offset = offsetof(AeroflyBridgeData, aircraft_best_airport_location); out_len = sizeof(((AeroflyBridgeData*)0)->aircraft_best_airport_location); out_field_name = "aircraft_best_airport_location"; return true; }
                        return false;
                    };

                    // Helper to detect Vector4d messages that are not stored in struct
                    auto is_vector4d_message = [](const std::string& name) -> bool {
                        return (name == "Simulation.SettingOrientation" || name == "Simulation.ExternalOrientation");
                    };

                    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                        const auto& kv = items[i];
                        const int logical = kv.second;
                        // Derive alias and simple group by prefix
                        std::string group;
                        if (kv.first.rfind("Aircraft.", 0) == 0) group = "aircraft";
                        else if (kv.first.rfind("Navigation.", 0) == 0) group = "navigation";
                        else if (kv.first.rfind("Communication.", 0) == 0) group = "communication";
                        else if (kv.first.rfind("Autopilot.", 0) == 0) group = "autopilot";
                        else if (kv.first.rfind("Controls.", 0) == 0) group = "controls";
                        else if (kv.first.rfind("Warnings.", 0) == 0) group = "warnings";
                        else if (kv.first.rfind("View.", 0) == 0) group = "view";
                        else if (kv.first.rfind("Simulation.", 0) == 0) group = "simulation";
                        else if (kv.first.rfind("Pressurization.", 0) == 0) group = "pressurization";
                        else if (kv.first.rfind("FlightManagementSystem.", 0) == 0) group = "fms";
                        else group = "other";

                        // Defaults assume double in all_variables
                        size_t byte_offset = header_size + static_cast<size_t>(logical) * stride;
                        const char* data_type = "double";
                        const char* storage = "all_variables";
                        size_t byte_length = sizeof(double);
                        const char* struct_field_name = nullptr;

                        size_t str_off = 0, str_len = 0;
                        if (get_string_field(kv.first, str_off, str_len, struct_field_name)) {
                            byte_offset = str_off;
                            data_type = "string";
                            storage = "struct_field";
                            byte_length = str_len;
                        } else {
                            size_t vec_off = 0, vec_len = 0;
                            if (get_vector3d_field(kv.first, vec_off, vec_len, struct_field_name)) {
                                byte_offset = vec_off;
                                data_type = "vector3d";
                                storage = "struct_field";
                                byte_length = vec_len;
                            } else if (get_vector2d_field(kv.first, vec_off, vec_len, struct_field_name)) {
                                byte_offset = vec_off;
                                data_type = "vector2d";
                                storage = "struct_field";
                                byte_length = vec_len;
                            } else if (is_vector4d_message(kv.first)) {
                                // Known Vector4d messages not persisted in struct
                                data_type = "vector4d";
                                storage = "message_only";
                                byte_offset = 0;
                                byte_length = sizeof(double) * 4;
                            }
                        }

                        ofs << "    {\n";
                        ofs << "      \"name\": \"" << kv.first << "\",\n";
                        ofs << "      \"group\": \"" << group << "\",\n";
                        ofs << "      \"logical_index\": " << logical << ",\n";
                        ofs << "      \"data_type\": \"" << data_type << "\",\n";
                        ofs << "      \"storage\": \"" << storage << "\",\n";
                        if (struct_field_name) {
                            ofs << "      \"struct_field_name\": \"" << struct_field_name << "\",\n";
                        }
                        ofs << "      \"byte_offset\": " << byte_offset << ",\n";
                        ofs << "      \"byte_length\": " << byte_length;

                        // Add component order for vector types
                        if (std::string(data_type) == std::string("vector3d")) {
                            ofs << ",\n      \"component_order\": [\"x\",\"y\",\"z\"]";
                        } else if (std::string(data_type) == std::string("vector2d")) {
                            ofs << ",\n      \"component_order\": [\"x\",\"y\"]";
                        } else if (std::string(data_type) == std::string("vector4d")) {
                            ofs << ",\n      \"component_order\": [\"x\",\"y\",\"z\",\"w\"]";
                        }

                        // Add exact unit from MESSAGE_LIST
                        auto uit = unit_map.find(kv.first);
                        if (uit != unit_map.end()) {
                            ofs << ",\n      \"unit\": \"" << unit_to_string(uit->second) << "\"";
                        }

                        // Add message id (hash) from MESSAGE_LIST
                        auto iit = id_map.find(kv.first);
                        if (iit != id_map.end()) {
                            ofs << ",\n      \"message_id\": " << iit->second;
                        }

                        // Add access and primary flag from MESSAGE_LIST
                        auto ait = access_map.find(kv.first);
                        if (ait != access_map.end()) {
                            ofs << ",\n      \"access\": \"" << access_to_string(ait->second) << "\"";
                        }
                        auto fit = flag_map.find(kv.first);
                        if (fit != flag_map.end()) {
                            ofs << ",\n      \"flag\": \"" << flag_to_string(fit->second) << "\"";
                            // Derived convenience booleans
                            const char* fstr = flag_to_string(fit->second);
                            ofs << ",\n      \"is_event\": " << (std::string(fstr) == "event" ? "true" : "false");
                            ofs << ",\n      \"is_toggle\": " << (std::string(fstr) == "toggle" ? "true" : "false");
                            ofs << ",\n      \"is_active_flag\": " << (std::string(fstr) == "active" ? "true" : "false");
                            ofs << ",\n      \"is_value\": " << (std::string(fstr) == "value" ? "true" : "false");
                        }

                        ofs << "\n";
                        ofs << "    }" << (i + 1 < (int)items.size() ? ",\n" : "\n");
                    }

                    ofs << "  ]\n";
                    ofs << "}\n";
                }
            }
        } catch (...) {
            // ignore export errors
        }

        return true;
    }
    
    /**
     * @brief Updates bridge state and processes messages.
     * 
     * This method:
     * 1. Updates shared memory with received messages
     * 2. Broadcasts current data to TCP clients (if any)
     * 3. Processes pending commands
     * 4. Returns generated response messages
     * 
     * @param received_messages Messages from Aerofly to process
     * @param delta_time Time elapsed since last update
     * @param sent_messages Vector to store response messages
     */
    /**
     * Runs one bridge tick:
     * - Writes received Aerofly messages into shared memory
     * - Broadcasts current telemetry over TCP and WebSocket
     * - Drains both interfaces' command queues and converts to Aerofly messages
     */
    void Update(const std::vector<tm_external_message>& received_messages, double delta_time,
                std::vector<tm_external_message>& sent_messages) {
        if (!initialized) return;
        
        // Update shared memory with latest data
        shared_memory.UpdateData(received_messages, delta_time);
        
        // Broadcast data via TCP (if clients connected)
        if (tcp_server.GetClientCount() > 0) {
            tcp_server.BroadcastData(shared_memory.GetData());
        }
        
        // Broadcast via WebSocket (if enabled and clients connected)
        if (ws_enabled && ws_server.GetClientCount() > 0) {
            ws_server.BroadcastData(shared_memory.GetData());
        }
        
        // Process any pending commands (merge TCP + WS)
        auto commands = tcp_server.GetPendingCommands();
        if (ws_enabled) {
            auto ws_commands = ws_server.GetPendingCommands();
            commands.insert(commands.end(), ws_commands.begin(), ws_commands.end());
        }
        if (!commands.empty()) {
            auto command_messages = command_processor.ProcessCommands(commands);
            sent_messages.insert(sent_messages.end(), command_messages.begin(), command_messages.end());
            
            // Process Step controls internally (doors, windows, etc.)
            for (const auto& msg : command_messages) {
                if (msg.GetFlags().IsSet(tm_msg_flag::Step)) {
                    shared_memory.ProcessMessage(msg);
                }
            }
        }
    }
    
    /**
     * @brief Performs clean shutdown of all bridge components.
     * 
     * Shutdown sequence:
     * 1. Stops TCP server first (most problematic threads)
     * 2. Cleans up shared memory
     * 3. Marks bridge as uninitialized
     * 
     * @note This method is safe to call multiple times
     */
    // Performs orderly shutdown of all components. Stops WebSocket and TCP
    // servers, then cleans shared memory. Safe to call multiple times.
    void Shutdown() {
        DBG("=== AeroflyBridge::Shutdown() STARTED ===\n");
        
        if (!initialized) {
            DBG("Bridge already closed\n");
            return;
        }
        
        // Stop WebSocket server first for clean shutdown order
        if (ws_enabled) {
            ws_server.Stop();
        }
        // Stop TCP server FIRST (most problematic threads)
        DBG("Stopping TCP server...\n");
        tcp_server.Stop();
        
        // Clean shared memory
        DBG("Cleaning shared memory...\n");
        shared_memory.Cleanup();
        
        initialized = false;
        DBG("=== AeroflyBridge::Shutdown() COMPLETED ===\n");
    }
    
    /**
     * @brief Checks if bridge is fully initialized.
     * @return true if bridge is initialized and ready
     * @return false if bridge is not initialized
     */
    bool IsInitialized() const { return initialized; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// GLOBAL INSTANCE
///////////////////////////////////////////////////////////////////////////////////////////////////

static AeroflyBridge* g_bridge = nullptr;
static std::vector<tm_external_message> MessageListReceive;

///////////////////////////////////////////////////////////////////////////////////////////////////
// AEROFLY FS4 DLL INTERFACE
///////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {
    __declspec(dllexport) int Aerofly_FS_4_External_DLL_GetInterfaceVersion() {
        return TM_DLL_INTERFACE_VERSION;
    }

    __declspec(dllexport) bool Aerofly_FS_4_External_DLL_Init(const HINSTANCE Aerofly_FS_4_hInstance) {
        try {
            g_bridge = new AeroflyBridge();
            return g_bridge->Initialize();
        }
        catch (...) {
            return false;
        }
    }

    __declspec(dllexport) void Aerofly_FS_4_External_DLL_Shutdown() {
        OutputDebugStringA("=== DLL SHUTDOWN STARTED ===\n");
        
        try {
            if (g_bridge) {
                OutputDebugStringA("Closing bridge...\n");
                
                // Bridge shutdown
                g_bridge->Shutdown();
                OutputDebugStringA("Deleting bridge object...\n");
                delete g_bridge;
                g_bridge = nullptr;
            }
            
            OutputDebugStringA("=== DLL SHUTDOWN COMPLETED SUCCESSFULLY ===\n");
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("ERROR in shutdown: " + std::string(e.what()) + "\n").c_str());
        }
        catch (...) {
            OutputDebugStringA("Unknown ERROR in shutdown\n");
        }
        
        // NEVER throw exceptions from DLL shutdown
    }

    __declspec(dllexport) void Aerofly_FS_4_External_DLL_Update(
        const tm_double delta_time,
        const tm_uint8* const message_list_received_byte_stream,
        const tm_uint32 message_list_received_byte_stream_size,
        const tm_uint32 message_list_received_num_messages,
        tm_uint8* message_list_sent_byte_stream,
        tm_uint32& message_list_sent_byte_stream_size,
        tm_uint32& message_list_sent_num_messages,
        const tm_uint32 message_list_sent_byte_stream_size_max) {
        
        if (!g_bridge || !g_bridge->IsInitialized()) {
            message_list_sent_byte_stream_size = 0;
            message_list_sent_num_messages = 0;
            return;
        }

        try {
            // Parse received messages
            MessageListReceive.clear();
            tm_uint32 pos = 0;
            for (tm_uint32 i = 0; i < message_list_received_num_messages; ++i) {
                auto msg = tm_external_message::GetFromByteStream(message_list_received_byte_stream, pos);
                MessageListReceive.emplace_back(msg);
            }

            // Process messages and get commands to send back
            std::vector<tm_external_message> sent_messages;
            g_bridge->Update(MessageListReceive, delta_time, sent_messages);

            // Build response message list
            message_list_sent_byte_stream_size = 0;
            message_list_sent_num_messages = 0;

            for (const auto& msg : sent_messages) {
                msg.AddToByteStream(message_list_sent_byte_stream, 
                                   message_list_sent_byte_stream_size, 
                                   message_list_sent_num_messages);
            }
        }
        catch (...) {
            // Error handling - ensure we don't crash Aerofly
            message_list_sent_byte_stream_size = 0;
            message_list_sent_num_messages = 0;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// AEROFLY FS4 BRIDGE - BUILD AND DEPLOYMENT GUIDE
//
// This DLL provides a multi-interface bridge between Aerofly FS4 and external
// applications, enabling real-time data exchange and remote control.
///////////////////////////////////////////////////////////////////////////////////////////////////

    /**
     * @section Build Instructions
     * 
     * Prerequisites:
     * - Microsoft Visual Studio 2022 (C++), Windows SDK
     * - tm_external_message.h (Aerofly SDK header) placed next to this file
     * 
     * Options:
     * - Recommended: run compile.bat (adds proper flags and outputs AeroflyBridge.dll)
     * - Manual example (x64 Native Tools Prompt for VS 2022):
     *   cl.exe /LD /EHsc /O2 /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL \
     *        aerofly_bridge_dll.cpp \
     *        /Fe:AeroflyBridge.dll \
     *        /link ws2_32.lib kernel32.lib user32.lib
     * 
     * Deploy:
     * - Copy AeroflyBridge.dll to:
     *   %USERPROFILE%\Documents\Aerofly FS 4\external_dll\AeroflyBridge.dll
     */

/**
 * @section Core Features
 * 
 * ✅ Multi-Interface (simultaneous):
 *    - Shared Memory: ultra-fast local access
 *    - TCP Server: JSON over sockets (data+command)
 *    - WebSocket Server: native browser/mobile connectivity
 * 
 * ✅ JSON Compatibility:
 *    - Same JSON payload for TCP and WebSocket (single builder)
 * 
 * ✅ 339+ Variables:
 *    - Full read/write coverage, organized by subsystem
 * 
 * ✅ Robust & Performant:
 *    - Thread-safe, non-blocking I/O, graceful fallbacks
 */

/**
 * @section Network Configuration
 * 
 * TCP (Winsock):
 * - Port 12345: Real-time data stream (JSON)
 * - Port 12346: Command channel (JSON, one command per connection)
 * 
 * WebSocket (Winsock, no external deps):
 * - Port 8765 (default): Bidirectional JSON, browser/mobile friendly
 * - Env: AEROFLY_BRIDGE_WS_ENABLE=1 (default), AEROFLY_BRIDGE_WS_PORT=8765
 * 
 * Shared Memory:
 * - Name: "AeroflyBridgeData"
 * - Access: MapViewOfFile() for direct reads/writes
 */

/**
 * @section Environment Variables
 * 
 * - AEROFLY_BRIDGE_WS_ENABLE     : 1 to enable WebSocket (default 1)
 * - AEROFLY_BRIDGE_WS_PORT       : WebSocket port (default 8765)
 * - AEROFLY_BRIDGE_BROADCAST_MS  : Telemetry broadcast interval in ms (default 20)
 */
 
///////////////////////////////////////////////////////////////////////////////////////////////////