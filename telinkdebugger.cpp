#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/divider.h"

#include "sws.pio.h"

#define SWS_PIN 20
#define RST_PIN 21
#define LED_PIN PICO_DEFAULT_LED_PIN

#define SM_CLOCK 0
#define SM_DATA 1

#define BUFFER_SIZE_BITS 4096

static uint32_t input_buffer[BUFFER_SIZE_BITS / 8];
static uint32_t output_buffer[BUFFER_SIZE_BITS / 8];

static int input_buffer_bit_ptr;
static int output_buffer_bit_ptr;

static int sws_program_offset;

static void write_raw_bit(bool bit)
{
    if (output_buffer_bit_ptr < BUFFER_SIZE_BITS)
    {
        uint32_t* p = &output_buffer[output_buffer_bit_ptr / 32];
        uint32_t mask = 0x80000000 >> (output_buffer_bit_ptr & 31);
        if (bit)
            *p |= mask;
        else
            *p &= ~mask;
        output_buffer_bit_ptr++;
    }
}

static void write_raw_bits(bool bit, int count)
{
    while (count--)
        write_raw_bit(bit);
}

static void write_baked_bit(bool bit)
{
    if (bit)
    {
        write_raw_bits(0, 4);
        write_raw_bit(1);
    }
    else
    {
        write_raw_bit(0);
        write_raw_bits(1, 4);
    }
}

static void write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++)
    {
        write_baked_bit(byte & 0x80);
        byte <<= 1;
    }
    write_baked_bit(0);
}

static void write_cmd_byte(uint8_t byte)
{
    write_baked_bit(1);
    write_byte(byte);
}

static void write_data_byte(uint8_t byte)
{
    write_baked_bit(0);
    write_byte(byte);
}

static void write_data_word(uint16_t word)
{
    write_data_byte(word >> 8);
    write_data_byte(word & 0xff);
}

static void send_receive(int readcount)
{
    sws_program_init(pio0, SM_DATA, sws_program_offset, SWS_PIN);
    pio_sm_set_enabled(pio0, SM_DATA, true);

    pio_sm_put_blocking(
        pio0, SM_DATA, output_buffer_bit_ptr);     // bits to transmit
    pio_sm_put_blocking(pio0, SM_DATA, readcount); // bits to receive

    uint32_t* p = &output_buffer[0];
    while (!pio_interrupt_get(pio0, 1))
    {
        if (pio_sm_is_tx_fifo_empty(pio0, SM_DATA))
            pio_sm_put(pio0, SM_DATA, *p++);
    }

    pio_interrupt_clear(pio0, 1);
    while (
        !pio_interrupt_get(pio0, 2) || !pio_sm_is_rx_fifo_empty(pio0, SM_DATA))
    {
        if (!pio_sm_is_rx_fifo_empty(pio0, SM_DATA))
            pio_sm_get(pio0, SM_DATA);
    }

    pio_interrupt_clear(pio0, 2);
}

static void halt_target()
{
    output_buffer_bit_ptr = 0;
    write_cmd_byte(0x5a);
    write_data_word(0x0602);
    write_data_byte(0x00);
    write_data_byte(0x50);
    write_cmd_byte(0xff);

    send_receive(0);
}

static void set_target_clock_speed(uint8_t speed)
{
    output_buffer_bit_ptr = 0;
    write_cmd_byte(0x5a);
    write_data_word(0x00b2);
    write_data_byte(0x00);
    write_data_byte(speed);
    write_cmd_byte(0xff);

    send_receive(0);
}

int main()
{
    stdio_init_all();

    gpio_init(RST_PIN);
    gpio_set_dir(RST_PIN, true);
    gpio_put(RST_PIN, true);

    gpio_set_pulls(SWS_PIN, false, false);

    sws_program_offset = pio_add_program(pio0, &sws_program);
    printf("Loaded program at %d\n", sws_program_offset);

    uint timer_offset = pio_add_program(pio0, &timer_program);
    printf("Loaded timer at %d\n", timer_offset);

    double ticks_per_second = clock_get_hz(clk_peri);
    double debug_clock_rate_hz = 0.500e-6;

    timer_program_init(
        pio0, SM_CLOCK, timer_offset, debug_clock_rate_hz * ticks_per_second);
    pio_sm_set_enabled(pio0, SM_CLOCK, true);

    halt_target();

    for (;;)
    {
        set_target_clock_speed(10);

        output_buffer_bit_ptr = 0;
        write_cmd_byte(0x5a);
        write_data_word(0x007e);
        write_data_byte(0x80);
        write_raw_bits(0, 4);
        send_receive(10 * 5);

        output_buffer_bit_ptr = 0;
        write_raw_bits(0, 4);
        send_receive(10 * 5);

        output_buffer_bit_ptr = 0;
        write_cmd_byte(0xff);
        send_receive(0);
    }
}
