# aerofly_master_control_panel.py
import tkinter as tk
from tkinter import ttk
import socket
import json

class AeroflyMasterControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Aerofly FS4 - Master Control Panel")
        self.root.geometry("1920x1080")
        self.root.configure(bg='#f0f0f0')
        
        self.setup_gui()
        
    def setup_gui(self):
        """Setup the complete control panel"""
        
        # Status label
        self.status_label = tk.Label(self.root, text="Ready - All Controls Available", 
                                   font=('Arial', 12, 'bold'), 
                                   fg='green', bg='#f0f0f0')
        self.status_label.pack(pady=5)
        
        # Main frame with scrollbar
        main_frame = tk.Frame(self.root, bg='#f0f0f0')
        main_frame.pack(fill='both', expand=True, padx=10, pady=5)
        
        # Create canvas and scrollbar
        canvas = tk.Canvas(main_frame, bg='#f0f0f0')
        scrollbar = ttk.Scrollbar(main_frame, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas, bg='#f0f0f0')
        
        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        # Pack canvas and scrollbar
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        # Configure grid - 6 columns for optimal space usage
        for i in range(6):
            scrollable_frame.grid_columnconfigure(i, weight=1)
        
        # === ALL CONTROL SECTIONS ===
        current_row = 0
        
        # Row 1: Flight Controls + Throttle + Aircraft Systems
        current_row = self.create_flight_controls(scrollable_frame, 0, 0)
        self.create_throttle_controls(scrollable_frame, 0, 1)
        self.create_aircraft_systems(scrollable_frame, 0, 2)
        
        # Row 2: Engine Controls + Brakes/Steering + Navigation
        self.create_engine_controls(scrollable_frame, 0, 3)
        self.create_brakes_steering(scrollable_frame, 0, 4)  
        self.create_navigation_controls(scrollable_frame, 0, 5)
        
        # Row 3: Autopilot + Helicopter + Trim + Simulation
        current_row = max(current_row, 25)  # Start next row
        self.create_autopilot_controls(scrollable_frame, current_row, 0)
        self.create_helicopter_controls(scrollable_frame, current_row, 1)
        self.create_trim_controls(scrollable_frame, current_row, 2)
        self.create_simulation_controls(scrollable_frame, current_row, 3)
        
        # Row 4: View Controls + Warning + Performance + Commands
        current_row = max(current_row + 22, 47)  # Start next row
        self.create_view_controls(scrollable_frame, current_row, 0)
        self.create_warning_controls(scrollable_frame, current_row, 1)
        self.create_performance_controls(scrollable_frame, current_row, 2)
        self.create_command_controls(scrollable_frame, current_row, 3)

        # Row 5: C172 Specific Controls
        current_row = max(current_row + 22, 69)  # Start next row
        self.create_c172_specific_controls(scrollable_frame, current_row, 0)
        self.create_c172_doors_windows(scrollable_frame, current_row, 1)
        
        # Bind mousewheel to canvas
        def _on_mousewheel(event):
            canvas.yview_scroll(int(-1*(event.delta/120)), "units")
        canvas.bind_all("<MouseWheel>", _on_mousewheel)
    
    def create_flight_controls(self, parent, row, col):
        """Flight Controls Section"""
        frame = tk.LabelFrame(parent, text="🕹️ FLIGHT CONTROLS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("Pitch UP", "Controls.Pitch.Input", -0.5),
            ("Pitch DOWN", "Controls.Pitch.Input", 0.5),
            ("Pitch NEUTRAL", "Controls.Pitch.Input", 0.0),
            ("Roll LEFT", "Controls.Roll.Input", -0.5),
            ("Roll RIGHT", "Controls.Roll.Input", 0.5),
            ("Roll NEUTRAL", "Controls.Roll.Input", 0.0),
            ("Yaw LEFT", "Controls.Yaw.Input", -0.5),
            ("Yaw RIGHT", "Controls.Yaw.Input", 0.5),
            ("Yaw NEUTRAL", "Controls.Yaw.Input", 0.0),
            ("Pitch Full UP", "Controls.Pitch.Input", -1.0),
            ("Pitch Full DOWN", "Controls.Pitch.Input", 1.0),
            ("Pitch Offset", "Controls.Pitch.InputOffset", 0.1),
            ("Roll Offset", "Controls.Roll.InputOffset", 0.1),
            ("Yaw Active", "Controls.Yaw.InputActive", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
        
        return row + len(controls)//2 + 1
    
    def create_throttle_controls(self, parent, row, col):
        """Throttle Controls Section"""
        frame = tk.LabelFrame(parent, text="🔥 THROTTLE & POWER", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("THROTTLE 0%", "Controls.Throttle", 0.0),
            ("THROTTLE 25%", "Controls.Throttle", 0.25),
            ("THROTTLE 50%", "Controls.Throttle", 0.5),
            ("THROTTLE 75%", "Controls.Throttle", 0.75),
            ("THROTTLE 100%", "Controls.Throttle", 1.0),
            ("THR1 0%", "Controls.Throttle1", 0.0),
            ("THR1 50%", "Controls.Throttle1", 0.5),
            ("THR1 100%", "Controls.Throttle1", 1.0),
            ("THR2 0%", "Controls.Throttle2", 0.0),
            ("THR2 50%", "Controls.Throttle2", 0.5),
            ("THR2 100%", "Controls.Throttle2", 1.0),
            ("THR3 50%", "Controls.Throttle3", 0.5),
            ("THR4 50%", "Controls.Throttle4", 0.5),
            ("THR1 Move", "Controls.Throttle1Move", 0.1),
            ("THR2 Move", "Controls.Throttle2Move", 0.1)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_aircraft_systems(self, parent, row, col):
        """Aircraft Systems Section"""
        frame = tk.LabelFrame(parent, text="⚙️ AIRCRAFT SYSTEMS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("GEAR UP", "Controls.Gear", 0.0),
            ("GEAR DOWN", "Controls.Gear", 1.0),
            ("GEAR TOGGLE", "Controls.GearToggle", 1.0),
            ("FLAPS UP", "Controls.Flaps", 0.0),
            ("FLAPS 1", "Controls.Flaps", 0.25),
            ("FLAPS 2", "Controls.Flaps", 0.5),
            ("FLAPS 3", "Controls.Flaps", 0.75),
            ("FLAPS FULL", "Controls.Flaps", 1.0),
            ("FLAPS EVENT", "Controls.FlapsEvent", 1.0),
            ("AIR BRAKE OFF", "Controls.AirBrake", 0.0),
            ("AIR BRAKE 50%", "Controls.AirBrake", 0.5),
            ("AIR BRAKE FULL", "Controls.AirBrake", 1.0),
            ("AIR BRAKE ACTIVE", "Controls.AirBrakeActive", 1.0),
            ("AIR BRAKE ARM", "Controls.AirBrake.Arm", 1.0),
            ("GLIDER BRAKE", "Controls.GliderAirBrake", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_engine_controls(self, parent, row, col):
        """Engine Controls Section"""
        frame = tk.LabelFrame(parent, text="🔧 ENGINE CONTROLS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("ENG1 MASTER ON", "Aircraft.EngineMaster1", 1.0),
            ("ENG1 MASTER OFF", "Aircraft.EngineMaster1", 0.0),
            ("ENG2 MASTER ON", "Aircraft.EngineMaster2", 1.0),
            ("ENG2 MASTER OFF", "Aircraft.EngineMaster2", 0.0),
            ("ENG1 STARTER", "Aircraft.Starter1", 1.0),
            ("ENG2 STARTER", "Aircraft.Starter2", 1.0),
            ("ENG1 IGNITION ON", "Aircraft.Ignition1", 1.0),
            ("ENG1 IGNITION OFF", "Aircraft.Ignition1", 0.0),
            ("ENG2 IGNITION ON", "Aircraft.Ignition2", 1.0),
            ("ENG2 IGNITION OFF", "Aircraft.Ignition2", 0.0),
            ("MIXTURE1 RICH", "Controls.Mixture1", 1.0),
            ("MIXTURE1 LEAN", "Controls.Mixture1", 0.5),
            ("MIXTURE1 CUTOFF", "Controls.Mixture1", 0.0),
            ("MIXTURE2 RICH", "Controls.Mixture2", 1.0),
            ("PROP1 HIGH", "Controls.PropellerSpeed1", 1.0),
            ("PROP2 HIGH", "Controls.PropellerSpeed2", 1.0),
            ("REVERSE1 ON", "Controls.ThrustReverse1", 1.0),
            ("REVERSE2 ON", "Controls.ThrustReverse2", 1.0),
            ("APU AVAILABLE", "Aircraft.APUAvailable", 1.0),
            ("THROTTLE LIMIT", "Aircraft.ThrottleLimit", 0.8)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_brakes_steering(self, parent, row, col):
        """Brakes & Steering Section"""
        frame = tk.LabelFrame(parent, text="🛑 BRAKES & STEERING", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("LEFT BRAKE 0%", "Controls.WheelBrake.Left", 0.0),
            ("LEFT BRAKE 50%", "Controls.WheelBrake.Left", 0.5),
            ("LEFT BRAKE 100%", "Controls.WheelBrake.Left", 1.0),
            ("RIGHT BRAKE 0%", "Controls.WheelBrake.Right", 0.0),
            ("RIGHT BRAKE 50%", "Controls.WheelBrake.Right", 0.5),
            ("RIGHT BRAKE 100%", "Controls.WheelBrake.Right", 1.0),
            ("LEFT BRAKE ACTIVE", "Controls.WheelBrake.LeftActive", 1.0),
            ("RIGHT BRAKE ACTIVE", "Controls.WheelBrake.RightActive", 1.0),
            ("PARKING BRAKE ON", "Aircraft.ParkingBrake", 1.0),
            ("PARKING BRAKE OFF", "Aircraft.ParkingBrake", 0.0),
            ("TILLER CENTER", "Controls.Tiller", 0.0),
            ("TILLER LEFT", "Controls.Tiller", -0.5),
            ("TILLER RIGHT", "Controls.Tiller", 0.5),
            ("NOSE WHEEL STEER", "Controls.NoseWheelSteering", 1.0),
            ("PEDALS DISCONNECT", "Controls.PedalsDisconnect", 1.0),
            ("PEDALS CONNECT", "Controls.PedalsDisconnect", 0.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_navigation_controls(self, parent, row, col):
        """Navigation Controls Section"""
        frame = tk.LabelFrame(parent, text="🧭 NAVIGATION & COMM", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("COM1 121.5", "Communication.COM1Frequency", 121500000),
            ("COM1 122.8", "Communication.COM1Frequency", 122800000),
            ("COM1 SWAP", "Communication.COM1FrequencySwap", 1.0),
            ("COM2 119.1", "Communication.COM2Frequency", 119100000),
            ("COM2 SWAP", "Communication.COM2FrequencySwap", 1.0),
            ("NAV1 108.5", "Navigation.NAV1Frequency", 108500000),
            ("NAV1 110.5", "Navigation.NAV1Frequency", 110500000),
            ("NAV1 SWAP", "Navigation.NAV1FrequencySwap", 1.0),
            ("NAV2 109.7", "Navigation.NAV2Frequency", 109700000),
            ("NAV2 SWAP", "Navigation.NAV2FrequencySwap", 1.0),
            ("NAV1 Course 0°", "Navigation.SelectedCourse1", 0.0),
            ("NAV1 Course 90°", "Navigation.SelectedCourse1", 1.57),
            ("NAV1 Course 180°", "Navigation.SelectedCourse1", 3.14),
            ("NAV2 Course 90°", "Navigation.SelectedCourse2", 1.57),
            ("ILS1 Freq", "Navigation.ILS1Frequency", 109500000),
            ("ILS1 Course", "Navigation.ILS1Course", 1.57),
            ("ADF1 Freq", "Navigation.ADF1Frequency", 350000),
            ("ADF1 SWAP", "Navigation.ADF1FrequencySwap", 1.0),
            ("XPDR 1200", "Communication.TransponderCode", 1200),
            ("XPDR 7000", "Communication.TransponderCode", 7000)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_autopilot_controls(self, parent, row, col):
        """Autopilot Controls Section"""
        frame = tk.LabelFrame(parent, text="🤖 AUTOPILOT", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("AP MASTER ON", "Autopilot.Master", 1.0),
            ("AP MASTER OFF", "Autopilot.Master", 0.0),
            ("AP ENGAGE", "Autopilot.Engaged", 1.0),
            ("AP DISENGAGE", "Autopilot.Disengage", 1.0),
            ("AP HDG MODE", "Autopilot.Heading", 1.0),
            ("AP VS MODE", "Autopilot.VerticalSpeed", 1.0),
            ("AP Speed 50", "Autopilot.SelectedAirspeed", 50),
            ("AP Speed 80", "Autopilot.SelectedAirspeed", 80),
            ("AP Speed 100", "Autopilot.SelectedAirspeed", 100),
            ("AP Hdg 0°", "Autopilot.SelectedHeading", 0.0),
            ("AP Hdg 90°", "Autopilot.SelectedHeading", 1.57),
            ("AP Hdg 180°", "Autopilot.SelectedHeading", 3.14),
            ("AP Alt 500m", "Autopilot.SelectedAltitude", 500),
            ("AP Alt 1000m", "Autopilot.SelectedAltitude", 1000),
            ("AP Alt 3000m", "Autopilot.SelectedAltitude", 3000),
            ("AP VS +5", "Autopilot.SelectedVerticalSpeed", 5),
            ("AP VS 0", "Autopilot.SelectedVerticalSpeed", 0),
            ("AP VS -5", "Autopilot.SelectedVerticalSpeed", -5),
            ("AUTOTHROTTLE ON", "Autopilot.ThrottleEngaged", 1.0),
            ("AUTOTHROTTLE OFF", "Autopilot.ThrottleEngaged", 0.0),
            ("AP Use MACH", "Autopilot.UseMachNumber", 1.0),
            ("AP Speed Managed", "Autopilot.SpeedManaged", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_helicopter_controls(self, parent, row, col):
        """Helicopter Controls Section"""
        frame = tk.LabelFrame(parent, text="🚁 HELICOPTER", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("COLLECTIVE DOWN", "Controls.Collective", 0.2),
            ("COLLECTIVE MID", "Controls.Collective", 0.5),
            ("COLLECTIVE UP", "Controls.Collective", 0.8),
            ("CYCLIC CENTER", "Controls.CyclicPitch", 0.0),
            ("CYCLIC FWD", "Controls.CyclicPitch", 0.3),
            ("CYCLIC AFT", "Controls.CyclicPitch", -0.3),
            ("CYCLIC NEUTRAL", "Controls.CyclicRoll", 0.0),
            ("CYCLIC LEFT", "Controls.CyclicRoll", -0.3),
            ("CYCLIC RIGHT", "Controls.CyclicRoll", 0.3),
            ("TAIL ROTOR 0", "Controls.TailRotor", 0.0),
            ("TAIL ROTOR L", "Controls.TailRotor", -0.5),
            ("TAIL ROTOR R", "Controls.TailRotor", 0.5),
            ("ROTOR BRAKE OFF", "Controls.RotorBrake", 0.0),
            ("ROTOR BRAKE ON", "Controls.RotorBrake", 1.0),
            ("HELI THR1 50%", "Controls.HelicopterThrottle1", 0.5),
            ("HELI THR1 75%", "Controls.HelicopterThrottle1", 0.75),
            ("HELI THR2 50%", "Controls.HelicopterThrottle2", 0.5),
            ("HELI THR2 75%", "Controls.HelicopterThrottle2", 0.75)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_trim_controls(self, parent, row, col):
        """Trim Controls Section"""
        frame = tk.LabelFrame(parent, text="✂️ TRIM & AIDS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("AILERON TRIM L", "Controls.AileronTrim", -0.1),
            ("AILERON TRIM 0", "Controls.AileronTrim", 0.0),
            ("AILERON TRIM R", "Controls.AileronTrim", 0.1),
            ("RUDDER TRIM L", "Controls.RudderTrim", -0.1),
            ("RUDDER TRIM 0", "Controls.RudderTrim", 0.0),
            ("RUDDER TRIM R", "Controls.RudderTrim", 0.1),
            ("TRIM STEP", "Controls.TrimStep", 0.1),
            ("TRIM MOVE", "Controls.TrimMove", 0.1),
            ("YAW DAMPER OFF", "Aircraft.YawDamperEnabled", 0.0),
            ("YAW DAMPER ON", "Aircraft.YawDamperEnabled", 1.0),
            ("AUTO PITCH TRIM", "Aircraft.AutoPitchTrim", 1.0),
            ("PITCH TRIM", "Aircraft.PitchTrim", 0.1),
            ("RUDDER TRIM", "Aircraft.RudderTrim", 0.1)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_simulation_controls(self, parent, row, col):
        """Simulation Controls Section"""
        frame = tk.LabelFrame(parent, text="🎮 SIMULATION", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("PAUSE OFF", "Simulation.Pause", 0.0),
            ("PAUSE ON", "Simulation.Pause", 1.0),
            ("LIFT AIRCRAFT", "Simulation.LiftUp", 1.0),
            ("SOUND ON", "Simulation.Sound", 1.0),
            ("SOUND OFF", "Simulation.Sound", 0.0),
            ("FLIGHT INFO", "Simulation.FlightInformation", 1.0),
            ("MOVING MAP", "Simulation.MovingMap", 1.0),
            ("MOUSE CONTROL", "Simulation.UseMouseControl", 1.0),
            ("TIME CHANGE", "Simulation.TimeChange", 1.0),
            ("VISIBILITY MAX", "Simulation.Visibility", 1.0),
            ("VISIBILITY MIN", "Simulation.Visibility", 0.1),
            ("PLAYBACK START", "Simulation.PlaybackStart", 1.0),
            ("PLAYBACK STOP", "Simulation.PlaybackStop", 1.0),
            ("SETTING SET", "Simulation.SettingSet", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_view_controls(self, parent, row, col):
        """View Controls Section"""
        frame = tk.LabelFrame(parent, text="📹 VIEW CONTROLS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("VIEW INTERNAL", "View.Internal", 1.0),
            ("VIEW EXTERNAL", "View.External", 1.0),
            ("VIEW FOLLOW", "View.Follow", 1.0),
            ("VIEW CATEGORY", "View.Category", 1.0),
            ("VIEW MODE", "View.Mode", 1.0),
            ("ZOOM IN", "View.Zoom", 0.5),
            ("ZOOM OUT", "View.Zoom", 2.0),
            ("PAN LEFT", "View.Pan.Horizontal", -0.5),
            ("PAN RIGHT", "View.Pan.Horizontal", 0.5),
            ("PAN UP", "View.Pan.Vertical", 0.5),
            ("PAN DOWN", "View.Pan.Vertical", -0.5),
            ("PAN CENTER", "View.Pan.Center", 1.0),
            ("LOOK LEFT", "View.Look.Horizontal", -0.5),
            ("LOOK RIGHT", "View.Look.Horizontal", 0.5),
            ("LOOK UP", "View.Look.Vertical", 0.5),
            ("LOOK DOWN", "View.Look.Vertical", -0.5),
            ("OFFSET X+", "View.OffsetX", 0.5),
            ("OFFSET X-", "View.OffsetX", -0.5),
            ("OFFSET Y+", "View.OffsetY", 0.5),
            ("OFFSET Z+", "View.OffsetZ", 0.5)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_warning_controls(self, parent, row, col):
        """Warning Controls Section"""
        frame = tk.LabelFrame(parent, text="⚠️ WARNINGS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("MASTER WARNING", "Warnings.MasterWarning", 1.0),
            ("MASTER CAUTION", "Warnings.MasterCaution", 1.0),
            ("ENGINE FIRE", "Warnings.EngineFire", 1.0),
            ("LOW OIL PRESS", "Warnings.LowOilPressure", 1.0),
            ("LOW FUEL PRESS", "Warnings.LowFuelPressure", 1.0),
            ("LOW HYDRAULIC", "Warnings.LowHydraulicPressure", 1.0),
            ("LOW VOLTAGE", "Warnings.LowVoltage", 1.0),
            ("ALTITUDE ALERT", "Warnings.AltitudeAlert", 1.0),
            ("WARNING ACTIVE", "Warnings.WarningActive", 1.0),
            ("WARNING MUTE", "Warnings.WarningMute", 1.0),
            ("PRESSURIZATION", "Pressurization.LandingElevation", 100.0),
                        ("PRESS MANUAL", "Pressurization.LandingElevationManual", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_performance_controls(self, parent, row, col):
        """Performance Controls Section"""
        frame = tk.LabelFrame(parent, text="📊 PERFORMANCE", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("VS0 = 25 m/s", "Performance.Speed.VS0", 25.0),
            ("VS1 = 30 m/s", "Performance.Speed.VS1", 30.0),
            ("VFE = 45 m/s", "Performance.Speed.VFE", 45.0),
            ("VNO = 75 m/s", "Performance.Speed.VNO", 75.0),
            ("VNE = 90 m/s", "Performance.Speed.VNE", 90.0),
            ("VAPP = 35 m/s", "Performance.Speed.VAPP", 35.0),
            ("Speed Min", "Performance.Speed.Minimum", 20.0),
            ("Speed Max", "Performance.Speed.Maximum", 100.0),
            ("Min Flap Retract", "Performance.Speed.MinimumFlapRetraction", 40.0),
            ("Max Flap Extend", "Performance.Speed.MaximumFlapExtension", 50.0),
            ("TO Flaps", "Configuration.SelectedTakeOffFlaps", 0.25),
            ("Landing Flaps", "Configuration.SelectedLandingFlaps", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    
    def create_command_controls(self, parent, row, col):
        """Command Controls Section"""
        frame = tk.LabelFrame(parent, text="🎯 COMMANDS", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            ("CMD EXECUTE", "Command.Execute", 1.0),
            ("CMD BACK", "Command.Back", 1.0),
            ("CMD UP", "Command.Up", 1.0),
            ("CMD DOWN", "Command.Down", 1.0),
            ("CMD LEFT", "Command.Left", 1.0),
            ("CMD RIGHT", "Command.Right", 1.0),
            ("MOVE H+", "Command.MoveHorizontal", 0.5),
            ("MOVE H-", "Command.MoveHorizontal", -0.5),
            ("MOVE V+", "Command.MoveVertical", 0.5),
            ("MOVE V-", "Command.MoveVertical", -0.5),
            ("ROTATE CW", "Command.Rotate", 0.5),
            ("ROTATE CCW", "Command.Rotate", -0.5),
            ("ZOOM IN", "Command.Zoom", 0.5),
            ("ZOOM OUT", "Command.Zoom", 2.0),
            ("LIGHTING PANEL", "Controls.Lighting.Panel", 1.0),
            ("LIGHTING INSTR", "Controls.Lighting.Instruments", 1.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                          command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)
    

    def create_c172_specific_controls(self, parent, row, col):
        """Cessna 172 Specific Controls Section"""
        frame = tk.LabelFrame(parent, text="🛩️ CESSNA 172 SPECIFIC", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            # === FUEL SYSTEM ===
            ("FUEL SEL OFF", "Controls.FuelSelector", 0.0),
            ("FUEL SEL LEFT", "Controls.FuelSelector", 1.0),
            ("FUEL SEL RIGHT", "Controls.FuelSelector", 2.0),
            ("FUEL SEL BOTH", "Controls.FuelSelector", 3.0),
            ("FUEL SHUTOFF", "Controls.FuelShutOff", 1.0),
            
            # === COCKPIT VISIBILITY ===
            ("HIDE YOKE LEFT", "Controls.HideYoke.Left", 1.0),
            ("SHOW YOKE LEFT", "Controls.HideYoke.Left", 0.0),
            ("HIDE YOKE RIGHT", "Controls.HideYoke.Right", 1.0),
            ("SHOW YOKE RIGHT", "Controls.HideYoke.Right", 0.0),
            
            # === SUN VISORS ===
            ("L SUN VISOR UP", "Controls.LeftSunBlocker", 0.0),
            ("L SUN VISOR DOWN", "Controls.LeftSunBlocker", 1.0),
            ("R SUN VISOR UP", "Controls.RightSunBlocker", 0.0),
            ("R SUN VISOR DOWN", "Controls.RightSunBlocker", 1.0),
            
            # === CABIN LIGHTING ===
            ("L CABIN LIGHT ON", "Controls.Lighting.LeftCabinOverheadLight", 1.0),
            ("L CABIN LIGHT OFF", "Controls.Lighting.LeftCabinOverheadLight", 0.0),
            ("R CABIN LIGHT ON", "Controls.Lighting.RightCabinOverheadLight", 1.0),
            ("R CABIN LIGHT OFF", "Controls.Lighting.RightCabinOverheadLight", 0.0),
            
            # === ENGINE CONTROLS ===
            ("MAGNETOS OFF", "Controls.Magnetos1", 0.0),
            ("MAGNETOS RIGHT", "Controls.Magnetos1", 1.0),
            ("MAGNETOS LEFT", "Controls.Magnetos1", 2.0),
            ("MAGNETOS BOTH", "Controls.Magnetos1", 3.0),
            ("MAGNETOS START", "Controls.Magnetos1", 4.0),
            
            # === YOKE CONTROLS ===
            ("YOKE PTT PRESS", "LeftYoke.Button", 1.0),
            ("YOKE PTT RELEASE", "LeftYoke.Button", 0.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                        command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)

    def create_c172_doors_windows(self, parent, row, col):
        """Cessna 172 Doors and Windows Section - STEP CONTROLS"""
        frame = tk.LabelFrame(parent, text="🚪 C172 DOORS & WINDOWS (STEP)", font=('Arial', 10, 'bold'), bg='#f0f0f0')
        frame.grid(row=row, column=col, sticky='nsew', padx=3, pady=3)
        
        controls = [
            # === DOORS (STEP CONTROLS) ===
            ("L DOOR OPEN +", "Doors.Left", 0.01),       # Step forward
            ("L DOOR CLOSE -", "Doors.Left", -0.01),     # Step backward
            ("L DOOR RESET", "Doors.Left", 0.0),         # Absolute reset
            ("L HANDLE +", "Doors.LeftHandle", 0.01),
            ("L HANDLE -", "Doors.LeftHandle", -0.01),
            
            ("R DOOR OPEN +", "Doors.Right", 0.01),
            ("R DOOR CLOSE -", "Doors.Right", -0.01),
            ("R DOOR RESET", "Doors.Right", 0.0),
            ("R HANDLE +", "Doors.RightHandle", 0.01),
            ("R HANDLE -", "Doors.RightHandle", -0.01),
            
            # === WINDOWS (STEP CONTROLS) ===
            ("L WINDOW OPEN +", "Windows.Left", 0.01),
            ("L WINDOW CLOSE -", "Windows.Left", -0.01), 
            ("L WINDOW RESET", "Windows.Left", 0.0),
            
            ("R WINDOW OPEN +", "Windows.Right", 0.01),
            ("R WINDOW CLOSE -", "Windows.Right", -0.01),
            ("R WINDOW RESET", "Windows.Right", 0.0)
        ]
        
        for i, (text, var, val) in enumerate(controls):
            btn = tk.Button(frame, text=text, font=('Arial', 8),
                        command=lambda v=var, val=val: self.send_command(v, val))
            btn.grid(row=i//2, column=i%2, sticky='ew', padx=1, pady=1)



    def send_command(self, variable, value):
        """Send command to Aerofly via TCP"""
        try:
            # Create command JSON
            command = {
                "variable": variable,
                "value": value
            }
            
            # Connect to command port
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0)  # 2 second timeout
            sock.connect(('localhost', 12346))
            
            # Send command
            message = json.dumps(command) + '\n'
            sock.send(message.encode('utf-8'))
            sock.close()
            
            # Update status
            self.status_label.config(text=f"✅ Command sent: {variable} = {value}", fg='green')
            self.root.after(2000, lambda: self.status_label.config(text="Ready - All Controls Available", fg='green'))
            
        except socket.error as e:
            self.status_label.config(text=f"❌ Connection Error: {str(e)}", fg='red')
            self.root.after(3000, lambda: self.status_label.config(text="Ready - All Controls Available", fg='green'))
        except Exception as e:
            self.status_label.config(text=f"❌ Error: {str(e)}", fg='red')
            self.root.after(3000, lambda: self.status_label.config(text="Ready - All Controls Available", fg='green'))
    
    
    def run(self):
        """Start the application"""
        # Quick scenarios removed for release
        
        # Center window on screen
        self.root.update_idletasks()
        width = self.root.winfo_width()
        height = self.root.winfo_height()
        x = (self.root.winfo_screenwidth() // 2) - (width // 2)
        y = (self.root.winfo_screenheight() // 2) - (height // 2)
        self.root.geometry(f'{width}x{height}+{x}+{y}')
        
        # Start main loop
        self.root.mainloop()

def main():
    """Main entry point"""
    print("=== Aerofly FS4 Master Control Panel ===")
    print("Connecting to localhost:12346 (Command Port)")
    print("Make sure AeroflyBridge.dll is loaded in Aerofly FS4")
    print("=" * 50)
    
    app = AeroflyMasterControlPanel()
    app.run()

if __name__ == "__main__":
    main()