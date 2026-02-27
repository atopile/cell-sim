# CellSim v2

Modular, rack-mountable battery cell simulator for BMS development and testing.

**Status:** In development — see [`docs/cellsim-v2-spec.md`](docs/cellsim-v2-spec.md) for the full specification.

## Overview

CellSim v2 uses a **backplane + card architecture**: a base board with a Raspberry Pi CM5 controller and gigabit Ethernet switch connects to up to ~20 plug-in cards, each carrying an STM32H723 MCU and 8 isolated cell channels.

### Key Specs (per channel)

| Parameter | Target |
|-----------|--------|
| Output voltage | 0 – 6 V |
| Output current | 0 – 1 A |
| Voltage accuracy | ±1 mV (after calibration) |
| Update rate | 100 Hz |
| Kelvin sensing | Force + Sense, 2-wire/4-wire selectable |

### Card Types

- **Cell Card** — 8 isolated cell channels with Kelvin output
- **Thermistor Card** — 8 thermistor/NTC emulation channels
- Same backplane connector and form factor for both

## Repository Structure

```
elec/
  cell-card/      # Cell card electronics (base-card + cell/thermistor modules)
  backplane/      # Base board electronics (CM5, switches, power, fans)
  packages/       # Custom atopile packages (e.g. ADS1219)
firmware/
  card/           # Zephyr RTOS firmware for STM32H723 card MCU
software/
  src/cellsim/    # CM5 orchestration server (FastAPI) + Python client
mechanical/       # Enclosure, rack mount hardware
docs/             # Specification, diagrams
```

## Architecture

```
                    ┌─────────────────────────────────┐
                    │          Base Board              │
  Ethernet ────────►│  CM5  ──RGMII──►  RTL8305NB x5  │
  (uplink)          │  eth0            (cascaded GbE)  │
                    └──────────┬──────────────────────┘
                               │ 100M per port
               ┌───────────────┼───────────────┐
               ▼               ▼               ▼
         ┌──────────┐   ┌──────────┐   ┌──────────┐
         │ Card #0  │   │ Card #1  │   │ Card #N  │
         │ STM32H723│   │ STM32H723│   │ STM32H723│
         │ 8 cells  │   │ 8 cells  │   │ 8 cells  │
         └──────────┘   └──────────┘   └──────────┘
```

## Development

### Electronics (atopile)

The hardware is designed using [atopile](https://atopile.io/). Each board has its own `ato.yaml` build manifest.

### Firmware (Zephyr)

Card firmware uses [Zephyr RTOS](https://zephyrproject.org/) targeting the STM32H723. Development board: Nucleo-H723ZG.

### Software (Python)

The CM5 orchestration server is a FastAPI application. Install for development:

```bash
cd software
pip install -e ".[dev]"
cellsim  # starts the server on port 8000
```

## License

See [LICENSE](LICENSE).
