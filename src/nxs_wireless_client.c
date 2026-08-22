#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/addr.h>
#include <string.h>
#include "nxs_wireless_client.h"

/*
 * NXS Wireless
 *   Service:              a5c1c000-cc20-ba91-0c1a-ef3f9e643d79
 *   Characteristic(auth):  a5c1cc02-cc20-ba91-0c1a-ef3f9e643d79
 *   Characteristic(ctrl):  a5c1cc01-cc20-ba91-0c1a-ef3f9e643d79
 *
 * ESP32-S3実装からの移植: 接続先(BlakBullet)に直接つなぎ、
 * 認証用キャラクタリスティックにPINを書き込んでから、
 * 制御用キャラクタリスティックにシフトコマンドを書き込む。
 * シフトコマンドの末尾6バイトは「接続先自身のMACアドレス」。
 */

#define BT_UUID_NXS_AUTH_VAL BT_UUID_128_ENCODE(0xa5c1cc02, 0xcc20, 0xba91, 0x0c1a, 0xef3f9e643d79)
#define BT_UUID_NXS_CTRL_VAL BT_UUID_128_ENCODE(0xa5c1cc01, 0xcc20, 0xba91, 0x0c1a, 0xef3f9e643d79)

static const struct bt_uuid_128 nxs_auth_uuid = BT_UUID_INIT_128(BT_UUID_NXS_AUTH_VAL);
static const struct bt_uuid_128 nxs_ctrl_uuid = BT_UUID_INIT_128(BT_UUID_NXS_CTRL_VAL);

static bt_addr_le_t peer_addr;
static uint8_t nxs_pin[4];
static uint8_t command_shift_up[8]   = {0x10, 0x00, 0, 0, 0, 0, 0, 0};
static uint8_t command_shift_down[8] = {0x11, 0x00, 0, 0, 0, 0, 0, 0};

static struct bt_conn *conn;
static uint16_t auth_handle, ctrl_handle;

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_write_params write_params;

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);
static K_SEM_DEFINE(sem_discovered, 0, 1);
static K_SEM_DEFINE(sem_written, 0, 1);
static bool last_op_ok;

enum { DISC_AUTH, DISC_CTRL };
static int disc_target;

static uint8_t discover_cb(struct bt_conn *c, const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params)
{
    if (attr) {
        struct bt_gatt_chrc *chrc = attr->user_data;
        if (disc_target == DISC_AUTH) {
            auth_handle = chrc->value_handle;
        } else {
            ctrl_handle = chrc->value_handle;
        }
        last_op_ok = true;
        return BT_GATT_ITER_STOP;
    }
    k_sem_give(&sem_discovered);
    return BT_GATT_ITER_STOP;
}

static bool discover_characteristic(const struct bt_uuid *uuid, int target)
{
    disc_target = target;
    last_op_ok = false;

    disc_params.uuid = uuid;
    disc_params.func = discover_cb;
    disc_params.start_handle = 0x0001;
    disc_params.end_handle = 0xffff;
    disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    if (bt_gatt_discover(conn, &disc_params)) {
        return false;
    }
    k_sem_take(&sem_discovered, K_SECONDS(5));
    return last_op_ok;
}

static void write_cb(struct bt_conn *c, uint8_t err, struct bt_gatt_write_params *params)
{
    last_op_ok = (err == 0);
    k_sem_give(&sem_written);
}

static bool gatt_write(uint16_t handle, const uint8_t *data, size_t len)
{
    if (!handle) {
        return false;
    }
    write_params.func = write_cb;
    write_params.handle = handle;
    write_params.offset = 0;
    write_params.data = data;
    write_params.length = len;

    if (bt_gatt_write(conn, &write_params)) {
        return false;
    }
    k_sem_take(&sem_written, K_SECONDS(5));
    return last_op_ok;
}

static void connected_cb(struct bt_conn *c, uint8_t err)
{
    last_op_ok = (err == 0);
    k_sem_give(&sem_connected);
}

static void disconnected_cb(struct bt_conn *c, uint8_t reason)
{
    k_sem_give(&sem_disconnected);
}

BT_CONN_CB_DEFINE(nxs_conn_cbs) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

void nxs_client_init(const char *peer_address_str, const uint8_t pin[4])
{
    bt_addr_le_from_str(peer_address_str, "random", &peer_addr);

    memcpy(&command_shift_up[2], peer_addr.a.val, 6);
    memcpy(&command_shift_down[2], peer_addr.a.val, 6);
    memcpy(nxs_pin, pin, 4);
}

bool nxs_connect(void)
{
    auth_handle = 0;
    ctrl_handle = 0;

    if (bt_conn_le_create(&peer_addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn)) {
        return false;
    }

    k_sem_take(&sem_connected, K_SECONDS(5));
    if (!last_op_ok) {
        conn = NULL;
        return false;
    }

    if (!discover_characteristic(&nxs_auth_uuid.uuid, DISC_AUTH)) goto fail;
    if (!gatt_write(auth_handle, nxs_pin, sizeof(nxs_pin))) goto fail;
    if (!discover_characteristic(&nxs_ctrl_uuid.uuid, DISC_CTRL)) goto fail;

    return true;

fail:
    nxs_disconnect();
    return false;
}

bool nxs_disconnect(void)
{
    if (!conn) {
        return true;
    }
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    k_sem_take(&sem_disconnected, K_SECONDS(5));
    bt_conn_unref(conn);
    conn = NULL;
    return true;
}

bool nxs_up(void)   { return gatt_write(ctrl_handle, command_shift_up, sizeof(command_shift_up)); }
bool nxs_down(void) { return gatt_write(ctrl_handle, command_shift_down, sizeof(command_shift_down)); }

bool nxs_connect_up_disconnect(void)
{
    if (!nxs_connect()) return false;
    bool ok = nxs_up();
    nxs_disconnect();
    return ok;
}

bool nxs_connect_down_disconnect(void)
{
    if (!nxs_connect()) return false;
    bool ok = nxs_down();
    nxs_disconnect();
    return ok;
}