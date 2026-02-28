/*
 * CellSim Card — Status LED (SK6805-EC20, WS2812-compatible)
 *
 * Single addressable RGB LED on each card for status indication.
 * Uses Zephyr's led_strip driver with ws2812-gpio binding on PF13.
 *
 * Color mapping:
 *   Blue    — Booting / self-test in progress
 *   Green   — Healthy, running normally
 *   Yellow  — Warning (partial self-test failure)
 *   Red     — Fault / safe state
 *   White   — Firmware update in progress
 *   Off     — LED not initialized or explicitly off
 */

#ifndef CELLSIM_LED_H
#define CELLSIM_LED_H

#include <stdint.h>

/* GRB color definitions (SK6805 native byte order) */
#define LED_COLOR_OFF       0x000000
#define LED_COLOR_GREEN     0xFF0000  /* G=0xFF, R=0x00, B=0x00 */
#define LED_COLOR_RED       0x00FF00  /* G=0x00, R=0xFF, B=0x00 */
#define LED_COLOR_BLUE      0x0000FF  /* G=0x00, R=0x00, B=0xFF */
#define LED_COLOR_YELLOW    0x80FF00  /* G=0x80, R=0xFF, B=0x00 */
#define LED_COLOR_WHITE     0xFFFFFF  /* G=0xFF, R=0xFF, B=0xFF */

/**
 * Initialize the status LED.
 * Must be called after kernel boot (device tree must be ready).
 *
 * @return 0 on success, negative errno on error
 */
int led_init(void);

/**
 * Set the LED to an arbitrary RGB color.
 *
 * @param r Red intensity (0-255)
 * @param g Green intensity (0-255)
 * @param b Blue intensity (0-255)
 */
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

/** Set LED to blue — booting / self-test in progress */
void led_set_status_booting(void);

/** Set LED to green — healthy, running normally */
void led_set_status_ok(void);

/** Set LED to yellow — warning (partial self-test failure) */
void led_set_status_warning(void);

/** Set LED to red — fault / safe state */
void led_set_status_fault(void);

/** Set LED to white — firmware update in progress */
void led_set_status_updating(void);

/** Turn LED off */
void led_off(void);

#endif /* CELLSIM_LED_H */
