/*
 * Copyright (c) 2025 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UART_CB_H
#define UART_CB_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>

extern const struct device *const uart_dev;
extern struct k_sem rx_data_sem;
extern struct k_sem rx_disabled_sem;

#endif /* UART_CB_H */
