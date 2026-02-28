/*
 * CellSim Card — Watchdog / Heartbeat Monitor Implementation
 */

#include "watchdog.h"
#include "cell.h"
#include "led.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

/* Hardware watchdog (IWDG) */
static const struct device *wdt_dev;
static int wdt_channel_id = -1;

/* Heartbeat state */
static int64_t last_heartbeat_ms;
static bool safe_state_active;

/* Heartbeat monitor thread */
#define HEARTBEAT_STACK_SIZE 1024
#define HEARTBEAT_PRIORITY 5
static K_THREAD_STACK_DEFINE(heartbeat_stack, HEARTBEAT_STACK_SIZE);
static struct k_thread heartbeat_thread;

/**
 * Heartbeat monitor thread entry.
 * Checks for CM5 heartbeat timeout and enters safe state if expired.
 */
static void heartbeat_monitor(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        k_sleep(K_MSEC(1000));

        if (safe_state_active) {
            continue;
        }

        int64_t now = k_uptime_get();
        int64_t elapsed = now - last_heartbeat_ms;

        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            LOG_WRN("CM5 heartbeat timeout (%lld ms), entering safe state", elapsed);
            watchdog_enter_safe_state();
        }
    }
}

int watchdog_init(void)
{
    safe_state_active = false;
    last_heartbeat_ms = k_uptime_get();

    /* Initialize hardware watchdog (IWDG) */
    wdt_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
    if (wdt_dev == NULL || !device_is_ready(wdt_dev)) {
        LOG_WRN("Hardware watchdog not available, continuing without it");
    } else {
        struct wdt_timeout_cfg cfg = {
            .window.min = 0,
            .window.max = 4000,  /* 4 second timeout */
            .callback = NULL,    /* Reset on timeout (no callback) */
            .flags = WDT_FLAG_RESET_SOC,
        };

        wdt_channel_id = wdt_install_timeout(wdt_dev, &cfg);
        if (wdt_channel_id < 0) {
            LOG_ERR("Failed to install watchdog timeout: %d", wdt_channel_id);
        } else {
            int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
            if (ret != 0) {
                LOG_ERR("Failed to start watchdog: %d", ret);
            } else {
                LOG_INF("Hardware watchdog started (4s timeout)");
            }
        }
    }

    /* Start heartbeat monitor thread */
    k_thread_create(&heartbeat_thread, heartbeat_stack,
                     HEARTBEAT_STACK_SIZE,
                     heartbeat_monitor, NULL, NULL, NULL,
                     HEARTBEAT_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&heartbeat_thread, "heartbeat_mon");

    LOG_INF("Watchdog subsystem initialized (heartbeat timeout: %d ms)",
            HEARTBEAT_TIMEOUT_MS);
    return 0;
}

void watchdog_feed(void)
{
    if (wdt_dev != NULL && wdt_channel_id >= 0) {
        wdt_feed(wdt_dev, wdt_channel_id);
    }
}

void watchdog_heartbeat_received(void)
{
    last_heartbeat_ms = k_uptime_get();

    if (safe_state_active) {
        LOG_INF("Heartbeat received, but safe state is active. "
                "Explicit recovery command required.");
    }
}

bool watchdog_heartbeat_expired(void)
{
    return safe_state_active;
}

void watchdog_enter_safe_state(void)
{
    if (safe_state_active) {
        return; /* Already in safe state */
    }

    safe_state_active = true;
    LOG_WRN("SAFE STATE ACTIVE: opening all relays, zeroing all DACs");

    cell_safe_state_all();

    /* Set status LED to red (fault indication) */
    led_set_status_fault();
}

int watchdog_exit_safe_state(void)
{
    if (!safe_state_active) {
        return -EINVAL;
    }

    safe_state_active = false;
    last_heartbeat_ms = k_uptime_get();
    LOG_INF("Safe state cleared — cells remain disabled until CM5 re-enables");
    return 0;
}
