/*
 * CellSim Card Firmware
 * =====================
 * Zephyr RTOS application for the CellSim v2 cell card.
 *
 * Architecture:
 *   - 100 Hz control loop: reads ADCs, updates DACs, runs calibration
 *   - UDP streaming: sends measurement packets to CM5 at 100 Hz
 *   - TCP command server: receives commands from CM5
 *   - mDNS: advertises as cellsim-card-{slot_id}.local
 *   - MCUmgr/SMP: OTA firmware update over UDP
 *   - Self-test: BIST on all 8 cells at boot + on demand
 *   - Watchdog: hardware IWDG + CM5 heartbeat monitor
 *   - Health: internal temp, Vrefint, input voltage in every packet
 *
 * I2C topology (per card):
 *   I2C1 -> TCA9548A -> [ISO1640 -> cell devices] x 4  (cells 0-3)
 *   I2C2 -> TCA9548A -> [ISO1640 -> cell devices] x 4  (cells 4-7)
 *
 * Per-cell devices (on isolated side):
 *   - MCP4725 DAC ×2 (buck + LDO setpoints, via I2C)
 *   - ADS131M04 ADC (4ch simultaneous, via SPI + ISO7741 isolator)
 *   - TCA6408 GPIO expander (relay, enable, load switch, 2/4-wire, via I2C)
 *   - TMP117 temperature sensor (via I2C)
 *
 * Backplane signals:
 *   - USART1 (PA9 TX, PA10 RX): remote debug/flash via CM5 slot mux
 *   - BOOT0: ROM bootloader entry (pulled low on card, driven by CM5 mux)
 *   - NRST: reset (pulled high on card, driven by CM5 mux)
 *   - Slot ID: 4 GPIO inputs (active-low, coded by backplane resistors)
 *   - Input voltage: ADC3 via 100k/10k divider from 24V bus
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "card_id.h"
#include "cell.h"
#include "led.h"
#include "self_test.h"
#include "watchdog.h"
#include "health.h"
#include "network.h"

LOG_MODULE_REGISTER(cellsim, LOG_LEVEL_INF);

/* Firmware version (embedded in mDNS TXT record and self-test response) */
#define FW_VERSION_MAJOR 2
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

/**
 * Read slot ID from 4 GPIO pins (active-low, coded by backplane resistors).
 * Returns 0-15 for valid slots.
 */
static uint8_t read_slot_id(void)
{
    /* TODO: read 4 GPIO pins and decode slot ID
     * Slot ID pins are active-low: 0 = pull-down on backplane, 1 = pull-up
     * Binary encoding: slot_id = ~(GPIO3:GPIO0) & 0x0F
     */
    return 0; /* Placeholder: slot 0 */
}

int main(void)
{
    LOG_INF("CellSim card firmware v%d.%d.%d starting...",
            FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);

    /* Read slot ID from backplane resistor coding */
    uint8_t slot_id = read_slot_id();
    LOG_INF("Slot ID: %d", slot_id);

    /* Read card identity EEPROM (24AA025E48 on I2C3) */
    int ret = card_id_init();
    if (ret != 0) {
        LOG_WRN("Card identity EEPROM read failed: %d (using fallback ID)", ret);
    }

    /* Initialize status LED and set booting color (blue) */
    ret = led_init();
    if (ret != 0) {
        LOG_WRN("Status LED init failed: %d (continuing without LED)", ret);
    } else {
        led_set_status_booting();
    }

    /* Initialize health monitoring (ADC3: internal temp, Vrefint, input voltage) */
    ret = health_init();
    if (ret != 0) {
        LOG_WRN("Health monitoring init failed: %d (continuing)", ret);
    }

    /* Initialize watchdog (hardware IWDG + CM5 heartbeat monitor) */
    ret = watchdog_init();
    if (ret != 0) {
        LOG_ERR("Watchdog init failed: %d", ret);
        /* Continue anyway — watchdog is important but not fatal */
    }

    /* Run self-test on all 8 cells */
    struct self_test_result st_result;
    ret = self_test_run(&st_result);
    if (ret != 0) {
        LOG_ERR("Self-test failed to run: %d", ret);
    } else if (!st_result.all_passed) {
        LOG_WRN("Self-test: some cells FAILED (MCU: %.1f°C, Vin: %.1fV)",
                (double)st_result.mcu_temp_celsius,
                (double)st_result.input_voltage);
        led_set_status_warning();
    } else {
        LOG_INF("Self-test: ALL PASSED (MCU: %.1f°C, Vin: %.1fV)",
                (double)st_result.mcu_temp_celsius,
                (double)st_result.input_voltage);
        led_set_status_ok();
    }

    /* Initialize cell control HAL (DACs, ADCs, GPIO expanders) */
    ret = cell_init();
    if (ret != 0) {
        LOG_ERR("Cell HAL init failed: %d", ret);
        /* Non-fatal: some cells may still work */
    }

    /* Initialize network services (mDNS, TCP, UDP) */
    ret = network_init(slot_id);
    if (ret != 0) {
        LOG_ERR("Network init failed: %d", ret);
        return ret;
    }

    /* Start TCP command server thread */
    ret = network_start_cmd_server();
    if (ret != 0) {
        LOG_ERR("Failed to start command server: %d", ret);
    }

    /* Start UDP measurement streaming + 100 Hz control loop */
    ret = network_start_meas_stream();
    if (ret != 0) {
        LOG_ERR("Failed to start measurement stream: %d", ret);
    }

    LOG_INF("CellSim card firmware initialized (slot %d)", slot_id);

    /* Main thread: nothing more to do, work happens in spawned threads.
     * Zephyr idle thread handles power management. */
    return 0;
}
