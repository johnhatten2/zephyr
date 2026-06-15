/*
 * Copyright (c) 2025 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample, LOG_LEVEL_INF);

#include "uart_cb.h"

#define RX_RING_BUF_SIZE 8192
RING_BUF_DECLARE(rx_ring_buf, RX_RING_BUF_SIZE);

enum rx_decode_state {
	RX_DECODE_HEADER_0,
	RX_DECODE_HEADER_1,
	RX_DECODE_LEN_0,
	RX_DECODE_LEN_1,
	RX_DECODE_COUNTER_0,
	RX_DECODE_COUNTER_1,
	RX_DECODE_COUNTER_2,
	RX_DECODE_COUNTER_3,
	RX_DECODE_PAYLOAD,
};

static volatile bool rx_recovery_pending;
static enum rx_decode_state rx_state;
static uint16_t rx_expected_len;
static uint16_t rx_payload_pos;
static uint32_t rx_frame_counter;

static uint32_t rx_valid_frames;
static uint32_t rx_last_frame_id;
static uint32_t rx_frames_before_recovery;
static bool rx_report_pending;

static void decode_frame_reset(void);

void on_rx_rdy(const struct uart_event *evt)
{
	uint32_t written;

	written = ring_buf_put(&rx_ring_buf,
			      evt->data.rx.buf + evt->data.rx.offset,
			      evt->data.rx.len);
	if (written < evt->data.rx.len && rx_recovery_pending == false) {
		LOG_WRN("    RX ISR : ring buffer overflow");
		rx_recovery_pending = true;
	}
}

static int uart_rx_recover(void)
{
	int rc;

	LOG_WRN("RX recovery triggered, disabling RX");

	rc = uart_rx_disable(uart_dev);
	if (rc != 0) {
		LOG_ERR("RX disable failed (%d)", rc);
	}

	rc = k_sem_take(&rx_disabled_sem, K_MSEC(100));
	if (rc != 0) {
		LOG_ERR("timeout waiting RX disabled");
	}

	/**
	 * You would expect this to be kind of a "quiet" state (data always incoming, 
	 * but receiver not active), but actually, uart_rx_disable() activates the RXNE and ERROR
	 * interrupts, which causes way more ISRs than the MCU can handle. We explicitely need
	 * the async API because we would not be able to manage 1 interrupt per byte.
	 */

	LOG_ERR("LOGS is red are gonna be very sluggish...");
	LOG_ERR("This is because we disabled the async RX, but the receiver isn't really disabled...");
	LOG_ERR("Instead of receiving 1 interrupt per 500 bytes, we receive 1 per byte, and this MCU can't handle that many interrupts");
	LOG_ERR("Flushing buffers...");

	rx_recovery_pending = false;
	rx_frames_before_recovery = rx_valid_frames;
	rx_valid_frames = 0;
	rx_report_pending = true;
	decode_frame_reset();

	ring_buf_reset(&rx_ring_buf);

	LOG_ERR("Enabling RX back..............");

	rc = uart_rx_enable(uart_dev, rx_ring_buf.buffer, RX_RING_BUF_SIZE, 0);
	if (rc != 0) {
		LOG_ERR("RX re-enable failed (%d)", rc);
	} else {
		LOG_WRN("RX restored");
	}

	return rc;
}

static void decode_frame_reset(void)
{
	rx_expected_len = 0;
	rx_payload_pos = 0;
	rx_frame_counter = 0;
	rx_state = RX_DECODE_HEADER_0;
}

extern int uart3_isr, uart3_rxne, uart3_error;
extern uint32_t tx_elapsed_us;
extern uint32_t tx_estimated_us;
static void decode_frame(uint8_t byte)
{
	switch (rx_state) {
	case RX_DECODE_HEADER_0:
		if (byte == ':') {
			rx_state = RX_DECODE_HEADER_1;
		}
		break;

	case RX_DECODE_HEADER_1:
		rx_state = (byte == ':') ? RX_DECODE_LEN_0 : RX_DECODE_HEADER_0;
		break;

	case RX_DECODE_LEN_0:
		rx_expected_len = byte;
		rx_payload_pos = 0;
		rx_frame_counter = 0;
		rx_state = RX_DECODE_LEN_1;
		break;

	case RX_DECODE_LEN_1:
		rx_expected_len |= ((uint16_t)byte << 8);

		if (rx_expected_len == 0U) {
			LOG_WRN("Received empty frame");
			decode_frame_reset();
			break;
		}

		rx_state = RX_DECODE_COUNTER_0;
		break;

	case RX_DECODE_COUNTER_0:
		rx_frame_counter = byte;
		rx_state = RX_DECODE_COUNTER_1;
		break;

	case RX_DECODE_COUNTER_1:
		rx_frame_counter |= ((uint32_t)byte << 8);
		rx_state = RX_DECODE_COUNTER_2;
		break;

	case RX_DECODE_COUNTER_2:
		rx_frame_counter |= ((uint32_t)byte << 16);
		rx_state = RX_DECODE_COUNTER_3;
		break;

	case RX_DECODE_COUNTER_3:
		rx_frame_counter |= ((uint32_t)byte << 24);
		rx_state = RX_DECODE_PAYLOAD;
		break;

	case RX_DECODE_PAYLOAD:
		rx_payload_pos++;

		while (rx_payload_pos < rx_expected_len) {
			uint8_t *payload;
			uint16_t remaining = rx_expected_len - rx_payload_pos;
			uint32_t claimed = ring_buf_get_claim(&rx_ring_buf, &payload, remaining);

			if (claimed == 0U) {
				break;
			}

			(void)ring_buf_get_finish(&rx_ring_buf, claimed);
			rx_payload_pos += (uint16_t)claimed;
		}

		if (rx_payload_pos >= rx_expected_len) {
			if (rx_report_pending) {
				uint32_t skipped = rx_frame_counter - rx_last_frame_id - 1;
				LOG_WRN("Last cycle got : %u valid frames, %u skipped frames. Tx up for %d%% of the time.",
					rx_frames_before_recovery, skipped,
					tx_elapsed_us ? tx_estimated_us * 100U / tx_elapsed_us : 0U);
				LOG_WRN("UART got : %d interrupts, including %d rxne and %d errors",
					(int)uart3_isr, (int)uart3_rxne, (int)uart3_error);

				uart3_isr = 0;
				uart3_rxne = 0;
				uart3_error = 0;
				rx_report_pending = false;
			}

			rx_valid_frames++;
			rx_last_frame_id = rx_frame_counter;

			/*if (rx_valid_frames % 10 == 0) {
				LOG_INF("RX frame %d to %d",
					(int)rx_frame_counter - 9, (int)rx_frame_counter);
			}*/
			LOG_INF("RX frame %d", rx_frame_counter);

			decode_frame_reset();
		}
		break;

	default:
		decode_frame_reset();
		break;
	}
}

static void rx_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("");
	LOG_INF("----- rx thread started -----");

	/* Enable RX once at startup */
	if (uart_rx_enable(uart_dev, rx_ring_buf.buffer, RX_RING_BUF_SIZE, 0) != 0) {
		LOG_ERR("RX enable failed");
		return;
	}

	while (1) {
		uint8_t byte;

		k_sem_take(&rx_data_sem, K_FOREVER);

		while (!rx_recovery_pending && ring_buf_get(&rx_ring_buf, &byte, 1) == 1) {
			decode_frame(byte);
		}

		if (rx_recovery_pending) {
			(void)uart_rx_recover();
		}
	}
}

#define RX_THREAD_PRIORITY 2
K_THREAD_DEFINE(rx_thread, 2048, rx_thread_fn, NULL, NULL, NULL, RX_THREAD_PRIORITY, 0, 0);
