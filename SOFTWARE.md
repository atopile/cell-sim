## Cell-sim software

This project has two software layers:

- **Firmware** on the on-board **ESP32-S3** (`firmware/src/`), built with PlatformIO (Arduino framework).
- **Host tools** in **Python** (`lib/` + `example_set_voltages.py`) that talk to the board over **USB serial**.

### Architecture overview

#### Firmware (`firmware/src/main.cpp`)

- **Transport**: USB CDC serial (`USBSerial`) using a simple line-based ASCII protocol.
- **Cell abstraction**: `Cell` class (`firmware/src/cell.h`, `firmware/src/cell.cpp`) represents one of the 16 channels.
- **I²C topology**:
  - Two MCU I²C controllers: `Wire` and `Wire1`.
  - Each bus drives one **TCA9548A** I²C mux (address `0x70`) so each bus provides **8 mux ports** → **16 cell channels total**.
  - Every `Cell` selects its port by writing `1 << mux_channel` to the mux (see `Cell::setMuxChannel()`).
- **Main loop responsibilities**:
  - Parse and execute serial commands (enable relays, set targets, read telemetry).
  - Periodically update **addressable LEDs** based on measured voltage/current.
  - Apply the current `voltageTargets[]` array to all 16 cells at a fixed cadence.

#### Per-cell firmware behavior (`firmware/src/cell.*`)

Each `Cell` controls and measures a channel via I²C devices behind the mux:

- **Two DACs** (`MCP4725` @ `0x60` and `0x61`) drive the “digital LDO” and “digital buck” control points.
- **ADC** (`ADS1115` @ `0x48`) reads:
  - buck output voltage
  - LDO output voltage
  - shunt/current sense voltage
  - output voltage
- **GPIO expander** (TCA6408 @ `0x20`) controls:
  - buck enable
  - LDO enable
  - output relay
  - load switch
- **Calibration mapping**:
  - `calculateSetpoint(voltage, ...)` does piecewise-linear interpolation between calibration points to convert a desired voltage into a DAC code.
  - `calibrate()` sweeps DAC values and records \((measured\_voltage, dac\_setpoint)\) pairs.

#### Host Python (`lib/` and `example_set_voltages.py`)

- `lib/client.py`: low-level serial transport (`CellSimClient.send_command()`).
- `lib/cellsim.py`: a friendlier wrapper (`CellSim`) with methods like `setVoltage()`, `getAllVoltages()`, etc.
- `example_set_voltages.py`: finds the serial port (via `PING`) and runs a basic functional test.

### Serial protocol (USB)

Commands are ASCII lines terminated by `\n`.

- Responses are typically a single line starting with `OK:` or `Error:`.
- Some “read-all” commands return one `OK:` line containing comma-separated values.

Supported commands (implemented in `firmware/src/main.cpp`):

- **Connectivity**
  - `PING` → `OK:PONG`
- **Voltage control**
  - `SETV <cell 1-16> <voltage>` → `OK:voltage_set:<v>`
  - `SETALLV <voltage>` → `OK:all_voltages_set:<v>`
- **Voltage telemetry**
  - `GETV <cell 1-16>` → `OK:voltage:<v>`
  - `GETALLV` → `OK:voltages:v1,v2,...,v16`
- **Current telemetry**
  - `GETALLI` → `OK:currents:i1,i2,...,i16`
- **Output relay (open-wire simulation)**
  - `ENABLE_OUTPUT <cell>` / `DISABLE_OUTPUT <cell>`
  - `ENABLE_OUTPUT_ALL` / `DISABLE_OUTPUT_ALL`
- **Load switch (speed up discharge / settling)**
  - `ENABLE_LOAD_SWITCH <cell>` / `DISABLE_LOAD_SWITCH <cell>`
  - `ENABLE_LOAD_SWITCH_ALL` / `DISABLE_LOAD_SWITCH_ALL`
- **DMM routing**
  - `ENABLE_DMM <cell>` (selects the DMM mux channel on the MCU)
  - `DISABLE_DMM`
- **Calibration**
  - `CALIBRATE <cell>` / `CALIBRATE_ALL`

Notes:

- The firmware clamps internal target rails (buck/LDO) to safe min/max ranges.
- `Cell::setVoltage()` typically sets the buck slightly above the target (currently ~5% headroom) and the LDO to the requested voltage.

### How to use (firmware)

Prereqs:

- PlatformIO CLI or the PlatformIO VSCode extension.

Build + flash:

```bash
pio run -t upload
```

Serial monitor (optional):

```bash
pio device monitor
```

### How to use (Python)

Prereqs:

- Python 3
- Install dependencies:

```bash
pip install -r requirements.txt
```

Run the example script (auto-discovers the board by sending `PING`):

```bash
python example_set_voltages.py
```

Minimal API example:

```python
from lib.cellsim import CellSim

cellsim = CellSim("/dev/ttyACM0")  # replace with your port
cellsim.setAllVoltages(3.5)
cellsim.enableOutputAll()
print(cellsim.getAllVoltages())
print(cellsim.getAllCurrents())
cellsim.close()
```

Interactive CLI (optional):

```bash
python -m lib.cli -p "/dev/ttyACM0"
```

### Functionality summary

- **Set per-channel “cell” voltages** (0–~4.5V typical operating envelope).
- **Measure per-channel voltage and current** via the onboard ADS1115.
- **Simulate open-wire faults** by opening the per-channel output relay.
- **Speed up voltage transitions** with a per-channel load switch.
- **Route one channel at a time to an external DMM** for high-accuracy measurement.

