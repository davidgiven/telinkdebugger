// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 David Given <dg@cowlark.com>
 * Copyright 2021 Álvaro Fernández Rojas <noltari@gmail.com>
 */

#include <hardware/structs/sio.h>
#include <hardware/uart.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <string.h>
#include <tusb.h>

#if !defined(MIN)
#define MIN(a, b) ((a > b) ? b : a)
#endif /* MIN */

#define LED_PIN 25

#define BUFFER_SIZE 2560

#define DEF_BIT_RATE 115200
#define DEF_STOP_BITS 1
#define DEF_PARITY 0
#define DEF_DATA_BITS 8

#define IF_CONTROL 0
#define IF_DATA 1

typedef struct
{
    uart_inst_t* const inst;
    uint8_t tx_pin;
    uint8_t rx_pin;
} uart_id_t;

typedef struct
{
    cdc_line_coding_t usb_lc;
    cdc_line_coding_t uart_lc;
    uint8_t uart_buffer[BUFFER_SIZE];
    uint32_t uart_pos;
    uint8_t usb_buffer[BUFFER_SIZE];
    uint32_t usb_pos;
} uart_data_t;

static void uart_read_bytes(uint8_t itf);
static void uart_write_bytes(uint8_t itf);
static void fifo_read_bytes(uint8_t itf);
static void fifo_write_bytes(uint8_t itf);

static const uart_id_t UART_ID[CFG_TUD_CDC] = {
    [IF_CONTROL] =
        {
                      .inst = nullptr,
                      },
    [IF_DATA] = {
                      .inst = uart0,
                      .tx_pin = 0,
                      .rx_pin = 1,
                      }
};

static uart_data_t UART_DATA[CFG_TUD_CDC];

queue_t rd_queue;
queue_t wr_queue;

static uint32_t databits_usb2uart(uint8_t data_bits)
{
    switch (data_bits)
    {
        case 5:
            return 5;
        case 6:
            return 6;
        case 7:
            return 7;
        default:
            return 8;
    }
}

static uart_parity_t parity_usb2uart(uint8_t usb_parity)
{
    switch (usb_parity)
    {
        case 1:
            return UART_PARITY_ODD;
        case 2:
            return UART_PARITY_EVEN;
        default:
            return UART_PARITY_NONE;
    }
}

static uint32_t stopbits_usb2uart(uint8_t stop_bits)
{
    switch (stop_bits)
    {
        case 2:
            return 2;
        default:
            return 1;
    }
}

static void update_uart_cfg(uint8_t itf)
{
    const uart_id_t* ui = &UART_ID[itf];
    uart_data_t* ud = &UART_DATA[itf];

    if (ud->usb_lc.bit_rate != ud->uart_lc.bit_rate)
    {
        uart_set_baudrate(ui->inst, ud->usb_lc.bit_rate);
        ud->uart_lc.bit_rate = ud->usb_lc.bit_rate;
    }

    if ((ud->usb_lc.stop_bits != ud->uart_lc.stop_bits) ||
        (ud->usb_lc.parity != ud->uart_lc.parity) ||
        (ud->usb_lc.data_bits != ud->uart_lc.data_bits))
    {
        uart_set_format(ui->inst,
            databits_usb2uart(ud->usb_lc.data_bits),
            stopbits_usb2uart(ud->usb_lc.stop_bits),
            parity_usb2uart(ud->usb_lc.parity));
        ud->uart_lc.data_bits = ud->usb_lc.data_bits;
        ud->uart_lc.parity = ud->usb_lc.parity;
        ud->uart_lc.stop_bits = ud->usb_lc.stop_bits;
    }
}

static void usb_read_bytes(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];
    uint32_t len = tud_cdc_n_available(itf);

    if (len)
    {
        len = MIN(len, BUFFER_SIZE - ud->usb_pos);
        if (len)
        {
            uint32_t count;

            count = tud_cdc_n_read(itf, &ud->usb_buffer[ud->usb_pos], len);
            ud->usb_pos += count;
        }
    }
}

static void usb_write_bytes(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];

    if (ud->uart_pos)
    {
        uint32_t count;

        count = tud_cdc_n_write(itf, ud->uart_buffer, ud->uart_pos);
        if (count < ud->uart_pos)
            memmove(
                ud->uart_buffer, &ud->uart_buffer[count], ud->uart_pos - count);
        ud->uart_pos -= count;

        if (count)
            tud_cdc_n_write_flush(itf);
    }
}

static void usb_cdc_process(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];

    tud_cdc_n_get_line_coding(itf, &ud->usb_lc);

    usb_read_bytes(itf);
    usb_write_bytes(itf);
}

static void core1_entry(void)
{
    tusb_init();

    while (1)
    {
        int itf;
        int con = 0;

        tud_task();

        for (itf = 0; itf < CFG_TUD_CDC; itf++)
        {
            /* Send/receive USB */

            if (tud_cdc_n_connected(itf))
            {
                con = 1;
                usb_cdc_process(itf);
            }

            /* Send/receive UARTs */

            update_uart_cfg(IF_DATA);
            uart_read_bytes(IF_DATA);
            uart_write_bytes(IF_DATA);

            fifo_read_bytes(IF_CONTROL);
            fifo_write_bytes(IF_CONTROL);
        }

        gpio_put(LED_PIN, con);
    }
}

static void uart_read_bytes(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];
    const uart_id_t* ui = &UART_ID[itf];

    while (uart_is_readable(ui->inst) && (ud->uart_pos < BUFFER_SIZE))
    {
        ud->uart_buffer[ud->uart_pos] = uart_getc(ui->inst);
        ud->uart_pos++;
    }
}

static void fifo_read_bytes(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];
    const uart_id_t* ui = &UART_ID[itf];

    while (ud->uart_pos < BUFFER_SIZE)
    {
        if (!queue_try_remove(&wr_queue, &ud->uart_buffer[ud->uart_pos]))
            break;

        ud->uart_pos++;
    }
}

static void uart_write_bytes(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];

    if (ud->usb_pos)
    {
        const uart_id_t* ui = &UART_ID[itf];
        uint32_t count = 0;

        while (uart_is_writable(ui->inst) && count < ud->usb_pos)
        {
            uart_putc_raw(ui->inst, ud->usb_buffer[count]);
            count++;
        }

        if (count < ud->usb_pos)
            memmove(
                ud->usb_buffer, &ud->usb_buffer[count], ud->usb_pos - count);
        ud->usb_pos -= count;
    }
}

static void fifo_write_bytes(uint8_t itf)
{
    uart_data_t* ud = &UART_DATA[itf];

    if (ud->usb_pos)
    {
        uint32_t count = 0;

        while (count < ud->usb_pos)
        {

            if (!queue_try_add(&rd_queue, &ud->usb_buffer[count]))
                break;
            count++;
        }

        if (count < ud->usb_pos)
            memmove(
                ud->usb_buffer, &ud->usb_buffer[count], ud->usb_pos - count);
        ud->usb_pos -= count;
    }
}

static void init_uart_data(uint8_t itf)
{
    const uart_id_t* ui = &UART_ID[itf];
    uart_data_t* ud = &UART_DATA[itf];

    /* USB CDC LC */
    ud->usb_lc.bit_rate = DEF_BIT_RATE;
    ud->usb_lc.data_bits = DEF_DATA_BITS;
    ud->usb_lc.parity = DEF_PARITY;
    ud->usb_lc.stop_bits = DEF_STOP_BITS;

    /* UART LC */
    ud->uart_lc.bit_rate = DEF_BIT_RATE;
    ud->uart_lc.data_bits = DEF_DATA_BITS;
    ud->uart_lc.parity = DEF_PARITY;
    ud->uart_lc.stop_bits = DEF_STOP_BITS;

    /* Buffer */
    ud->uart_pos = 0;
    ud->usb_pos = 0;

    /* UART start */
    if (ui->inst)
    {
        /* Pinmux */
        gpio_set_function(ui->tx_pin, GPIO_FUNC_UART);
        gpio_set_function(ui->rx_pin, GPIO_FUNC_UART);

        uart_init(ui->inst, ud->usb_lc.bit_rate);
        uart_set_hw_flow(ui->inst, false, false);
        uart_set_format(ui->inst,
            databits_usb2uart(ud->usb_lc.data_bits),
            stopbits_usb2uart(ud->usb_lc.stop_bits),
            parity_usb2uart(ud->usb_lc.parity));
        uart_set_fifo_enabled(ui->inst, true);
    }
}

void usb_bridge_init(void)
{
    usbd_serial_init();

    for (int itf = 0; itf < CFG_TUD_CDC; itf++)
        init_uart_data(itf);

    queue_init(&rd_queue, 1, 256);
    queue_init(&wr_queue, 1, 256);
    multicore_launch_core1(core1_entry);
}
