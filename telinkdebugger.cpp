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

static uint32_t input_buffer[BUFFER_SIZE_BITS/8];
static uint32_t output_buffer[BUFFER_SIZE_BITS/8];

static int input_buffer_bit_ptr;
static int output_buffer_bit_ptr;

static void write_raw_bit(bool bit)
{
    if (output_buffer_bit_ptr < BUFFER_SIZE_BITS)
    {
        uint32_t* p = &output_buffer[output_buffer_bit_ptr/32];
        *p |= 0x80000000 >> (output_buffer_bit_ptr & 31);
        output_buffer_bit_ptr++;
    }
}

int main()
{
    stdio_init_all();

    PIO pio = pio0;
    uint offset = pio_add_program(pio, &sws_program);
    printf("Loaded program at %d\n", offset);

    uint timer_offset = pio_add_program(pio, &timer_program);
    printf("Loaded timer at %d\n", timer_offset);

    double ticks_per_second = clock_get_hz(clk_peri);
    double debug_clock_rate_hz = 0.5e-6;

    timer_program_init(
        pio, SM_CLOCK, timer_offset, debug_clock_rate_hz * ticks_per_second);
    pio_sm_set_enabled(pio, SM_CLOCK, true);

    output_buffer_bit_ptr = 0;
    write_raw_bit(false);
    write_raw_bit(true);
    write_raw_bit(false);

    gpio_set_pulls(SWS_PIN, false, false);

    for (;;)
    {
        sws_program_init(pio, SM_DATA, offset, SWS_PIN);
        pio_sm_set_enabled(pio, SM_DATA, true);

        pio_sm_put_blocking(pio, SM_DATA, output_buffer_bit_ptr); // bits to transmit
        pio_sm_put_blocking(pio, SM_DATA, 16); // bits to receive

        uint32_t* p = &output_buffer[0];
        while (!pio_interrupt_get(pio, 1))
        {
            if (pio_sm_is_tx_fifo_empty(pio, SM_DATA))
                pio_sm_put(pio, SM_DATA, *p++);
        }

        pio_interrupt_clear(pio, SM_DATA);
        while (!pio_interrupt_get(pio, 2) ||
               !pio_sm_is_rx_fifo_empty(pio, SM_DATA))
        {
            if (!pio_sm_is_rx_fifo_empty(pio, SM_DATA))
                pio_sm_get(pio, SM_DATA);
        }

        pio_interrupt_clear(pio, 2);
    }
}
