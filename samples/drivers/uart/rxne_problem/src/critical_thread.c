/*
 * Copyright (c) 2025 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(sample, LOG_LEVEL_INF);

#define CRITICAL_THREAD_BUSY_TIME_MS  500
#define CRITICAL_THREAD_SLEEP_TIME_MS 60000

/**
 * This task is waken periodically to simulate some critical operations that preempt the main task.
 * Increase CRITICAL_THREAD_BUSY_TIME_MS to increase the odds of overflowing the main's RX ring buffer
 * and trigger the recovery mechanism.
 */
static void critical_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("");
	LOG_INF("----- critical thread started -----");
	LOG_INF("Busy %d ms every %d ms",
		CRITICAL_THREAD_BUSY_TIME_MS, CRITICAL_THREAD_SLEEP_TIME_MS);

	while (1) {
        k_msleep(CRITICAL_THREAD_SLEEP_TIME_MS);

		LOG_WRN("Critical thread: Enter busy wait");

		k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(CRITICAL_THREAD_BUSY_TIME_MS));
		while (!sys_timepoint_expired(deadline)) {
			k_busy_wait(1);
			k_yield(); // Allow the log task to run, for convenience
		}

		LOG_WRN("Critical thread: Leave busy wait");
	}
}

// Should be higher prio than rx thread, but lower than tx
#define CRITICAL_THREAD_PRIORITY      1
K_THREAD_DEFINE(critical_thread, 1024, critical_thread_fn, NULL, NULL, NULL, CRITICAL_THREAD_PRIORITY, 0, 0);
