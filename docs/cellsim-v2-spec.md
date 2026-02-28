# CellSim v2 — Specification & Architecture

**Status:** DRAFT v7
**Date:** 2026-02-27
**Branch:** `feature/cellsim-v2`

---

## 1. Overview

CellSim v2 is a modular, rack-mountable battery cell simulator for BMS development and testing. It uses a **backplane + card** architecture: a base board with a Raspberry Pi CM5 controller and gigabit Ethernet switch connects to up to 16 plug-in cards, each carrying an STM32H723 MCU and 8 isolated cell channels.

The software stack mirrors the proven Peak cellsim-48ch design (FastAPI REST server, device tree abstraction, async drivers, calibration system) but distributed across Ethernet rather than centralized I2C.

### 1.1 Design Goals

- **Modular card architecture:** GPU-style plug-in cards into a common backplane
- **Fully isolated:** Each cell has its own isolated DC-DC + I2C/SPI isolators (cells at different potentials)
- **Scalable:** 8 to 128 channels (up to 16 cards)
- **Kelvin sensing:** 4-wire (force + sense) per channel, software-switchable to 2-wire mode
- **Self-testable:** ADC verifies every DAC output; full BIST for every card
- **High update rate:** Up to 10 kHz measurement and control across all channels
- **Gigabit Ethernet backbone:** CM5 ↔ cards via on-backplane GbE switch
- **Thermal management:** Fan controller on base board, temperature sensors on every card
- **OTA firmware updates:** CM5 pushes firmware to cards over Ethernet
- **Package-first:** Maximize atopile registry packages, minimize custom components
- **Peak-compatible software:** FastAPI + device tree + async drivers + calibration

### 1.2 Key Specs (per channel)

| Parameter | Target | Notes |
|-----------|--------|-------|
| Output voltage range | 0 – 5 V | Programmable |
| Output current | 0 – 1 A | Continuous, with current sensing |
| Voltage resolution | < 0.1 mV | 24-bit ADC (ADS131M04, ~20 noise-free bits) |
| Voltage accuracy | ±1 mV | After calibration |
| Current measurement | ±0.1 mA | 24-bit ADC |
| Update rate | up to 10 kHz | Measurement; control loop 1–10 kHz |
| Open-circuit simulation | Yes | Output relay per channel |
| Kelvin sensing | Yes | Force + Sense, 2-wire/4-wire selectable |
| Channels per card | 8 | |
| Max cards per system | 16 | Backplane limited |

### 1.3 Card Types

| Card Type | Description |
|-----------|-------------|
| **Cell Card** | 8 isolated cell channels (0–5 V, 0–1 A) with Kelvin output |
| **Thermistor Card** | Same form factor, thermistor/NTC emulation outputs |
| *(future)* | Other emulation cards sharing the same backplane |

---

## 2. System Architecture

### 2.1 Physical Topology

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         RACK MOUNT ENCLOSURE (19", 2U/3U)                │
│                                                                          │
│  FRONT PANEL                                                             │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ [E-STOP] [OLED] [Status LEDs] [RJ45 Uplink] [USB-C CM5] [Fan %] │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                      BASE BOARD (BACKPLANE)                        │  │
│  │                                                                    │  │
│  │  ┌──────┐  ┌────────────┐  ┌────────────────────────────────────┐ │  │
│  │  │ CM5  │──│ GbE Switch │──│  Card Slots (×16)                  │ │  │
│  │  │      │  │ (RTL8305NB │  │  ┌──┐┌──┐┌──┐┌──┐┌──┐     ┌──┐   │ │  │
│  │  │      │  │  cascade)  │  │  │0 ││1 ││2 ││3 ││4 │ ... │15│   │ │  │
│  │  └──────┘  └────────────┘  │  └──┘└──┘└──┘└──┘└──┘     └──┘   │ │  │
│  │                             └────────────────────────────────────┘ │  │
│  │  ┌──────────┐  ┌───────────────┐                                  │  │
│  │  │ Fan Ctrl │  │ Temp Sensors  │                                  │  │
│  │  │ (PWM)    │  │ (base board)  │                                  │  │
│  │  └──────────┘  └───────────────┘                                  │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  REAR PANEL                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ [IEC C14 Mains] [Card 0 Out][Card 1 Out]...[Card 15 Out]         │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  INTERNAL: Meanwell 24V PSU (e.g. LRS-350-24) + cooling fans            │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Base Board (Backplane)

| Component | Function |
|-----------|----------|
| **Raspberry Pi CM5** | Main controller — FastAPI server, card orchestration, firmware updates |
| **GbE Switch** | Cascaded RTL8305NB switches (proven in Hyperion) connecting CM5 ↔ all card slots |
| **Meanwell PSU** | Off-the-shelf enclosed 24V supply (IEC C14 mains input on rear panel) |
| **24V power bus** | Distributes 24V to each card slot via per-slot relay |
| **Per-slot power relays** | 16× Omron G5Q-1A4 DC24 (SPST-NO, 10A/30VDC), individually software-controlled via MCP23017 + N-FET. E-stop button cuts coil supply to all relays simultaneously. |
| **Per-slot current monitoring** | 7× INA3221 (3-ch each) behind TCA9548A mux — measures per-slot 24V current downstream of relay |
| **Bus power monitor** | INA228 on main 24V bus — total system current + power |
| **Fan controller** | PWM-driven fans, temperature-feedback closed loop |
| **Temperature sensors** | On base board + reported by each card |
| **Card slot connectors** | GPU-style edge connectors: Ethernet pairs + 24V + GND + slot ID bits + CAL traces |
| **Calibration mux** | 16× DPDT relays (one per slot), 2× TCA6408 GPIO expanders on CM5 I2C, banana jacks for external DMM |
| **E-Stop circuit** | Front panel NC mushroom-head button, series with relay coil supply — kills all 16 slot relays simultaneously |
| **OLED display** | System status (IP, card count, temps, fan speed) |
| **Uplink RJ45** | External network (front panel) |
| **USB-C** | CM5 debug/console (front panel) |

### 2.3 Cell Card (8-Channel)

Each card is a self-contained PCB that plugs into the backplane like a GPU. The card has **two tiers of galvanic isolation**:

1. **Card-level isolation (backplane ↔ card MCU domain):** An isolated 24V→24V DC-DC converter, a discrete Ethernet transformer, and a UART digital isolator create a fully isolated card domain. This allows stacking multiple cards for high-voltage configurations (up to ~1 kV theoretical).

2. **Per-cell isolation (card MCU domain ↔ each cell):** Each of the 8 cells has its own isolated DC-DC, I2C isolator, and SPI isolator. Cells stack in series (emulating an 8S battery), so cell 0's ground might be at 0 V while cell 7's ground is at ~40 V relative to the card domain.

```
                         CARD (plugs vertically into backplane)
┌───────────────────────────────────────────────────────────────────┐
│                                                                   │
│  BACKPLANE SIDE (non-isolated)                                    │
│  ═══════════════════════════════════════════════════════════      │
│  EDGE CONNECTOR (to backplane)                                    │
│  [Eth TX+/-] [Eth RX+/-] [24V×4] [GND×4] [ID0-3] [UART] [RST]  │
│  ═══════════════════════════════════════════════════════════      │
│       │            │            │                                  │
│       │     ┌──────┴──────┐    │                                  │
│       │     │ Eth Xfmr    │    │  ◄── Discrete Ethernet           │
│       │     │ (magnetics) │    │      transformer (galvanic iso)  │
│       │     └──────┬──────┘    │                                  │
│       │            │     ┌─────┴──────┐                           │
│       │            │     │ ISO DC-DC  │ ◄── 24V→24V isolated      │
│       │            │     │ (24V→24V)  │     (e.g. TDK20-24S24WH) │
│       │            │     └─────┬──────┘                           │
│       │            │           │                                  │
│  ┌────┴────┐       │    ┌──────┴──────┐                           │
│  │ UART    │       │    │ 24V_iso     │                           │
│  │ Isolator│       │    │    │        │                           │
│  └────┬────┘       │    │ TPSM84209  │ ◄── 24V→3.3V/5V buck      │
│       │            │    │ (24V→3.3V) │     for card electronics   │
│  ═════╪════════════╪════╪════════════╪════════════════════════    │
│  CARD ISOLATED DOMAIN  │            │                             │
│       │            │    │  3.3V_card │                             │
│  ┌────┴────────────┴────┴────────────┴─────────┐                  │
│  │ STM32H723 + LAN8742A PHY                    │                  │
│  │                                              │                  │
│  │  I2C1 ────→ TCA9548A ──→ Cells 0-7          │                  │
│  │              (8 channels, ISO1640 per cell)  │                  │
│  │                                              │                  │
│  │  SPI1 ────→ ISO7741 ×4 → Cells 0-3 ADC      │                  │
│  │  SPI2 ────→ ISO7741 ×4 → Cells 4-7 ADC      │                  │
│  │                                              │                  │
│  │  ADC  ────→ Input voltage sense, board temp  │                  │
│  │  GPIO ────→ Slot ID pins (from backplane)    │                  │
│  │  SWD  ────→ Debug header (optional)          │                  │
│  │  USB  ────→ USB-C (DFU fallback)             │                  │
│  └──────────────────────────────────────────────┘                  │
│       │                                                            │
│       │  ┌──────────────────────────────────────────────────┐      │
│       │  │  8× Isolated Cell Channels                       │      │
│       │  │  (each powered from 24V_iso via per-cell         │      │
│       │  │   isolated DC-DC, stacked in series like 8S)     │      │
│       │  │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐               │      │
│       │  │  │ C0  │ │ C1  │ │ C2  │ │ C3  │ (I2C1 + SPI1) │      │
│       │  │  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘               │      │
│       │  │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐               │      │
│       │  │  │ C4  │ │ C5  │ │ C6  │ │ C7  │ (I2C1 + SPI2) │      │
│       │  │  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘               │      │
│       │  └─────┼───────┼───────┼───────┼────────────────┘      │
│       │        │       │       │       │                       │
│  REAR CONNECTOR (to DUT)                                          │
│  [F0 S0] [F1 S1] [F2 S2] [F3 S3] [F4 S4] [F5 S5] [F6 S6]      │
│  [F7 S7] [GND_K] [GND_K_S]                                       │
└───────────────────────────────────────────────────────────────────┘
```

**Card-level isolation components:**

| Component | Function | Notes |
|-----------|----------|-------|
| **Isolated DC-DC (24V→24V)** | Galvanic isolation between backplane and card domain | e.g. TDPOWER TDK20-24S24WH (20W), proven in Peak 48ch design |
| **Discrete Ethernet transformer** | Isolates Ethernet PHY from backplane | Ethernet passes through backplane (not a separate RJ45 on the card) |
| **UART digital isolator** | Isolates UART for card programming/debug | Backplane routes UART to CM5 via slot mux for remote flash |
| **TPSM84209 buck** | 24V_iso → 3.3V (or 5V→3.3V) for MCU domain | 4.5–28V in, 2.5A, integrated inductor |

### 2.4 Multi-Bus Architecture (I2C + SPI)

The STM32H723 uses **I2C for control** (DACs, GPIO, temp) and **SPI for high-speed ADC** (ADS131M04):

**I2C bus (400 kHz Fast Mode) — control path:**

A single TCA9548A I2C mux provides 8 channels — one per cell. Each cell's I2C devices (DACs, GPIO expander, temp sensor) sit behind an ISO1640 isolator on the corresponding mux channel.

| Operation | Time per cell | Notes |
|-----------|---------------|-------|
| TCA9548A mux switch | ~50 µs | 2-byte write |
| ISO1640 transparent | ~0 µs | Adds no delay |
| MCP4725 DAC write × 2 | ~200 µs | Buck + LDO setpoints |
| TCA6408 GPIO write | ~50 µs | Relay/enable states |
| **Per-cell I2C total** | **~300 µs** | ADC no longer on I2C |
| **8 cells sequential** | **~2.4 ms** | Single I2C bus |

**SPI bus (up to 25 MHz) — measurement path:**

| Operation | Time per cell | Notes |
|-----------|---------------|-------|
| ADS131M04 read (4 channels) | ~10 µs | 24-bit × 4ch via SPI at 10 MHz |
| ISO7741 transparent | ~0 µs | 100 Mbps digital isolator |
| **Per-cell SPI total** | **~10 µs** | Simultaneous 4-ch, no mux |

**Combined cycle time (8 cells):**

| Path | Time | Rate |
|------|------|------|
| I2C control (DAC + GPIO) | ~2.4 ms | ~415 Hz (single bus, 8 cells sequential) |
| SPI measurement (ADC) | ~80 µs | **~12 kHz** (sequential scan, all 8 cells, 2 SPI buses) |
| Overhead + Ethernet | ~1 ms | Packet assembly, send |
| **Total (control + measure)** | **~3.5 ms** | **~285 Hz full loop** |
| **Measurement only** | **~1.1 ms** | **~900 Hz** (read all 8 cells) |

**Note:** At 32 kSPS/channel, the ADS131M04 produces data faster than the I2C control path can update DACs. The practical architecture is: ADC runs continuously at up to 32 kSPS, firmware reads at 1–10 kHz, DAC updates only when setpoint changes (event-driven, not every cycle). See §4.9.9 for SPI DAC upgrade path that eliminates the I2C bottleneck.

**Bus assignment:**
- **I2C1:** TCA9548A → Cells 0–7 (each behind ISO1640, mux channels 0–7) — DACs + GPIO + temp
- **SPI1:** Cells 0–3 ADC (each behind ISO7741) — via MISO mux (CD74HC4052)
- **SPI2:** Cells 4–7 ADC (each behind ISO7741) — via MISO mux (CD74HC4052)
- **I2C3:** (spare — on-card EEPROM, card-level temp sensor)

DMA-driven I2C and SPI transfers on the STM32H7 allow SPI buses to run in parallel with minimal CPU intervention. The single I2C bus handles the slow-changing control path (DAC setpoints only update when voltage target changes, not every cycle).

**SPI bus architecture per 4-cell group:**
```
STM32H723 SPIx
  ├── SCLK ──→ [to all 4 ISO7741 forward ch A]
  ├── MOSI ──→ [to all 4 ISO7741 forward ch B]
  ├── CS0..CS3 ──→ [to each ISO7741 forward ch C] (individual per cell)
  └── MISO ←── [CD74HC4052 2:1 mux] ←── 4× ISO7741 reverse ch
                     ↑ select via GPIO (2 bits)
```
Each cell's ISO7741 isolates the SPI signals across the galvanic barrier. The MISO return uses an analog mux (CD74HC4052) on the non-isolated side to avoid bus contention, selected in sync with the CS line.

**I2C bus speed analysis:**

The I2C bus is limited by the slowest components:
- STM32H723: 1 MHz Fm+ ✓
- TCA9548A mux: **400 kHz max** (weakest link — upgrade to NXP PCA9848 for 1 MHz if needed)
- ISO1640 isolator: 1.7 MHz ✓
- MCP4725 DAC: 3.4 MHz ✓
- TCA6408 GPIO: **400 kHz max** (upgrade to NXP PCAL6408A for 1 MHz if needed)

**Practical I2C limit: 400 kHz.** Sufficient for control path — the I2C path only handles slow-changing setpoints (DAC writes) and GPIO state changes, not high-speed measurement. The full 8-cell sequential scan takes ~2.4 ms, but in practice DAC updates are event-driven (only when setpoint changes), making the I2C bus lightly loaded.

**Why single I2C bus (changed from 2× I2C):** The TCA9548A has 8 channels, sufficient for all 8 cells. Two I2C buses provided only ~2× speedup for the control path (which is already not the bottleneck — SPI ADC measurement is the fast path). A single I2C bus simplifies routing, reduces MCU pin usage, and eliminates one TCA9548A. If higher control rates are needed, the SPI DAC upgrade path (§4.9.9) is the correct solution.

### 2.5 Per-Cell Architecture

Same proven isolated supply → buck → LDO topology from the 48ch design, uprated for 5 V / 1 A. Each cell is powered from the card's isolated 24V rail (24V_iso), NOT directly from the backplane:

```
24V_iso (from card-level isolated DC-DC)
  │
  ├── Per-cell Isolated DC-DC (24V → 12V, 3W) ── [CELL DOMAIN, floating]
  │   YLPTEC B2412S-3WR2 (C5369517)                    │
  │                                                      │
  │         ┌──── I2C Isolator (ISO1640) ────────────────┤ (DACs + GPIO + temp)
  │         ├──── SPI Isolator (ISO7741) ───────────────┤ (ADC)
  │         │                                            │
  │    (Card MCU domain)                          (Cell domain)
  │                                                      │
  │                                          ┌───────────┴───────────┐
  │                                          │  3.3V LDO (LDK220)   │
  │                                          │  12V → 3.3V          │
  │                                          │  (control rail)      │
  │                                          └───────────┬───────────┘
  │                                                      │
  │                                               Digital Buck
  │                                               (TPS563201, DAC-controlled)
  │                                               12V → ~5.5V variable
  │                                                      │
  │                                               Digital LDO
  │                                               (TLV75901, DAC-controlled)
  │                                               Buck out → 0–5V
  │                                                      │
  │                                              Current Sensor
  │                                               (INA185 + 50mΩ shunt)
  │                                                      │
  │                                               Output Relay (DPDT)
  │                                                   │      │
  │                                                FORCE    SENSE
  │                                                   │      │
  │                                              Kelvin output connector
  │
  │                                               [Temp Sensor (TMP117)]
  │                                               (on cell isolated side)
```

**All cells float.** Even cell 0 is isolated — cells stack in series to emulate an 8S battery pack. Cell 0 GND at ~0 V, cell 7 GND at up to ~40 V relative to the card domain.

**Per-cell I2C devices (on isolated side, behind ISO1640 + TCA9548A mux channel):**

| Device | Function | Bus | Speed | Self-test |
|--------|----------|-----|-------|-----------|
| MCP4725 DAC | Buck voltage setpoint | I2C | 3.4 MHz | ADC reads buck output |
| MCP4725 DAC | LDO voltage setpoint | I2C | 3.4 MHz | ADC reads LDO output |
| TCA6408 GPIO | Relay, enable, load switch, 2/4-wire | I2C | 400 kHz | Read-back via ADC |
| **ADS131M04 ADC** | Voltage + current measurement (4ch) | **SPI** | 25 MHz | Verifies DAC outputs |

**ADC: ADS131M04 (24-bit, 4ch simultaneous, SPI)**

Upgraded from ADS1115 (16-bit, I2C) to ADS131M04 (24-bit, SPI). Key specs:
- 24-bit delta-sigma, **4 channels sampled simultaneously** (no mux — all channels every conversion)
- Up to **32 kSPS per channel** — enables 1–10 kHz control loop
- ~20 noise-free bits at 4 kSPS (gain=1)
- SPI interface (up to 25 MHz clock) — isolated via ISO7741 per cell
- Internal 1.2V reference only (no external VREF input); PGA gain = 1 → ±1.2V input range
- With 1:5 voltage dividers (see §4.2.8) for 0–5V input → ~10 µV resolution at signal → **well under 1 mV**
- JLCPCB: C2862562 (537 units in stock, **$1.83** — cheaper than ADS1219 at $7.82)
- Power: 3.3 mW total — lower than ADS1219 (~5 mW)

**SPI isolator: ISO7741FDBQR (4-ch digital isolator)**
- 3 forward channels (SCLK, MOSI, CS) + 1 reverse channel (MISO) — perfect SPI match
- 100 Mbps data rate per channel
- 5 kV reinforced isolation
- JLCPCB: C571196 ($0.44)
- Replaces I2C path for ADC only; ISO1640 still used for I2C devices (DACs, GPIO)

**ADS131M04 channel mapping:**

| ADC Channel | Measurement | Self-test for |
|-------------|-------------|---------------|
| CH0 | Buck output voltage | Buck DAC |
| CH1 | LDO output voltage | LDO DAC |
| CH2 | Current sense amplifier output | Shunt + INA185 |
| CH3 | Cell output voltage (Sense line) | Full signal chain |

**Per-cell GPIO expander (TCA6408):**

| GPIO | Function |
|------|----------|
| 0 | DMM calibration relay (connects cell output to calibration bus) |
| 1 | (spare) |
| 2 | Buck enable |
| 3 | LDO enable |
| 4 | Load switch (internal discharge) |
| 5 | Output relay control |
| 6 | External load switch |
| 7 | 2-wire/4-wire mode select relay |

### 2.6 Kelvin (4-Wire) Sensing

Each cell output has separate **Force** and **Sense** lines:

- **4-wire mode (default):** Force drives current, Sense feeds back to ADC CH3 for true output voltage at the DUT. Eliminates cable/connector drop errors.
- **2-wire mode:** Force and Sense shorted on-card (via relay on GPIO 7). Simpler wiring when precision isn't critical.
- **Software-controlled** per channel via TCA6408.

```
4-Wire Mode:                          2-Wire Mode:
  Card ──── Force ────→ DUT             Card ──┬── Force ────→ DUT
  Card ←─── Sense ────← DUT                    └── Sense (shorted)
  ADC reads Sense line                  ADC reads Force line
```

### 2.7 Temperature Monitoring

| Location | Sensor | Read by |
|----------|--------|---------|
| Per cell (isolated side) | TMP117 or similar on I2C | STM32H7 via ISO1640 + mux |
| Card-level (MCU side) | STM32H7 internal temp ADC | STM32H7 |
| Base board | TMP117 on CM5 I2C | CM5 |
| Exhaust air | Thermistor on base board | CM5 (fan controller feedback) |

Each cell dissipates up to ~3.3W worst-case (5V/1A output). An 8-channel card at full load ≈ 22W. Temperature sensing is essential for thermal protection and fan speed control.

### 2.8 Isolation Architecture

**Two-tier galvanic isolation:**

The CellSim v2 card uses two independent tiers of galvanic isolation:

**Tier 1 — Card-level isolation (backplane ↔ card MCU domain):**
- Isolated 24V→24V DC-DC converter creates isolated 24V rail for the entire card
- Discrete Ethernet transformer isolates the PHY from backplane differential pairs
- UART digital isolator isolates debug/flash UART from backplane
- Purpose: allows multiple cards to be stacked for high-voltage configurations. Each card's MCU domain floats independently of the backplane ground.

**Tier 2 — Per-cell isolation (card MCU domain ↔ each cell):**
- Per-cell isolated DC-DC (24V→12V) creates individual floating ground domains
- ISO1640 I2C isolator per cell (DACs, GPIO, temp sensor)
- ISO7741 SPI isolator per cell (ADS131M04 ADC)
- Purpose: cells stack in series emulating an 8S battery. Cell 0 GND at ~0V, cell 7 GND at ~40V relative to the card domain. ALL cells must float, including cell 0.

```
Backplane Ground Domain
┌───────────────────────────────────────────────────────────┐
│  24V bus                                                   │
│  Ethernet (differential pairs)                            │
│  UART (TX/RX)                                             │
│  Slot ID, Reset                                           │
└──────────┬──────────┬──────────┬──────────────────────────┘
           │          │          │
    [ISO DC-DC]  [Eth Xfmr]  [UART Iso]  ◄── Tier 1: Card isolation
           │          │          │
┌──────────┴──────────┴──────────┴──────────────────────────┐
│  Card MCU Ground Domain (24V_iso)                          │
│  STM32H723 + LAN8742A + TPSM84209 buck                   │
│    │                                                       │
│    ├── I2C1 → TCA9548A (8 channels)                        │
│    │              │                                        │
│    │     ┌────────┼────────┬────────┬──── ... ──┐          │
│    │     │ Ch0    │ Ch1    │ Ch2         Ch7     │          │
│    │   ISO1640 ISO1640  ISO1640       ISO1640    │ ◄── Tier 2 │
│    │     │        │        │             │       │          │
│    │  Cell 0   Cell 1   Cell 2  ...   Cell 7    │          │
│    │  (GND₀)  (GND₁)  (GND₂)       (GND₇)     │          │
│    │     ↕ ~0V    ↕ ~5V    ↕ ~10V      ↕ ~35V   │  (series stack) │
│    │                                             │          │
│    ├── SPI1 → ISO7741 ×4 → Cells 0–3 ADC        │          │
│    ├── SPI2 → ISO7741 ×4 → Cells 4–7 ADC        │          │
│    │                                             │          │
│    └── Ethernet → LAN8742A → [xfmr] → backplane │          │
└──────────────────────────────────────────────────┘
```

**Voltage ratings:**
- Tier 1 (card isolation): 1.5 kV minimum (DC-DC + Ethernet transformer)
- Tier 2 (per-cell isolation): 1.5 kV minimum (ISO1640 + ISO7741 + per-cell DC-DC)
- Combined: theoretical maximum ~1 kV across a full stack of cards

---

## 3. Controllers

### 3.1 Base Board: Raspberry Pi CM5

The CM5 is the system orchestrator:

| Function | Details |
|----------|---------|
| REST API server | FastAPI on Python 3.13+, same endpoints as Peak |
| Card discovery | Slot ID bits read over Ethernet at card boot |
| Card firmware updates | Push .bin to cards over Ethernet (custom bootloader) |
| Calibration storage | Per-cell calibration in config dir |
| Web UI | Real-time dashboard, served from CM5 |
| OLED display | System status (hostname, IP, card count, temps) |
| Fan control | PWM output to chassis fans, PID loop on temperature |
| Uplink | GbE to lab network / host PC |

**CM5 connections on base board:**
- GbE to switch IC (RGMII) — uses `atopile/rpi-cm5` package
- USB-C (debug, front panel)
- I2C (OLED display, base board temp sensors, fan controller, 2× TCA6408 calibration mux relay drivers)
- GPIO (E-stop input, system status LEDs, fan PWM)

### 3.2 Cell Card: STM32H723

Uses the `atopile/st-stm32h723` package (proven in Hyperion project).

| Feature | Benefit |
|---------|---------|
| 480 MHz Cortex-M7 | 100 Hz control loop with DMA I2C — massive headroom |
| Built-in Ethernet MAC | RMII to LAN8742A, same as Hyperion |
| 4× I2C peripherals | 1 bus for cells (TCA9548A 8ch mux), 1 for EEPROM, 2 spare |
| Hardware FPU + DSP | Real-time calibration interpolation |
| USB OTG | DFU firmware update fallback |
| 12-bit internal ADC | Input voltage, board temperature |
| 1 MB Flash | Calibration tables, Ethernet bootloader |

### 3.3 Ethernet PHY (on Cell Card)

Uses `atopile/microchip-lan8742a` package (proven in Hyperion project).

- RMII interface to STM32H723
- 100 Mbps — plenty for 8ch × 100 Hz
- Connects via backplane to GbE switch (auto-negotiates to 100M)

### 3.4 Card Identity (EEPROM + MAC Address)

Each card carries a **Microchip 24AA025E48** I2C EEPROM with a factory-programmed globally unique EUI-48 identifier. This solves two problems with one IC:

| Function | Details |
|----------|---------|
| **Unique card ID** | 48-bit EUI-48 (globally unique, factory-programmed, read-only) |
| **Ethernet MAC address** | Same EUI-48 used as LAN8742A MAC — no manual MAC assignment needed |
| **User EEPROM** | 256 bytes writable — card metadata (manufacturing date, HW revision, card type) |
| **Calibration binding** | CM5 stores calibration data keyed by this ID — survives card moves between slots |

**Why not use the STM32H723 built-in 96-bit UID?**
The STM32's UID is tied to the MCU die. If the MCU is replaced during rework, all calibration data for that card is orphaned. The EEPROM stays with the PCB.

**I2C connection:** On the card's non-isolated I2C3 bus (spare), address 0x50 (fixed). Read at boot by card firmware, reported to CM5 in mDNS TXT record and card status API.

**Card boot sequence with identity:**
1. STM32H723 reads 24AA025E48 EUI-48 from I2C3
2. Programs LAN8742A MAC address register with EUI-48
3. Reads slot ID from backplane GPIO pins
4. Assigns IP: `10.0.0.{100 + slot_id}`
5. Advertises mDNS: `cellsim-card-{slot_id}.local` with TXT records: `id={EUI-48}, type=cell, fw={version}`
6. CM5 discovers card, matches EUI-48 to stored calibration data

**Package:** `atopile/microchip-24aa025e48` (to create — SOT-23-5 or MSOP-8, ~$0.50)

### 3.5 STM32H723 Pin Budget (LQFP-100)

| Function | Pins | Notes |
|----------|------|-------|
| Ethernet RMII | 9 | REF_CLK, MDIO, MDC, CRS_DV, RXD0/1, TX_EN, TXD0/1 |
| I2C1 (all 8 cells) | 2 | SCL, SDA → TCA9548A (8ch mux, DACs + GPIO + temp) |
| I2C3 (card identity) | 2 | 24AA025E48 EEPROM (0x50), card-level temp sensor |
| SPI1 (cells 0-3 ADC) | 3 | SCLK, MOSI, MISO → 4× ISO7741 (ADS131M04) |
| SPI1 CS lines | 4 | CS0..CS3 → each ISO7741 forward ch C |
| SPI1 MISO mux select | 2 | CD74HC4052 address bits (select return ch) |
| SPI2 (cells 4-7 ADC) | 3 | SCLK, MOSI, MISO → 4× ISO7741 (ADS131M04) |
| SPI2 CS lines | 4 | CS4..CS7 → each ISO7741 forward ch C |
| SPI2 MISO mux select | 2 | CD74HC4052 address bits (select return ch) |
| USB OTG FS | 2 | DFU fallback |
| UART (debug/flash) | 2 | TX, RX (isolated via UART isolator to backplane) |
| SWD | 2 | SWDIO, SWCLK |
| Internal ADC inputs | 2 | Input voltage divider, temp |
| HSE crystal | 2 | 25 MHz (shared with PHY) |
| Slot ID inputs | 4 | Active-low from backplane (2⁴ = 16 slots) |
| Boot/Reset | 3 | BOOT0, NRST, user button |
| GPIO (spare) | ~6 | Status LED, etc. (2 freed from I2C2 removal) |
| **Total** | **~51** | Well within 100-pin budget |

---

## 4. Power Architecture

The power architecture is specified bottom-up: cell output requirements drive per-cell component selection, which cascades to per-card power draw, which sizes the system PSU.

### 4.1 Cell Output Requirements (Design Target)

| Parameter | v1 (proven) | v2 (target) | Notes |
|-----------|-------------|-------------|-------|
| Output voltage | 0–4.5 V | **0–5 V** | +11%, TPS563201 buck max ~5.5V |
| Output current | 500 mA | **1 A** | +100%, drives thermal + component changes |
| Output power (max) | 2.25 W | **5 W** | Per-cell delivered to DUT |
| Voltage resolution | ~1 mV | < 1 mV | 12-bit DAC over range |
| Voltage accuracy | ±10 mV | **±5 mV** | After calibration |

### 4.2 Per-Cell Power Path (Isolated Domain)

Same proven topology as v1/Peak — isolated supply → digital buck → digital LDO — but uprated for 5 V / 1 A. Each cell is powered from the card's isolated 24V rail (24V_iso, output of card-level isolated DC-DC):

```
24V_iso (from card-level isolated DC-DC)
  │
  ├── Per-cell Isolated DC-DC (24V → 12V, 3W) ── [CELL DOMAIN, floating]
  │   YLPTEC B2412S-3WR2 (C5369517, $1.95)             │
  │                                                      │
  │         ┌──── I2C Isolator (ISO1640) ────────────────┤ (DACs + GPIO + temp)
  │         ├──── SPI Isolator (ISO7741) ───────────────┤ (ADS131M04 ADC)
  │         │                                            │
  │    (Card MCU domain)                          (Cell domain)
  │                                                      │
  │                                               ┌──────┴──────┐
  │                                               │  3.3V LDO   │ 12V → 3.3V (control rail)
  │                                               │  (LDK220)   │ powers: DACs, ADC, GPIO exp.
  │                                               └──────┬──────┘
  │                                                      │
  │                                               Digital Buck (TPS563201DDCR)
  │                                               MCP4725 DAC → feedback network
  │                                               12V → ~5.5V (variable, tracks LDO + margin)
  │                                               External inductor, 3A, 500 kHz
  │                                                      │
  │                                               Digital LDO (TLV75901)
  │                                               MCP4725 DAC → feedback network
  │                                               Buck out → 0–5V output (fine regulation)
  │                                               Output cap: 10µF ceramic (0805)
  │                                                      │
  │                                              Current Sensor
  │                                               INA185A2 (50× gain) + 50mΩ shunt
  │                                                      │
  │                                               Output Relay (DPDT, HFD4/5)
  │                                                   │      │
  │                                                FORCE    SENSE
  │                                              (Kelvin output connector)
```

#### 4.2.1 Stage 1: Per-Cell Isolated DC-DC Converter

Provides galvanic isolation per cell (cells stack in series at different potentials up to ~40 V total for 8S). Each cell's isolated DC-DC is powered from the card's isolated 24V rail (24V_iso), creating a second tier of isolation.

| Parameter | v1 (B1205S-2WR2) | v2 (YLPTEC B2412S-3WR2) | Notes |
|-----------|-------------------|--------------------------|-------|
| Input voltage | 10.8–13.2 V | **21.6–26.4 V (24V nom.)** | Matches 24V_iso rail |
| Output voltage | 5 V ±10% | **12 V ±10% (11.4–12.6V)** | Headroom for buck + control LDO |
| Output current | 400 mA (2 W) | **250 mA (3 W)** | Sufficient — see power budget below |
| Isolation | 1.5 kV | 1.5 kV | OK |
| Efficiency | ~80% | ~80% | — |
| LCSC | C2992386 | **C5369517** | $1.95 (325 stock) |
| Package | SIP-6 THT | **SIP-4 THT** | PWRM-TH_BXXXXS-3WR2 |
| Atopile package | `atopile/ylptech-byyxx` | `atopile/ylptech-byyxx` | YLPTEC_B2412S_3WR2 module |

**Why 12 V output (not 8 V or 5 V):**
- LDO needs 5 V + 0.225 V dropout = 5.225 V minimum input
- Buck (TPS563201) needs Vin > Vout + dropout: 12V >> 5.5V, comfortable margin
- 12 V provides the 3.3V per-cell control rail with comfortable headroom (via LDK220)
- 12 V is a standard voltage with readily available isolated DC-DC modules
- The slightly higher voltage vs 8V means lower current for the same power, reducing losses

**Why 3 W is sufficient:**
- Buck at worst case (5V/1A output, 90% eff.): 5.5W input from 12V = 0.46A
- Control rail: ~55 mW (negligible)
- Total per-cell load from 12V rail: ~0.52A × 12V = **~6.2W**
- At 80% efficiency from 24V: 6.2W / 0.80 = **7.7W from 24V_iso** → 0.32A from 24V
- **BUT** the B2412S-3WR2 is only rated 3W (250mA @ 12V). This limits maximum continuous cell output to approximately:
  - 3W at 12V = 250mA → buck input power limited to 3W
  - At 90% buck efficiency: ~2.7W available at buck output
  - At 5V output: 2.7W / 5V = **~540 mA continuous**
  - **For full 1A operation**, need a higher-power variant or parallel units. See open items.

**Decoupling:**
- Input: 2.2 µF ceramic (0805) — per YLPTEC datasheet
- Output: 10 µF ceramic (0805)

**Power dissipation at 3W max output:** (3W / 0.80) - 3W = **0.75W waste heat per cell** — manageable.

**STATUS: RESOLVED (component selected). OPEN: 3W may limit max cell current to ~540mA — evaluate if 1A continuous is truly required or if typical 300mA operation is sufficient. For 1A, consider YLPTEC B2405S-2WR3 (2W, 24V→5V) + higher-power buck, or find a 24V→12V 5W+ part.**

#### 4.2.2 Stage 2: Digital Buck Converter

Coarse voltage regulation. DAC-controlled via MCP4725 modulating the TPS563201 feedback network. Same proven part from v1, now operating from a 12V input rail (from per-cell isolated DC-DC).

| Parameter | v1 value | v2 value | Status |
|-----------|----------|----------|--------|
| IC | TPS563201DDCR | **TPS563201DDCR** | **Reuse** |
| Input voltage | 4.5–17 V (from 5V iso) | 4.5–17 V (from 12V iso) | OK (12V within range) |
| Output voltage | 0.768–7 V | **0.768–5.5 V** | Max cell output ~5V |
| Output current | 3 A rated | 3 A rated | OK (1A load = 33% utilization) |
| Vref | 0.768 V | 0.768 V | — |
| Efficiency | ~90% | ~90% | Same part |
| Switching freq | 500 kHz | 500 kHz | — |
| LCSC | C116592 | C116592 | ~$0.35 |
| Package | SOT-23-6 | SOT-23-6 | External inductor required |

**Feedback network (from v1 codebase, proven):**
- R_top: 37 kΩ ±2%, R_bottom: 10 kΩ ±2%, Ctrl_resistor: 24 kΩ ±2%
- DAC A0 tied to VIN → address 0x61
- Vout = Vref × (1 + R_top/R_bottom) adjusted by DAC current injection
- Configured for 0–5 V output range (proven in v1)

**Supporting components:**
- Input caps: 10 µF + 10 µF (0805) + 100 nF (0402)
- Output caps: 2× 22 µF (0805)
- Bootstrap cap: 100 nF (0402)
- External inductor: 4.7 µH (e.g. Wurth WE-LQS 4040, 3A rated)
- Enable: 10 kΩ pull-up (0402), controlled via TCA6408 GPIO P2

**Buck operating point at worst case (5V/1A cell output):**
- Input: 12 V, Output: 5.5 V, Load: 1 A
- Pin = 5.5 V × 1 A / 0.90 = 6.1 W
- **Loss: 0.6 W** (at 90% efficiency)
- Vin/Vout ratio = 2.18 → good operating point

**Note on TPSM863257 upgrade path:** The TPSM863257RDXR (integrated inductor, 1.2 MHz, 3–17V input) was considered as an upgrade but is deferred to Phase 2. The TPS563201 is proven in v1 and the 12V input rail is well within its 4.5–17V range. The main advantage of TPSM863257 (integrated inductor) saves one component but adds $0.50/cell and requires feedback network recalculation for the 0.6V Vref.

**STATUS: RESOLVED. Reusing v1 part (TPS563201) with proven feedback network. 12V input is comfortable within 4.5–17V range.**

#### 4.2.3 Stage 3: Digital LDO (Fine Regulation)

Provides low-noise, precise output regulation. DAC-controlled via MCP4725 modulating the TLV75901 feedback network.

| Parameter | v1 value | v2 value | Status |
|-----------|----------|----------|--------|
| IC | TLV75901PDRVR | TLV75901PDRVR | **Reuse — now works** |
| Input voltage | 1.5–6 V | 1.5–6 V | OK (buck max output 5.5V < 6V) |
| Output voltage | 0.55–5.5 V | 0.55–**5 V** | OK (5V within 5.5V LDO max) |
| Output current | 1 A rated | 1 A (continuous) | 0% margin but proven in v1 |
| Vref | 0.55 V | 0.55 V | — |
| Dropout voltage | 225 mV | 225 mV (typ @ 25°C) | Rises with temp |
| LCSC | C544759 | C544759 | — |
| Package | WSON-6 (2×2 mm) | WSON-6 | Adequate with buck tracking |

**v1 feedback network (from codebase):**
- R_top: 60 kΩ ±2%, R_bottom: 10 kΩ ±2%, Ctrl_resistor: 43 kΩ ±2%
- DAC A0 tied to GND → address 0x60
- v1 configured for 0–5 V output range
- Output cap: increase from 1 µF (0402) to **10 µF ceramic (0805)** for 1A transient response

**v2 status — RESOLVED (P1 blocker cleared):**

With the TPS563201 buck capping at 5.5V output, the TLV75901 (max Vin = 6V) now operates within its rated input range. The LDO can output 0–5V with comfortable margin:

1. **Input voltage:** Buck max output = 5.5V, well within TLV75901 max Vin of 6V. ✓
2. **Output voltage:** 0–5V is within the TLV75901's 0.55–5.5V adjustable range. ✓
3. **Thermal dissipation at 1A:** With buck tracking, max LDO dropout ≈ 0.5V → P = 0.5W. WSON-6 Θ_JA ≈ 60°C/W → 30°C rise. Acceptable. ✓
4. **Output capacitance:** 10 µF ceramic (0805). The LDO's PSRR (>60 dB at 500 kHz buck switching frequency) handles ripple rejection without an LC filter. ✓

**Buck-tracks-LDO control strategy (essential for v2):**
The firmware must set the buck output to Vout_target + ~0.5V headroom. This keeps LDO dropout loss manageable across the full range:

| Cell output | Buck setpoint | LDO dropout | LDO loss @ 1A |
|-------------|---------------|-------------|----------------|
| 5.0 V | 5.5 V | 0.5 V | 0.5 W |
| 4.0 V | 4.5 V | 0.5 V | 0.5 W |
| 2.0 V | 2.5 V | 0.5 V | 0.5 W |
| 0.5 V | 1.5 V | 1.0 V | 1.0 W |

This approach caps LDO dissipation at ~1W across the range.

**STATUS: RESOLVED. TLV75901 works as-is with TPS563201 buck (max 5.5V input to LDO). Feedback network unchanged from v1 (already supports 0–5V). Only output cap upgrade needed (1µF → 10µF).**

#### 4.2.4 Stage 4: Current Sensor

| Parameter | v1 value | v2 value | Status |
|-----------|----------|----------|--------|
| IC | INA185A2IDRLR (50× gain) | Same | OK |
| Shunt | 100 mΩ ±10% (0805) | **50 mΩ ±1% (0805)** | Change for 1A range |
| Max current | 500 mA | **1 A** | — |
| LCSC | C2059320 | C2059320 | — |

**v1 (from codebase):** `current_sense.current = 0.5A`, `shunt.value = 100mohm +/- 10%`
- At 0.5A: V_shunt = 50 mV, V_out = 50 mV × 50 = 2.5 V — good ADC utilization

**v2 recalculation with 50 mΩ shunt:**
- At 1A: V_shunt = 50 mV, V_out = 50 mV × 50 = 2.5 V — same ADC utilization
- Shunt power: 1A² × 0.05Ω = **50 mW** (well within 0805 rating)
- INA output at 1A: 0.05 × 1 × 50 = 2.5V; through 2:5 divider → 1.0V at ADC (within 1.2V full-scale, see §4.2.8)

**STATUS: OK. Update shunt value from 100 mΩ to 50 mΩ, tighten tolerance to ±1%.**

#### 4.2.5 Stage 5: Output Relay

| Parameter | v1 value | v2 value | Status |
|-----------|----------|----------|--------|
| Relay | HFD4/5-SR (DPDT) | Same | OK |
| Contact rating | ≥1A | ≥1A | OK for v2 |
| Coil voltage | 5V ±10% | 5V ±10% | Powered from 5V card rail (non-isolated) for v1; from isolated 8V→5V for v2 |
| Driver | IRLML0040 N-FET | Same | OK |
| Control | TCA6408 GPIO P5 | Same | — |

**v2 note:** Relay coil power comes from the cell's isolated domain. With 12V isolated output, add a 5V regulator per cell or use the 12V rail with appropriate relay selection. Alternatively, the relay coil can be powered from the 3.3V control rail via a higher-resistance coil variant (3V coil relay).

**STATUS: OK. Verify relay coil supply in isolated domain.**

#### 4.2.6 Output Path Resistance Budget

Maximum allowed resistance from LDO output to cell output connector, ensuring acceptable voltage drop at 1A:

| Element | Resistance | Voltage Drop @1A |
|---------|-----------|-----------------|
| Current sense shunt | 50 mΩ | 50 mV |
| PCB trace (LDO→shunt→relay) | ≤20 mΩ | 20 mV |
| Output relay contacts | ~100 mΩ | 100 mV |
| PCB trace (relay→connector) | ≤10 mΩ | 10 mV |
| Connector contact resistance | ~20 mΩ | 20 mV |
| **Total** | **≤200 mΩ** | **≤200 mV** |

**Design notes:**
- 200 mV drop at 1A is within the LDO's regulation capability. Kelvin sense feeds back the actual output voltage to ADS131M04 CH3, so the firmware compensates for the drop.
- PCB layout: use ≥1 oz copper, ≥0.5 mm trace width for the force path (target ≤10 mΩ/cm).
- The shunt resistor drop is intentional (current measurement), not waste.
- The 200 mV total drop is well within the 500 mV buck-LDO tracking margin — the buck can always provide enough headroom for the LDO to regulate at the target output voltage.

**STATUS: Spec. Verify during PCB layout (trace resistance) and prototype validation.**

#### 4.2.7 Per-Cell Internal Control Rail

Each cell's isolated domain also requires a 3.3V rail for the I2C/SPI devices (DACs, ADC, GPIO expander, isolator secondary sides).

| Parameter | v1 value | v2 value | Status |
|-----------|----------|----------|--------|
| IC | LDK220M-R | **LDK220M-R** | **Reuse** (Vin max 13.2V, 12V input OK) |
| Input | 5V (from B1205S) | **12V** (from YLPTEC B2412S-3WR2) | 12V within LDK220 Vin range |
| Output | 3.3V ±5% | 3.3V ±5% | — |
| Current | 200 mA max | ≥50 mA | Sufficient for I2C/SPI devices |
| Dropout | ~225 mV | ~225 mV | 12V→3.3V = 8.7V headroom, no concern |

**Loads on 3.3V control rail:**
- ISO1640 isolated side: ~3 mA
- ISO7741 isolated side: ~3 mA (SPI isolator for ADC)
- ADS131M04 ADC: ~1 mA (3.3 mW @ 3.3V)
- 2× MCP4725 DAC: ~0.4 mA each
- TCA6408 GPIO: ~0.1 mA
- TMP117 temp sensor: ~0.1 mA
- I2C pull-ups (2× 2 kΩ): ~3.3 mA
- **Total: ~11.8 mA** — well within LDK220's 200 mA budget

**Control LDO dissipation at 12V input:** (12V - 3.3V) × 11.8 mA = **103 mW** — acceptable, slightly higher than v1 due to 12V input vs 5V. Well within SOT-23-5 thermal limits.

**STATUS: RESOLVED. LDK220M-R reused — Vin max of 13.2V accommodates the 12V ±10% input from the isolated DC-DC (11.4–12.6V). The `st-ldk220-local/` package is already integrated in cell-card.**

#### 4.2.8 ADC Input Conditioning

Each ADS131M04 channel requires analog input conditioning to map the cell-domain signals into the ADC's ±1.2V input range (internal reference, PGA gain = 1). All four channels use resistive voltage dividers followed by a first-order RC anti-aliasing filter.

**ADS131M04 key constraints:**
- Internal reference: 1.2V (typical 1.25V) — no external VREF input
- Single-ended usable range: 0 to +1.2V (gain = 1)
- Input impedance: ~330 kΩ at gain 1–4
- All 4 channels on isolated 3.3V domain (AVDD = 3.3V)

**Per-channel input conditioning:**

| CH | Signal | Source Range | Divider | ADC Input Range | Headroom | Anti-Alias f_c | Notes |
|----|--------|-------------|---------|-----------------|----------|---------------|-------|
| 0 | Buck output voltage | 0–5.5V | 1:5 | 0–1.10V | 8% | ~50 kHz | 500 kHz buck ripple — tighter filter |
| 1 | LDO output voltage | 0–5.0V | 1:5 | 0–1.00V | 17% | ~100 kHz | Clean LDO output |
| 2 | Current sense (INA185 out) | 0–2.5V | 2:5 | 0–1.00V | 17% | ~100 kHz | INA185A2: 50× gain, 50 mΩ shunt; INA BW ~350 kHz pre-filters |
| 3 | Kelvin sense voltage | 0–5.0V | 1:5 | 0–1.00V | 17% | ~100 kHz | 4-wire sense line, high-Z input |

**Anti-aliasing filter:**
- **Topology:** Series R_filter + shunt C_filter to AGND, between divider output and ADC input pin
- **CH0 cutoff ~50 kHz:** Buck switching at 500 kHz → ~20 dB rejection at switching frequency
- **CH1/2/3 cutoff ~100 kHz:** Clean signals, 10× above control bandwidth (10 kHz)
- **Capacitor dielectric:** C0G/NP0 mandatory (no voltage-dependent capacitance)
- **Source impedance target:** 2–5 kΩ total (divider Thévenin ∥ R_filter) — low enough for ADC settling, high enough for ESD protection
- Delta-sigma oversampling + digital decimation filter provides additional noise rejection

**Resolution analysis (at ~19 ENOB, 10 kHz output rate):**

| CH | Signal Range | Divider | Resolution (at signal) | Notes |
|----|-------------|---------|----------------------|-------|
| 0 | 0–5.5V | 1:5 | ~10.5 µV | Well under ±1 mV target |
| 1 | 0–5.0V | 1:5 | ~9.5 µV | Well under ±1 mV target |
| 2 | 0–2.5V | 2:5 | ~4.8 µV → ~1.9 µA | Well under ±0.1 mA target |
| 3 | 0–5.0V | 1:5 | ~9.5 µV | Well under ±1 mV target |

**AGND routing:**
- Isolated analog ground plane under ADC + all input conditioning components
- Single-point connection to isolated digital ground near ADC AGND pin
- No digital traces routed under analog area

### 4.3 Per-Cell Power Budget Summary

**Note:** Per-cell isolated DC-DC (B2412S-3WR2) is rated 3W (250mA @ 12V). This limits continuous operation — see §4.2.1 for details. The budget below shows the realistic operating envelope.

**Maximum continuous (limited by 3W iso DC-DC): ~5V output, ~540mA load**

| Stage | Input | Output | Loss | Efficiency |
|-------|-------|--------|------|------------|
| Isolated DC-DC (80% eff.) | 24V_iso × 0.16A = **3.75W** | 12V × 0.25A = 3.0W | **0.75W** | 80% |
| Digital Buck (90% eff.) | 12V × 0.25A = 3.0W | 5.5V × 0.49A = 2.7W | **0.3W** | 90% |
| Digital LDO (tracking buck) | 5.5V × 0.49A = 2.7W | 5.0V × 0.49A = 2.45W | **0.25W** | 91% |
| Current Sensor (50mΩ) | 2.45W | 2.45W | **0.012W** | 99.5% |
| Output Relay | 2.45W | 2.45W | **~0.02W** | ~100% |
| 3.3V control rail (LDK220) | 0.14W | 0.039W | **0.103W** | — |
| Relay coil | — | — | **~0.1W** | — |
| **TOTAL per cell** | **3.75W from 24V_iso** | **2.45W to DUT** | **1.5W waste** | **65% end-to-end** |

**Per-cell 24V_iso current:** 3.75W ÷ 24V = **0.16A per cell** (at max continuous)

**Typical operating point (3.7V / 300mA = 1.1W):**

| Stage | Input | Output | Loss | Efficiency |
|-------|-------|--------|------|------------|
| Isolated DC-DC (80% eff.) | 24V_iso × 0.08A = **1.88W** | 12V × 0.13A = 1.5W | **0.38W** | 80% |
| Digital Buck (90% eff.) | 12V × 0.13A = 1.5W | 4.2V × 0.3A = 1.26W | **0.14W** | 90% |
| Digital LDO (tracking) | 4.2V × 0.3A = 1.26W | 3.7V × 0.3A = 1.1W | **0.15W** | 88% |
| Other | — | — | **~0.25W** | — |
| **TOTAL per cell** | **1.88W from 24V_iso** | **1.1W to DUT** | **0.92W waste** | **59% e2e** |

### 4.4 Per-Card Power Budget

Each card has 8 cells plus card-level electronics. The card receives 24V from the backplane, passes it through a card-level isolated DC-DC (24V→24V), then distributes 24V_iso to both the card MCU domain (via TPSM84209 buck) and the 8 per-cell isolated DC-DCs.

#### 4.4.1 Card Electronics Rail

```
24V (from backplane)
  │
  └──→ Isolated DC-DC (24V → 24V, 20W) ──→ 24V_iso
            e.g. TDPOWER TDK20-24S24WH              │
            (card-level galvanic isolation)           │
                                                      ├──→ TPSM84209 Buck (24V → 3.3V, 2.5A)
                                                      │         └── STM32H723, LAN8742A, mux,
                                                      │             TCA9548A, ISO1640/ISO7741 non-iso ×8
                                                      │
                                                      └──→ 8× Per-cell isolated DC-DC (24V → 12V, 3W)
                                                                └── Per-cell power chain (see §4.3)
```

| Component | Supply | Current Draw | Notes |
|-----------|--------|-------------|-------|
| STM32H723 (active, I2C + SPI DMA + Ethernet) | 3.3V | ~150 mA | Worst-case with all peripherals |
| LAN8742A PHY | 3.3V | ~80 mA | Active link |
| 1× TCA9548A I2C mux | 3.3V | ~1 mA | Single mux, 8 channels |
| 8× ISO1640 non-isolated side | 3.3V | ~24 mA | 3 mA each |
| 8× ISO7741 non-isolated side | 3.3V | ~24 mA | 3 mA each (SPI isolator) |
| 2× CD74HC4052 MISO mux | 3.3V | ~1 mA | SPI MISO multiplexers |
| Misc (pull-ups, indicators) | 3.3V | ~10 mA | — |
| **3.3V total** | 3.3V | **~290 mA** | Via TPSM84209 direct to 3.3V |

**Card-level buck (TPSM84209RKHR):**
- Input: 24V_iso, Output: 3.3V @ 0.29A (direct, no intermediate 5V stage needed)
- Power: 3.3V × 0.29A = 0.96W output, ~1.07W input (at 90% eff.)
- Integrated inductor, 4.5–28V in, 1.2–6V out, 2.5A rated
- **Card electronics loss: ~0.11W** (better efficiency than 24V→5V→3.3V two-stage)

**Card-level isolated DC-DC (24V→24V):**
- e.g. TDPOWER TDK20-24S24WH (20W, proven in Peak 48ch)
- Input: 24V (backplane), Output: 24V_iso
- At 90% efficiency: 20W output capacity, 22.2W input
- Powers: TPSM84209 (1.07W) + 8× per-cell DC-DCs (8 × 3.75W = 30W max)
- **Note:** 20W TDK20 limits total card power. At typical loads (~15W total) this is adequate. For full 8× max continuous, may need higher-power variant.

**Note:** Per-cell SK6805 LEDs removed. Card status can be reported via Ethernet to CM5 OLED/dashboard.

#### 4.4.2 Card Total

**Max continuous (limited by 3W per-cell iso DC-DC: 8 cells at ~5V/540mA)**

| Load | 24V Current | Power | Waste Heat |
|------|-------------|-------|------------|
| 8× cell isolated supplies | 8 × 0.16A = **1.25A** | 30W | **6.0W** |
| Card-level iso DC-DC (90% eff.) | — | — | **3.3W** |
| Card electronics (TPSM84209) | 0.04A | 1.07W | 0.11W |
| **Card total (max continuous)** | **~1.4A** | **~34W** | **~9.4W** |

**Typical (8 cells at 3.7V/300mA)**

| Load | 24V Current | Power | Waste Heat |
|------|-------------|-------|------------|
| 8× cell (3.7V/300mA) | 8 × 0.08A = **0.63A** | 15W | **3.0W** |
| Card-level iso DC-DC (90% eff.) | — | — | **1.7W** |
| Card electronics | 0.04A | 1.07W | 0.11W |
| **Card total (typical)** | **~0.7A** | **~18W** | **~4.8W** |

**Backplane current per slot:** ~1.4A max continuous → **4× power pins at 1.5A each** provides ample margin.

### 4.5 System Power Budget

#### 4.5.1 Full System Power Tree

```
AC Mains (110/240V)
  │
  ├── IEC C14 inlet + fuse + EMI filter
  │
  ├── Meanwell PSU (24V DC output)
  │     │
  │     ├── 24V Bus (on backplane, heavy copper)
  │     │     │
  │     │     ├── INA228 shunt (total system current sense)
  │     │     │     │
  │     │     │     ├── Slot 0: G5Q relay (NO) → INA3221 shunt → Card connector (~1.4A max)
  │     │     │     │         Card: 24V → [ISO DC-DC 24V→24V] → 24V_iso → TPSM84209 + 8× cells
  │     │     │     ├── Slot 1: G5Q relay (NO) → INA3221 shunt → Card connector (~1.4A max)
  │     │     │     ├── ...
  │     │     │     └── Slot 15: G5Q relay (NO) → INA3221 shunt → Card connector (~1.4A max)
  │     │     │
  │     │     └── Relay coil supply bus (24V_COIL)
  │     │           │
  │     │           ├── E-Stop button (NC, series) ← kills all relay coils
  │     │           │
  │     │           ├── MCP23017 #1 → N-FET × 8 → Relay coils 0-7
  │     │           └── MCP23017 #2 → N-FET × 8 → Relay coils 8-15
  │     │
  │     └── Base Board 24V (always-on, not behind relays)
  │           │
  │           ├── Buck (24V → 5V) → CM5 (5V, ~2A)
  │           ├── Buck (24V → 5V) → GbE Switch ICs (×5 RTL8305NB)
  │           ├── LDO (5V → 3.3V) → OLED, temp sensors, misc I2C
  │           └── Fan drivers (24V PWM, 2–4× 120mm fans)
  │
  └── Fan power: 24V, ~0.5A per 120mm fan × 4 = 2A
```

#### 4.5.2 System Power Summary

| Configuration | Cards | 24V Current | PSU Input Power | Waste Heat |
|---------------|-------|-------------|-----------------|------------|
| **Min (1 card, typical load)** | 1 | 0.7A + 1.5A base = 2.2A | 53W | 5W |
| **Small (4 cards, typical)** | 4 | 2.8A + 1.5A base = 4.3A | 103W | 19W |
| **Medium (10 cards, typical)** | 10 | 7.0A + 1.5A base = 8.5A | 204W | 48W |
| **Full (16 cards, typical)** | 16 | 11.2A + 1.5A base = 12.7A | 305W | 77W |
| **Full (16 cards, MAX CONTINUOUS)** | 16 | 22.4A + 1.5A base = 23.9A | 574W | 150W |

**Note:** Max continuous is limited by the 3W per-cell isolated DC-DC (~540mA per cell, not 1A). System power is significantly reduced vs previous estimates due to lower per-cell power draw.

**Base board load estimate:**
- CM5: ~5W (5V @ 1A)
- 5× RTL8305NB switch ICs: ~5W
- Fans (4×): ~24W (24V × 0.25A each)
- OLED, sensors, LEDs: ~1W
- **Base board total: ~35W → ~1.5A from 24V** (including regulation losses)

#### 4.5.3 PSU Selection

| Scenario | Min PSU Rating | Recommendation |
|----------|---------------|----------------|
| ≤4 cards (32 cells), typical | 103W | **Meanwell LRS-150-24** (150W, 6.5A) |
| ≤10 cards (80 cells), typical | 204W | **Meanwell LRS-350-24** (350W, 14.6A) |
| 16 cards, typical | 305W | **Meanwell LRS-350-24** (350W, 14.6A) |
| 16 cards, max continuous | 574W | **Meanwell LRS-600-24** (600W, 25A) |

**Reality check:** The 3W per-cell isolated DC-DC significantly reduces system power. Even 16 cards at max continuous only draws ~574W, easily handled by a single LRS-600-24. System power requirements are roughly halved compared to the original 8W/cell estimate.

**For initial Phase 1–3 development (≤4 cards):** LRS-150-24 is sufficient.

### 4.5a Per-Slot Power Switching & Monitoring

Each card slot has individual power isolation via a relay, with per-slot current monitoring and a hardware E-stop that kills all slots simultaneously.

#### 4.5a.1 Per-Slot Relay

Each of the 16 slots has a dedicated **Omron G5Q-1A4 DC24** relay (SPST-NO, 10A @ 30VDC, LCSC C89311, $0.52):

| Parameter | Value |
|-----------|-------|
| Contact form | SPST-NO (1 Form A) |
| DC switching rating | 10A @ 30VDC (300W max) |
| Per-slot load | 3.45A @ 24V worst case (83W) — 3× margin |
| Coil voltage | 24VDC |
| Coil power | 200mW (coil resistance 2.88 kΩ) |
| Coil current | 8.3 mA per relay |
| Contact material | Cadmium-free Ag alloy |
| Mechanical life | 10M cycles |
| Electrical life | 100K cycles (at rated load) |
| Footprint | 20 × 10 mm (4-pin THT) |

**Why relay instead of solid-state (BTS5215L, P-FET, etc.):**
- Relay provides galvanic isolation when open — true card disconnect
- E-stop works by simply cutting relay coil supply — no firmware involved, purely hardware safety
- Zero on-resistance concerns (relay contacts < 100 mΩ vs. FET RDS(on))
- Dual-purpose: replaces both the E-stop bus relay and per-slot FET switch

#### 4.5a.2 Relay Drive Circuit

```
24V_BUS ─────────────────────────────────────────→ Relay Common (pin 1)
                                                     Relay NO (pin 2) ─→ INA3221 shunt ─→ Slot connector

24V_COIL ──[E-Stop NC]──┬──────────────────────────→ Relay coil + (pin 3)
                         │
                         │   Relay coil - (pin 4) ──→ N-FET drain
                         │                            N-FET source → GND
                         │                            N-FET gate ← MCP23017 GPIO (via 1k resistor)
                         │                            Flyback diode (1N4148) across coil
                         │
                         └── (repeated ×16)
```

**Drive components per slot:**
- 1× N-FET (IRLML0040 or similar, SOT-23, 200mW drive) — gate driven by MCP23017 GPIO
- 1× Flyback diode (1N4148W, SOD-323) — protects N-FET from coil back-EMF
- 1× 1kΩ gate resistor (0402)

**GPIO expanders:**
- 2× MCP23017 (16-bit I2C GPIO) on CM5 I2C bus — 8 relay drives each
- MCP23017 #1 (addr 0x20): slots 0–7
- MCP23017 #2 (addr 0x21): slots 8–15
- Default power-on state: all outputs LOW → all relays open → all cards de-powered
- Firmware sequence: discover card → close relay → wait for card boot → establish TCP

**E-stop:**
- NC mushroom-head button in series with 24V_COIL supply
- Pressing E-stop breaks coil supply to all 16 relays → all relays open → all cards instantly lose 24V
- Does NOT affect base board power (CM5, switches, fans stay running)
- Total coil current: 16 × 8.3 mA = 133 mA (well within E-stop contact rating)

#### 4.5a.3 Per-Slot Current Monitoring

**INA3221** (3-channel current/power monitor, I2C) — 6× INA3221 monitors 16 slots (3 channels × 6 = 18 channels, 16 used):

| Parameter | Value |
|-----------|-------|
| Shunt resistor | 10 mΩ (per slot, 0.1W at 3.45A, 1206 package) |
| Full-scale current | 8.19A (±81.92 mV range, 10 mΩ shunt) |
| Resolution | ~0.5 mA (13-bit ADC, 40 µV LSB) |
| Position | Between relay NO contact and slot connector (downstream) |
| Bus voltage measurement | Yes — monitors 24V at each slot |

**INA3221 I2C addressing (6 devices):**
- Behind TCA9548A mux on CM5 I2C bus (same mux used for other backplane I2C devices)
- All INA3221 at address 0x40 (A0=GND), selected by mux channel

**INA228** (main bus monitor):
- Single device on 24V bus input (before relay distribution)
- Monitors total system current + power
- I2C address 0x45 on CM5 I2C bus

#### 4.5a.4 Backplane I2C Bus (CM5)

All backplane I2C devices share a single I2C bus on the CM5:

| Device | Qty | Address | Muxed? | Function |
|--------|-----|---------|--------|----------|
| TCA9548A | 1 | 0x70 | — | I2C mux for INA3221 + other backplane devices |
| MCP23017 #1 | 1 | 0x20 | No | Relay drive GPIO (slots 0–7) |
| MCP23017 #2 | 1 | 0x21 | No | Relay drive GPIO (slots 8–15) |
| INA228 | 1 | 0x45 | No | Main bus current monitor |
| INA3221 ×6 | 6 | 0x40 | Yes (mux ch 0–5) | Per-slot current monitoring |
| TCA6408 #1 | 1 | 0x20 | Yes (mux ch 6) | Calibration relay drive (slots 0–7) |
| TCA6408 #2 | 1 | 0x21 | Yes (mux ch 7) | Calibration relay drive (slots 8–15) |
| EMC2101 | 1 | 0x4C | No | Fan controller + base temp |
| TMP117 | 1 | 0x48 | No | Base board temp sensor |

### 4.6 Thermal Budget & Cooling

#### 4.6.1 Heat Sources by Level

| Level | Source | Max Continuous | Typical |
|-------|--------|---------------|---------|
| Per cell | Isolated DC-DC (B2412S-3WR2) | 0.75W | 0.38W |
| Per cell | Buck regulator (TPS563201) | 0.3W | 0.14W |
| Per cell | LDO regulator (TLV75901) | 0.25W | 0.15W |
| Per cell | Other (relay, shunt, ctrl rail) | 0.22W | 0.15W |
| **Per cell total** | | **1.5W** | **0.92W** |
| Per card | Card-level iso DC-DC (24V→24V) | 3.3W | 1.7W |
| **Per card (8 cells + card electronics)** | | **~15.4W** | **~9.6W** |
| Base board | CM5 + switches + fans | ~10W | ~10W |
| **System (16 cards)** | | **257W** | **164W** |

#### 4.6.2 Critical Thermal Points

1. **Card-level isolated DC-DC (24V→24V)** — hottest single component on card (~3.3W waste at max continuous). Needs adequate PCB copper pour and airflow.
2. **Per-cell isolated DC-DC (B2412S-3WR2)** — 0.75W in SIP-4 package. Through-hole mounting provides good thermal path.
3. **LDO (TLV75901 in WSON-6)** — 0.25W in 2×2mm package. Θ_JA ≈ 60°C/W → 15°C rise. Comfortable.
4. **Card-level: 8 per-cell DC-DCs + card DC-DC in close proximity** — ~9.3W waste in a ~200×120mm card area. Board thermal relief + forced air mandatory.

#### 4.6.3 Cooling Strategy

| Method | Details |
|--------|---------|
| **Forced air** | Front-to-rear through enclosure, across card surfaces |
| **Fans** | 2–4× 120mm PWM fans on base board exhaust |
| **Fan controller** | CM5 I2C: PID loop on max(card temps, exhaust temp) |
| **Thermal shutdown** | Per-card: STM32H7 self-disables cells if T > 85°C. System: CM5 can kill 24V to individual slots via relay. |

**Airflow requirement:**

| Configuration | Waste Heat | CFM Required (ΔT=20°C) | Fans Needed |
|---------------|-----------|------------------------|-------------|
| 4 cards typical | 38W | 1.1 CFM | 1× 120mm (low speed) |
| 10 cards typical | 96W | 2.7 CFM | 1× 120mm (low-medium) |
| 16 cards typical | 164W | 4.7 CFM | 1× 120mm (medium) |
| 16 cards max continuous | 257W | 7.3 CFM | 2× 120mm (low-medium) |

Formula: CFM = P_waste / (1.76 × ΔT), where 1.76 = air heat capacity factor in CFM·°C/W.

Typical 120mm fan: 40–100 CFM at moderate speed. **2× 120mm fans provide 80–200 CFM — substantial margin even at worst case.**

### 4.7 Per-Cell Component Status (v1 → v2)

| Component | v1 Part | v2 Part | Change Required | Priority |
|-----------|---------|---------|-----------------|----------|
| Isolated DC-DC | B1205S-2WR2 (12V→5V, 2W) | **YLPTEC B2412S-3WR2** (24V→12V, 3W, C5369517) | **Selected** — $1.95, 325 stock | **Resolved** |
| Digital Buck | TPS563201 (3A, SOT-23-6) | **TPS563201** (reuse, 12V input) | Proven, 12V within 4.5–17V range | **Resolved** |
| Digital LDO | TLV75901 (1A, 6V max in) | **TLV75901** (reuse) | Now works — buck max 5.5V < Vin 6V | **Resolved** |
| ~~Pi Filter~~ | ~~YNR4030-101M + 2×10µF~~ | **Removed** | Inductor DCR causes unacceptable voltage drop at 1A; LDO PSRR sufficient | — |
| Current Sensor | INA185A2 + 100mΩ shunt | INA185A2 + **50mΩ** shunt | Shunt value change | P2 |
| Output Relay | HFD4/5-SR DPDT | Same | Verify coil supply from 12V domain | P2 |
| Control LDO | LDK220M-R (5V→3.3V) | **LDK220M-R** (12V→3.3V, Vin max 13.2V) | **Reuse** — 12V within range | **Resolved** |
| Buck DAC | MCP4725 (addr 0x61) | Same | — | — |
| LDO DAC | MCP4725 (addr 0x60) | Same | — | — |
| ADC | ADS1115 (16-bit, I2C) | **ADS131M04** (24-bit, SPI, 32kSPS) | **Upgrade** — 4ch simultaneous, 10× faster | **Upgrade** |
| GPIO Expander | TCA6408 (addr 0x20) | Same | — | — |
| I2C Isolator | ISO1640BDR | Same | I2C path for DACs + GPIO | — |
| SPI Isolator | — | **ISO7741FDBQR** (4ch, 3F+1R) | **New** — isolates SPI for ADS131M04 | **New** |

**Card-level components (new in v2):**

| Component | Part | Function | Notes |
|-----------|------|----------|-------|
| Card-level isolated DC-DC | TDK20-24S24WH (or similar 24V→24V) | Backplane ↔ card isolation | 20W, proven in Peak 48ch |
| Card-level buck | TPSM84209RKHR | 24V_iso → 3.3V for MCU domain | Integrated inductor, 2.5A |
| Discrete Ethernet transformer | TBD | Ethernet PHY isolation | Required since Ethernet passes through backplane |
| UART digital isolator | TBD | UART isolation for card programming | Backplane routes UART via slot mux |

### 4.8 Power Architecture Open Items

| # | Item | Priority | Impact |
|---|------|----------|--------|
| ~~1~~ | ~~Select 24V→8V isolated DC-DC~~ | ~~P0~~ | **RESOLVED** — YLPTEC B2412S-3WR2 (24V→12V, 3W, C5369517, $1.95) |
| ~~2~~ | ~~Resolve LDO for 6V output~~ | ~~P1~~ | **RESOLVED** — TPS563201 buck max 5.5V, TLV75901 works as-is |
| ~~3~~ | ~~Recalculate feedback divider~~ | ~~P2~~ | **RESOLVED** — Reusing TPS563201 with v1 feedback network (Vref=0.768V, proven) |
| 4 | Determine relay coil supply in 12V isolated domain (5V LDO or 3V coil variant) | P2 | Component selection |
| ~~5~~ | ~~PSU sizing~~ | ~~P2~~ | **RESOLVED** — LRS-350-24 sufficient for 16 cards typical, LRS-600-24 for max continuous |
| ~~6~~ | ~~Select per-cell control rail LDO~~ | ~~P2~~ | **RESOLVED** — LDK220M-R reused (Vin max 13.2V, 12V input OK) |
| ~~7~~ | ~~Select card-level 3.3V regulator~~ | ~~P2~~ | **RESOLVED** — TPSM84209 (24V_iso → 3.3V direct) |
| ~~8~~ | ~~Per-slot 24V relay/FET~~ | ~~P3~~ | **RESOLVED** — 16× Omron G5Q-1A4 DC24 relay, MCP23017 + N-FET drive, E-stop |
| 9 | **Evaluate 3W per-cell DC-DC limit** — B2412S-3WR2 limits cell to ~540mA continuous. For full 1A, need 5W+ part (MORNSUN B2412S-5WR2 or similar) or accept 540mA limit. | P1 | Max cell current |
| 10 | **Select card-level isolated DC-DC** — TDK20-24S24WH ($6.21) is expensive. Evaluate alternatives for 24V→24V, 20W+. | P2 | Card-level BOM cost |
| 11 | **Select discrete Ethernet transformer** — Required for backplane Ethernet isolation. | P1 | Card PCB design |
| 12 | **Select UART digital isolator** — For card programming/debug via backplane. | P2 | Card PCB design |

### 4.9 Control Loop Margins Analysis

This section validates that the analog signal chain — DAC → regulator → sensor → ADC — supports the target 1–10 kHz control loop with the required ±1 mV voltage accuracy and ±0.1 mA current accuracy.

#### 4.9.1 Component Bandwidth Summary

| Component | Parameter | Value | Source |
|-----------|-----------|-------|--------|
| **TPS563201 buck** | Switching frequency | 500 kHz | Datasheet |
| | Control mode | D-CAP2 (internally compensated) | No external compensation needed |
| | Typical loop BW | ~50 kHz (est.) | D-CAP2 typical for 500 kHz f_sw |
| | Transient response | <100 µs to 1% (est.) | Fast — no external compensation delay |
| | Output ripple | ~20–40 mV p-p (typ.) | With 2× 22 µF ceramic output caps |
| | Soft-start time | ~5 ms (typ.) | — |
| **TLV75901 LDO** | PSRR at 100 kHz | 45 dB | Datasheet |
| | PSRR at 500 kHz (buck f_sw) | ~31 dB (est.) | Rolls off ~20 dB/decade above 100 kHz |
| | Output noise | 53 µVrms | Integrated 10 Hz – 100 kHz |
| | Dropout voltage | 225 mV max @ 1A | Rises with temperature |
| | Output accuracy | ±0.7% (typ), ±1% (max) | Before calibration |
| | Min output cap | 1 µF | Using 10 µF ceramic (0805) |
| **MCP4725 DAC** | Resolution | 12-bit (4096 steps) | — |
| | Settling time | 6 µs (typ) | To within ±0.5 LSB |
| | I2C write time (400 kHz) | ~100 µs per 3-byte write | Dominant DAC update bottleneck |
| | Output range | 0 – VDD (3.3V) | Rail-to-rail |
| **INA185A2 current sense** | Bandwidth (-3 dB) | 105 kHz @ 50 V/V | Datasheet |
| | Slew rate | 2 V/µs | — |
| | Gain | 50 V/V (fixed) | — |
| | Input offset | ±55 µV (max) | ±100 µV at V_CM = 12V |
| | Gain error | ±0.2% (max) | — |
| **ADS131M04 ADC** | Max sample rate | 32 kSPS/ch | All 4 channels simultaneous |
| | Digital filter -3 dB | ~0.4 × f_DR | sinc3; at 32 kSPS → ~13 kHz |
| | ENOB at 4 kSPS | ~20 bits | At gain = 1 |
| | ENOB at 32 kSPS | ~17 bits | Higher noise floor at max rate |
| | SPI read time | ~10 µs per cell | 4ch × 24-bit at 10 MHz SPI |

#### 4.9.2 Control Loop Timing Budget

The firmware control loop reads ADC, computes PID, and writes DAC setpoints. Two operating modes:

**Full control loop (setpoint change → DAC → regulator → ADC → verify):**

| Step | Duration | Notes |
|------|----------|-------|
| I2C mux select (TCA9548A) | ~50 µs | Channel select for target cell |
| I2C DAC write × 2 (buck + LDO) | ~200 µs | 2× MCP4725 at 400 kHz |
| DAC settling | 6 µs | Negligible vs I2C time |
| Buck transient response | ~100 µs | D-CAP2 response (500 kHz) |
| LDO transient response | ~50 µs | Tracks buck output |
| ADC conversion period | 31–250 µs | At 32 kSPS → 31 µs; at 4 kSPS → 250 µs |
| SPI ADC read | ~10 µs | 4ch × 24-bit |
| **Total per cell** | **~420–640 µs** | Depending on ADC rate |
| **8 cells (single I2C bus)** | **~3.4–5.1 ms** | 8 cells sequential on 1 bus |

**Maximum control loop rates:**

| Mode | Rate | Notes |
|------|------|-------|
| Full loop (DAC update + ADC read, 8 cells) | ~195–295 Hz | I2C-bottlenecked (single bus) |
| Measurement only (ADC read, 8 cells) | ~900 Hz – 12 kHz | SPI-limited, depends on sample rate |
| Per-cell (single cell control) | ~1.6–2.4 kHz | Full DAC + ADC cycle |
| ADC streaming (per channel) | up to 32 kHz | ADC data ready interrupt |

**Conclusion:** Full 8-cell control loop tops out at ~195–295 Hz with single I2C bus. Measurement-only streaming can reach ~12 kHz. For applications requiring >1 kHz control per cell, use event-driven DAC updates (only write DAC when setpoint changes) and continuous ADC streaming. The SPI DAC upgrade path (§4.9.9) eliminates the I2C bottleneck entirely.

#### 4.9.3 Voltage Setpoint Resolution

The MCP4725 (12-bit, 0–3.3V) modulates the LDO feedback network through a control resistor. The effective output voltage resolution depends on the feedback divider:

**LDO (TLV75901) feedback network (from v1 codebase):**
- R_top = 60 kΩ, R_bottom = 10 kΩ, R_ctrl = 43 kΩ
- Vref = 0.55V
- DAC LSB = 3.3V / 4096 = **0.806 mV** (at DAC output)
- At DAC output, the control resistor injects current into the feedback node
- Effective output voltage per DAC LSB: ~0.806 mV × (R_top ∥ R_bottom) / R_ctrl × (1 + R_top/R_bottom) ≈ **0.9 mV/LSB** (approximate — actual value depends on exact injection point)

**Buck (TPS563201) feedback network (reused from v1, Vref = 0.768V):**
- R_top = 37 kΩ, R_bottom = 10 kΩ, R_ctrl = 24 kΩ (proven values from v1)
- Similar architecture, but buck only needs coarse control (±0.5V tracking margin)
- Buck LSB resolution ~1–2 mV/LSB — sufficient for tracking

**Voltage setpoint accuracy budget (at output):**

| Error Source | Magnitude | Notes |
|-------------|-----------|-------|
| DAC quantization (LDO) | ±0.45 mV | ±0.5 LSB at ~0.9 mV/LSB |
| DAC INL | ±4 LSB = ±3.6 mV | 12-bit DAC typical |
| LDO reference accuracy | ±0.7% = ±35 mV | At 5V output, before calibration |
| Feedback resistor tolerance | ±2% | Compounds with reference error |
| **Total (uncalibrated)** | **~±40 mV** | Dominated by LDO reference + resistors |
| **After calibration** | **< ±1 mV** | ADC resolution (~10 µV) enables fine correction |

Calibration eliminates the static errors (reference, resistor tolerance, DAC INL). The residual error is bounded by DAC quantization (~0.9 mV) and ADC noise (~10 µV). The firmware PID loop dithers between adjacent DAC codes to achieve sub-LSB average accuracy.

#### 4.9.4 Current Measurement Accuracy

**Signal chain: Shunt → INA185A2 (50×) → 2:5 divider → ADS131M04 CH2**

| Error Source | Magnitude (referred to current) | Notes |
|-------------|--------------------------------|-------|
| Shunt resistance tolerance | ±1% = ±10 mA at 1A | 50 mΩ ±1% shunt |
| Shunt tempco | ~50 ppm/°C = ±0.5% over 100°C | Standard thin-film |
| INA185A2 gain error | ±0.2% = ±2 mA at 1A | — |
| INA185A2 offset voltage | ±55 µV / (50 × 0.05Ω) = **±22 µA** | Referred to input current |
| ADC quantization | ~1.9 µA per LSB | At ~19 ENOB (see §4.2.8) |
| Divider ratio tolerance | ~0.1% (matched resistors) | Calibrated out |
| **Total (uncalibrated)** | **~±12 mA at 1A** | Dominated by shunt tolerance |
| **After calibration** | **< ±0.1 mA** | INA offset + ADC noise floor |

The ±0.1 mA target is achievable after per-cell calibration. The INA185A2 offset (±22 µA referred to current) and ADC noise (~1.9 µA) are both well below 0.1 mA.

#### 4.9.5 Buck–LDO Tracking Margin

The buck must maintain enough headroom above the LDO output for the LDO to stay in regulation. The margin budget:

| Parameter | Value | Condition |
|-----------|-------|-----------|
| **LDO dropout (typ)** | 190 mV | 1A, 25°C |
| **LDO dropout (max)** | 225 mV | 1A, 25°C |
| **LDO dropout (hot, est.)** | ~300 mV | 1A, 85°C (MOSFET R_DS(ON) increase) |
| **Buck tracking setpoint** | V_out + 500 mV | Firmware-controlled |
| **Available margin** | 500 mV - 300 mV = **200 mV** | Worst case (hot, max current) |

**Buck output ripple impact:**
- Buck ripple: ~20–40 mV p-p at 500 kHz
- LDO PSRR at 500 kHz: ~31 dB (estimated from 45 dB at 100 kHz, rolling off ~20 dB/decade)
- Ripple at LDO output: 40 mV × 10^(-31/20) ≈ **1.1 mV p-p** (worst case)
- LDO output noise: 53 µVrms (broadband)
- **Combined output ripple: ~1.1 mV p-p** — within ±1 mV target after averaging by ADC
- Note: Lower buck switching frequency (500 kHz vs 1.2 MHz) means LDO PSRR is actually better at the ripple frequency

**Buck transient during setpoint change:**
- D-CAP2 responds in ~50–100 µs (no external compensation)
- LDO absorbs the buck transient with its output capacitor (10 µF)
- LDO load-step response: ~50 µs to settle within 1% for 500 mA step
- The 200 mV margin covers the buck's transient undershoot during step changes

**Tracking at low output voltage:**
- At V_out = 0.5V, buck setpoint = 1.5V (well above buck minimum 0.768V)
- At V_out = 0V (disabled), buck output = 0.768V (minimum), LDO in dropout → output ≈ 0.768V - 0.225V = 0.543V
- True 0V requires disabling the buck (EN pin via GPIO P2)

**Conclusion:** 500 mV tracking margin provides adequate headroom across the full operating range. The 200 mV worst-case margin above dropout at 85°C is tight but sufficient since the D-CAP2 buck maintains excellent load regulation.

#### 4.9.6 Signal Chain Bandwidth vs Control Loop

The control loop bandwidth must be less than the analog signal chain bandwidth at every stage. Verification:

| Stage | Bandwidth | Margin over 10 kHz loop | OK? |
|-------|-----------|------------------------|-----|
| Buck internal loop (D-CAP2) | ~50 kHz | 5× | Yes |
| LDO bandwidth | >100 kHz | >10× | Yes |
| INA185A2 (current sense) | 105 kHz | 10× | Yes |
| Anti-alias filter (CH0) | 50 kHz | 5× | Yes |
| Anti-alias filter (CH1/2/3) | 100 kHz | 10× | Yes |
| ADS131M04 digital filter at 32 kSPS | ~13 kHz | 1.3× | Marginal at 10 kHz |
| ADS131M04 digital filter at 16 kSPS | ~6.4 kHz | **0.64×** | **Limits loop to ~6 kHz** |
| I2C DAC update (single cell) | ~5 kHz | 0.5× | **Bottleneck** |

**Key findings:**
1. **I2C DAC update rate is the control loop bottleneck** at ~5 kHz per cell (two 100 µs DAC writes per update). For 8 cells sequential on 1 bus → ~285 Hz full-loop rate.
2. **ADC digital filter** at 32 kSPS gives ~13 kHz -3 dB bandwidth — sufficient for 10 kHz control, but with 3 dB attenuation at 10 kHz. For flat response up to 10 kHz, oversample at 32 kSPS.
3. **Analog signal chain** (regulators + INA + anti-alias filter) is comfortably above 10 kHz at every stage.
4. **For >5 kHz control:** Would need faster DAC interface (SPI DAC or higher-speed I2C). Current architecture supports ~195–295 Hz full 8-cell control (single I2C bus), or ~1.6–2.4 kHz per-cell with event-driven updates.

#### 4.9.7 Noise Floor and Measurement Resolution

**Voltage measurement (CH0/1/3):**

| Parameter | Value |
|-----------|-------|
| ADC ENOB at 4 kSPS | ~20 bits → ~1.1 µV at ADC → **~5.7 µV at signal** (×5 divider) |
| ADC ENOB at 32 kSPS | ~17 bits → ~9.2 µV at ADC → **~46 µV at signal** |
| LDO output noise (53 µVrms) | **53 µV** (broadband, reduced by anti-alias filter) |
| Buck ripple at LDO output | ~1.1 mV p-p → **~320 µVrms** (sinusoidal approx) |
| **Dominant noise source** | **Buck ripple through LDO** |

At 4 kSPS, the ADS131M04 digital filter attenuates 500 kHz buck ripple by >50 dB — effectively eliminating it. The limiting noise source becomes LDO broadband noise (~53 µV) which is well below the ±1 mV accuracy target.

**Current measurement (CH2):**

| Parameter | Value |
|-----------|-------|
| ADC resolution (19 ENOB, 10 kHz) | ~4.8 µV at ADC → ~1.9 µA at shunt |
| INA185A2 output noise | ~100 µVrms (estimated from 105 kHz BW) → ~40 µA referred to input |
| INA185A2 offset error | ±55 µV → ±22 µA referred to current |
| **Noise floor (1σ)** | **~40 µA** (INA noise-limited at high BW) |
| **At 4 kSPS (filtered)** | **~5 µA** (anti-alias filter + digital filter reduce noise) |

The ±0.1 mA accuracy target requires ~2.5σ margin above the noise floor. At 4 kSPS with filtering, the ~5 µA noise floor provides >20× margin — easily achieved.

#### 4.9.8 Summary: Control Margin Verification

| Requirement | Target | Achieved | Margin | Status |
|-------------|--------|----------|--------|--------|
| Voltage accuracy | ±1 mV | ~±0.5 mV (calibrated) | 2× | **OK** |
| Current accuracy | ±0.1 mA | ~±0.05 mA (calibrated) | 2× | **OK** |
| Voltage resolution | <0.1 mV | ~10 µV (ADC) | 10× | **OK** |
| Control loop rate (8 cells) | 1 kHz | ~195–295 Hz | **0.2–0.3×** | **BELOW TARGET** (single I2C bus) |
| Control loop rate (per cell) | 10 kHz | ~1.6–2.4 kHz | **0.16–0.24×** | **BELOW TARGET** |
| Measurement rate (8 cells) | 10 kHz | ~12 kHz | 1.2× | **OK** |
| Buck–LDO tracking margin | >0 mV | 200 mV (hot, max load) | — | **OK** |
| Output ripple (at LDO out) | <1 mV | ~1.1 mV p-p (~0.3 mVrms) | — | **OK** (averaged) |

**Open issues identified:**
1. **Full 8-cell control loop rate** (~195–295 Hz with single I2C bus) is below the 1 kHz target. This is an I2C bottleneck — see §4.9.9 for SPI DAC upgrade path which eliminates this bottleneck.
2. **Output ripple** of ~1.1 mV p-p marginally exceeds ±1 mV. Acceptable because: (a) the ADC averages it, (b) DUT sees the averaged value, (c) the spec target is for the measured/reported value, not the instantaneous waveform. The 500 kHz buck switching frequency actually provides better LDO PSRR than the 1.2 MHz TPSM863257 would have.

#### 4.9.9 SPI DAC Upgrade Path (I2C Bottleneck Mitigation)

The I2C MCP4725 DAC is the control loop bottleneck. Each DAC write takes ~100 µs at 400 kHz I2C (mux select + 3-byte write), and two DACs per cell (buck + LDO) = ~200 µs per cell just for setpoint updates. Replacing the MCP4725 with an SPI DAC eliminates this bottleneck since the SPI bus is already routed through the ISO7741 isolator to each cell.

**Candidate SPI DACs (JLCPCB stock, verified via `ato search parts`):**

| Part | LCSC | Resolution | Settling | SPI Clock | Package | Price | Stock | Notes |
|------|------|-----------|----------|-----------|---------|-------|-------|-------|
| **DAC7311IDCKR** | C128601 | 12-bit | 9 µs | 50 MHz | SOT-363-6 | **$0.86** | 12,106 | Tiny, cheap, pin-compatible family |
| DAC8311IDCKT | C48227 | 14-bit | 6 µs | 50 MHz | SC70-6 | $5.45 | 6 | Drop-in upgrade — low stock, expensive |
| DAC8551IADGKR | C524800 | 16-bit | 10 µs | 30 MHz | VSSOP-8 | $2.51 | 1,082 | Ultralow glitch, 1 LSB INL |
| MCP4922-E/SL | C39851 | 12-bit × 2ch | 4.5 µs | — | SOIC-14 | $3.05 | 2,715 | Dual-channel — one per cell replaces both DACs |

All use external reference (VDD as reference). All have built-in power-on reset.

**Recommended: DAC7311IDCKR** (C128601, 12-bit, SOT-363-6, $0.86) — same 12-bit resolution as MCP4725, 9 µs settling, 50 MHz SPI, excellent stock. Use 2× per cell (buck + LDO). The DAC8311 is pin-compatible for a 14-bit upgrade but currently has poor stock and 6× the price.

**SPI DAC timing improvement:**

| Operation | MCP4725 (I2C) | DAC7311 (SPI) | Speedup |
|-----------|--------------|---------------|---------|
| Single DAC write | ~100 µs | **~1 µs** (16 clocks at 10 MHz SPI + CS overhead) | **100×** |
| 2 DACs per cell | ~200 µs | ~2 µs | 100× |
| 8 cells (2 SPI buses) | ~2 ms (single I2C) | ~8 µs | 250× |
| Full loop (DAC + ADC, 8 cells) | ~3.5 ms (**285 Hz**) | ~120 µs (**8.3 kHz**) | **29×** |

**Architecture change required:**

The MCP4725 sits on the isolated I2C bus (behind ISO1640). SPI DACs would share the same SPI bus as the ADS131M04 ADC (behind ISO7741). This is viable because:

1. **ISO7741 has 3 forward channels** (SCLK, MOSI, CS) — the CS line can be shared between ADC and DACs using additional GPIO-driven chip selects on the isolated side
2. **But:** ISO7741 only has **1 reverse channel** (MISO), and the DAC7311 has no MISO (write-only, 3-wire). No bus contention for write-only DACs.
3. **Two DACs per cell** need 2 additional CS lines on the isolated side. Options:
   - Route 2 more CS lines through the ISO7741's unused forward channel capacity (ISO7741 is 4-ch: 3F+1R, currently using all 3F for SCLK/MOSI/CS_ADC)
   - Use a **second isolator** (ISO7721 — 2-ch forward, $0.30) for the two DAC CS lines
   - Use a **SPI demux** on the isolated side (e.g., 74HC138 3:8 decoder driven by the existing GPIO expander TCA6408 — already on isolated I2C)

**Simplest approach:** Keep the ISO7741 for ADC (SCLK, MOSI, CS_ADC + MISO_ADC). Add an **ISO7720DR** (2-ch forward-only, SOIC-8, C366164, **$0.57**, 6,299 stock) to carry CS_BUCK_DAC and CS_LDO_DAC across the isolation barrier. SCLK and MOSI are already on the isolated side from ISO7741 — just wire them to the DACs directly (shared bus). The DAC7311 ignores bus traffic when CS is high (write-only, no MISO).

```
STM32H723 SPIx (non-isolated side)
  ├── SCLK ──→ ISO7741 ch A ──→ [isolated SCLK] → ADS131M04 + DAC7311 ×2
  ├── MOSI ──→ ISO7741 ch B ──→ [isolated MOSI] → ADS131M04 + DAC7311 ×2
  ├── CS_ADC ──→ ISO7741 ch C ──→ [isolated CS_ADC] → ADS131M04
  ├── CS_BUCK_DAC ──→ ISO7720 ch A ──→ [isolated CS_BUCK] → DAC7311 #1
  ├── CS_LDO_DAC ──→ ISO7720 ch B ──→ [isolated CS_LDO] → DAC7311 #2
  └── MISO ←── ISO7741 ch D (reverse) ←── ADS131M04
```

**Impact on I2C bus:** The I2C bus (ISO1640) is still needed for TCA6408 GPIO expander (relay/enable control). But the DACs are removed from it, so I2C traffic drops from ~300 µs/cell to ~100 µs/cell. The I2C mux (TCA9548A) remains for GPIO addressing.

**Cost impact per cell (JLCPCB unit prices):**
- Add: 2× DAC7311 ($1.72) + 1× ISO7720DR ($0.57) = +$2.29
- Remove: 2× MCP4725 (~$1.00) = −$1.00
- **Net: +$1.29/cell** ($10.32/card, $165/system at 128 cells)
- At 100+ qty pricing: 2× DAC7311 ($1.05) + ISO7720 ($0.24) − 2× MCP4725 ($0.60) = **net +$0.69/cell**

**Recommendation:** This is a compelling upgrade. The DAC7311 is cheaper than the MCP4725, the SPI interface eliminates the I2C bottleneck, and the additional ISO7721 per cell is cheap. The control loop rate jumps from ~430 Hz to ~8 kHz for all 8 cells — comfortably exceeding the 1 kHz target.

**Decision status: P1 — should be adopted for v2 design.**

---

## 5. Backplane & Mechanical

### 5.1 Card Format (GPU-Style)

Cards plug vertically into the backplane via a card-edge connector. Like a GPU in a PC motherboard.

| Parameter | Value |
|-----------|-------|
| Card dimensions | TBD — ~200mm × 120mm estimated (8 cells + MCU) |
| Orientation | Vertical, edge connector plugging into horizontal backplane |
| Edge connector | Card-edge (gold fingers) or high-density connector (Samtec, TE) |
| Rear connector | Cell output, accessible from rear panel (Kelvin pairs) |
| Guide rails | Enclosure has guide rails for card alignment |
| Retention | Screw or latch at rear panel bracket |

### 5.2 Slot Enumeration (Hardware)

Each backplane slot hardwires a unique **slot ID** via resistor dividers or pulled-low GPIO lines. The card reads these at boot to know its position.

**Implementation:** 4 GPIO pins on the STM32H723 connected to the backplane connector. Each slot has a different combination of pull-up/pull-down resistors on these lines, giving 2⁴ = 16 unique addresses.

```
Backplane Slot 0:  ID[3:0] = 0000  (all pulled low)
Backplane Slot 1:  ID[3:0] = 0001  (ID0 pulled high)
Backplane Slot 2:  ID[3:0] = 0010  (ID1 pulled high)
...
Backplane Slot 15: ID[3:0] = 1111  (all pulled high)
```

The card firmware reads these at startup and uses the slot ID for:
- Ethernet IP address assignment (e.g., `10.0.0.{100 + slot_id}`)
- mDNS name: `cellsim-card-{slot_id}.local`
- UDP measurement stream tagging
- Reported to CM5 for card registry

### 5.3 Backplane Connector Pinout (per slot)

| Signal | Pins | Notes |
|--------|------|-------|
| Ethernet TX+/TX- | 2 | 100Ω differential, to GbE switch port (before card-level Eth transformer) |
| Ethernet RX+/RX- | 2 | 100Ω differential, from GbE switch port (before card-level Eth transformer) |
| 24V power | 4 | Multiple pins for ≥2A capacity per slot |
| GND | 4 | Multiple pins, low-impedance return |
| UART TX | 1 | CM5 → card (via slot mux + card UART isolator) |
| UART RX | 1 | Card → CM5 (via slot mux + card UART isolator) |
| Slot ID [3:0] | 4 | Per-slot resistor-coded (2⁴ = 16 slots) |
| Card present | 1 | Pulled high on card, read by CM5/switch |
| Reset | 1 | Open-drain, CM5 can reset individual cards |
| BOOT0 | 1 | For STM32 bootloader entry (via slot mux) |
| CAL_V+ | 1 | Per-slot calibration trace — cell output to slot's backplane relay |
| CAL_V- | 1 | Per-slot calibration trace — cell ground reference to slot's backplane relay |
| **Total** | **~20** | |

**Calibration bus:** Each slot has its own dedicated CAL_V+/CAL_V- traces from the card connector to a per-slot DPDT relay on the backplane (not a shared bus). Only one backplane relay is activated at a time, connecting that slot's calibration signals through to a common output with 100Ω series protection resistors and banana jacks for an external precision DMM. This two-level relay mux (per-cell on card + per-slot on backplane) eliminates the need for manual cable switching during calibration. See §8.7 for full architecture.

### 5.4 Cell Output Connector (per card, rear panel)

| Pin | Signal |
|-----|--------|
| F0, S0 | Cell 0 Force, Cell 0 Sense |
| F1, S1 | Cell 1 Force, Cell 1 Sense |
| ... | ... |
| F7, S7 | Cell 7 Force, Cell 7 Sense |
| GND_K | Kelvin ground Force |
| GND_K_S | Kelvin ground Sense |
| **Total** | **18 pins** |

**Connector candidates:** D-Sub 25-pin (fits 18 signals + spares), Molex Micro-Fit 3.0 (20-pin), or custom high-density.

### 5.5 Enclosure

- **19" rack mount**, 2U or 3U height
- **Front panel:** E-stop button, OLED display, status LEDs, RJ45 uplink, USB-C
- **Rear panel:** IEC C14 mains inlet, per-card cell output connectors (×16), possibly additional RJ45
- **Internal:** Meanwell PSU mounted, base board horizontal, cards vertical, fan(s) at exhaust end
- **Cooling:** 2-4× 120mm or 80mm fans, PWM-controlled, front-to-rear airflow
- **Card guide rails:** Machined or sheet-metal slots for card alignment

### 5.6 Connector Specification

All connectors in the system, organized by function with current/voltage ratings derived from the power architecture (§4).

#### 5.6.1 Master Connector Table

| # | Connector | Location | Qty | Voltage | Max Current (per pin) | Total Pins | Direction | Status |
|---|-----------|----------|-----|---------|----------------------|------------|-----------|--------|
| **Power Input** | | | | | | | | |
| 1 | IEC C14 mains inlet | Rear panel | 1 | 110–240 VAC | 10A | 3 (L/N/PE) | In | To create |
| 2 | PSU → Backplane (internal wiring) | Internal | 1 | 24V DC | 15A+ | 2 (+/−) | In | Wire harness |
| **Network** | | | | | | | | |
| 3 | RJ45 uplink (w/ magnetics) | Front panel | 1 | 3.3V (signal) | N/A (signal) | 8 + shield | In/Out | Available (`rj45-connectors`) |
| 4 | USB-C (CM5 debug) | Front panel | 1 | 5V | 500 mA | 24 (USB-C) | In/Out | Available (`usb-connectors`) |
| 5 | USB-C (card DFU) | Card top edge | 1/card | 5V | 500 mA | 24 (USB-C) | In/Out | Available (`usb-connectors`) |
| **Board-to-Board (Backplane)** | | | | | | | | |
| 6 | Card-edge connector (backplane side) | Backplane PCB | 16 | 24V / 3.3V | 1.5A (power pins) | ~40 (2×20) | Mating pair | To create |
| 7 | Card-edge connector (card side) | Card bottom edge | 1/card | 24V / 3.3V | 1.5A (power pins) | ~40 (2×20) | Mating pair | To create |
| **Cell Output** | | | | | | | | |
| 8 | Cell output connector (Kelvin) | Card rear bracket | 1/card | 0–5V | 1A (force pins) | 18 | Out | To create |
| **Thermistor Output** | | | | | | | | |
| 9 | Thermistor output connector | Card rear bracket | 1/card | 0–3.3V | <1 mA | 16 (8×2) | Out | To create |
| **Calibration** | | | | | | | | |
| 10 | Banana jacks (4mm, pair) | Internal chassis | 1 pair | 0–5V | <10 mA (DMM input) | 2 | Out | To create |
| **Debug / Dev** | | | | | | | | |
| 11 | SWD TC2030 (pogo) | Card top | 1/card | 3.3V | <50 mA | 6 | In/Out | Available (`programming-headers`) |
| 12 | Fan header (4-pin PWM) | Base board | 2–4 | 24V / 3.3V | 500 mA (fan) | 4 | Out | To create |
| 13 | OLED header (I2C, 4-pin) | Base board | 1 | 3.3V | <50 mA | 4 | Out | Pin header |
| 14 | E-Stop button | Front panel | 1 | 24V | ~133 mA (16 relay coils) | 2 (NC) | In | To create |

#### 5.6.2 Power Input Connectors

**IEC C14 Mains Inlet (#1)**

| Parameter | Value |
|-----------|-------|
| Standard | IEC 60320 C14 |
| Voltage rating | 250 VAC |
| Current rating | 10A (standard C14) |
| Mounting | Rear panel, snap-in or screw-mount |
| Integrated fuse | Yes — IEC fused inlet recommended |
| EMI filter | Optional integrated EMI filter variant |

**Current justification:** At 240V / 10A the C14 supports 2,400W — covers 16 cards at typical load (701W) with headroom. For worst-case scenarios (>1,200W at 110V), upgrade to **IEC C20** (16A / 250V) or use dual inlets.

| System Size | Typical Load | 110V Current | 240V Current | C14 OK? |
|-------------|-------------|-------------|-------------|---------|
| 4 cards (32 cells) | 230W | 2.1A | 1.0A | Yes |
| 10 cards (80 cells) | 466W | 4.2A | 1.9A | Yes |
| 16 cards (128 cells) | 701W | 6.4A | 2.9A | Yes |
| 16 cards worst-case | 2,046W | 18.6A | 8.5A | Yes (at 240V), marginal at 110V |

**PSU → Backplane Internal Wiring (#2)**

| Parameter | Value |
|-----------|-------|
| Wire gauge | 12 AWG minimum (for 15A+), or dual 14 AWG |
| Termination | Ring terminals or Anderson PowerPole (PP45) at PSU, soldered or screw terminal at backplane |
| Current | Up to 29.2A typical (16 cards) — may need dual PSU bus bars |
| Voltage | 24V DC |

For systems ≤10 cards, a single wire pair (12 AWG) from the Meanwell PSU screw terminals to the backplane is sufficient. For a full 16-card system, use bus bars or parallel conductors.

#### 5.6.3 Network Connectors

**RJ45 Uplink (#3)**

| Parameter | Value |
|-----------|-------|
| Type | RJ45 with integrated magnetics (Bob Smith termination) |
| Speed | 10/100/1000 Mbps (GbE) |
| Package | `atopile/rj45-connectors` → `RJ45_Horizontal_SMD_Magnetics` |
| Mounting | Front panel, horizontal SMD |
| Isolation | Magnetic isolation per IEEE 802.3 (1.5 kV) |
| Current | Signal-level only (no PoE) |

**USB-C Connectors (#4, #5)**

| Parameter | CM5 Debug (#4) | Card DFU (#5) |
|-----------|----------------|---------------|
| Standard | USB 2.0 over Type-C | USB 2.0 over Type-C |
| Package | `atopile/usb-connectors` → `USB2_0TypeCHorizontalConnector` | Same |
| Location | Front panel (base board) | Card top edge |
| Current | 500 mA (bus-powered) | 500 mA (DFU only) |
| Purpose | CM5 serial console, debug | STM32H723 DFU firmware recovery |

#### 5.6.4 Board-to-Board: Backplane Card-Edge Connector

This is the most critical connector — every card mates through it. Must carry 24V power (up to 5.14A per slot), GbE differential pairs, and control signals reliably through thousands of insertion cycles.

**Requirements:**

| Parameter | Requirement |
|-----------|-------------|
| Power capacity | 5.14A at 24V per slot (worst case), 1.64A typical |
| Signal integrity | 100Ω differential impedance for GbE (TX+/−, RX+/−) |
| Insertion cycles | ≥500 (cards swapped during maintenance/upgrades) |
| Pin count | ~20 signals × 2 rows = ~40 pins (see §5.3 pinout) |
| Keying | Mechanical keying to prevent misalignment / reversed insertion |
| Hot-plug | Desirable (power pins make-first / break-last) |
| Gold plating | Required for signal pins (≥30 µ" for reliability) |

**Current per pin breakdown (from §4.4):**

| Signal Group | Pins | Current per Pin | Total Current | Notes |
|--------------|------|----------------|---------------|-------|
| 24V power | 4 | 1.29A worst / 0.41A typ | 5.14A / 1.64A | Sized per §4.4 |
| GND return | 4 | 1.29A worst / 0.41A typ | 5.14A / 1.64A | Mirrors power |
| Ethernet TX+/TX− | 2 | Signal (<50 mA) | — | 100Ω differential |
| Ethernet RX+/RX− | 2 | Signal (<50 mA) | — | 100Ω differential |
| Slot ID [3:0] | 4 | Signal (<1 mA) | — | Resistor-coded |
| Card present | 1 | Signal (<1 mA) | — | Pull-up on card |
| Reset | 1 | Signal (<1 mA) | — | Open-drain |
| CAL_V+ | 1 | <10 mA | — | Per-slot trace to backplane relay |
| CAL_V− | 1 | <10 mA | — | Per-slot trace to backplane relay |
| **Total** | **~20** | | | |

**Candidate connectors:**

| Candidate | Pitch | Current/Pin | Pins Available | Features | Notes |
|-----------|-------|-------------|----------------|----------|-------|
| **PCIe-style card-edge (gold fingers)** | 1.0 mm | 1.5A (power) | 36–164 | Industry standard, cheap, proven | Requires gold plating on card PCB edge |
| **Samtec HSEC8** | 0.8 mm | 1.5A | 40–200 | High-speed (25 Gbps), robust | Expensive but excellent signal integrity |
| **TE MULTI-BEAM XLE** | 2.0 mm | 3.0A | 2–120 | High-current, blade-style | Good for power, less ideal for high-speed |
| **Molex Mirror Mezz** | 1.27 mm | 1.5A | 20–200 | Right-angle options, mixed signal | Good all-rounder |
| **2×25 pin header (2.54 mm)** | 2.54 mm | 3.0A | 50 | Simple, cheap, available | Poor signal integrity for GbE, no keying |

**Recommendation:** PCIe-style card-edge (gold fingers on card PCB, socket on backplane). This is the proven GPU/expansion card approach, handles mixed power + signal, supports hot-plug pin staggering, and keeps card BOM cost low (gold fingers are a PCB fab option, not a separate connector part on the card side).

#### 5.6.5 Cell Output Connector (Kelvin)

Each cell card has 8 Kelvin (4-wire) outputs plus shared Kelvin ground, totaling 18 conductors (see §5.4).

**Current requirements per conductor:**

| Conductor | Count | Max Current | Notes |
|-----------|-------|-------------|-------|
| Force (F0–F7) | 8 | **1A** each | Main current path to DUT |
| Sense (S0–S7) | 8 | <1 mA | Voltage measurement, high-impedance |
| GND_K (Force) | 1 | **8A** (all cells share) | Common current return |
| GND_K_S (Sense) | 1 | <1 mA | Kelvin ground sense |
| **Total pins** | **18** | | |

**Critical note on GND_K:** If all 8 cells drive 1A into a series-connected BMS string, return current flows through GND_K. In practice the DUT provides its own return path per cell, but worst-case the shared ground must handle 8A. Use **2–3 GND pins in parallel** or a high-current contact.

**Candidate connectors:**

| Candidate | Pins | Current/Pin | Mating Style | Pros | Cons |
|-----------|------|-------------|-------------|------|------|
| **D-Sub 25-pin (DA-25)** | 25 (18 used + 7 spare) | 5A (power contacts available) | Screw-lock, panel mount | Standard, lockable, rugged, cheap | Bulky per card; 16 cards = wide rear panel |
| **Molex Micro-Fit 3.0 (20-pin)** | 20 (18 used + 2 spare) | **5A per pin** | Push-to-lock | Compact, high current, locking | Slightly less rugged than D-Sub |
| **Molex Mini-Fit Jr (24-pin)** | 24 | **9A per pin** | Push-to-lock | Very high current capacity | Larger footprint than Micro-Fit |
| **Phoenix Contact MSTB (18-pos)** | 18 | **8A** | Screw terminal, plug-in | Easy field wiring, no crimping | Very bulky, expensive |
| **TE Micro MATE-N-LOK (20-pin)** | 20 | **3A per pin** | Push-to-lock | Compact, proven | Lower current than Micro-Fit |

**Recommendation:** **Molex Micro-Fit 3.0 (43045-series)**, 2×10 pin (20-position). Rated 5A per pin, which covers the 1A force lines with margin. Compact enough to fit 16 connectors across a 19" rear panel (~16.5mm pitch per connector ≈ 264mm for 16 cards, fits in 19" = 483mm with margin). Use 2–3 pins ganged for GND_K to handle 8A return.

**Rear panel pitch check:**
- 19" usable width ≈ 450 mm
- Micro-Fit 3.0, 2×10: housing width ≈ 24 mm
- 16 cards × 24mm = 384 mm — **fits comfortably** with room for mains + RJ45

#### 5.6.6 Thermistor Output Connector

Each thermistor card has 8 emulated thermistor outputs. Current requirements are minimal (BMS thermistor inputs typically use µA-level excitation).

**Current requirements per conductor:**

| Conductor | Count | Max Current | Notes |
|-----------|-------|-------------|-------|
| Thermistor output (T0–T7) | 8 | <1 mA | DAC → resistor → output (simulates NTC) |
| GND (shared) | 8 | <1 mA each | Reference ground per channel |
| **Total pins** | **16** | | |

**Candidate connectors:**

| Candidate | Pins | Current/Pin | Notes |
|-----------|------|-------------|-------|
| **Molex Micro-Fit 3.0 (2×8, 16-pin)** | 16 | 5A (overkill) | Same family as cell output — reduces BOM variety |
| **D-Sub 15-pin (DA-15)** | 15 (+ 1 shell ground) | 5A | Smaller than DA-25, standard |
| **JST XH (16-pin)** | 16 | 3A | Low cost, compact, common in consumer electronics |
| **2×8 keyed pin header (2.54 mm)** | 16 | 3A | Cheapest, uses IDC ribbon cable |

**Recommendation:** Use the **same connector family as cell output** (Molex Micro-Fit 3.0, 2×8) for BOM simplification and to allow the same rear panel cutout tooling. Thermistor and cell cards share the same slot and rear bracket — using the same connector footprint means a universal rear bracket design.

#### 5.6.7 Calibration Connectors

**Internal Banana Jacks (#10)**

| Parameter | Value |
|-----------|-------|
| Type | Standard 4mm banana socket (chassis-mount) |
| Quantity | 2 (CAL_V+ and CAL_V−) |
| Voltage | 0–5V (cell output range) |
| Current | <10 mA (DMM voltage measurement input impedance >10 MΩ) |
| Mounting | Internal chassis wall or base board edge, accessible when lid removed |
| Color coding | Red (CAL_V+), Black (CAL_V−) |

Not customer-facing. Used only during factory/service calibration with a Keysight DMM connected via standard banana-to-banana test leads.

#### 5.6.8 Debug & Auxiliary Connectors

**SWD TC2030 (#11)**

| Parameter | Value |
|-----------|-------|
| Package | `atopile/programming-headers` → `SWD_TC2030` |
| Pins | 6 (VCC, SWDIO, SWCLK, SWO, nRST, GND) |
| Current | <50 mA (debug interface) |
| Mounting | Card top surface (pogo contact pads, no mating connector needed) |
| Usage | Factory programming, development debugging |

**Fan Headers (#12)**

| Parameter | Value |
|-----------|-------|
| Type | 4-pin PWM fan header (KF2510 or standard PC fan) |
| Voltage | 24V (fan power), 3.3V (PWM + tach signals) |
| Current | Up to 500 mA per fan (120mm, 24V) |
| Quantity | 2–4 (one per fan zone) |
| Pins | +24V, GND, PWM, Tach |
| Controlled by | EMC2101 fan controller IC on base board |

**OLED Display Header (#13)**

| Parameter | Value |
|-----------|-------|
| Type | 4-pin header (2.54 mm pitch) |
| Pins | VCC (3.3V), GND, SCL, SDA |
| Current | <50 mA |
| Display | SSD1306 128×64 OLED (I2C) |
| Usage | System status on front panel (IP, temp, card count) |

**E-Stop Button (#14)**

| Parameter | Value |
|-----------|-------|
| Type | Panel-mount mushroom-head emergency stop button |
| Contacts | NC (normally-closed), latching |
| Circuit | Series with 24V_COIL supply bus — pressing breaks coil supply to all 16 per-slot relays, de-energizing them and opening all card power connections simultaneously |
| Current | ~220 mA max (16 relay coils × 200mW ÷ 24V = 8.3mA each × 16 = 133mA, plus MCP23017/N-FET overhead) |
| Voltage | 24V DC |
| Mounting | Front panel, red mushroom-head, twist-to-release |

#### 5.6.9 Connector Count Summary (Full 16-Card System)

| Connector Type | Qty on Base Board | Qty per Card | Total (16 cards) |
|----------------|-------------------|-------------|------------------|
| IEC C14 mains inlet | 1 | — | 1 |
| RJ45 uplink | 1 | — | 1 |
| USB-C (CM5) | 1 | — | 1 |
| Card-edge (backplane socket) | 16 | — | 16 |
| Card-edge (card side) | — | 1 | 16 |
| USB-C (card DFU) | — | 1 | 16 |
| SWD TC2030 | — | 1 | 16 |
| Cell/thermistor output | — | 1 | 16 |
| Banana jacks (calibration) | 1 pair | — | 1 pair |
| Fan headers | 2–4 | — | 2–4 |
| OLED header | 1 | — | 1 |
| E-stop button | 1 | — | 1 |
| PSU wiring (internal) | 1 | — | 1 |
| **Unique connector types** | | | **~10** |

#### 5.6.10 Open Items — Connectors

| Item | Decision Needed | Impact |
|------|----------------|--------|
| IEC C14 vs C20 | Max system size at 110V — C14 limits to ~10A (1,100W) | Rear panel cutout |
| Card-edge connector part selection | PCIe-style gold fingers vs Samtec/TE connector pair | Card PCB stackup, backplane layout |
| Cell output connector final selection | Micro-Fit 3.0 vs D-Sub vs other | Rear panel cutout, cable harness design |
| Rear panel density at 16 cards | 16 × output connectors + mains + RJ45 must fit in 19" | Fits at 384mm (450mm available) |
| Thermistor card same connector? | Same physical connector as cell card (universal bracket) vs smaller | BOM, rear bracket tooling |
| GND_K pin ganging | 2 or 3 pins for 8A return on cell output connector | Connector pin count (18→19 or 20) |
| Fan connector: 24V vs 12V fans | 24V fans (direct from bus) vs 12V fans (need regulator) | Fan header voltage, component availability |

---

## 6. Gigabit Ethernet Switch

### 6.1 Approach: Cascaded RTL8305NB

The Hyperion project already uses `atopile/realtek-rtl8305nb` — a 5-port GbE switch with integrated PHYs plus a separate CPU/RGMII port. For 16 card slots + CM5 + uplink, cascade 5× RTL8305NB switches:

```
                CM5 (RGMII — CPU port)
                    │
              ┌─────┴─────┐
              │ RTL8305NB  │ (Root switch)
              │  CPU port:  CM5 (RGMII)
              │  PHY Port 0: Uplink RJ45
              │  PHY Port 1: → Cascade A
              │  PHY Port 2: → Cascade B
              │  PHY Port 3: → Cascade C
              │  PHY Port 4: → Cascade D
              └─────┬─────┘
        ┌──────┬────┴────┬──────┐
   ┌────┴───┐ ┌┴───────┐ ┌┴──────┐ ┌┴──────┐
   │Switch A│ │Switch B│ │Switch C│ │Switch D│
   │4 slots │ │4 slots │ │4 slots │ │4 slots │
   │ (0-3)  │ │ (4-7)  │ │ (8-11) │ │(12-15) │
   └────────┘ └────────┘ └────────┘ └────────┘
```

**5× RTL8305NB** gives exactly 16 card ports. The CM5 connects via the root switch's dedicated CPU/RGMII port (not a PHY port), leaving all 5 PHY ports available: 1 for uplink + 4 for cascade. Each leaf switch uses 1 PHY port for cascade uplink + 4 PHY ports for card slots.

**Port allocation:**
- Root: CPU port = CM5 (RGMII), PHY Port 0 = Uplink RJ45, PHY Ports 1–4 = cascade to leaf switches
- Leaf A: PHY Port 0 = cascade uplink, PHY Ports 1–4 = slots 0–3
- Leaf B: PHY Port 0 = cascade uplink, PHY Ports 1–4 = slots 4–7
- Leaf C: PHY Port 0 = cascade uplink, PHY Ports 1–4 = slots 8–11
- Leaf D: PHY Port 0 = cascade uplink, PHY Ports 1–4 = slots 12–15

**Alternative:** If a higher-port-count switch IC becomes available/desirable, the cascade can be replaced. The GbE switch is designed as a separate atopile package for reusability.

### 6.2 Available Packages

| Package | IC | Notes |
|---------|----|-------|
| `atopile/realtek-rtl8305nb` | RTL8305NB | 5-port GbE switch, proven in Hyperion |
| `atopile/microchip-lan8742a` | LAN8742A | 100M PHY, per-card |
| `atopile/rj45-connectors` | RJ45 w/ magnetics | Front panel uplink |

---

## 7. Firmware Update (OTA)

### 7.1 Architecture

The CM5 must be able to update STM32H723 firmware on any card without physical access.

**Approach: Ethernet bootloader on STM32H723**

1. STM32H723 Flash is partitioned: bootloader (64 KB) + application (rest)
2. Bootloader runs on reset, checks for update flag or CM5 command
3. CM5 sends new firmware image over TCP to card's bootloader
4. Bootloader writes to Flash, verifies CRC, reboots into new application
5. Fallback: USB DFU if Ethernet bootloader is bricked

**Update flow:**
```
CM5                              Card (STM32H723)
 │                                    │
 ├── POST /api/firmware/upload ───→   │
 │   (binary + target slot/all)       │
 │                                    │
 ├── TCP: FIRMWARE_UPDATE command ──→ │
 │                                    ├── Enter bootloader
 │                                    │
 ├── TCP: Send chunks ──────────────→ ├── Write Flash
 │                                    │
 ├── TCP: VERIFY command ───────────→ ├── CRC check
 │                                    │
 ├── TCP: REBOOT command ───────────→ ├── Boot new firmware
 │                                    │
 ←── mDNS re-advertisement ─────────←┤
```

### 7.2 Version Management

- Each card reports its firmware version in mDNS TXT record and API response
- CM5 can detect version mismatches across cards
- Web UI shows firmware version per card, offers batch update

---

## 8. Communication & Software

### 8.1 Software Architecture (Peak-Style)

```
┌──────────────────────────────────────────────────────────────────┐
│                         CM5 (Base Board)                          │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  FastAPI REST Server (port 8000)                             │ │
│  │                                                              │ │
│  │  GET  /api/state                → all cells + thermistors    │ │
│  │  GET  /api/state/cell/{id}      → single cell                │ │
│  │  POST /api/config               → bulk update                │ │
│  │  POST /api/config/cell          → single cell config         │ │
│  │  POST /api/calibration          → calibrate all              │ │
│  │  POST /api/self-test            → run BIST on all cards      │ │
│  │  GET  /api/cards                → card discovery/status       │ │
│  │  POST /api/firmware/upload      → OTA firmware update         │ │
│  │  GET  /api/thermal              → temps, fan speeds           │ │
│  │  GET  /ws/stream                → WebSocket 100Hz data        │ │
│  └──────────────┬──────────────────────────────────────────────┘ │
│                 │                                                 │
│  ┌──────────────┴──────────────────────────────────────────────┐ │
│  │  Card Manager                                                │ │
│  │  - Discovers cards by slot ID + mDNS                         │ │
│  │  - Maintains registry (type, slot, health, fw version)       │ │
│  │  - Routes commands to correct card via TCP                   │ │
│  │  - Aggregates 100 Hz UDP measurement streams                 │ │
│  │  - Pushes firmware updates                                   │ │
│  └──────────────┬──────────────────────────────────────────────┘ │
│                 │ GbE Switch                                      │
│        ┌────────┼────────┬────────┐                               │
│        ▼        ▼        ▼        ▼                               │
│     Card 0   Card 1   Card 2  ... Card N                         │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                      Card (STM32H723)                             │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  Embedded Server (lwIP on FreeRTOS)                          │ │
│  │  - TCP: Receives commands from CM5                           │ │
│  │  - UDP: Streams 100Hz measurements                           │ │
│  │  - TCP: Firmware update receiver                             │ │
│  │  - mDNS: cellsim-card-{slot_id}.local                       │ │
│  └──────────────┬──────────────────────────────────────────────┘ │
│                 │                                                 │
│  ┌──────────────┴──────────────────────────────────────────────┐ │
│  │  Real-Time Control Loop (Zephyr RTOS, 100 Hz)                │ │
│  │  - DMA I2C on 2 buses in parallel                            │ │
│  │  - Read 8× ADC, update 8× DAC, read temps                   │ │
│  │  - Closed-loop voltage regulation + calibration              │ │
│  │  - Self-test (continuous background)                         │ │
│  └──────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘

### 8.1a Firmware: Zephyr RTOS

Card firmware uses **Zephyr RTOS** (not FreeRTOS). Key advantages for this project:

| Feature | Benefit |
|---------|---------|
| **Device tree** | Declarative hardware description, overlay-able per board variant |
| **Native TCP/IP stack** | Built-in networking (not lwIP), BSD sockets API |
| **mDNS + DNS-SD** | Card discovery built-in with Kconfig toggle |
| **MCUboot + MCUmgr** | OTA firmware update with SMP over UDP, cryptographic verification, rollback |
| **DMA I2C** | Supported on STM32H7 (merged March 2025, PR #81814) |
| **Standardized APIs** | Same `i2c_write()` / `gpio_pin_set()` across all SoCs |
| **Shell subsystem** | Interactive debug console over UART or network |

**Zephyr STM32H723 support:**
- `nucleo_h723zg` board target fully supported
- Ethernet (RMII + LAN8742A) working
- 4× I2C with DMA
- MCUboot for dual-bank A/B firmware updates
- SMP/UDP for network-based firmware push from CM5

**Build:**
```bash
west build -b nucleo_h723zg --sysbuild app/cellsim-card
west flash
```
```

### 8.2 CM5 ↔ Card Protocol

| Transport | Use | Rate |
|-----------|-----|------|
| **UDP** | 100 Hz measurement streaming (card → CM5) | ~136 bytes/packet × 100 Hz ≈ 13.6 KB/s per card |
| **TCP** | Commands (CM5 → card): set voltage, enable, calibrate, self-test | Event-driven |
| **TCP** | Firmware update (CM5 → card): chunked binary transfer | On demand |
| **Slot ID GPIO** | Card position enumeration | At boot |

**Measurement packet (UDP, per card, 100 Hz):**
```c
struct CardMeasurement {
    uint8_t  slot_id;
    uint8_t  card_type;       // 0=cell, 1=thermistor
    uint16_t sequence;
    uint32_t timestamp_us;
    float    board_temp_c;
    float    input_voltage;
    struct CellMeasurement {
        float voltage;        // Output voltage (Sense line)
        float current;        // Output current
        float buck_voltage;   // Self-test: buck DAC readback
        float ldo_voltage;    // Self-test: LDO DAC readback
        float cell_temp_c;    // Per-cell temperature
    } cells[8];
};  // ~172 bytes per packet
```

### 8.3 Self-Test Architecture

Every card runs Built-In Self-Test:

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Buck DAC → ADC | Set buck DAC, read ADC CH0 | Within ±50 mV of expected |
| LDO DAC → ADC | Set LDO DAC, read ADC CH1 | Within ±20 mV of expected |
| Current path | Enable load switch, read ADC CH2 | Current > threshold |
| Output relay | Toggle relay, read ADC CH3 | Voltage appears/disappears |
| I2C isolator | Ping all devices behind ISO1640 | All 4 devices ACK |
| 2/4-wire relay | Toggle mode, verify sense path | ADC CH3 tracks appropriately |
| Cell temperature | Read per-cell temp sensor | Within safe range |
| Card temperature | STM32 internal temp ADC | Within safe range |
| Input voltage | STM32 internal ADC via divider | 24V ± 10% |
| Ethernet link | PHY link status register | Link up |

BIST runs automatically at power-up, on CM5 command, and continuously in background (non-disruptive checks only).

### 8.4 Data Models (Pydantic)

```python
@dataclass
class Measurement:
    value: float
    timestamp: datetime

@dataclass
class CellState:
    id: int                          # Global: card_slot * 8 + cell_index
    card_slot: int
    cell_index: int
    voltage_setpoint: float
    voltage_measured: Measurement
    current_measured: Measurement
    buck_voltage: float              # Self-test readback
    ldo_voltage: float               # Self-test readback
    cell_temperature: float          # Per-cell temp (°C)
    enabled: bool
    output_relay_enabled: bool
    load_switch_enabled: bool
    kelvin_mode: str                 # "4-wire" | "2-wire"
    self_test_passed: bool

@dataclass
class CardState:
    slot: int
    card_type: str                   # "cell" | "thermistor"
    firmware_version: str
    ip_address: str
    cells: list[CellState]
    board_temperature: float
    input_voltage: float
    self_test_passed: bool

@dataclass
class ThermalState:
    card_temperatures: dict[int, float]  # slot → temp
    base_board_temperature: float
    exhaust_temperature: float
    fan_speeds: list[float]              # RPM or duty %
    fan_mode: str                        # "auto" | "manual"

@dataclass
class SystemState:
    cards: list[CardState]
    thermal: ThermalState
    total_cells: int
    total_thermistors: int
    e_stop_active: bool
    uptime: float
```

### 8.5 Python Client Library

```python
from cellsim import CellSimSystem

# Connect to CM5
system = CellSimSystem("cellsim.local")

# System-wide
system.set_all_voltages(3.7)
system.enable_all_outputs()
system.self_test()

# Per-card
card = system.cards[0]
card.set_voltage(cell=3, voltage=4.2)
card.enable_output(cell=3)
card.set_kelvin_mode(cell=3, mode="4-wire")

# Per-cell (global addressing)
system.cells[0].voltage = 3.7
system.cells[0].enabled = True
system.sync()  # Batch push

# 100 Hz streaming
async for frame in system.stream():
    print(frame.cells[0].voltage_measured)

# Thermal monitoring
thermal = system.get_thermal()
print(f"Fan: {thermal.fan_speeds}, Exhaust: {thermal.exhaust_temperature}°C")

# Firmware update
system.update_firmware("cellsim-card-v1.2.0.bin", slots="all")
```

### 8.6 Web UI

Served from CM5:

- **Dashboard:** Real-time voltage/current for all channels (100 Hz WebSocket)
- **Card manager:** Connected cards, types, health, firmware versions
- **Cell control:** Voltage, enable/disable, 2/4-wire mode per channel
- **Thermal view:** Per-card and per-cell temperatures, fan speeds, live graph
- **Calibration:** Trigger, view error curves, export/import
- **Self-test:** Run and view results per card
- **Firmware update:** Upload .bin, select cards, progress bar
- **System config:** E-stop status, power monitoring, network settings

### 8.7 Calibration System

#### 8.7.1 Overview

Each cell's output has manufacturing tolerances in the DAC, feedback resistors, ADC, current sense amplifier, and shunt resistor. Calibration maps the ideal setpoint/measurement to the actual hardware behavior using an external precision DMM as the reference.

**Calibration is per-cell and bound to the card's unique ID** (24AA025E48 EUI-48). When a card is moved to a different slot, its calibration data follows it automatically because the CM5 looks up calibration by card ID, not slot number.

#### 8.7.2 Hardware Architecture — Two-Level Relay Mux

The calibration system uses a **two-level relay mux** so that one pair of banana jacks can serve all cards without manual cable switching:

- **Level 1 (card):** Per-cell DMM relay, selects which cell (0–7) on the card
- **Level 2 (backplane):** Per-slot DPDT relay, selects which card (0–15) in the system

```
CARD (Slot N)                                    BACKPLANE
─────────────                                    ─────────
Cell K output
  │
  └─[DMM Relay, GPIO P0]──[200Ω limit]──CAL_V+──→ [Slot N DPDT Relay] ──┐
                                                                          ├─→ [100Ω] → Banana+ → DMM
  Cell K iso GND ──────────[200Ω limit]──CAL_V-──→ [Slot N DPDT Relay] ──┘
                                                                          └─→ [100Ω] → Banana- → DMM
```

**Why relays, not analog mux:** Each cell has its own isolated ground domain (cells stack in a BMS). An analog mux (e.g. CD74HC4067) on the non-isolated backplane can't safely handle signals referenced to floating isolated grounds. Relays are voltage-agnostic and naturally handle isolation — same proven approach as the per-cell DMM relay on each card.

**Level 1 — Per-cell DMM relay (on card):**
- Controlled by TCA6408 GPIO P0 on the cell's isolated domain
- Only one cell per card connects to the calibration traces at a time
- Normally-open contacts — all disconnected at power-on
- 200Ω series protection resistors on card between DMM relay output and CAL_V+/CAL_V- connector pins

**Level 2 — Per-slot backplane relay (16× DPDT):**
- 16× DPDT signal relays on backplane (same type as per-cell DMM relay: HFD4/5-SR or similar)
- DPDT switches both CAL_V+ and CAL_V- simultaneously
- Normally-open contacts — all disconnected at power-on
- 5V coil, driven via N-FET (IRLML0040) from GPIO expander
- Contact resistance: ~100 mΩ (negligible for DMM measurement)
- Each slot has its own dedicated CAL_V+/CAL_V- traces from the card connector to that slot's relay (not a shared bus)
- Only one backplane relay active at a time (CM5 firmware enforces)

**Backplane relay control:**
- 2× TCA6408 GPIO expanders on CM5 I2C bus (8 outputs each = 16 relay drivers)
  - TCA6408 #1 at addr 0x23: slots 0–7
  - TCA6408 #2 at addr 0x24: slots 8–15

**Common output path:**
- 100Ω series protection resistors on common output to banana jacks (protects DMM if multiple relays accidentally activated)
- Standard 4mm banana sockets, mounted internally on the base board or chassis wall
- Accessible by opening the enclosure for calibration, but not exposed on front/rear panels

**DMM connection:** Keysight 34461A or similar 6.5-digit DMM connected via USB or LAN to the CM5. CM5 communicates with DMM over SCPI (pyvisa / python-vxi11).

**Total series resistance in measurement path:** ~400Ω (200Ω card-side protection + ~100 mΩ card relay + ~100 mΩ backplane relay + 100Ω backplane protection). At DMM input Z >10 GΩ, voltage error < 1 µV — negligible.

**Thermistor card calibration:** Same system. Thermistor cards get per-channel DMM relays (same TCA6408 GPIO P0 approach). Since thermistors share ground (no isolation), CAL_V- is at backplane GND — no floating ground concern. The same backplane relay selects the thermistor card's slot.

**BOM addition (backplane):** 16× DPDT relay (HFD4/5-SR or similar), 16× N-FET (IRLML0040), 16× flyback diode (1N4148), 2× TCA6408, 2× 100Ω 0402, 5× 100nF 0402, 2× banana jacks

#### 8.7.3 Calibration Procedure

**Voltage calibration (per cell):**

1. CM5 sets backplane relay for slot N (write to TCA6408 #1 or #2 on CM5 I2C)
2. CM5 sends Ethernet command to card N: "connect cell K to CAL bus" (card activates GPIO P0)
3. Enable cell's output relay (GPIO P5)
4. Sweep DAC setpoints across range (e.g., 8 points: 0.5V, 1V, 1.5V, 2V, 2.5V, 3V, 4V, 5V)
5. At each point:
   a. Set buck DAC + LDO DAC to target voltage
   b. Wait for settling (~100 ms)
   c. Read on-board ADC (ADS131M04) — the "internal" measurement
   d. Read external DMM via SCPI — the "reference" measurement
   e. Record: {dac_code, internal_adc_reading, dmm_reference_voltage}
6. Card deactivates cell K's DMM relay
7. Move to next cell on same card (repeat steps 2–6)
8. After all 8 cells, CM5 deactivates backplane relay, moves to next card

**Current calibration (per cell):**

1. Enable cell output with a known load (internal load switch + load resistor, or external precision load)
2. At each current point:
   a. Read on-board current sense ADC channel
   b. Read external DMM in current mode (or measure voltage across precision shunt)
   c. Record: {internal_current_reading, reference_current}

**Calibration time (16-card system):**
- Per cell: ~2s (8 points × 200ms + relay overhead)
- Per card: ~18s (8 cells × 2s + relay switching)
- Full system: ~5 minutes (fully automated, unattended)

#### 8.7.4 Calibration Data Model

```python
@dataclass
class CellCalibration:
    card_id: str                     # EUI-48 from 24AA025E48
    cell_index: int                  # 0–7
    calibrated_at: datetime
    temperature_c: float             # Ambient temp during calibration

    # Voltage: maps DAC code → actual output voltage
    voltage_dac_codes: list[int]     # DAC setpoints used
    voltage_internal: list[float]    # On-board ADC readings (V)
    voltage_reference: list[float]   # External DMM readings (V)

    # Current: maps ADC reading → actual current
    current_internal: list[float]    # On-board current sense ADC readings
    current_reference: list[float]   # External DMM/shunt readings (A)

@dataclass
class CardCalibration:
    card_id: str                     # EUI-48
    card_type: str                   # "cell" | "thermistor"
    hw_revision: str                 # Read from EEPROM user area
    cells: list[CellCalibration]     # 8 entries
```

#### 8.7.5 Calibration Application (Firmware)

The STM32H723 firmware applies calibration using **piecewise linear interpolation**:

- **Voltage setpoint → DAC code:** Given a target voltage, interpolate the `voltage_reference` → `voltage_dac_codes` table to find the correct DAC setting
- **ADC reading → actual voltage:** Given a raw ADC reading, interpolate the `voltage_internal` → `voltage_reference` table
- **ADC reading → actual current:** Same approach for current

Calibration tables are pushed from CM5 to the card over TCP at boot (or after re-calibration). Stored in CM5's filesystem at `/etc/cellsim/calibration/{card_id}.json`.

Optionally, a copy of the calibration table is stored in the card's STM32H723 Flash (survives network loss) and in the 24AA025E48 EEPROM user area (if it fits — limited to 256 bytes, so likely just a checksum + timestamp).

#### 8.7.6 Calibration API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/calibration/start` | POST | Start automated calibration (requires DMM connected) |
| `/api/calibration/start/{card_id}` | POST | Calibrate single card |
| `/api/calibration/status` | GET | Calibration progress (cell N of M) |
| `/api/calibration/data/{card_id}` | GET | Retrieve calibration data for card |
| `/api/calibration/data/{card_id}` | PUT | Upload/import calibration data |
| `/api/calibration/verify/{card_id}` | POST | Quick verification sweep (3 points) |
| `/api/calibration/dmm/status` | GET | Check if external DMM is connected and responding |

---

## 9. Thermistor Card

Same physical form factor and backplane connector. Plugs into any slot.

| Parameter | Value |
|-----------|-------|
| Channels per card | 8 (matched to cell card for consistent wiring) |
| Output | Analog voltage simulating NTC thermistor resistance |
| DAC | MCP4728 (4-ch) × 2, or MCP4725 × 8 |
| Self-test | ADC reads back each DAC output |
| Isolation | Shared ground OK (thermistors don't stack in series) |
| MCU | Same STM32H723 + LAN8742A (reuse card design) |
| Temperature sensor | On-card for thermal monitoring |

Any of the 16 backplane slots can accept either a cell card or a thermistor card. The user decides the mix based on their test requirements (e.g., 12 cell cards + 4 thermistor cards = 96 cell channels + 32 thermistor channels).

Card type detected by:
1. Firmware image (different binary for cell vs. thermistor)
2. Or: hardware card-type pin on backplane (1 extra bit in slot connector)

---

## 10. Package Dependencies (atopile)

### 10.1 Existing Packages (confirmed available)

| Package | Used For | Proven In |
|---------|----------|-----------|
| `atopile/st-stm32h723` | Card MCU | Hyperion |
| `atopile/microchip-lan8742a` | Ethernet PHY | Hyperion |
| `atopile/realtek-rtl8305nb` | GbE switch (backplane) | Hyperion |
| `atopile/rj45-connectors` | RJ45 w/ magnetics | Hyperion |
| `atopile/rpi-cm5` | CM5 on base board | Peak 48ch |
| `atopile/ti-tca9548a` | I2C multiplexer | Peak 48ch |
| `atopile/microchip-mcp4725` | DAC (buck/LDO) | Peak 48ch |
| `atopile/ti-iso1640x` | I2C isolator | Peak 48ch |
| ~~`atopile/st-ldk220`~~ | ~~LDO regulator~~ | ~~Peak 48ch~~ (removed — replaced with TBD) |
| ~~`atopile/ti-tps54560x`~~ | ~~Buck converter~~ | ~~Peak 48ch~~ (replaced by TPSM84209 for card-level) |
| `atopile/xt-connectors` | Power connectors | Peak 48ch |
| `atopile/relays` | Output relays | Peak 48ch |
| `atopile/ylptech-byyxx` | Isolated DC-DC | Peak 48ch |
| `atopile/pin-headers` | Debug headers | Both |
| `atopile/buttons` | Reset/boot buttons | Both |
| `atopile/microchip-mcp4728` | Quad DAC (thermistor) | Peak 48ch |

### 10.2 Also Available (confirmed in registry)

| Package | Version | Used For |
|---------|---------|----------|
| `atopile/ti-ads1115` | 0.2.0 | 16-bit ADC (fallback, not primary) |
| `atopile/ti-tca6408` | 0.2.0 | 8-bit GPIO expander |
| `atopile/ti-ina185a1` | 0.1.0 | Current sense amplifier |
| `atopile/ti-tmp117` | 0.2.0 | Temperature sensor (±0.1°C, I2C) |
| `atopile/microchip-emc2101` | 0.2.0 | Fan controller with temp sensor + tach |
| `atopile/ti-tlv75901` | 0.4.0 | 1A adjustable LDO |
| `atopile/ti-tps563201` | 0.1.9 | 3A sync buck (per-cell, proven from v1) |
| `atopile/ti-lv2842x` | 0.2.3 | Buck regulator (4–40V input) |
| `atopile/tdpower-tdk20x` | 0.2.0 | 20W isolated DC-DC (for card-level 24V→24V isolation) |
| `atopile/ti-ina228` | 0.1.2 | 20-bit power monitor (input current sensing) |
| `atopile/ti-ina3221` | 0.2.0 | Triple-channel current/power monitor |
| ~~`atopile/opsco-sk6805-ec20`~~ | ~~0.4.0~~ | ~~Addressable RGB LED~~ (removed — no per-cell LEDs) |
| `atopile/usb-connectors` | 0.4.1 | USB-C connectors |
| `atopile/programming-headers` | 0.2.0 | SWD/JTAG debug headers |
| `atopile/saleae-header` | 0.3.0 | Logic analyzer headers |
| `atopile/testpoints` | 0.2.0 | Test points |
| `atopile/mounting-holes` | 0.2.0 | PCB mounting holes |
| `atopile/indicator-leds` | 0.2.3 | Status LEDs |
| `atopile/ti-dac7578` | 0.3.0 | 8-ch DAC (thermistor card alternative) |
| `atopile/adi-ltc4311` | 0.2.0 | I2C accelerator (if bus integrity issues) |

### 10.3 Packages to Create

| Component | Priority | Notes |
|-----------|----------|-------|
| **ADS131M04 (24-bit ADC)** | P0 | SPI, 4ch simultaneous, 32 kSPS — no existing package |
| **ISO7741 (SPI isolator)** | P0 | 4-ch digital isolator (3F+1R) for ADS131M04 SPI |
| ~~TPSM863257 (integrated buck)~~ | ~~P0~~ | **Deferred to Phase 2** — using TPS563201 (proven v1 part) for per-cell buck |
| **TPSM84209 (integrated buck)** | P0 | 2.5A integrated buck module with inductor (card-level 24V→3.3V) |
| **24AA025E48 EEPROM** | P1 | I2C, EUI-48 MAC + 256B user EEPROM — card identity |
| Backplane card-edge connector | P1 | GPU-style, ~20-pin (incl. per-slot CAL traces) |
| Cell output connector (Kelvin) | P1 | 18-pin, D-Sub or Micro-Fit |
| IEC C14 mains inlet | P1 | Standard panel-mount |
| Internal banana jacks (4mm) | P2 | Chassis-mount, for calibration DMM connection |

---

## 11. Comparison: v1 → 48ch → v2

| Feature | v1 (16ch) | 48ch (Peak) | v2 (proposed) |
|---------|-----------|-------------|---------------|
| Channels | 16 | 48 | 8 × N (up to ~160) |
| Voltage range | 0–4.5 V | 0–4.5 V | **0–5 V** |
| Current / channel | 500 mA | 500 mA | **1 A** |
| Update rate | ~10 Hz | ~10 Hz | **up to 10 kHz** |
| Kelvin sensing | No | No | **Yes (4-wire)** |
| Self-test | No | No | **Full BIST** |
| Temp monitoring | No | 1 sensor | **Per-cell + per-card + exhaust** |
| Form factor | Single PCB | Single PCB | **Backplane + plug-in cards** |
| Main controller | ESP32-S3 | RPi CM5 | **RPi CM5** |
| Per-card MCU | — | — | **STM32H723** |
| I2C topology | 1 bus, muxed | 3 buses, muxed | **1 I2C (8ch mux) + 2 SPI buses per card** |
| Network | USB serial | I2C (centralized) | **GbE (distributed)** |
| Switch | None | None | **Cascaded RTL8305NB** |
| Isolation | Per-cell DC-DC | Per-cell DC-DC | **Two-tier: card-level (24V→24V + Eth xfmr) + per-cell (DC-DC + ISO1640 + ISO7741)** |
| Software | Arduino | FastAPI + DT | **FastAPI + DT (same stack)** |
| Precision ADC | ADS1115 + MicroVault | ADS1219 + MicroVault | **ADS131M04 (SPI, 4ch, 32kSPS)** |
| PSU | 12V external | 24V XT30 | **Meanwell 24V (IEC mains)** |
| Enclosure | None | None | **19" rack mount** |
| FW update | USB DFU | SD card | **OTA via Ethernet** |
| Card enumeration | — | — | **Hardware slot ID bits** |
| Fan control | None | None | **PWM + temp feedback** |
| Thermistors | 4ch add-on | 20ch separate | **Same card format** |

---

## 12. Open Questions

### 12.1 Resolved

- [x] I2C isolators within block — YES (cells at different potentials)
- [x] Form factor — Backplane + GPU-style cards
- [x] Main controller — CM5 on base board
- [x] Per-card controller — STM32H723 (proven in Hyperion)
- [x] Ethernet PHY — LAN8742A (proven in Hyperion)
- [x] GbE switch — Cascaded RTL8305NB (proven in Hyperion)
- [x] Kelvin sensing — Yes, 4-wire with 2-wire fallback
- [x] Self-test — ADC verifies every DAC, continuous BIST
- [x] Thermistors — Same card format, different firmware
- [x] Software stack — Peak-style (FastAPI, device tree, calibration)
- [x] PSU — Meanwell 24V, IEC C14 mains on rear panel
- [x] Voltage/Current — 0–5V, 0–1A (TPS563201 buck max 5.5V, TLV75901 LDO proven)
- [x] Update rate — up to 10 kHz (ADS131M04 SPI ADC at 32 kSPS/ch)
- [x] Bus architecture — 1 I2C (control, TCA9548A 8ch mux) + 2 SPI (ADC), DMA-driven parallel
- [x] Card-level isolation — 24V→24V isolated DC-DC + Ethernet transformer + UART isolator (two-tier isolation)
- [x] Card enumeration — Hardware slot ID bits (4 GPIO pins, 16 slots)
- [x] Precision ADC — Per-cell ADS131M04 (SPI, 32 kSPS, simultaneous 4-ch)
- [x] OTA firmware — CM5 pushes over Ethernet to card bootloader
- [x] Cooling — PWM fans on base board with temp feedback loop
- [x] Temp sensors — Per-cell, per-card, base board, exhaust

### 12.2 Newly Resolved

- [x] **ADC:** ADS131M04 (24-bit, SPI, 4ch simultaneous, 32 kSPS) — sub-mV accuracy, 10× faster than ADS1219, cheaper ($1.83 vs $7.82)
- [x] **SPI isolation:** ISO7741 (4-ch digital isolator, 3F+1R) — perfect SPI match, $0.44
- [x] **Firmware framework:** Zephyr RTOS — device tree, native TCP/IP, MCUboot OTA, DMA I2C + SPI
- [x] **Bus architecture:** I2C for control (DACs + GPIO), SPI for high-speed ADC — parallel operation
- [x] **I2C bus speed:** 400 kHz for control path (TCA9548A + TCA6408A bottleneck), sufficient for DAC/GPIO
- [x] **Buck converter:** TPS563201 (reused from v1) — proven feedback network, 12V input within 4.5–17V range. TPSM863257 upgrade deferred to Phase 2.
- [x] **LDO:** TLV75901 works as-is — buck max 5.5V < LDO max Vin 6V, P1 blocker resolved
- [x] **Package availability:** Nearly all components have existing atopile packages (see §10)
- [x] **Card identity:** 24AA025E48 EEPROM — factory-unique EUI-48 for card ID + Ethernet MAC address (§3.4)
- [x] **Calibration architecture:** Two-level relay mux (per-cell on card + per-slot on backplane) → banana jacks → external DMM (§8.7)
- [x] **Slot count:** 16 slots (reduced from 20); any slot can be cell or thermistor card
- [x] **Pi filter:** Removed — LDO PSRR sufficient; inductor DCR caused unacceptable voltage drop at 1A
- [x] **Output path resistance:** ≤200 mΩ total (LDO→connector), ≤200 mV drop at 1A (§4.2.6)
- [x] **Calibration data binding:** Per-cell calibration keyed by card EUI-48, survives slot changes (§8.7)
- [x] **Power budget:** Detailed bottom-up analysis from cell → card → system, PSU sizing table (§4.3–4.5)
- [x] **Card-level buck:** TPSM84209RKHR — integrated buck module (24V_iso→3.3V, 2.5A), replaces TPS54560x
- [x] **Per-cell isolated DC-DC:** YLPTEC B2412S-3WR2 (24V→12V, 3W, C5369517, $1.95)
- [x] **Per-cell control LDO:** LDK220M-R reused (12V→3.3V, Vin max 13.2V accommodates 12V ±10%)
- [x] **Card-level isolation:** Isolated 24V→24V DC-DC + discrete Ethernet transformer + UART digital isolator
- [x] **Per-slot power switching:** 16× Omron G5Q-1A4 DC24 relay (SPST-NO, 10A/30VDC) — replaces both E-stop bus relay and P-FET/high-side switch. Per-slot isolation + E-stop via coil supply cutoff (§4.5a)
- [x] **Per-slot current monitoring:** 6× INA3221 (3-ch each) with 10 mΩ shunts, behind TCA9548A mux on CM5 I2C. Plus INA228 on main bus input.
- [x] **Relay drive:** 2× MCP23017 (16-bit GPIO) + N-FET per relay. Default state: all relays open (cards de-powered). E-stop NC button in series with coil supply.

### 12.3 Still Open

- [ ] **3W per-cell DC-DC limit:** YLPTEC B2412S-3WR2 limits continuous cell current to ~540mA. Evaluate if 1A is truly required or if typical 300mA is sufficient. For 1A, find 24V→12V 5W+ part.
- [ ] **Card-level isolated DC-DC:** TDK20-24S24WH ($6.21) is expensive. Evaluate cheaper 24V→24V, 20W+ alternatives.
- [ ] **Discrete Ethernet transformer:** Required for backplane Ethernet isolation. Select part.
- [ ] **UART digital isolator:** Required for card programming/debug via backplane. Select part.
- [ ] **Backplane connector family:** Samtec? TE? Molex? PCB card-edge (cheapest)? Now 20+ pins (incl. per-slot CAL traces, UART).
- [ ] **Cell output connector:** D-Sub 25? Molex Micro-Fit? Terminal blocks?
- [ ] **Enclosure dimensions:** 2U vs. 3U, 16 cards fit comfortably in 19"
- [ ] **Card mechanical:** Exact PCB dimensions, rear bracket design, ejector mechanism
- [ ] **Calibration DMM interface:** USB vs LAN connection to CM5, SCPI library selection (pyvisa vs python-vxi11)
- [ ] **24AA025E48 atopile package:** Needs to be created (SOT-23-5 or MSOP-8)

---

## 13. Development Phases

### Phase 1: Architecture Validation
- STM32H723 + LAN8742A Ethernet bringup (use Nucleo-H723ZG or Hyperion board)
- Validate I2C + SPI: DMA on 1 I2C bus (TCA9548A, 8ch mux, DACs/GPIO) + 2 SPI buses (ADS131M04), measure actual cycle time
- CM5 ↔ STM32H723 communication over Ethernet (UDP streaming, TCP commands)
- Test card-level isolation chain: 24V→24V iso DC-DC + TPSM84209 buck → 3.3V
- Test per-cell power chain: YLPTEC B2412S-3WR2 (24V→12V) + TPS563201 buck + TLV75901 LDO at 5V / 540 mA
- Prototype card bootloader (Ethernet firmware update via UART isolator)

### Phase 2: Cell Card PCB
- Design cell card in atopile
- STM32H723 + LAN8742A (from Hyperion packages)
- Card-level isolation: 24V→24V iso DC-DC + Ethernet transformer + UART isolator + TPSM84209 buck
- 1× TCA9548A (8ch), 8× ISO1640, 8× ISO7741, 8× cell circuits (from Peak cellsim package, uprated)
- Card-edge connector + Kelvin output connector
- Temp sensors per cell
- Manufacture prototype, validate

### Phase 3: Base Board + Enclosure
- CM5 + cascaded RTL8305NB switches
- 24V power distribution, per-slot relays (G5Q-1A4), MCP23017 drive, INA3221 current monitoring, E-stop
- Fan controller (PWM + temp sensors)
- Card slot connectors with slot ID resistors
- IEC C14 inlet, front panel (RJ45, USB-C, OLED, E-stop)
- Source or design 19" rack enclosure

### Phase 4: Software Stack
- Port Peak cellsim-48ch software to v2 architecture
- CM5: FastAPI server, card manager, firmware updater, fan controller
- STM32H723: FreeRTOS + lwIP, 100 Hz control loop, UDP streaming, TCP commands
- Python client library
- Web UI (dashboard, card manager, thermal view, calibration, firmware update)
- Calibration system (per-cell, persistent on CM5)

### Phase 5: Thermistor Card + Integration
- Design thermistor card (same form factor, simpler circuit)
- Multi-card system test (mixed cell + thermistor)
- Full BIST validation
- Thermal stress test (all cards at load)

### Phase 6: Production
- DFM optimization
- Final enclosure design (sheet metal, silkscreen, labeling)
- Manufacturing files, BOM, assembly docs
- User documentation
