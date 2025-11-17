/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

int zmk_rgb_led_toggle(void);
int zmk_rgb_led_get_state(bool *state);
int zmk_rgb_led_on(void);
int zmk_rgb_led_off(void);
int zmk_rgb_led_change_brightness(int direction);
