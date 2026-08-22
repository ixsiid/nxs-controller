#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/bluetooth/bluetooth.h>

#include "nxs_wireless_client.h"
#include "nxs_macaddr.h"

#define NON_WAKEUP_RESET_REASON (RESET_PIN | RESET_SOFTWARE | RESET_POR | RESET_DEBUG)

static const struct gpio_dt_spec btn_up   = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec btn_down = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);

#if DT_NODE_EXISTS(DT_NODELABEL(p25q16h))
static const struct device *ext_flash = DEVICE_DT_GET(DT_NODELABEL(p25q16h));
#endif

static void arm_wakeup_and_poweroff(void)
{
    gpio_pin_configure_dt(&btn_up, GPIO_INPUT);
    gpio_pin_configure_dt(&btn_down, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_up, GPIO_INT_LEVEL_ACTIVE);
    gpio_pin_interrupt_configure_dt(&btn_down, GPIO_INT_LEVEL_ACTIVE);

#if DT_NODE_EXISTS(DT_NODELABEL(p25q16h))
    /* XIAOのオンボードQSPIフラッシュを明示的にディープパワーダウン。
       やらないとSystem OFF中も~1mA食われ続ける */
    if (device_is_ready(ext_flash)) {
        pm_device_action_run(ext_flash, PM_DEVICE_ACTION_SUSPEND);
    }
#endif

    hwinfo_clear_reset_cause();
    sys_poweroff();
    /* 戻ってこない */
}

int main(void)
{
    uint32_t reset_cause = 0;
    hwinfo_get_reset_cause(&reset_cause);

    if (reset_cause & NON_WAKEUP_RESET_REASON) {
        /* 書き込み直後・通常リセット。何もせずSystem OFFへ */
        arm_wakeup_and_poweroff();
        return 0;
    }

    /* GPIOによるSystem OFFからの復帰 = ボタン押下起床 */
    gpio_pin_configure_dt(&btn_up, GPIO_INPUT);
    gpio_pin_configure_dt(&btn_down, GPIO_INPUT);

    bool up   = gpio_pin_get_dt(&btn_up) > 0;
    bool down = gpio_pin_get_dt(&btn_down) > 0;

    if (up || down) {
        bt_enable(NULL);
        nxs_client_init(NXS_MAC, NXS_PIN);

        int retry = 3;
        bool ok = false;
        while (retry-- && !ok) {
            ok = up ? nxs_connect_up_disconnect()
                     : nxs_connect_down_disconnect();
        }
        printk("shift %s: %s\n", up ? "up" : "down", ok ? "ok" : "failed");
    }

    arm_wakeup_and_poweroff();
    return 0;
}