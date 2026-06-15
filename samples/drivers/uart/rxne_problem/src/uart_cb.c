/*
 * Copyright (c) 2025 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(sample, LOG_LEVEL_INF);

#include "uart_cb.h"

const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));
K_SEM_DEFINE(rx_data_sem, 0, UINT_MAX);
K_SEM_DEFINE(rx_disabled_sem, 0, UINT_MAX);

extern void on_tx_done(void);
extern void on_rx_rdy(const struct uart_event *evt);

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	switch (evt->type) {
	case UART_TX_DONE:
		on_tx_done();
		break;
	case UART_RX_RDY:
		//LOG_INF("    RX ISR offset %d, len %d", evt->data.rx.offset, evt->data.rx.len);
		on_rx_rdy(evt);
		k_sem_give(&rx_data_sem);
		break;
	case UART_RX_DISABLED:
		k_sem_give(&rx_disabled_sem);
		break;
	case UART_RX_BUF_REQUEST:
	case UART_RX_BUF_RELEASED:
		break;
	default:
		LOG_WRN("Unhandled UART event %d", evt->type);
	}

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
}

static int uart_cb_init(void)
{
	LOG_INF("");
	LOG_INF("----- UART callback init -----");
	return uart_callback_set(uart_dev, uart_cb, NULL);
}

#define UART_CB_INIT_PRIORITY 1
SYS_INIT(uart_cb_init, APPLICATION, UART_CB_INIT_PRIORITY);
