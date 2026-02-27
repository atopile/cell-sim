/*
 * CellSim Card — Network Services
 *
 * TCP command server: receives control commands from CM5
 * UDP measurement streaming: sends 100 Hz measurement packets
 * mDNS advertisement: cellsim-card-{slot_id}.local
 *
 * Wire protocol:
 *   UDP measurement packet (sent card → CM5 at 100 Hz):
 *     [slot_id:u8][seq:u32][timestamp_us:u64]
 *     [cell0: setpoint_mv:u16, voltage_mv:u16, current_ua:u32, temp_c10:i16, flags:u8]
 *     × 8 cells
 *     [health: mcu_temp_c10:i16, vin_mv:u16, uptime_ms:u32]
 *
 *   TCP command (CM5 → card):
 *     [cmd:u8][cell_id:u8][payload_len:u16][payload:...]
 *     Response: [status:u8][payload_len:u16][payload:...]
 */

#ifndef CELLSIM_NETWORK_H
#define CELLSIM_NETWORK_H

#include <stddef.h>
#include <stdint.h>

/* Network ports */
#define CELLSIM_UDP_MEAS_PORT   5000  /* Card → CM5 measurement stream */
#define CELLSIM_TCP_CMD_PORT    5001  /* CM5 → Card command channel */

/* TCP command opcodes */
enum cellsim_cmd {
    CMD_NOP             = 0x00,
    CMD_SET_VOLTAGE     = 0x01,  /* Set cell voltage setpoint */
    CMD_ENABLE_OUTPUT   = 0x02,  /* Enable/disable cell output relay */
    CMD_SET_MODE        = 0x03,  /* Set 2-wire/4-wire Kelvin mode */
    CMD_SELF_TEST       = 0x10,  /* Run self-test, return results */
    CMD_GET_STATE       = 0x11,  /* Get full card state */
    CMD_CALIBRATE       = 0x12,  /* Run calibration sequence */
    CMD_HEARTBEAT       = 0x20,  /* CM5 heartbeat (keep-alive) */
    CMD_SAFE_STATE      = 0x21,  /* Enter safe state immediately */
    CMD_RECOVERY        = 0x22,  /* Exit safe state */
    CMD_REBOOT          = 0xFE,  /* Reboot card */
};

/* TCP response status codes */
enum cellsim_status {
    STATUS_OK           = 0x00,
    STATUS_ERR_INVALID  = 0x01,
    STATUS_ERR_BUSY     = 0x02,
    STATUS_ERR_SAFE     = 0x03,  /* In safe state, command rejected */
    STATUS_ERR_INTERNAL = 0xFF,
};

/**
 * Initialize network services.
 *
 * @param slot_id  Card slot ID (0-15), used for mDNS hostname
 * @return 0 on success, negative errno on error
 */
int network_init(uint8_t slot_id);

/**
 * Start the TCP command server thread.
 * Listens on CELLSIM_TCP_CMD_PORT and dispatches commands.
 */
int network_start_cmd_server(void);

/**
 * Start the UDP measurement streaming thread.
 * Sends measurement packets at 100 Hz to the CM5.
 */
int network_start_meas_stream(void);

/**
 * Send a measurement packet (called from the 100 Hz control loop).
 *
 * @param data  Pointer to packed measurement data
 * @param len   Length of data
 * @return 0 on success, negative errno on error
 */
int network_send_measurement(const void *data, size_t len);

#endif /* CELLSIM_NETWORK_H */
