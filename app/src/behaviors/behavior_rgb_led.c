/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgb_led

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/rgb.h>
#include <zmk/rgb_led.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata no_arg_values[] = {
    {
        .display_name = "Toggle On/Off",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = RGB_TOG_CMD,
    },
    {
        .display_name = "Turn On",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = RGB_ON_CMD,
    },
    {
        .display_name = "Turn OFF",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = RGB_OFF_CMD,
    },
    {
        .display_name = "Brightness Up",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = RGB_BRI_CMD,
    },
    {
        .display_name = "Brightness Down",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = RGB_BRD_CMD,
    },
};

static const struct behavior_parameter_metadata_set no_args_set = {
    .param1_values = no_arg_values,
    .param1_values_len = ARRAY_SIZE(no_arg_values),
};

static const struct behavior_parameter_metadata_set sets[] = {
    no_args_set,
    // hsv_value_metadata_set,
};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(sets),
    .sets = sets,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int
on_keymap_binding_convert_central_state_dependent_params(struct zmk_behavior_binding *binding,
                                                         struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case RGB_TOG_CMD: {
        bool state;
        int err = zmk_rgb_led_get_state(&state);
        if (err) {
            LOG_ERR("Failed to get RGB led state (err %d)", err);
            return err;
        }

        binding->param1 = state ? RGB_OFF_CMD : RGB_ON_CMD;
        break;
    }
    default:
        return 0;
    }

    LOG_DBG("RGB relative convert to absolute (%d/%d)", binding->param1, binding->param2);

    return 0;
};

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case RGB_TOG_CMD:
        return zmk_rgb_led_toggle();
    case RGB_ON_CMD:
        return zmk_rgb_led_on();
    case RGB_OFF_CMD:
        return zmk_rgb_led_off();
    case RGB_BRI_CMD:
        return zmk_rgb_led_change_brightness(10);
    case RGB_BRD_CMD:
        return zmk_rgb_led_change_brightness(-10);
    }

    return -ENOTSUP;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_rgb_led_driver_api = {
    .binding_convert_central_state_dependent_params =
        on_keymap_binding_convert_central_state_dependent_params,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_rgb_led_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
