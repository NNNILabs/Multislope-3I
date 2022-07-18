#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "pico/bootrom.h"
#include "ms.pio.h"

#define MAINS_FREQ 50                   // Hz

const uint8_t MCLK = 18;                // SPI Clock Pin
const uint8_t M_EN = 16;

const uint8_t MUX_A0 = 22;
const uint8_t MUX_A1 = 26;
const uint8_t MUX_A2 = 27;

const uint8_t PWMA = 3;                // need to be next to each other
const uint8_t PWMB = 4;                // need to be next to each other
const uint8_t MEAS = 5;
const uint8_t COMP = 6;

const uint8_t RESGP = 28;
const uint8_t RESADC = 2;

const uint64_t mainsPeriodus = 1000000/MAINS_FREQ;

#if MAINS_FREQ == 50
    const uint32_t MScyclesPerPLC = 6000;
#elif MAINS_FREQ == 60
    const uint32_t MScyclesPerPLC = 5000;
#endif

// MCP3202 ADC
#define CS 13
#define CLK 10
#define MOSI 11
#define MISO 12

bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i);
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return button_state;
}


uint32_t get_counts(PIO pio, uint sm , uint32_t countNum){
    uint32_t counts = 0;
    pio_sm_put_blocking(pio, sm, countNum - 1);
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    counts = ~pio_sm_get_blocking(pio, sm);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    //return ((2*(int64_t)counts) - (int64_t)countNum);
    return counts;
}

uint16_t readMCP(bool channel){
    uint8_t buffer[] = {0x01, 0x80 + (0x40 & (channel << 6)) + 0x20, 0x00};
    uint8_t read_buffer[3];
    gpio_put(CS, false);
    spi_read_blocking(spi1, buffer[0], &read_buffer[0], 1);
    spi_read_blocking(spi1, buffer[1], &read_buffer[1], 1);
    spi_read_blocking(spi1, buffer[2], &read_buffer[2], 1);
    sleep_us(5);
    gpio_put(CS, true);
    uint16_t result = ((read_buffer[1] << 8) | read_buffer[2]);
    return result;
}

int main() {
    set_sys_clock_khz(96000, true);  
    stdio_init_all();
    spi_init(spi1, 100 * 1000); // 100kHz

    spi_set_format(spi1, 8, 0, 0, SPI_MSB_FIRST);

    gpio_set_function(MISO, GPIO_FUNC_SPI);
    gpio_set_function(CLK, GPIO_FUNC_SPI);
    gpio_set_function(MOSI, GPIO_FUNC_SPI);
    //gpio_set_function(CS, GPIO_FUNC_SPI);
    gpio_init(CS);
    gpio_set_dir(CS, GPIO_OUT);
    gpio_put(CS, true);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    gpio_init(MUX_A0);
    gpio_init(MUX_A1);
    gpio_init(MUX_A2);
    gpio_init(MCLK);

    gpio_set_dir(MCLK, GPIO_OUT);
    gpio_set_dir(MUX_A0, GPIO_OUT);
    gpio_set_dir(MUX_A1, GPIO_OUT);
    gpio_set_dir(MUX_A2, GPIO_OUT);
    
    sleep_us(10);
    gpio_put(MCLK, true);
    gpio_put(MUX_A0, true);
    gpio_put(MUX_A1, false);
    gpio_put(MUX_A2, false);

    // Choose PIO instance (0 or 1)
    PIO pio = pio0;

    // Get first free state machine in PIO 0
    uint multislopeSM = pio_claim_unused_sm(pio, true);

    // Add PIO program to PIO instruction memory. SDK will find location and
    // return with the memory offset of the program.
    uint multislopeOffset = pio_add_program(pio, &ms_program);

    const float div = 10;

    // Initialize the program using the helper function in our .pio file
    ms_program_init(pio, multislopeSM, multislopeOffset, PWMA, COMP, div, MEAS);


    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, multislopeSM, true);

    int chr = 0;

    while(true){
        //uint32_t preCharge = get_counts(pio, multislopeSM, 1000);
        sleep_ms(10);
        int read1 = readMCP(false);
        uint32_t counts = get_counts(pio, multislopeSM, 60000);
        int read2 = readMCP(false);
        float approximate = (((2 * counts) - 6000) / 60000) * 14;
        float residue = ((read2 - read1) * 0.002685) * 0.00005;
        float result = approximate + residue;
        printf("%f\n", result);
        //sleep_ms(10);
        if(get_bootsel_button()){
            reset_usb_boot(0,0);
        }

        chr = getchar_timeout_us(0);
        if(chr != PICO_ERROR_TIMEOUT){
            chr = 0;
            uint16_t val = readMCP(true);
            float temp = ((3.3/4096) * val * 100);
            sleep_ms(10);
            printf("%f\n", temp);
        }
    }
    return 0;
}