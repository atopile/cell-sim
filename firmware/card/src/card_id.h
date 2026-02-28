/*
 * CellSim Card — Card Identity (24AA025E48 EEPROM)
 *
 * Reads the factory-programmed EUI-48 from the Microchip 24AA025E48
 * EEPROM on I2C3. Used as:
 *   - Unique card identifier (survives MCU replacement)
 *   - Ethernet MAC address for LAN8742A
 *   - Calibration data binding key
 *
 * The EUI-48 is stored at EEPROM addresses 0xFA–0xFF (read-only).
 * The first 250 bytes (0x00–0xF9) are user-writable EEPROM.
 */

#ifndef CELLSIM_CARD_ID_H
#define CELLSIM_CARD_ID_H

#include <stdbool.h>
#include <stdint.h>

#define CARD_ID_EUI48_LEN  6

/**
 * Initialize card identity by reading EUI-48 from EEPROM on I2C3.
 *
 * @return 0 on success, negative errno on error
 */
int card_id_init(void);

/**
 * Get the 6-byte EUI-48 (MAC address).
 *
 * @param out  Buffer of at least CARD_ID_EUI48_LEN bytes
 * @return true if ID is valid (init succeeded), false otherwise
 */
bool card_id_get_eui48(uint8_t out[CARD_ID_EUI48_LEN]);

/**
 * Format EUI-48 as "XX:XX:XX:XX:XX:XX" string.
 *
 * @param buf      Output buffer (at least 18 bytes)
 * @param buf_len  Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
int card_id_format_eui48(char *buf, size_t buf_len);

#endif /* CELLSIM_CARD_ID_H */
