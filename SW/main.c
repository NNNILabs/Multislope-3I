#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/spi.h"
#include "pico/bootrom.h"

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

float readMCP(bool channel, float reference){
    uint8_t buffer[] = {0x01, 0x80 + (0x40 & (channel << 6)) + 0x20, 0x00};
    uint8_t read_buffer[3];
    gpio_put(CS, false);
    spi_read_blocking(spi1, buffer[0], &read_buffer[0], 1);
    spi_read_blocking(spi1, buffer[1], &read_buffer[1], 1);
    spi_read_blocking(spi1, buffer[2], &read_buffer[2], 1);
    sleep_us(5);
    gpio_put(CS, true);
    uint16_t result = ((read_buffer[1] << 8) | read_buffer[2]);
    return (result * (reference/4096));
}

int main() {
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
    while(true){
        float read = readMCP(false, 3.3);
        printf("%f\n", read);
        sleep_ms(100);
        if(get_bootsel_button()){
            reset_usb_boot(0,0);
        }
    }
    return 0;
}