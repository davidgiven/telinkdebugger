#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/divider.h"

#include "sws.pio.h"

#define SWS_PIN 20
#define RST_PIN 21
#define DBG_PIN 22
#define LED_PIN PICO_DEFAULT_LED_PIN

#define SM_RX 0
#define SM_TX 1

#define BUFFER_SIZE_BITS 4096

#define REG_ADDR8(n) (n)
#define REG_ADDR16(n) (n)
#define REG_ADDR32(n) (n)

#define reg_soc_id REG_ADDR16(0x7e)

#define reg_swire_data REG_ADDR8(0xb0)
#define reg_swire_ctrl1 REG_ADDR8(0xb1)
#define reg_swire_clk_div REG_ADDR8(0xb2)
#define reg_swire_id REG_ADDR8(0xb3)

#define reg_tmr_ctl REG_ADDR32(0x620)
#define FLD_TMR_WD_EN (1 << 23)

#define reg_debug_runstate REG_ADDR8(0x602)

#define BITS_PER_RECEIVED_BYTE (10 * 10)

static uint32_t input_buffer[BUFFER_SIZE_BITS / 8];
static uint32_t output_buffer[BUFFER_SIZE_BITS / 8];

static int input_buffer_bit_ptr;
static int output_buffer_bit_ptr;

static int sws_tx_program_offset;
static int sws_rx_program_offset;

static bool is_connected;

static void write_nine_bit_byte(uint16_t byte)
{
    pio_gpio_init(pio0, SWS_PIN);
    pio_interrupt_clear(pio0, 0);

    pio_sm_put(pio0, SM_TX, byte);

    while (!pio_interrupt_get(pio0, 0))
        ;
    pio_interrupt_clear(pio0, 0);
}

static void write_cmd_byte(uint8_t byte)
{
    write_nine_bit_byte(0x100 | byte);
}

static void write_data_byte(uint8_t byte)
{
    write_nine_bit_byte(0x000 | byte);
}

static void write_data_word(uint16_t word)
{
    write_data_byte(word >> 8);
    write_data_byte(word & 0xff);
}

static uint8_t read_byte()
{
    pio_gpio_init(pio1, SWS_PIN);
    pio_gpio_init(pio1, DBG_PIN);
    pio_sm_clear_fifos(pio1, SM_RX);
    pio_sm_exec_wait_blocking(pio1, SM_RX, sws_rx_program_offset); // JMP offset

    return pio_sm_get_blocking(pio1, SM_RX);
}

static uint8_t read_first_debug_byte(uint16_t address)
{
    write_cmd_byte(0x5a);
    write_data_word(address);
    write_data_byte(0x80);

    return read_byte();
}

static uint8_t read_next_debug_byte()
{
    return read_byte();
}

static void finish_reading_debug_bytes()
{
    write_cmd_byte(0xff);
}

static uint8_t read_single_debug_byte(uint16_t address)
{
    uint8_t value = read_first_debug_byte(address);
    finish_reading_debug_bytes();
    return value;
}

static uint16_t read_single_debug_word(uint16_t address)
{
    uint8_t v1 = read_first_debug_byte(address);
    uint8_t v2 = read_next_debug_byte();
    finish_reading_debug_bytes();
    return v1 | (v2 << 8);
}

static void write_first_debug_byte(uint16_t address, uint8_t value)
{
    write_cmd_byte(0x5a);
    write_data_word(address);
    write_data_byte(0x00);
    write_data_byte(value);
}

static void write_next_debug_byte(uint8_t value)
{
    write_data_byte(value);
}

static void finish_writing_debug_bytes()
{
    write_cmd_byte(0xff);
}

static void write_single_debug_byte(uint16_t address, uint8_t value)
{
    write_first_debug_byte(address, value);
    finish_writing_debug_bytes();
}

static void write_single_debug_word(uint16_t address, uint16_t value)
{
    write_first_debug_byte(address, value);
    write_next_debug_byte(value >> 8);
    finish_writing_debug_bytes();
}

static void write_single_debug_quad(uint16_t address, uint32_t value)
{
    write_first_debug_byte(address, value);
    write_next_debug_byte(value >> 8);
    write_next_debug_byte(value >> 16);
    write_next_debug_byte(value >> 24);
    finish_writing_debug_bytes();
}

static void halt_target()
{
    write_single_debug_byte(reg_debug_runstate, 0x05);
}

static void set_target_clock_speed(uint8_t speed)
{
    write_single_debug_byte(reg_swire_clk_div, speed);
}

static void banner()
{
    printf("# Telink debugger bridge\n");
    printf("# (help placeholder text here)\n");
}

static void init_cmd()
{
    printf("# init\n");

    gpio_put(RST_PIN, false);
    sleep_ms(20);
    gpio_put(RST_PIN, true);
    sleep_ms(20);

    halt_target();

    uint16_t socid = read_single_debug_word(reg_soc_id);
    if (socid == 0x5316)
    {
        printf("S\n");
        is_connected = true;

        /* Disable the watchdog timer. */

        write_single_debug_quad(reg_tmr_ctl, 0);
        return;
    }

    printf("E\n# init failed\n");
}

static uint8_t read_hex_byte()
{
    char buffer[3];
    buffer[0] = getchar();
    buffer[1] = getchar();
    buffer[2] = 0;
    return strtoul(buffer, nullptr, 16);
}

static uint16_t read_hex_word()
{
    uint8_t hi = read_hex_byte();
    uint8_t lo = read_hex_byte();
    return lo | (hi << 8);
}

void set_tx_clock(double clock_hz)
{
    sws_tx_program_init(pio0, SM_TX, sws_tx_program_offset, SWS_PIN, clock_hz);
    pio_sm_set_enabled(pio0, SM_TX, true);
}

int main()
{
    stdio_init_all();

    gpio_init(RST_PIN);
    gpio_set_dir(RST_PIN, true);
    gpio_put(RST_PIN, false);

    gpio_set_pulls(RST_PIN, false, false);
    gpio_set_pulls(SWS_PIN, true, false);
    gpio_set_pulls(DBG_PIN, false, false);

    sws_tx_program_offset = pio_add_program(pio0, &sws_tx_program);
    set_tx_clock(10.0e6);

    sws_rx_program_offset = pio_add_program(pio1, &sws_rx_program);
    sws_rx_program_init(pio1, SM_RX, sws_rx_program_offset, SWS_PIN);
    pio_sm_set_enabled(pio1, SM_RX, true);

    for (;;)
    {
        static bool was_usb_connected = false;
        bool is_usb_connected = stdio_usb_connected();
        if (is_usb_connected && !was_usb_connected)
            banner();
        was_usb_connected = is_usb_connected;

        if (is_usb_connected)
        {
            int c = getchar();
            switch (c)
            {
                case 'i':
                    init_cmd();
                    break;

                case 'r':
                {
                    int i = getchar() == '1';
                    printf("# reset <- %d\n", i);
                    gpio_put(RST_PIN, i);
                    if (i == 0)
                        is_connected = false;
                    printf("S\n");
                    break;
                }

                case 's':
                {
                    uint16_t socid = read_single_debug_word(reg_soc_id);
                    printf("# socid = %04x\nS\n", socid);
                    break;
                }

                case 'R':
                {
                    uint16_t address = read_hex_word();
                    uint16_t count = read_hex_word();

                    if (count)
                    {
                        uint8_t b = read_first_debug_byte(address);
                        printf("%02x", b);
                        count--;

                        while (count--)
                        {
                            b = read_next_debug_byte();
                            printf("%02x", b);
                        }

                        finish_reading_debug_bytes();
                        printf("\n");
                    }
                    printf("S\n");
                    break;
                }

                case 'W':
                {
                    uint16_t address = read_hex_word();
                    uint16_t count = read_hex_word();

                    if (count)
                    {
                        uint8_t b = read_hex_byte();
                        write_first_debug_byte(address, b);
                        count--;

                        while (count--)
                        {
                            b = read_hex_byte();
                            write_next_debug_byte(b);
                        }

                        finish_writing_debug_bytes();
                    }

                    printf("S\n");
                    break;
                }

                default:
                    printf("?\n");
                    printf("# unknown command\n");
            }
        }
    }
}
