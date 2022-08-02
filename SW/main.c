#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/bootrom.h"
#include "ms.pio.h"

#define MAINS_FREQ 50                   // Hz

const uint8_t MCLK = 18;                // SPI Clock Pin
const uint8_t M_EN = 16;

const uint8_t MUX_A0 = 0;
const uint8_t MUX_A1 = 2;
const uint8_t MUX_A2 = 1;

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

// function that allows us to use the BOOTSEL button as user input
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
    const uint8_t buffer[] = {0x01, 0x80 + (0x40 & (channel << 6)) + 0x20, 0x00};
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

#define TRANSFER_LENGTH 3

uint8_t DMA_SPI_ADC_writeBuffer[TRANSFER_LENGTH] = {0x01, 0x80 + (0x40 & (false << 6)) + 0x20, 0x00};
uint8_t DMA_SPI_ADC_readBuffer[TRANSFER_LENGTH];

uint dma_rx;
uint dma_tx;

uint16_t resultPreMultislope = 0;
uint16_t resultPostMultislope = 0;

bool fistReading = true;

PIO pio;
uint multislopeSM;

void configureDMA();

void dma_irq_handler() {
    // Clear interrupt.
    // and disable the interrupt
    dma_hw->ints0 = (1u << dma_rx);
    irq_set_enabled(DMA_IRQ_0, false);
    printf("Capture finished, %d\n", time_us_32());
    if(fistReading){
        fistReading = false;
        resultPreMultislope = ((DMA_SPI_ADC_readBuffer[1] << 8) | DMA_SPI_ADC_readBuffer[2]);
        pio_sm_put(pio, multislopeSM, (uint32_t)1);
    }else{
        resultPostMultislope = ((DMA_SPI_ADC_readBuffer[1] << 8) | DMA_SPI_ADC_readBuffer[2]);
        pio_sm_put(pio, multislopeSM, (uint32_t)1);
    }
    // reset the DMA
    configureDMA();
}

void pio_irq(){
    //irqMCPdiff(false);
    if (pio0_hw->irq & 1) {
        // PIO0 IRQ0 fired means we need to take first MCP reading
        // start DMAs simultaneously
        // enable IRQ
        printf("PIO0 IRQ0 fired\nStarting capture: %d\n", time_us_32());
        irq_set_enabled(DMA_IRQ_0, true);
        dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
        pio0_hw->irq = 1;
        
        //first = readMCP(false);
    }else if (pio0_hw->irq & 2) {
        printf("PIO0 IRQ1 fired\nStarting capture: %d\n", time_us_32());
        // PIO0 IRQ1 fired means it's time for the second reading
        irq_set_enabled(DMA_IRQ_0, true);
        dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
        pio0_hw->irq = 2;
    }
    
}

void configureDMA(){
    // We set the outbound DMA to transfer from a memory buffer to the SPI transmit FIFO paced by the SPI TX FIFO DREQ
    // The default is for the read address to increment every element (in this case 1 byte = DMA_SIZE_8)
    // and for the write address to remain unchanged.

    dma_channel_config dma_conf_tx = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&dma_conf_tx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_conf_tx, spi_get_dreq(spi1, true));
    dma_channel_configure(dma_tx, &dma_conf_tx,
                          &spi_get_hw(spi1)->dr, // write address
                          DMA_SPI_ADC_writeBuffer, // read address
                          TRANSFER_LENGTH, // element count (each element is of size transfer_data_size)
                          false); // don't start yet

    // We set the inbound DMA to transfer from the SPI receive FIFO to a memory buffer paced by the SPI RX FIFO DREQ
    // We configure the read address to remain unchanged for each element, but the write
    // address to increment (so data is written throughout the buffer)
    dma_channel_config dma_conf_rx = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&dma_conf_rx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_conf_rx, spi_get_dreq(spi1, false));
    channel_config_set_read_increment(&dma_conf_rx, false);
    channel_config_set_write_increment(&dma_conf_rx, true);
    dma_channel_configure(dma_rx, &dma_conf_rx,
                          DMA_SPI_ADC_readBuffer, // write address
                          &spi_get_hw(spi1)->dr, // read address
                          TRANSFER_LENGTH, // element count (each element is of size transfer_data_size)
                          false); // don't start yet
    
    // configure DMA interrupts
    dma_channel_set_irq0_enabled(dma_rx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    //irq_set_enabled(DMA_IRQ_0, true);
}

int main() {
    set_sys_clock_khz(96000, true);  
    stdio_init_all();
    spi_init(spi1, 500 * 1000); // 500kHz

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
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    sleep_ms(1000);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
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
    gpio_put(MUX_A0, false);
    gpio_put(MUX_A1, false);
    gpio_put(MUX_A2, false);

    // initialise multislope PIO
    pio = pio0;
    multislopeSM = pio_claim_unused_sm(pio, true);
    uint multislopeOffset = pio_add_program(pio, &ms_program);
    const float div = 10;
    ms_program_init(pio, multislopeSM, multislopeOffset, PWMA, COMP, div, MEAS);

    // Enable IRQ0 & 1 on PIO0
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
    pio0_hw->inte0 = PIO_IRQ0_INTE_SM0_BITS | PIO_IRQ0_INTE_SM1_BITS;

    // IRQ setup:
    // PIO sends interrupt in IRQ0 for first residue reading
    // IRQ0 handler starts DMA to read SPI of the ADC
    // when DMA finishes, it sends a value over to the TX FIFO of the PIO instance
    // all this time an OUT command was stalling the state machine to wait for the reading to finish

    // set up DMA for ADC writing and reading
    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);

    configureDMA();

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, multislopeSM, true);
    
    int chr = 0;
    
    while(true){
        //uint32_t preCharge = get_counts(pio, multislopeSM, 1000);
        sleep_ms(10);
        //sleep_ms(10);
        if(get_bootsel_button()){
            reset_usb_boot(0,0);
        }

        chr = getchar_timeout_us(0);
        if(chr != PICO_ERROR_TIMEOUT){
            chr = 0;
            //int read1 = readMCP(false); //First residue reading
            gpio_put(PICO_DEFAULT_LED_PIN, true);
            uint32_t counts = get_counts(pio, multislopeSM, 10); //Multisloping for 200ms
            fistReading = true;
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            //int read2 = readMCP(false); //Second residue reading
            //int residueDiff = read1 - read2; //Difference in residue readings, 1 - 2 because scaling amp is inverted
            int residueDiff = resultPreMultislope - resultPostMultislope;
            printf("%d, %d\n", resultPreMultislope, resultPostMultislope);
            printf("spi wite buffer: %d, %d, %d\n", DMA_SPI_ADC_writeBuffer[0], DMA_SPI_ADC_writeBuffer[1], DMA_SPI_ADC_writeBuffer[2]);
            printf("spi read buffer: %d, %d, %d\n", DMA_SPI_ADC_readBuffer[0], DMA_SPI_ADC_readBuffer[1], DMA_SPI_ADC_readBuffer[2]);
            int countDifference = 60000 - (2 * counts); //calculate count difference
            float residueVolt = residueDiff * 0.002685; //calculate residue voltage 
            float residue = residueVolt * 0.000050; //scale residue voltage by integrator and meas time parameters
            float approximate = countDifference * 0.000233; //calculate rough voltage
            float result = approximate + residue; //calculate final voltage by adding rough and residue
            printf("%f\n", result);
            // uint16_t val = readMCP(true);
            // float temp = ((3.3/4096) * val * 100);
            // printf("%f\n", temp);
            sleep_ms(10);
        }
    }
    return 0;
}