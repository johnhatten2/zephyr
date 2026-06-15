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

#include "uart_cb.h"

#define LOREM_IPSUM "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod " \
	"tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim " \
	"veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea " \
	"commodo consequat. Duis aute irure dolor in reprehenderit in voluptate " \
	"velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat " \
	"cupidatat non proident, sunt in culpa qui officia deserunt mollit anim " \
	"id est laborum."

#define TX_PAYLOAD_MIN     	(sizeof(lorem_ipsum) - 1) 	// 445
#define TX_PAYLOAD_MAX     	(sizeof(lorem_ipsum) - 1) 	// 445

#define TX_HEADER_SIZE     	8U
#define TX_BUFFER_SIZE  	(TX_HEADER_SIZE + TX_PAYLOAD_MAX) // 453

#define TX_BAUDRATE        DT_PROP(DT_NODELABEL(usart3), current_speed)
#define TX_MEAN_FRAME_BYTES (TX_HEADER_SIZE + (TX_PAYLOAD_MIN + TX_PAYLOAD_MAX) / 2U)
#define TX_FRAME_TIME_US   (TX_MEAN_FRAME_BYTES * 10U * 1000000U / TX_BAUDRATE)

static const char lorem_ipsum[] = LOREM_IPSUM;
static char tx_buffer[TX_BUFFER_SIZE];
static size_t tx_frame_len;

static uint32_t tx_counter;
static uint32_t tx_last_cycle;
uint32_t tx_elapsed_us;
uint32_t tx_estimated_us;

void on_tx_done(void)
{
	uint32_t now = k_cycle_get_32();
	uint32_t frame_counter = tx_counter++;

	tx_elapsed_us = k_cyc_to_us_floor32(now - tx_last_cycle);
	tx_last_cycle = now;

	tx_buffer[4] = (uint8_t)(frame_counter & 0xFF);
	tx_buffer[5] = (uint8_t)((frame_counter >> 8) & 0xFF);
	tx_buffer[6] = (uint8_t)((frame_counter >> 16) & 0xFF);
	tx_buffer[7] = (uint8_t)((frame_counter >> 24) & 0xFF);

	uart_tx(uart_dev, tx_buffer, tx_frame_len, SYS_FOREVER_US);
}

static int tx_init(void)
{
	int payload_len = sizeof(lorem_ipsum) - 1;

	memcpy(&tx_buffer[TX_HEADER_SIZE], lorem_ipsum, payload_len);

	tx_buffer[0] = ':';
	tx_buffer[1] = ':';
	tx_buffer[2] = (uint8_t)(payload_len & 0xFF);
	tx_buffer[3] = (uint8_t)((payload_len >> 8) & 0xFF);
	tx_buffer[4] = 0;
	tx_buffer[5] = 0;
	tx_buffer[6] = 0;
	tx_buffer[7] = 0;

	tx_frame_len = payload_len + TX_HEADER_SIZE;
	tx_estimated_us = (uint32_t)((uint64_t)tx_frame_len * 10ULL * 1000000ULL / TX_BAUDRATE);

	LOG_INF("");
	LOG_INF("----- TX INIT -----");
	LOG_INF("TX: frame %u B at %u baud, estimated %u us",
		(uint32_t)tx_frame_len, (uint32_t)TX_BAUDRATE, tx_estimated_us);

	tx_last_cycle = k_cycle_get_32();
	on_tx_done();
	return 0;
}

#define TX_INIT_PRIORITY 2
SYS_INIT(tx_init, APPLICATION, TX_INIT_PRIORITY);
