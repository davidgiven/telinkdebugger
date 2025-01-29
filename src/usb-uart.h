// SPDX-License-Identifier: MIT
// Copyright 2025 Antonio Vázquez Blanco <antoniovazquezblanco@gmail.com>

#if !defined(_USB_UART_H_)
#define _USB_UART_H_

#include <pico/util/queue.h>

extern queue_t rd_queue;
extern queue_t wr_queue;

void usb_bridge_init(void);

#endif
