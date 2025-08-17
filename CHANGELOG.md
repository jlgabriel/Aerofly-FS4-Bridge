# Changelog

All notable changes to this project will be documented in this file.

This project adheres to Keep a Changelog and Semantic Versioning.

## [v0.2.0] - 2025-08-16

### 🚀 Major API Enhancement

#### **BREAKING CHANGE: Simplified JSON Structure**
- **Removed** `all_variables` array from JSON output to eliminate duplication
- **Expanded** variable coverage from 191 to **361 total variables** with canonical Aerofly SDK names
- **Enhanced** JSON structure now provides all variables with descriptive names (e.g., `"Aircraft.Altitude"`, `"Controls.Throttle"`)

#### **Updated BuildDataJSON() Function**
- Complete rewrite to include all 361 variables with canonical naming
- Eliminated redundant `all_variables` array that duplicated data
- Improved performance with cleaner, more efficient JSON payload
- Better developer experience with named variable access instead of numeric indices

#### **Documentation Overhaul**
- Updated all documentation files to reflect new 361-variable structure
- Removed references to deprecated `all_variables` array throughout docs
- Enhanced examples with canonical variable names
- Updated JSON schema to match simplified format
- Improved code examples across all tutorials and API reference

#### **Benefits of v0.2.0**
- ✅ **+170 additional variables** available (191 → 361)
- ✅ **No duplication** - eliminated redundant `all_variables` array
- ✅ **Descriptive names** - `Aircraft.Altitude` vs numeric index access
- ✅ **Better IDE support** with autocompletion for variable names
- ✅ **Reduced errors** from hardcoded array indices
- ✅ **More efficient** JSON payload without duplicate data
- ✅ **Enhanced maintainability** with self-documenting variable names

#### **Migration Guide**
**Before (v0.1.0):**
```javascript
const altitude = data.all_variables[1];  // Index-based access
const throttle = data.all_variables[28]; // Hard to remember indices
```

**After (v0.2.0):**
```javascript
const altitude = data.variables["Aircraft.Altitude"];    // Self-documenting
const throttle = data.variables["Controls.Throttle"];    // Clear and maintainable
```

### **Files Updated**
- `aerofly_bridge_dll.cpp` - Enhanced BuildDataJSON() with 361 variables
- `docs/` - Complete documentation update for new structure
- `reference/json_schema.json` - Updated schema without `all_variables`

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

[v0.2.0]: https://github.com/jlgabriel/Aerofly-FS4-Bridge/releases/tag/v0.2.0
[v0.1.0]: https://github.com/jlgabriel/Aerofly-FS4-Bridge/releases/tag/v0.1.0
