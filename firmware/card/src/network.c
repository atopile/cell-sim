/*
 * CellSim Card — Network Services Implementation
 *
 * TCP command server: dispatches commands to cell HAL
 * UDP measurement stream: 100 Hz packed binary packets
 * mDNS: advertises as cellsim-card-{slot_id}.local
 *
 * Packet layout matches software/src/cellsim/protocol.py exactly.
 */

#include "network.h"
#include "card_id.h"
#include "cell.h"
#include "watchdog.h"
#include "self_test.h"
#include "health.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/hostname.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(network, LOG_LEVEL_INF);

/* ── mDNS ────────────────────────────────────────────────────────── */

static char mdns_hostname[32];

/* DNS-SD service registration for _cellsim._tcp discovery */
DNS_SD_REGISTER_TCP_SERVICE(cellsim_service, "_cellsim", "_tcp", "local",
                            DNS_SD_EMPTY_TXT, CELLSIM_TCP_CMD_PORT);

/* ── UDP measurement socket ──────────────────────────────────────── */

static int udp_sock = -1;
static struct sockaddr_in cm5_addr;
static bool cm5_addr_valid;

/* ── Thread stacks ───────────────────────────────────────────────── */

#define CMD_SERVER_STACK_SIZE  4096
#define CMD_SERVER_PRIORITY    7
static K_THREAD_STACK_DEFINE(cmd_server_stack, CMD_SERVER_STACK_SIZE);
static struct k_thread cmd_server_thread;

#define MEAS_STREAM_STACK_SIZE 4096
#define MEAS_STREAM_PRIORITY   3
static K_THREAD_STACK_DEFINE(meas_stream_stack, MEAS_STREAM_STACK_SIZE);
static struct k_thread meas_stream_thread;

/* ── State ───────────────────────────────────────────────────────── */

static uint8_t card_slot_id;
static uint32_t meas_seq;

/* Firmware version — imported from main.c convention */
#define FW_VERSION_MAJOR 2
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

/* ── Measurement Packet Packing ──────────────────────────────────── */

/*
 * Wire format (big-endian, 109 bytes total):
 *   Header (13):  slot_id:u8, seq:u32, timestamp_us:u64
 *   Cells (8×11): setpoint_mv:u16, voltage_mv:u16, current_ua:u32,
 *                 temp_c10:i16, flags:u8
 *   Health (8):   mcu_temp_c10:i16, vin_mv:u16, uptime_ms:u32
 */
#define MEAS_CELL_SIZE    11
#define MEAS_HEADER_SIZE  13
#define MEAS_HEALTH_SIZE  8
#define MEAS_PACKET_SIZE  (MEAS_HEADER_SIZE + (CELL_COUNT * MEAS_CELL_SIZE) + MEAS_HEALTH_SIZE)

BUILD_ASSERT(MEAS_PACKET_SIZE == 109, "Measurement packet must be exactly 109 bytes");

/* Big-endian pack helpers */
static inline void pack_u8(uint8_t *buf, uint8_t v)     { buf[0] = v; }
static inline void pack_u16(uint8_t *buf, uint16_t v)   { buf[0] = v >> 8; buf[1] = v; }
static inline void pack_i16(uint8_t *buf, int16_t v)    { pack_u16(buf, (uint16_t)v); }
static inline void pack_u32(uint8_t *buf, uint32_t v)   { buf[0] = v >> 24; buf[1] = v >> 16; buf[2] = v >> 8; buf[3] = v; }
static inline void pack_u64(uint8_t *buf, uint64_t v)   {
    pack_u32(buf, (uint32_t)(v >> 32));
    pack_u32(buf + 4, (uint32_t)v);
}

static int build_measurement_packet(uint8_t *pkt)
{
    uint8_t *p = pkt;

    /* Header */
    pack_u8(p, card_slot_id);           p += 1;
    pack_u32(p, meas_seq++);            p += 4;
    pack_u64(p, (uint64_t)k_uptime_get() * 1000ULL);  p += 8;

    /* Per-cell measurements */
    for (int i = 0; i < CELL_COUNT; i++) {
        const struct cell_state *cs = cell_get_state(i);
        if (cs == NULL) {
            memset(p, 0, MEAS_CELL_SIZE);
            p += MEAS_CELL_SIZE;
            continue;
        }
        pack_u16(p, cs->setpoint_mv);      p += 2;
        pack_u16(p, cs->voltage_mv);       p += 2;
        pack_u32(p, cs->current_ua);       p += 4;
        pack_i16(p, cs->temp_c10);         p += 2;
        pack_u8(p, cs->flags);             p += 1;
    }

    /* Health trailer */
    struct health_data hd;
    health_get_all(&hd);
    int16_t mcu_temp_c10 = (int16_t)(hd.mcu_temp_celsius * 10.0f);
    uint16_t vin_mv = (uint16_t)(hd.input_voltage * 1000.0f);

    pack_i16(p, mcu_temp_c10);          p += 2;
    pack_u16(p, vin_mv);                p += 2;
    pack_u32(p, hd.uptime_ms);          p += 4;

    return MEAS_PACKET_SIZE;
}

/* ── CM5 Address Discovery ───────────────────────────────────────── */

static void discover_cm5_address(void)
{
    if (cm5_addr_valid) {
        return;
    }

    /* Strategy: use the default gateway as the CM5 address.
     * The CM5 is the gateway on the 10.0.0.x subnet. */
    struct net_if *iface = net_if_get_default();
    if (iface == NULL) {
        return;
    }

    struct net_if_router *router = net_if_ipv4_router_find_default(iface, NULL);
    if (router != NULL) {
        memset(&cm5_addr, 0, sizeof(cm5_addr));
        cm5_addr.sin_family = AF_INET;
        cm5_addr.sin_port = htons(CELLSIM_UDP_MEAS_PORT);
        memcpy(&cm5_addr.sin_addr, &router->address.in_addr,
               sizeof(struct in_addr));
        cm5_addr_valid = true;

        char addr_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &cm5_addr.sin_addr, addr_str, sizeof(addr_str));
        LOG_INF("CM5 address resolved via gateway: %s:%d",
                addr_str, CELLSIM_UDP_MEAS_PORT);
    }
}

/* ── TCP Command Handling ────────────────────────────────────────── */

/*
 * Serialize self-test results into response payload.
 * Format: [all_passed:u8] [per_cell_bitmap:u8]×8
 *   bitmap bits: 0=isolator, 1=dac_buck, 2=dac_ldo, 3=adc, 4=gpio, 5=temp, 6=relay
 */
static uint16_t serialize_self_test(uint8_t *buf, size_t buf_size)
{
    struct self_test_result st;
    int ret = self_test_run(&st);
    if (ret != 0) {
        return 0;
    }

    if (buf_size < 1 + CELL_COUNT) {
        return 0;
    }

    buf[0] = st.all_passed ? 1 : 0;
    for (int i = 0; i < CELL_COUNT; i++) {
        struct cell_test_result *c = &st.cells[i];
        uint8_t bits = 0;
        if (c->i2c_isolator_ok) bits |= (1 << 0);
        if (c->dac_buck_ok)     bits |= (1 << 1);
        if (c->dac_ldo_ok)      bits |= (1 << 2);
        if (c->adc_ok)          bits |= (1 << 3);
        if (c->gpio_ok)         bits |= (1 << 4);
        if (c->temp_ok)         bits |= (1 << 5);
        if (c->relay_ok)        bits |= (1 << 6);
        buf[1 + i] = bits;
    }

    return 1 + CELL_COUNT;
}

/*
 * Serialize full card state into response payload.
 * Format: [fw_major:u8][fw_minor:u8][fw_patch:u8][slot_id:u8][safe_state:u8]
 *         [eui48:6B]
 *         [cells × 8: setpoint_mv:u16, voltage_mv:u16, current_ua:u32, temp_c10:i16, flags:u8]
 *         [mcu_temp_c10:i16][vin_mv:u16][uptime_ms:u32]
 */
static uint16_t serialize_card_state(uint8_t *buf, size_t buf_size)
{
    const size_t hdr = 5 + CARD_ID_EUI48_LEN;  /* 5 + 6 = 11 bytes */
    const size_t cell_block = CELL_COUNT * MEAS_CELL_SIZE;
    const size_t health_block = MEAS_HEALTH_SIZE;
    const size_t total = hdr + cell_block + health_block;

    if (buf_size < total) {
        return 0;
    }

    uint8_t *p = buf;

    /* Card info header */
    pack_u8(p++, FW_VERSION_MAJOR);
    pack_u8(p++, FW_VERSION_MINOR);
    pack_u8(p++, FW_VERSION_PATCH);
    pack_u8(p++, card_slot_id);
    pack_u8(p++, watchdog_heartbeat_expired() ? 1 : 0);

    /* Card identity (EUI-48) */
    if (!card_id_get_eui48(p)) {
        memset(p, 0, CARD_ID_EUI48_LEN);  /* Zero if not available */
    }
    p += CARD_ID_EUI48_LEN;

    /* Per-cell state */
    for (int i = 0; i < CELL_COUNT; i++) {
        const struct cell_state *cs = cell_get_state(i);
        if (cs == NULL) {
            memset(p, 0, MEAS_CELL_SIZE);
            p += MEAS_CELL_SIZE;
            continue;
        }
        pack_u16(p, cs->setpoint_mv);  p += 2;
        pack_u16(p, cs->voltage_mv);   p += 2;
        pack_u32(p, cs->current_ua);   p += 4;
        pack_i16(p, cs->temp_c10);     p += 2;
        pack_u8(p, cs->flags);         p += 1;
    }

    /* Health */
    struct health_data hd;
    health_get_all(&hd);
    pack_i16(p, (int16_t)(hd.mcu_temp_celsius * 10.0f));  p += 2;
    pack_u16(p, (uint16_t)(hd.input_voltage * 1000.0f));  p += 2;
    pack_u32(p, hd.uptime_ms);                            p += 4;

    return (uint16_t)total;
}

static int handle_command(int client_fd)
{
    uint8_t header[4]; /* cmd, cell_id, payload_len (u16 BE) */

    ssize_t n = zsock_recv(client_fd, header, sizeof(header), 0);
    if (n <= 0) {
        return -1; /* Connection closed or error */
    }
    if (n < 4) {
        LOG_WRN("Short command header (%zd bytes)", n);
        return -EINVAL;
    }

    uint8_t cmd = header[0];
    uint8_t cell_id = header[1];
    uint16_t payload_len = (header[2] << 8) | header[3];

    /* Read payload if present */
    uint8_t payload[256];
    if (payload_len > 0) {
        if (payload_len > sizeof(payload)) {
            LOG_ERR("Payload too large: %u", payload_len);
            return -ENOMEM;
        }
        n = zsock_recv(client_fd, payload, payload_len, ZSOCK_MSG_WAITALL);
        if (n != payload_len) {
            return -EIO;
        }
    }

    /* Any valid command resets heartbeat (except NOP) */
    if (cmd != CMD_NOP) {
        watchdog_heartbeat_received();
    }

    /* Dispatch */
    uint8_t status = STATUS_OK;
    uint8_t resp_payload[256];
    uint16_t resp_len = 0;
    int ret;

    switch (cmd) {
    case CMD_NOP:
        break;

    case CMD_HEARTBEAT:
        /* Heartbeat already recorded above */
        break;

    case CMD_SAFE_STATE:
        watchdog_enter_safe_state();
        LOG_WRN("Safe state triggered by CM5 command");
        break;

    case CMD_SELF_TEST:
        LOG_INF("Self-test requested");
        resp_len = serialize_self_test(resp_payload, sizeof(resp_payload));
        if (resp_len == 0) {
            status = STATUS_ERR_INTERNAL;
        }
        break;

    case CMD_GET_STATE:
        resp_len = serialize_card_state(resp_payload, sizeof(resp_payload));
        if (resp_len == 0) {
            status = STATUS_ERR_INTERNAL;
        }
        break;

    case CMD_SET_VOLTAGE:
        if (watchdog_heartbeat_expired()) {
            status = STATUS_ERR_SAFE;
        } else if (payload_len < 2 || cell_id >= CELL_COUNT) {
            status = STATUS_ERR_INVALID;
        } else {
            uint16_t mv = (payload[0] << 8) | payload[1];
            ret = cell_set_voltage(cell_id, mv);
            if (ret != 0) {
                LOG_ERR("cell_set_voltage(%d, %u) failed: %d", cell_id, mv, ret);
                status = STATUS_ERR_INTERNAL;
            } else {
                LOG_DBG("Cell %d voltage → %u mV", cell_id, mv);
            }
        }
        break;

    case CMD_ENABLE_OUTPUT:
        if (watchdog_heartbeat_expired()) {
            status = STATUS_ERR_SAFE;
        } else if (payload_len < 1 || cell_id >= CELL_COUNT) {
            status = STATUS_ERR_INVALID;
        } else {
            bool enable = (payload[0] != 0);
            ret = cell_set_output(cell_id, enable);
            if (ret != 0) {
                LOG_ERR("cell_set_output(%d, %d) failed: %d", cell_id, enable, ret);
                status = STATUS_ERR_INTERNAL;
            } else {
                LOG_DBG("Cell %d output → %s", cell_id, enable ? "ON" : "OFF");
            }
        }
        break;

    case CMD_SET_MODE:
        if (watchdog_heartbeat_expired()) {
            status = STATUS_ERR_SAFE;
        } else if (payload_len < 1 || cell_id >= CELL_COUNT) {
            status = STATUS_ERR_INVALID;
        } else {
            bool four_wire = (payload[0] != 0);
            ret = cell_set_mode(cell_id, four_wire);
            if (ret != 0) {
                LOG_ERR("cell_set_mode(%d, %d) failed: %d", cell_id, four_wire, ret);
                status = STATUS_ERR_INTERNAL;
            } else {
                LOG_DBG("Cell %d mode → %s", cell_id, four_wire ? "4-wire" : "2-wire");
            }
        }
        break;

    case CMD_CALIBRATE:
        /* Calibration: read ADC at known setpoints to compute offset/gain */
        LOG_INF("Calibration requested (cell %d)", cell_id);
        status = STATUS_ERR_BUSY; /* TODO: implement calibration sequence */
        break;

    case CMD_RECOVERY:
        ret = watchdog_exit_safe_state();
        if (ret != 0) {
            LOG_WRN("Recovery requested but not in safe state");
            status = STATUS_ERR_INVALID;
        } else {
            LOG_INF("Safe state cleared by CM5 recovery command");
        }
        break;

    case CMD_REBOOT:
        LOG_WRN("Reboot requested by CM5");
        /* Send response before rebooting */
        {
            uint8_t rh[3] = { STATUS_OK, 0, 0 };
            zsock_send(client_fd, rh, sizeof(rh), 0);
        }
        k_sleep(K_MSEC(50));  /* Let response flush */
        sys_reboot(SYS_REBOOT_COLD);
        break; /* unreachable */

    default:
        LOG_WRN("Unknown command: 0x%02X", cmd);
        status = STATUS_ERR_INVALID;
        break;
    }

    /* Send response (except for REBOOT which already sent its own) */
    if (cmd != CMD_REBOOT) {
        uint8_t resp_header[3] = {
            status,
            (resp_len >> 8) & 0xFF,
            resp_len & 0xFF,
        };
        zsock_send(client_fd, resp_header, sizeof(resp_header), 0);
        if (resp_len > 0) {
            zsock_send(client_fd, resp_payload, resp_len, 0);
        }
    }

    return 0;
}

/* ── TCP Command Server Thread ───────────────────────────────────── */

static void cmd_server_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        LOG_ERR("Failed to create TCP socket: %d", errno);
        return;
    }

    /* Allow port reuse after restart */
    int opt = 1;
    zsock_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(CELLSIM_TCP_CMD_PORT),
    };

    if (zsock_bind(server_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("TCP bind failed: %d", errno);
        zsock_close(server_fd);
        return;
    }

    if (zsock_listen(server_fd, 2) < 0) {
        LOG_ERR("TCP listen failed: %d", errno);
        zsock_close(server_fd);
        return;
    }

    LOG_INF("TCP command server listening on port %d", CELLSIM_TCP_CMD_PORT);

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = zsock_accept(server_fd, (struct sockaddr *)&client_addr,
                                      &client_len);
        if (client_fd < 0) {
            LOG_ERR("TCP accept failed: %d", errno);
            k_sleep(K_MSEC(100));
            continue;
        }

        /* Set TCP_NODELAY for low-latency command response */
        opt = 1;
        zsock_setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        char addr_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
        LOG_INF("TCP client connected from %s", addr_str);

        /* Remember CM5 address for UDP if not yet known */
        if (!cm5_addr_valid) {
            memset(&cm5_addr, 0, sizeof(cm5_addr));
            cm5_addr.sin_family = AF_INET;
            cm5_addr.sin_port = htons(CELLSIM_UDP_MEAS_PORT);
            memcpy(&cm5_addr.sin_addr, &client_addr.sin_addr,
                   sizeof(struct in_addr));
            cm5_addr_valid = true;
            LOG_INF("CM5 address learned from TCP client: %s", addr_str);
        }

        /* Handle commands until connection closes */
        while (handle_command(client_fd) == 0) {
            /* keep processing */
        }

        zsock_close(client_fd);
        LOG_INF("TCP client disconnected");
    }
}

/* ── UDP Measurement Stream Thread ───────────────────────────────── */

static void meas_stream_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Wait for network (DHCP) to come up */
    k_sleep(K_SECONDS(3));

    LOG_INF("UDP measurement stream started (port %d, %d bytes/pkt)",
            CELLSIM_UDP_MEAS_PORT, MEAS_PACKET_SIZE);

    uint8_t pkt[MEAS_PACKET_SIZE];
    int64_t next_tick = k_uptime_get();
    const int64_t period_ms = 10;  /* 100 Hz */

    while (true) {
        /* Read measurements from all cells */
        for (uint8_t i = 0; i < CELL_COUNT; i++) {
            struct cell_state state;
            cell_read_measurements(i, &state);
        }

        /* Build and send packet */
        int len = build_measurement_packet(pkt);

        if (cm5_addr_valid && udp_sock >= 0) {
            ssize_t sent = zsock_sendto(udp_sock, pkt, len, 0,
                                         (struct sockaddr *)&cm5_addr,
                                         sizeof(cm5_addr));
            if (sent < 0 && errno != EAGAIN && errno != ENETUNREACH) {
                LOG_WRN("UDP send failed: %d", errno);
            }
        } else {
            /* Try to discover CM5 address */
            discover_cm5_address();
        }

        /* Feed hardware watchdog */
        watchdog_feed();

        /* Sleep until next tick (drift-compensating) */
        next_tick += period_ms;
        int64_t now = k_uptime_get();
        int64_t remaining = next_tick - now;
        if (remaining > 0) {
            k_sleep(K_MSEC(remaining));
        } else {
            /* Overrun — skip and realign */
            next_tick = now + period_ms;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

int network_init(uint8_t slot_id)
{
    card_slot_id = slot_id;
    meas_seq = 0;
    cm5_addr_valid = false;

    /* Set mDNS hostname */
    snprintf(mdns_hostname, sizeof(mdns_hostname),
             "cellsim-card-%d", slot_id);
    net_hostname_set(mdns_hostname, strlen(mdns_hostname));

    /* Log card identity for mDNS TXT record context */
    char id_str[18];
    if (card_id_format_eui48(id_str, sizeof(id_str)) > 0) {
        LOG_INF("Card ID: %s (slot %d, fw %d.%d.%d)",
                id_str, slot_id,
                FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    }

    /* Create UDP socket for measurement streaming */
    udp_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        LOG_ERR("Failed to create UDP socket: %d", errno);
        return -errno;
    }

    LOG_INF("Network initialized for slot %d (%s.local)", slot_id, mdns_hostname);
    return 0;
}

int network_start_cmd_server(void)
{
    k_thread_create(&cmd_server_thread, cmd_server_stack,
                     CMD_SERVER_STACK_SIZE,
                     cmd_server_entry, NULL, NULL, NULL,
                     CMD_SERVER_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&cmd_server_thread, "tcp_cmd_srv");
    return 0;
}

int network_start_meas_stream(void)
{
    k_thread_create(&meas_stream_thread, meas_stream_stack,
                     MEAS_STREAM_STACK_SIZE,
                     meas_stream_entry, NULL, NULL, NULL,
                     MEAS_STREAM_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&meas_stream_thread, "udp_meas");
    return 0;
}

int network_send_measurement(const void *data, size_t len)
{
    if (udp_sock < 0 || data == NULL || !cm5_addr_valid) {
        return -EINVAL;
    }

    ssize_t sent = zsock_sendto(udp_sock, data, len, 0,
                                (struct sockaddr *)&cm5_addr, sizeof(cm5_addr));
    if (sent < 0) {
        return -errno;
    }

    return 0;
}
