
Overview
********

This sample demonstrates the problem I had with a constant stream of RXNE interrupts
when the async RX is disabled but the receiver isn't really disabled. 

My MCU is wayyy too slow to handle 1 interrupt per byte, that's why we need the async RX in
the first place... 

Building and Running
********************

```
# Build
west build -p -b nucleo_g070rb zephyr/samples/drivers/uart/rxne_problem/

# Build with my hotfix for the problem
west build -p -b nucleo_g070rb zephyr/samples/drivers/uart/rxne_problem/ -DCONFIG_FIX_RXNE_PROBLEM=y

# Flash
west flash
# or
STM32_Programmer_CLI --connect 'port=swd reset=HWrst' --download /home/jonathan/Documents/work/repo-ext/zephyr-fork/build/zephyr/zephyr.hex -rst

# Start the RTT server through stlink
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg   -c "adapter speed 30000"   -c "init"   -c "rtt setup 0x20000000 0x9000 {SEGGER RTT}"   -c "rtt polling_interval 1"   -c "rtt start"   -c "rtt server start 9090 0"   -c "rtt server start 9091 1"

# Open RTT console
nc localhost 9091

```

Sample Output
=============

The sample will be able to transmit, receive (by loopback) and decode packets at light speed,
but once in a while, we have more critical things to do. Every minute, the critical thread will
wake up and starve the rx thread. The uart will continue to write new data and will eventually
overflow the ring buffer. The rx thread detects the overflow and starts the recovery procedure.

The problem comes when we disable the async RX. We start receiving RXNE and ORE interrupts faster
than we can handle. You can see something is wrong because every LOG_ERR messages are barely able
to be printed byte per byte. Then, once we finally re-enable rx, everything go back to normal.

**Watch the video I commited along the sample**

.. code-block:: console

[00:05:02.033,000] <inf> sample: RX frame 58178
[00:05:02.038,000] <inf> sample: RX frame 58179
[00:05:02.043,000] <inf> sample: RX frame 58180
[00:05:02.049,000] <inf> sample: RX frame 58181
[00:05:02.054,000] <inf> sample: RX frame 58182
[00:05:02.059,000] <inf> sample: RX frame 58183
[00:05:02.064,000] <inf> sample: RX frame 58184
[00:05:02.069,000] <inf> sample: RX frame 58185
[00:05:02.075,000] <inf> sample: RX frame 58186
[00:05:02.075,000] <wrn> sample: Critical thread: Enter busy wait
[00:05:02.171,000] <wrn> sample:     RX ISR : ring buffer overflow
[00:05:02.578,000] <wrn> sample: Critical thread: Leave busy wait
[00:05:02.584,000] <wrn> sample: RX recovery triggered, disabling RX
[00:05:02.587,000] <err> sample: LOGS is red are gonna be very sluggish...
[00:05:02.590,000] <err> sample: This is because we disabled the async RX, but the receiver isn't really disabled...
[00:05:02.595,000] <err> sample: Instead of receiving 1 interrupt per 500 bytes, we receive 1 per byte, and this MCU can't handle that many interrupts
[00:05:02.600,000] <err> sample: Flushing buffers...
[00:05:02.603,000] <err> sample: Enabling RX back..............
[00:05:02.606,000] <wrn> sample: RX restored
[00:05:02.616,000] <wrn> sample: Last cycle got : 11552 valid frames, 105 skipped frames. Tx up for 95% of the time.
[00:05:02.621,000] <wrn> sample: UART got : 23309 interrupts, including 0 rxne and 5 errors
[00:05:02.626,000] <inf> sample: RX frame 58292
[00:05:02.628,000] <inf> sample: RX frame 58293
[00:05:02.631,000] <inf> sample: RX frame 58294
[00:05:02.634,000] <inf> sample: RX frame 58295
[00:05:02.637,000] <inf> sample: RX frame 58296
[00:05:02.641,000] <inf> sample: RX frame 58297
[00:05:02.646,000] <inf> sample: RX frame 58298

