/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>

#include <zephyr/drivers/led.h>
#include <zephyr/drivers/led/is31fl3733.h>

#include <zmk/rgb_led.h>

#include <zmk/activity.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_led)

#error "A zmk,led chosen node must be declared"

#endif

#define LED_CHOSEN DT_CHOSEN(zmk_led)
#define HW_ROW_COUNT 12
#define HW_COL_COUNT 16
#define OFF 0x00
#define ON 0xFF

/* LED matrix is addressed using a row major format */
#define LED_MATRIX_COORD(x, y) ((x) * HW_COL_COUNT) + (y)

static uint8_t led_state[HW_COL_COUNT * HW_ROW_COUNT];

struct rgb_led_state {
    bool on;
    int brightness;
};

static struct rgb_led_state state;

static const struct device *led_device;

static int zmk_rgb_led_init(void) {
    led_device = DEVICE_DT_GET(LED_CHOSEN);
    if (!device_is_ready(led_device)) {
        printk("Error- LED device is not ready\n");
        return 0;
    }
    else {
        printk("Error- LED device is ready\n");
    }
    LOG_ERR("Unable to enable EXT_POWER");

    return 0;
}

int zmk_rgb_led_get_state(bool *on_off) {
    if (!led_device)
        return -ENODEV;

    *on_off = state.on;
    return 0;
}

static int led_channel_write(const uint8_t brigtness)
{
    uint32_t led_idx;

    memset(led_state, 0, sizeof(led_state));
    for (uint8_t row = 0; row < HW_ROW_COUNT; row++) {
        for (uint8_t col = 0; col < HW_COL_COUNT; col++) {
            led_idx = LED_MATRIX_COORD(row, col);
            led_state[led_idx] = brigtness;
        }
    }
    led_write_channels(led_device, 0, sizeof(led_state), led_state);
    return 0;
}

int zmk_rgb_led_on(void) {
    if (!led_device)
        return -ENODEV;

    state.on = true;
    led_channel_write(ON);

    return 0;
}

static void zmk_rgb_led_off_handler(struct k_work *work) {
    led_channel_write(OFF);
}

K_WORK_DEFINE(led_off_work, zmk_rgb_led_off_handler);

int zmk_rgb_led_off(void) {
    if (!led_device)
        return -ENODEV;

    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &led_off_work);
    state.on = false;

    return 0;
}

int zmk_rgb_led_toggle(void) {
    return state.on ? zmk_rgb_led_off() : zmk_rgb_led_on();
}

static int led_brightness(int brigtness)
{
    uint8_t row, col;

    for (row = 0; row < HW_ROW_COUNT; row++) {
        for (col = 0; col < HW_COL_COUNT; col++) {
            LOG_INF("Setting led brigtness to %i", brigtness);
            led_set_brightness(led_device, LED_MATRIX_COORD(row, col), brigtness);
        }
    }
    return 0;
}

int zmk_rgb_led_change_brightness(int modifier) {
    if (!led_device)
        return -ENODEV;

    int s = state.brightness + modifier;
    if (s < 0) {
        s = 0;
    } else if (s > 100) {
        s = 100;
    }
    state.brightness = s;
    led_brightness(state.brightness);

    return 0;
}

SYS_INIT(zmk_rgb_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
