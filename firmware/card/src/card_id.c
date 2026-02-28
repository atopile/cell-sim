/*
 * CellSim Card — Card Identity (24AA025E48 EEPROM) Implementation
 */

#include "card_id.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(card_id, LOG_LEVEL_INF);

/* 24AA025E48 on I2C3 */
#define EEPROM_ADDR         0x50
#define EEPROM_EUI48_OFFSET 0xFA  /* EUI-48 starts at byte address 0xFA */

static uint8_t eui48[CARD_ID_EUI48_LEN];
static bool id_valid;

int card_id_init(void)
{
    const struct device *bus = DEVICE_DT_GET(DT_ALIAS(i2c_aux));
    if (!device_is_ready(bus)) {
        LOG_ERR("I2C3 (card identity bus) not ready");
        return -ENODEV;
    }

    /* Read 6 bytes starting at address 0xFA */
    uint8_t reg = EEPROM_EUI48_OFFSET;
    int ret = i2c_write_read(bus, EEPROM_ADDR, &reg, 1,
                             eui48, CARD_ID_EUI48_LEN);
    if (ret != 0) {
        LOG_ERR("24AA025E48 EEPROM read failed: %d", ret);
        id_valid = false;
        return ret;
    }

    /* Sanity: check it's not all-FF (blank EEPROM) or all-00 */
    bool all_ff = true, all_00 = true;
    for (int i = 0; i < CARD_ID_EUI48_LEN; i++) {
        if (eui48[i] != 0xFF) all_ff = false;
        if (eui48[i] != 0x00) all_00 = false;
    }

    if (all_ff || all_00) {
        LOG_WRN("EEPROM EUI-48 appears blank (all %s)",
                all_ff ? "0xFF" : "0x00");
        id_valid = false;
        return -ENODATA;
    }

    id_valid = true;

    char id_str[18];
    card_id_format_eui48(id_str, sizeof(id_str));
    LOG_INF("Card ID: %s", id_str);

    return 0;
}

bool card_id_get_eui48(uint8_t out[CARD_ID_EUI48_LEN])
{
    if (!id_valid || out == NULL) {
        return false;
    }
    memcpy(out, eui48, CARD_ID_EUI48_LEN);
    return true;
}

int card_id_format_eui48(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len < 18) {
        return 0;
    }
    return snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                    eui48[0], eui48[1], eui48[2],
                    eui48[3], eui48[4], eui48[5]);
}
