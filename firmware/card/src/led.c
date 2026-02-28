/*
 * CellSim Card — Status LED (SK6805-EC20, WS2812-compatible)
 *
 * Uses Zephyr's led_strip API with the ws2812-gpio driver.
 * Single LED on PF13, configured via device tree overlay.
 */

#include "led.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);

#define STRIP_NODE DT_ALIAS(led_strip)

#if !DT_NODE_HAS_STATUS_OKAY(STRIP_NODE)
#warning "Status LED (led-strip alias) not found in device tree — LED functions will be no-ops"
#endif

static const struct device *strip_dev = DEVICE_DT_GET_OR_NULL(STRIP_NODE);
static struct led_rgb pixel;
static bool led_ready;

int led_init(void)
{
    if (strip_dev == NULL) {
        LOG_WRN("Status LED device not found in device tree");
        return -ENODEV;
    }

    if (!device_is_ready(strip_dev)) {
        LOG_WRN("Status LED device not ready");
        return -ENODEV;
    }

    led_ready = true;
    LOG_INF("Status LED initialized");

    /* Start with LED off */
    led_off();
    return 0;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_ready) {
        return;
    }

    pixel.r = r;
    pixel.g = g;
    pixel.b = b;

    int ret = led_strip_update_rgb(strip_dev, &pixel, 1);
    if (ret != 0) {
        LOG_ERR("LED strip update failed: %d", ret);
    }
}

void led_set_status_booting(void)
{
    led_set_color(0x00, 0x00, 0xFF); /* Blue */
}

void led_set_status_ok(void)
{
    led_set_color(0x00, 0xFF, 0x00); /* Green */
}

void led_set_status_warning(void)
{
    led_set_color(0xFF, 0x80, 0x00); /* Yellow/amber */
}

void led_set_status_fault(void)
{
    led_set_color(0xFF, 0x00, 0x00); /* Red */
}

void led_set_status_updating(void)
{
    led_set_color(0xFF, 0xFF, 0xFF); /* White */
}

void led_off(void)
{
    led_set_color(0x00, 0x00, 0x00); /* Off */
}
