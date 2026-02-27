/*
 * CellSim Card — Watchdog / Heartbeat Monitor
 *
 * If the CM5 stops sending heartbeat commands for HEARTBEAT_TIMEOUT_MS,
 * the card enters safe state: all relays open, all DACs zeroed, fault LED on.
 * The card stays on the network for recovery.
 *
 * Also configures the STM32 IWDG as a hardware watchdog for firmware hangs.
 */

#ifndef CELLSIM_WATCHDOG_H
#define CELLSIM_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

/* CM5 heartbeat timeout (milliseconds) */
#define HEARTBEAT_TIMEOUT_MS 5000

/**
 * Initialize the watchdog subsystem.
 * - Starts the STM32 IWDG (hardware watchdog, ~4s timeout)
 * - Starts the heartbeat monitoring thread
 *
 * @return 0 on success, negative errno on error
 */
int watchdog_init(void);

/**
 * Feed the hardware watchdog. Call from the main control loop.
 */
void watchdog_feed(void);

/**
 * Record a heartbeat from the CM5.
 * Call when a valid command or heartbeat packet is received.
 */
void watchdog_heartbeat_received(void);

/**
 * Check if the CM5 heartbeat has timed out.
 *
 * @return true if heartbeat timed out (safe state active)
 */
bool watchdog_heartbeat_expired(void);

/**
 * Enter safe state: open all relays, zero all DACs, set fault LED.
 * Called automatically on heartbeat timeout, or manually for E-stop.
 */
void watchdog_enter_safe_state(void);

/**
 * Exit safe state (recovery). Resets heartbeat timer.
 * Cells remain disabled — CM5 must re-enable them explicitly.
 *
 * @return 0 on success, -EINVAL if not currently in safe state
 */
int watchdog_exit_safe_state(void);

#endif /* CELLSIM_WATCHDOG_H */
