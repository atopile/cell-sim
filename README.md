# Cell Simulator

This cell-sim is designed to mimic a LiPo battery pack for development of surrounding electronics (e.g. a BMS).

- 16 channels
- Open-Source hardware design, you can embed onto your own HIL setup
- ⚡️ 0-4.5V and 0-500mA per channel
- DMM muxed to each channel for arbitrarily precise measurement
- Open-circuit (open-wire) simulation on each channel
- 📏 16bit ADC feedback for voltage and current
- 🔌 USB w/ Python software interface (+ 100MBit Ethernet + WiFi waiting for firmware support)

![IMG_0374 3](https://github.com/user-attachments/assets/d8fa4661-c460-48e2-a26a-71079aa79707)

## Documentation

- **Electronics**: this README (see diagrams below)
- **Software (firmware + Python)**: `SOFTWARE.md`

## Design overview

![Cell Diagram](docs/cell.png)

![Board Render](docs/board.png)

## How it works (electronics)

### System-level overview

```mermaid
flowchart TB
  %% ----------------------------
  %% Power tree
  %% ----------------------------
  subgraph PWR["Power tree"]
    XT30["XT30 input<br/>12-20V"] --> PI["Pi filter"] --> ISENSE["Input current sensor"] --> VIN["POWER_IN<br/>12-20V rail"]
    VIN --> BUCK5["LV2842X buck<br/>12-20V to 5V"]
    USBC["USB-C"] -->|VBUS 5V| OR["Diode OR-ing"]
    BUCK5 --> OR --> P5V["5V rail"]
    P5V --> LDO33["LDK220 LDO<br/>5V to 3.3V"] --> P3V3["3.3V rail"]
  end

  %% ----------------------------
  %% Controller + comms
  %% ----------------------------
  subgraph MCU["Controller + comms"]
    ESP["ESP32-S3"]:::mcu
    P3V3 --> ESP
    USBC -->|USB2 data| ESP

    ESP -->|I2C bus #1| I2C1["I2C1"]
    ESP -->|I2C bus #2| I2C2["I2C2"]

    I2C1 --> MUX1["TCA9548A #1<br/>addr 0x70"]
    I2C2 --> MUX2["TCA9548A #2<br/>addr 0x70"]
    P3V3 --> MUX1
    P3V3 --> MUX2

    ESP -->|SPI| W5500["W5500 Ethernet<br/>hw present; fw WIP"]
    P3V3 --> W5500
  end

  %% ----------------------------
  %% 16 cell channels (stacked)
  %% ----------------------------
  subgraph STACK["16x isolated cell channels<br/>stacked like a battery pack"]
    direction TB
    C1["Cell #1<br/>pack GND reference"] --> C2["Cell #2"] --> C3["Cell #3"] --> C4["Cell #4"] --> C5["Cell #5"] --> C6["Cell #6"] --> C7["Cell #7"] --> C8["Cell #8"]
    C8 --> C9["Cell #9"] --> C10["Cell #10"] --> C11["Cell #11"] --> C12["Cell #12"] --> C13["Cell #13"] --> C14["Cell #14"] --> C15["Cell #15"] --> C16["Cell #16<br/>pack +"]
  end

  %% Per-cell services fed from global rails
  VIN -->|12-20V feed| STACK
  P5V -->|logic / relay / LED feed| STACK
  P3V3 -->|isolator side power| STACK

  %% I2C fanout
  MUX1 -->|ports 0-7 to cells 1-8| C1
  MUX2 -->|ports 0-7 to cells 9-16| C9

  %% ----------------------------
  %% DMM path (precision external measurement)
  %% ----------------------------
  subgraph DMM["External DMM measurement path"]
    BANANA["Banana jacks<br/>DMM connector"] --> DMMBUS["DMM_OUT bus"]

    ESP -->|S0..S3 + /EN| DMMSW["CD74HC4067<br/>16:1 mux"]
    P3V3 -->|3.3V select level| DMMSW
    DMMSW -->|CH0..CH15 -> per-cell dmm_relay_enable| STACK

    STACK -->|selected cell closes DMM relay, routes output to DMM_OUT| DMMBUS
  end

  %% ----------------------------
  %% Thermistor simulator
  %% ----------------------------
  subgraph THERM["4x digital thermistor simulator"]
    TSIM["Isolated thermistor simulator<br/>MCP4728 DAC + ADS1115 ADC"] -->|4 analog thermistor nodes| THOUT["thermistor outputs"]
    I2C2 -->|I2C control| TSIM
    VIN -->|12-20V| TSIM
    P3V3 -->|logic-side isolator power| TSIM
  end

  classDef mcu fill:#eef,stroke:#446;
```

### What a single cell channel does internally

Each channel is effectively an *isolated, digitally-controlled power supply* whose output is treated like a single LiPo cell. The channels are then wired in series to create a 16S “battery pack” at the output connector.

```mermaid
flowchart LR
  %% One "Cell" module from elec/src/cell.ato
  subgraph CELL["One cell channel<br/>isolated"]
    direction LR

    %% Power path (high-level)
    VIN["POWER_IN<br/>12-20V"] --> ISO_DCDC["Isolated DC/DC<br/>B1205S"] --> ISO5V["Isolated 5V"]
    ISO5V --> INTLDO["Local LDO<br/>5V to 3.3V"] --> ISO3V3["Isolated 3.3V<br/>logic"]

    ISO5V --> DBUCK["Digitally-controlled buck"] --> DLDO["Digitally-controlled LDO"] --> PF["Pi filter"] --> CS["Current sense"] --> OUTRELAY["Output relay"] --> VOUT["Cell output"]

    %% Control + monitoring (isolated I2C domain)
    I2C_IN["I2C from mux port"] --> ISOI2C["ISO1640 I2C isolator"] --> I2C_ISO["I2C isolated"]
    ISO3V3 --> ISOI2C

    I2C_ISO --> DACB["DAC @0x61<br/>buck setpoint"]
    I2C_ISO --> DACL["DAC @0x60<br/>LDO setpoint"]
    I2C_ISO --> ADC["ADS1115 @0x48<br/>telemetry"]
    I2C_ISO --> GPIO["TCA6408 @0x20<br/>GPIO expander"]
    I2C_ISO --> ISENSE_IC["Current sense IC @0x21"]

    GPIO -->|enable| DBUCK
    GPIO -->|enable| DLDO
    GPIO -->|control| LOADSW["Load switch"]
    GPIO -->|control| OUTRELAY

    %% Telemetry points
    ADC -->|buck V| DBUCK
    ADC -->|LDO V| DLDO
    ADC -->|output V| VOUT
    ADC -->|shunt V| CS

    %% Fast discharge path
    PF --> LOADSW --> RLOAD["Discharge resistor"] --> PF

    %% Precision measurement path (selected externally)
    CS --> DMMRELAY["DMM relay"] --> RLIM["Current-limiting resistors"] --> DMM_OUT["DMM_OUT bus"]
  end
```

## Getting Started

1. Install python if you don't have it already
2. Install the requirements `pip install -r requirements.txt`
3. Connect the board via USB
![Port](docs/port.png)
4. Power the board with the 12V input (supply: 1A minimum, 3A recommended)
5. Run the example python script `python example_set_voltages.py`
6. Voltages should be set to 3.5V, then rainbow from 1V to 4V across the 16 channels and current should be close to 0A.

![Test Output](docs/test-output.png)

You should see something like this:
![Rainbow](docs/labeled-interfaces.png)

## Notes
1. To get best low noise performance, use a quality power supply for the input supply.

## Manufacturing
Board is intended to be manufactured with JLC, files are in the main directory of the repository.
Note, the highlighted isolated regulator needs to be depopulated:

<img width="758" height="372" alt="Screenshot 2026-02-26 at 3 23 36 PM" src="https://github.com/user-attachments/assets/a89a9241-7d18-4d6f-adbc-bf89ab977e0b" />


## Firmware

### Update Firmware
We use PlatformIO to build and upload the firmware via USB.
1. Install PlatformIO: https://platformio.org/install
2. Connect the board via USB (might need to accept connection popup on Mac)
3. Run `pio run -t upload` or install the PlatformIO VSCode extension and use the upload button.

See `SOFTWARE.md` for firmware architecture, protocol, and troubleshooting.

## API Docs
### Connecting to cellsim

```python
from lib.cellsim import CellSim
cellsim = CellSim(port)
```

### Enable outputs
Each cell has a relay in series between the cell output and the connector, this can be  used to simulate open-wire faults for example.

```python
# Global enable/disable
cellsim.enableOutputAll()
time.sleep(1)
cellsim.disableOutputAll()
time.sleep(1)

# Enable/disable one at a time
# Turn relays on in a wave
for i in range(1, 17):
    cellsim.enableOutput(i)
    time.sleep(0.1)

time.sleep(1)

# Turn relays off in a wave
for i in range(1, 17):
    cellsim.disableOutput(i)
    time.sleep(0.1)
```

### Setting voltages
Each output voltage can be specified between 0.5V and 4.4V

```python
# Set all at once
cellsim.setAllVoltages(3.5)
time.sleep(1)

# Set one at a time
print("\nSetting rainbow voltage pattern...")
for i in range(16):
    voltage = 1.0 + (3.0 * i / 15)  # Spread 1V to 4V across 16 channels
    cellsim.setVoltage(i + 1, voltage)
```

### Measuring voltages (onboard)
Each channel has an onboard 16bit ADC that measures the cell output. Note this is not a kelvin connection to your DUT, the measurement will only be accurate when minimal current is being drawn by the DUT.

```python
voltages = cellsim.getAllVoltages()
print(f"Voltages: {[f'{v:.3f}' for v in voltages]}")
```
  
### Measuring voltages externally
To take a precision measurement, each channel has a multiplexed output that connects to the DMM connector via a relay.

```python
channel_to_measure = 3
# Enable DMM
cellsim.enableDMM(channel_to_measure)

# take measurment using external DMM
# that is connected to the DMM banana outputs
time.sleep(1)

cellsim.disableDMM()
```

### Load switch
Each channel has a resistor + mosfet across its output, this can be used to speed up the discharge of the output capacitors. For example when transitioning from a high output voltage to a low output voltage, there will be an RC decay time before the voltage settles, typically in the ~100ms range. Turning on the load switch will make this much faster.

```python
cellsim.enableLoadSwitchAll()
time.sleep(1)

cellsim.disableLoadSwitchAll()
time.sleep(1)
```

### Disconnecting session
Once your test is finished, you can use the following to close the serial session:
```python
  cellsim.close()
```
