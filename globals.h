#pragma once

#include "pico/util/queue.h"

extern void usb_bridge_init(void);
extern void stdio_queue_init(void);

extern queue_t rd_queue;
extern queue_t wr_queue;
