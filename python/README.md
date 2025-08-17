# Python Examples

This folder contains small, self‑contained Python applications that demonstrate how to use the Aerofly FS4 Bridge from Python via Shared Memory, TCP data stream, and the TCP command channel.

How to run: open a terminal in the repository root and execute the scripts shown below. Ensure Aerofly FS4 is running and the Bridge is active.

## Files Index

- **`aerofly_fs4_maps_bridge.py`**: Position tracker using the TCP data stream (12345); shows live aircraft position and attitude.
  - Example: `python python/aerofly_fs4_maps_bridge.py`
- **`aerofly_realtime_monitor.py`**: Tk desktop monitor that reads variables from the shared‑memory mapping (`AeroflyBridgeData`). Optionally pass the offsets JSON path.
  - Example: `python python/aerofly_realtime_monitor.py AeroflyBridge_offsets.json`
- **`flight_logger.py`**: Comprehensive flight data logger with CSV export, real-time monitoring, and flight phase detection.
  - Example: `python python/flight_logger.py`
- **`master_control_panel.py`**: Command/control panel that sends JSON commands over the TCP command port (12346).
  - Example: `python python/master_control_panel.py`

Notes
- Data stream: TCP 12345 (JSON frames)
- Command channel: TCP 12346 (JSON commands)
- Shared memory: mapping name `AeroflyBridgeData` (Windows)
