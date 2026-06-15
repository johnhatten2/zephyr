/*
 * Copyright (c) 2025 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>

/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

/* Maximum size of our TX/RX packets */
#define MAX_TX_LEN 2048
#define RX_CHUNK_LEN 2048
#define TX_PERIOD_MS 1000

/* Buffer pool for our TX payloads */
NET_BUF_POOL_DEFINE(tx_pool, 2, MAX_TX_LEN, 0, NULL);

struct net_buf *tx_pending_buffer;
uint8_t async_rx_buffer[2][RX_CHUNK_LEN];
volatile uint8_t async_rx_buffer_idx;
struct k_work tx_work;
struct k_sem sem_array[3];
volatile bool rx_disable_wait;

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

LOG_MODULE_REGISTER(sample, LOG_LEVEL_INF);

static void tx_work_handler(struct k_work *work)
{
	static uint32_t tx_counter;
	struct net_buf *tx_buf;
	int tx_len;
	int rc;

	ARG_UNUSED(work);

	tx_buf = net_buf_alloc(&tx_pool, K_NO_WAIT);
	if (tx_buf == NULL) {
		LOG_WRN("No TX buffer available");
		return;
	}

	tx_len = snprintk(tx_buf->data, net_buf_tailroom(tx_buf),
			  "Timer TX frame: %u\r\n", tx_counter++);
	net_buf_add(tx_buf, tx_len);

	rc = uart_tx(uart_dev, tx_buf->data, tx_buf->len, SYS_FOREVER_US);
	if (rc == 0) {
		tx_pending_buffer = tx_buf;
	} else {
		LOG_WRN("Timer TX busy/error (%d), dropping frame", rc);
		net_buf_unref(tx_buf);
	}
}

static void tx_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_work_submit(&tx_work);
}

K_TIMER_DEFINE(tx_timer, tx_timer_handler, NULL);

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	uint8_t buf_idx;
	int rc;

	LOG_DBG("EVENT: %d", evt->type);

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX complete %p", tx_pending_buffer);

		/* Free TX buffer */
		if (tx_pending_buffer != NULL) {
			net_buf_unref(tx_pending_buffer);
			tx_pending_buffer = NULL;
		}
		break;
	case UART_RX_BUF_REQUEST:
		/* If current buffer has not been processed, signal overflow */
		if (k_sem_count_get(&sem_array[async_rx_buffer_idx]) != 0) {
			LOG_WRN("Buffer %d not processed yet", async_rx_buffer_idx);
			k_sem_give(&sem_array[2]);
		}

		/* Return the next buffer index */
		LOG_DBG("Providing buffer index %d", async_rx_buffer_idx);
		rc = uart_rx_buf_rsp(dev, async_rx_buffer[async_rx_buffer_idx],
				     sizeof(async_rx_buffer[0]));
		__ASSERT_NO_MSG(rc == 0);
		async_rx_buffer_idx = async_rx_buffer_idx ? 0 : 1;
		break;
	case UART_RX_BUF_RELEASED:
		break;
	case UART_RX_DISABLED:
		k_sem_give(&sem_array[2]);
		break;
	case UART_RX_RDY:
		buf_idx = (evt->data.rx.buf == async_rx_buffer[0]) ? 0 : 1;
		LOG_HEXDUMP_INF(evt->data.rx.buf + evt->data.rx.offset,
				evt->data.rx.len, "RX_RDY");
		k_sem_give(&sem_array[buf_idx]);
		k_sem_give(&sem_array[buf_idx]);
		break;
	default:
		LOG_WRN("Unhandled event %d", evt->type);
	}
}

int main(void)
{
	int rc;

	/* Register the async interrupt handler */
	uart_callback_set(uart_dev, uart_callback, (void *)uart_dev);
	k_work_init(&tx_work, tx_work_handler);
	k_sem_init(&sem_array[0], 0, 2);
	k_sem_init(&sem_array[1], 0, 2);
	k_sem_init(&sem_array[2], 0, 2);

	/* Enable RX once at startup */
	async_rx_buffer_idx = 1;
	rc = uart_rx_enable(uart_dev, async_rx_buffer[0], RX_CHUNK_LEN, 100);
	if (rc != 0) {
		LOG_ERR("RX enable failed (%d)", rc);
		return 0;
	}

	/* Periodic TX source */
	k_timer_start(&tx_timer, K_SECONDS(1), K_SECONDS(1));

	while (1) {
		/* Error semaphore: overflow or RX disable completion while recovering */
		if (k_sem_take(&sem_array[2], K_NO_WAIT) == 0) {
			LOG_ERR("uart error encountered");

			rc = uart_rx_disable(uart_dev);
			if (rc != 0) {
				LOG_ERR("RX disable failed (%d)", rc);
			}

			if (k_sem_take(&sem_array[2], K_MSEC(100)) != 0) {
				LOG_ERR("timeout waiting RX disabled");
			}

			// drain the semaphores
			while (k_sem_take(&sem_array[0], K_NO_WAIT) == 0) {
			}
			while (k_sem_take(&sem_array[1], K_NO_WAIT) == 0) {
			}

			async_rx_buffer_idx = 1;
			rc = uart_rx_enable(uart_dev, async_rx_buffer[0], RX_CHUNK_LEN, 100);
			if (rc != 0) {
				LOG_ERR("RX re-enable failed (%d)", rc);
			} else {
				LOG_ERR("uart restored");
			}
		}

		if (k_sem_take(&sem_array[0], K_NO_WAIT) == 0) {
			LOG_HEXDUMP_INF(async_rx_buffer[0], RX_CHUNK_LEN, "Buffer 0 data");
			(void)k_sem_take(&sem_array[0], K_NO_WAIT);
		}

		if (k_sem_take(&sem_array[1], K_NO_WAIT) == 0) {
			LOG_HEXDUMP_INF(async_rx_buffer[1], RX_CHUNK_LEN, "Buffer 1 data");
			(void)k_sem_take(&sem_array[1], K_NO_WAIT);
		}

		k_sleep(K_MSEC(1));
	}
}
