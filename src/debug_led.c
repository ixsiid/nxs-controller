#include "debug_led.h"

#if defined(CONFIG_APP_DEBUG_LED)

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

void debug_led_init(void)
{
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
}

static void blink(const struct gpio_dt_spec *led, int times, int on_ms, int off_ms)
{
    for (int i = 0; i < times; i++) {
        gpio_pin_set_dt(led, 1);
        k_msleep(on_ms);
        gpio_pin_set_dt(led, 0);
        if (i + 1 < times) {
            k_msleep(off_ms);
        }
    }
}

void debug_led_shift_result(bool up, bool ok)
{
    if (!ok) {
        blink(&led_red, 3, 100, 100);   /* 失敗: 赤3回点滅（上下共通） */
        return;
    }
    /* 成功: 方向で色を分ける（アップ=緑、ダウン=青） */
    blink(up ? &led_green : &led_blue, 1, 300, 0);
}

#else /* !CONFIG_APP_DEBUG_LED */

void debug_led_init(void) {}
void debug_led_shift_result(bool up, bool ok) {}

#endif