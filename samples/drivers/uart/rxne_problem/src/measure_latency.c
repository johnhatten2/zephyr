/*
 * Copyright (c) 2025 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(sample, LOG_LEVEL_INF);

#define MEASURE_BYTES 100
#define BAUDRATE DT_PROP(DT_NODELABEL(usart3), current_speed)

static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));

static struct k_sem tx_done_sem;
static uint32_t t0, t2;
static volatile uint32_t t1;

static void temp_uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	if (evt->type == UART_TX_DONE) {
		t1 = k_cycle_get_32();
		k_sem_give(&tx_done_sem);
	}
}

static int measure_latency_init(void)
{
	uint8_t buf[MEASURE_BYTES];
	uint64_t elapsed_ns, theoretical_ns, overhead_ns, isr_exit_ns;
	int rc;

	memset(buf, 'X', sizeof(buf));
	k_sem_init(&tx_done_sem, 0, 1);
	uart_callback_set(uart_dev, temp_uart_callback, NULL);

	t0 = k_cycle_get_32();
	rc = uart_tx(uart_dev, buf, sizeof(buf), SYS_FOREVER_US);
	if (rc != 0) {
		LOG_WRN("Latency: uart_tx failed (%d)", rc);
		return 0;
	}

	if (k_sem_take(&tx_done_sem, K_FOREVER) != 0) {
		LOG_WRN("Latency: TX_DONE timeout");
		return 0;
	}
	t2 = k_cycle_get_32();

	elapsed_ns = k_cyc_to_ns_floor64(t1 - t0);
	isr_exit_ns = k_cyc_to_ns_floor64(t2 - t1);

	/* 8N1: 1 start + 8 data + 1 stop = 10 bits per byte */
	theoretical_ns = (uint64_t)MEASURE_BYTES * 10ULL * 1000000000ULL / BAUDRATE;
	overhead_ns = elapsed_ns - theoretical_ns;

	LOG_INF("");
	LOG_INF("----- UART LATENCY TEST -----");

	LOG_INF("UART TX latency @ %u baud, %d bytes:", BAUDRATE, MEASURE_BYTES);
	LOG_INF("  theoretical : %u us", (uint32_t)(theoretical_ns / 1000U));
	LOG_INF("  elapsed     : %u us", (uint32_t)(elapsed_ns / 1000U));
	LOG_INF("");
	LOG_INF("  Measured ISR exit latency   : %u us", (uint32_t)(isr_exit_ns / 1000U));
	LOG_INF("  Calculated ISR exit latency : %u us", (uint32_t)(overhead_ns / 1000U));
	LOG_INF("  Time between bytes		 : %u us", (uint32_t)(elapsed_ns / 1000U / MEASURE_BYTES));
	LOG_INF("  (Problems start when ISR latency gets close to time per byte)");

	// Restore uart how it was...
	uart_callback_set(uart_dev, NULL, NULL);

	return 0;
}

// First thing to run, optionnal test to measure latencies
// You can also run zephyr/tests/benchmarks/latency_measure to get more accurate latency measurements.
#define MEASURE_LATENCY_INIT_PRIORITY 0
SYS_INIT(measure_latency_init, APPLICATION, MEASURE_LATENCY_INIT_PRIORITY);
