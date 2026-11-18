/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_is31fl3733_strip

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(is31fl3733_strip, CONFIG_LED_STRIP_LOG_LEVEL);

#define CMD_SEL_REG 0xFD
#define CMD_SEL_LED 0x00
#define CMD_SEL_PWM 0x01
#define CMD_SEL_ABM 0x02
#define CMD_SEL_FUNC 0x03
#define CMD_SEL_NONE 0xFF

#define CMD_LOCK_REG 0xFE
#define CMD_LOCK_UNLOCK 0xC5

#define CONF_REG 0x00
#define CONF_REG_SSD_MASK 0x01
#define CONF_REG_B_EN_MASK 0x02
#define CONF_REG_SYNC_SHIFT 0x06

#define GLOBAL_CURRENT_CTRL_REG 0x01
#define TIME_UPDATE_REG 0x0E
#define SW_PULLUP_REG 0x0F
#define CS_PULLDOWN_REG 0x10
#define PULL_RES_MASK 0x07
#define RESET_REG 0x11

#define IS31FL3733_ROW_COUNT 12
#define IS31FL3733_COL_COUNT 16
#define IS31FL3733_MAX_CHANNELS (IS31FL3733_ROW_COUNT * IS31FL3733_COL_COUNT)
#define IS31FL3733_MAX_RGB_PIXELS (IS31FL3733_MAX_CHANNELS / 3)
#define IS31FL3733_ROW_STRIDE (IS31FL3733_COL_COUNT * 3)
#define IS31FL3733_GREEN_OFFSET 0
#define IS31FL3733_RED_OFFSET 16
#define IS31FL3733_BLUE_OFFSET 32
#define IS31FL3733_LED_ENABLE_BYTES (IS31FL3733_MAX_CHANNELS / 8)

struct is31fl3733_strip_config {
    struct i2c_dt_spec bus;
    struct gpio_dt_spec sdb;
    size_t chain_length;
    uint8_t current_limit;
    uint8_t sync;
    uint8_t sw_pullup;
    uint8_t cs_pulldown;
};

struct is31fl3733_strip_data {
    uint8_t selected_page;
    uint8_t scratch_buf[IS31FL3733_MAX_CHANNELS + 1];
    bool powered;
};

static int is31fl3733_strip_select_page(const struct device *dev, uint8_t page) {
    const struct is31fl3733_strip_config *config = dev->config;
    struct is31fl3733_strip_data *data = dev->data;

    if (data->selected_page == page) {
        return 0;
    }

    int ret = i2c_reg_write_byte_dt(&config->bus, CMD_LOCK_REG, CMD_LOCK_UNLOCK);
    if (ret < 0) {
        LOG_ERR("Failed to unlock page register (%d)", ret);
        return ret;
    }

    ret = i2c_reg_write_byte_dt(&config->bus, CMD_SEL_REG, page);
    if (ret < 0) {
        LOG_ERR("Failed to select page %u (%d)", page, ret);
        return ret;
    }

    data->selected_page = page;
    return 0;
}

static int is31fl3733_strip_set_power(const struct device *dev, bool enable) {
    const struct is31fl3733_strip_config *config = dev->config;
    struct is31fl3733_strip_data *data = dev->data;

    if (config->sdb.port == NULL || data->powered == enable) {
        return 0;
    }

    int ret = gpio_pin_set_dt(&config->sdb, enable ? 1 : 0);
    if (ret < 0) {
        LOG_ERR("Failed to set SDB pin (%d)", ret);
        return ret;
    }

    if (enable) {
        /* Rising edge on SDB resets the I2C interface module (datasheet Table 4). */
        data->selected_page = CMD_SEL_NONE;
    }

    data->powered = enable;
    return 0;
}

static bool is31fl3733_strip_any_active(const struct led_rgb *pixels, size_t num_pixels) {
    for (size_t i = 0; i < num_pixels; i++) {
        if (pixels[i].r > 0 || pixels[i].g > 0 || pixels[i].b > 0) {
            return true;
        }
    }

    return false;
}

static int is31fl3733_strip_update_rgb(const struct device *dev, struct led_rgb *pixels,
                                       size_t num_pixels) {
    const struct is31fl3733_strip_config *config = dev->config;
    struct is31fl3733_strip_data *data = dev->data;

    if (num_pixels > config->chain_length || num_pixels > IS31FL3733_MAX_RGB_PIXELS) {
        return -EINVAL;
    }

    bool active = is31fl3733_strip_any_active(pixels, num_pixels);
    if (active) {
        int ret = is31fl3733_strip_set_power(dev, true);
        if (ret < 0) {
            return ret;
        }
    }

    uint8_t *pwm = &data->scratch_buf[1];
    memset(pwm, 0, IS31FL3733_MAX_CHANNELS);

    for (size_t i = 0; i < num_pixels; i++) {
        size_t row = i / IS31FL3733_COL_COUNT;
        size_t col = i % IS31FL3733_COL_COUNT;
        size_t offset = (row * IS31FL3733_ROW_STRIDE) + col;

        pwm[offset + IS31FL3733_GREEN_OFFSET] = pixels[i].g;
        pwm[offset + IS31FL3733_RED_OFFSET] = pixels[i].r;
        pwm[offset + IS31FL3733_BLUE_OFFSET] = pixels[i].b;
    }

    int ret = is31fl3733_strip_select_page(dev, CMD_SEL_PWM);
    if (ret < 0) {
        return ret;
    }

    data->scratch_buf[0] = 0x00;
    ret = i2c_write_dt(&config->bus, data->scratch_buf, IS31FL3733_MAX_CHANNELS + 1);
    if (ret < 0) {
        LOG_ERR("Failed to write PWM buffer (%d)", ret);
        return ret;
    }

    if (!active) {
        ret = is31fl3733_strip_set_power(dev, false);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static size_t is31fl3733_strip_length(const struct device *dev) {
    const struct is31fl3733_strip_config *config = dev->config;

    return config->chain_length;
}

static int is31fl3733_strip_init(const struct device *dev) {
    const struct is31fl3733_strip_config *config = dev->config;
    struct is31fl3733_strip_data *data = dev->data;

    if (!i2c_is_ready_dt(&config->bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    if (config->sdb.port != NULL) {
        if (!gpio_is_ready_dt(&config->sdb)) {
            LOG_ERR("SDB GPIO not ready");
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&config->sdb, GPIO_OUTPUT_ACTIVE);
        if (ret < 0) {
            return ret;
        }
        data->powered = true;
    }

    data->selected_page = CMD_SEL_NONE;

    int ret = is31fl3733_strip_select_page(dev, CMD_SEL_FUNC);
    if (ret < 0) {
        return ret;
    }

    uint8_t dummy = 0;
    ret = i2c_reg_read_byte_dt(&config->bus, RESET_REG, &dummy);
    if (ret < 0) {
        return ret;
    }

    /* Reading RESET_REG resets all registers including page selection to POR state. */
    data->selected_page = CMD_SEL_NONE;
    ret = is31fl3733_strip_select_page(dev, CMD_SEL_FUNC);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_write_byte_dt(&config->bus, SW_PULLUP_REG, config->sw_pullup & PULL_RES_MASK);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_write_byte_dt(&config->bus, CS_PULLDOWN_REG, config->cs_pulldown & PULL_RES_MASK);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_write_byte_dt(&config->bus, GLOBAL_CURRENT_CTRL_REG, config->current_limit);
    if (ret < 0) {
        return ret;
    }

    uint8_t conf_reg = (config->sync << CONF_REG_SYNC_SHIFT) | CONF_REG_SSD_MASK;
    ret = i2c_reg_write_byte_dt(&config->bus, CONF_REG, conf_reg);
    if (ret < 0) {
        return ret;
    }

    ret = is31fl3733_strip_select_page(dev, CMD_SEL_LED);
    if (ret < 0) {
        return ret;
    }

    data->scratch_buf[0] = 0x00;
    memset(&data->scratch_buf[1], 0xFF, IS31FL3733_LED_ENABLE_BYTES);
    ret = i2c_write_dt(&config->bus, data->scratch_buf, IS31FL3733_LED_ENABLE_BYTES + 1);
    if (ret < 0) {
        return ret;
    }

    ret = is31fl3733_strip_select_page(dev, CMD_SEL_PWM);
    if (ret < 0) {
        return ret;
    }

    data->scratch_buf[0] = 0x00;
    memset(&data->scratch_buf[1], 0x00, IS31FL3733_MAX_CHANNELS);
    ret = i2c_write_dt(&config->bus, data->scratch_buf, IS31FL3733_MAX_CHANNELS + 1);
    if (ret < 0) {
        return ret;
    }

    return is31fl3733_strip_set_power(dev, false);
}

static DEVICE_API(led_strip, is31fl3733_strip_api) = {
    .update_rgb = is31fl3733_strip_update_rgb,
    .length = is31fl3733_strip_length,
};

#define IS31FL3733_STRIP_DEVICE(n)                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, chain_length) <= IS31FL3733_MAX_RGB_PIXELS,                       \
                 "IS31FL3733 supports at most 64 RGB pixels");                                     \
                                                                                                   \
    static const struct is31fl3733_strip_config is31fl3733_strip_config_##n = {                    \
        .bus = I2C_DT_SPEC_INST_GET(n),                                                            \
        .sdb = GPIO_DT_SPEC_INST_GET_OR(n, sdb_gpios, {}),                                         \
        .chain_length = DT_INST_PROP(n, chain_length),                                             \
        .current_limit = DT_INST_PROP(n, current_limit),                                           \
        .sync = DT_INST_ENUM_IDX(n, sync_mode),                                                    \
        .sw_pullup = DT_INST_PROP(n, sw_pullup),                                                   \
        .cs_pulldown = DT_INST_PROP(n, cs_pulldown),                                               \
    };                                                                                             \
                                                                                                   \
    static struct is31fl3733_strip_data is31fl3733_strip_data_##n = {                              \
        .selected_page = CMD_SEL_NONE,                                                             \
        .powered = false,                                                                          \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, &is31fl3733_strip_init, NULL, &is31fl3733_strip_data_##n,             \
                          &is31fl3733_strip_config_##n, POST_KERNEL,                               \
                          CONFIG_LED_STRIP_INIT_PRIORITY, &is31fl3733_strip_api);

DT_INST_FOREACH_STATUS_OKAY(IS31FL3733_STRIP_DEVICE)
