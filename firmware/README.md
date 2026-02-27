# CellSim Card Firmware

Zephyr RTOS firmware for the CellSim v2 cell card (STM32H723 + LAN8742A).

Each card manages 8 battery cell simulation channels. Cards communicate with a
central CM5 compute module over Ethernet: TCP for commands, UDP for 100 Hz
measurement streaming, mDNS for discovery.

## Prerequisites

- **Podman** (or Docker) — the entire toolchain runs in a container
- ~10 GB disk space for the Zephyr workspace (downloaded once, cached in a volume)

```bash
# Fedora / RHEL
sudo dnf install podman

# Ubuntu / Debian
sudo apt install podman

# Or use Docker — set CONTAINER_RT=docker in make commands
```

## Quick Start

```bash
cd firmware/

# First time: pull image + initialize west workspace (~5 min)
make setup

# Build firmware
make build
```

The output binary is at `build/zephyr/zephyr.bin`.

## Build Commands

| Command           | Description                                              |
|-------------------|----------------------------------------------------------|
| `make setup`      | Pull Zephyr image + init west workspace (first time)     |
| `make build`      | Build firmware for nucleo_h723zg                         |
| `make clean`      | Remove build artifacts                                   |
| `make shell`      | Interactive shell inside the build container              |
| `make menuconfig` | Run Zephyr menuconfig (TUI Kconfig editor)               |
| `make flash`      | Flash via ST-Link (requires ST-Link on host)             |
| `make help`       | Show available targets                                   |

Override the board: `make build BOARD=stm32h723zg_custom`

Use Docker instead of Podman: `make build CONTAINER_RT=docker`

## Project Structure

```
firmware/
├── Makefile                  # Build system (this file)
├── west.yml                  # West manifest — pins Zephyr v4.0.0
├── README.md                 # This file
├── build/                    # Build output (gitignored)
└── card/                     # Application source
    ├── CMakeLists.txt        # CMake build definition
    ├── prj.conf              # Zephyr Kconfig
    ├── boards/
    │   └── nucleo_h723zg.overlay  # Device tree overlay
    └── src/
        ├── main.c            # Entry point, slot ID, init sequence
        ├── network.c/.h      # TCP command server, UDP streaming, mDNS
        ├── self_test.c/.h    # Per-cell BIST (I2C probe, temp check)
        ├── watchdog.c/.h     # IWDG + CM5 heartbeat monitor
        └── health.c/.h       # MCU temp, Vrefint, input voltage (ADC3)
```

## Architecture

```
┌──────────────────────────────────────────────┐
│  main.c                                      │
│  ├─ read_slot_id()         GPIO decode       │
│  ├─ health_init()          ADC3 setup        │
│  ├─ watchdog_init()        IWDG + heartbeat  │
│  ├─ self_test_run()        I2C BIST all 8    │
│  └─ network_init()         mDNS + sockets    │
│       ├─ TCP cmd server    (thread, pri 7)   │
│       └─ UDP meas stream   (thread, pri 3)   │
│           └─ 100 Hz loop: ADC → pack → send  │
│              └─ watchdog_feed() per tick      │
└──────────────────────────────────────────────┘

I2C topology:
  I2C1 → TCA9548A → [ISO1640 → cell devices] × 4  (cells 0–3)
  I2C2 → TCA9548A → [ISO1640 → cell devices] × 4  (cells 4–7)

Per-cell devices (isolated side):
  MCP4725 ×2 (DAC: buck + LDO setpoint)
  ADS1219    (ADC: voltage + current)
  TCA6408    (GPIO: relay, enable, load switch)
  TMP117     (temperature)
```

## Configuration

### Kconfig (`card/prj.conf`)

Key options:
- `CONFIG_NET_DHCPV4=y` — DHCP for IP assignment
- `CONFIG_MDNS_RESPONDER=y` — mDNS advertisement
- `CONFIG_WATCHDOG=y` — Hardware IWDG
- MCUboot/MCUmgr options are commented out for initial builds

Edit interactively: `make menuconfig`

### Device Tree Overlay (`card/boards/nucleo_h723zg.overlay`)

Defines I2C buses, UART, ADC, watchdog, and Ethernet for the card PCB.
Pin assignments are placeholders pending final PCB layout.

### West Manifest (`west.yml`)

Pins Zephyr v4.0.0 with only the modules needed:
- `hal_stm32` — STM32H7 HAL
- `cmsis` — ARM CMSIS headers
- `mbedtls` — TLS stack
- `mcuboot` — Bootloader (for later OTA)
- `net-tools` — Networking utilities

## Flashing

### ST-Link (development)

Connect ST-Link to the card's SWD header, then:

```bash
make flash
```

### UART Bootloader (production — via CM5)

The CM5 can flash cards remotely using the STM32 ROM bootloader (AN3155):

1. CM5 selects the card via slot mux (CD74HC4067SM)
2. Asserts BOOT0 high + pulses NRST
3. Streams firmware over UART using the AN3155 protocol
4. Releases BOOT0, pulses NRST to boot into new firmware

### DFU over USB (if available)

```bash
dfu-util -a 0 -s 0x08000000 -D build/zephyr/zephyr.bin
```

## Troubleshooting

**`make setup` fails with permission error**
Podman's rootless mode needs `--userns=keep-id`. If you're using Docker, set
`CONTAINER_RT=docker`.

**`west update` hangs or fails**
The Zephyr tree is large. Ensure you have a stable internet connection.
The `--narrow -o=--depth=1` flags minimize download size.

**Build fails with "Zephyr not found"**
Run `make setup` first. The west workspace must be initialized before building.

**`CONFIG_ETH_STM32_HAL` warning**
If Zephyr renames this symbol, check `make menuconfig` under
Drivers → Ethernet for the current name.

**Flash fails: "No ST-Link detected"**
- Ensure the ST-Link is connected and powered
- Check `lsusb` for the ST-Link device
- The `--privileged` flag and `/dev/bus/usb` mount are needed for USB access

## Adapting for Other Projects

1. Copy `firmware/` to your project
2. Edit `west.yml` to add/remove Zephyr modules
3. Replace `card/` with your application
4. Update `BOARD` in the Makefile
5. Run `make setup && make build`
