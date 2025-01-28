// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2024 David Given <dg@cowlark.com>
 */

#include <stdio-queue.h>

#include <usb-uart.h>

#include <pico/stdio/driver.h>
#include <stdio.h>


static void stdio_queue_out_chars(const char* buf, int length)
{
    for (int i = 0; i < length; i++)
        queue_add_blocking(&wr_queue, &buf[i]);
}

static int stdio_queue_in_chars(char* buf, int length)
{
    int i = 0;
    while (i < length && !queue_is_empty(&rd_queue))
        queue_remove_blocking(&rd_queue, &buf[i++]);
    return i ? i : PICO_ERROR_NO_DATA;
}

static stdio_driver_t queue_driver = {.out_chars = stdio_queue_out_chars,
    .in_chars = stdio_queue_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true
#endif
};

void stdio_queue_init()
{
    stdio_set_driver_enabled(&queue_driver, true);
}
