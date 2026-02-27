# CellSim v2 Spec v5 — Design Review

**Date:** 2026-02-26
**Reviewer:** Claude
**Scope:** Review of spec changes: 16-slot reduction, pi filter removal, output path resistance spec, backplane calibration relay mux

---

## Changes Applied

1. **Slot count 20 → 16** — Updated throughout all sections (§1–§12)
2. **Pi filter removed** — Stage 4 deleted, stages renumbered, power path diagrams updated, component table marked
3. **Output path resistance spec** — New §4.2.6 with ≤200 mΩ / ≤200 mV @1A budget
4. **Backplane calibration mux** — §8.7.2 rewritten for two-level relay mux (16× DPDT backplane relays + 2× TCA6408)
5. **GbE switch** — §6.1 rewritten with clean 5× RTL8305NB star topology (root CPU port for CM5, 4 leaf switches × 4 card ports = 16)

---

## Issues Found & Fixed During Review

| # | Issue | Resolution |
|---|-------|------------|
| 1 | GbE switch IC count was "×4" in §4.5 power tree and base board load (should be ×5 per §6.1) | Fixed to ×5 in both locations |
| 2 | Base board power estimate was ~34W (based on 4 switch ICs), should be ~35W | Fixed |

---

## Remaining Observations (Not Bugs, But Worth Noting)

### 1. GbE Switch Topology — CPU Port Assumption

The §6.1 rewrite assumes the RTL8305NB's CPU/RGMII port is **separate from the 5 PHY ports** (i.e., 6 effective ports). This is correct for RTL8305NB, but should be confirmed against the actual datasheet/package. If the RGMII port counts as one of the 5, the topology needs a 6th switch IC.

**Recommendation:** Verify with `atopile/realtek-rtl8305nb` package or RTL8305NB datasheet.

### 2. Backplane Relay Power Budget Not in §4.5

The 16× DPDT relay coils on the backplane (5V, ~150mW each when active) and 2× TCA6408 are not explicitly accounted for in the base board power budget (§4.5.1). Only one relay is active at a time during calibration, so worst-case draw is ~150mW — negligible. But the TCA6408 quiescent draw (~0.1mA × 2 = 0.2mA) is also negligible.

**Impact:** None — well within the "Misc" budget.

### 3. TCA6408 I2C Address Space

The calibration mux uses TCA6408 at addresses 0x23 and 0x24. The existing per-cell TCA6408 is at 0x20. The spec should document the full CM5 I2C address map somewhere to avoid conflicts with OLED (typically 0x3C), temp sensors (TMP117 at 0x48+), fan controller (EMC2101 at 0x4C), and INA228/INA3221 for bus monitoring.

**Recommendation:** Add a CM5 I2C address map table to §3.1 or §5.

### 4. Calibration Protection: Multi-Relay Activation

The spec says "only one backplane relay active at a time (CM5 firmware enforces)." Hardware protection (e.g., address decoding that prevents multiple outputs being high simultaneously) would be safer. However, the 100Ω series protection resistors on the common output provide sufficient protection against accidental multi-activation.

**Recommendation:** Firmware-only enforcement is acceptable given the protection resistors. Consider a software watchdog that auto-disables all calibration relays after a timeout.

### 5. LDO Output Cap Change (1µF → 10µF)

The pi filter removal increases the importance of the LDO output cap. The spec now notes 10µF ceramic (0805) in the power path diagram and the LDO issues section. However, the v1 value in §4.2.3 still says "Output cap: only 1 µF (0402) — minimal" — this is the v1 baseline description, so it's correct as-is (documenting what v1 had). The v2 change is properly called out in issue #4 of that section.

**Status:** Consistent.

### 6. Per-Cell Architecture Description (§4.2) Still Says "Same Proven Topology"

Line 431 says "Same proven topology as v1/Peak — isolated supply → digital buck → digital LDO" which is still accurate after pi filter removal (pi filter was post-LDO, not part of the core topology). No change needed.

### 7. Thermistor Card Calibration CAL_V- Grounding

§8.7.2 notes that for thermistor cards, "CAL_V- is at backplane GND — no floating ground concern." This is correct because thermistors share ground. The backplane relay still switches both CAL_V+ and CAL_V- (DPDT), which is fine — CAL_V- just happens to be at the common ground for thermistor cards.

### 8. PSU Sizing Table (§4.5.3)

The table jumps from "≤10 cards" to "16 cards" — there's no row for the common "12 cards" config. This is minor but could be useful for users planning intermediate deployments.

---

## Overall Assessment

The spec is internally consistent after the v5 changes. All slot count references are updated, the pi filter is cleanly removed with the output path resistance spec filling the gap, and the calibration system architecture is well-documented with the two-level relay mux approach.

**Key remaining design risks (unchanged from v4):**
- P0: Isolated DC-DC selection (24V→8V, ~12W)
- P1: LDO upgrade for 6V output capability
- P2: ADS1219 JLCPCB stock availability
