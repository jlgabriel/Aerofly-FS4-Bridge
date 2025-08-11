# Changelog

All notable changes to this project will be documented in this file.

This project adheres to Keep a Changelog and Semantic Versioning.

## [v0.1.0] - 2025-08-11

### Added
- AeroflyBridge.dll initial release with:
  - Shared Memory interface (fast local telemetry)
  - TCP interface: data stream on 12345, command channel on 12346
  - WebSocket server on 8765 for web/mobile apps
- Documentation in `docs/`:
  - Installation, API reference, variables, architecture, threading, performance
  - Tutorials: Web App (WebSocket) and Python App (Shared Memory + TCP)
- Examples in `examples/`:
  - `aerofly_realtime_monitor.py` (telemetry monitor)
  - `master_control_panel.py` (command/control panel)
- Build scripts: `scripts/compile.bat`, `scripts/make_release.ps1`
- GitHub Actions workflow to build and attach DLL on tag push (`.github/workflows/release.yml`)

[v0.1.0]: https://github.com/jlgabriel/Aerofly-FS4-Bridge/releases/tag/v0.1.0
