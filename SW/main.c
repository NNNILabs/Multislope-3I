#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "pico/bootrom.h"
#include "ms.pio.h"

#define MAINS_FREQ 50                   // Hz

const float div = 20;
uint32_t pwmCycles = 12;

const uint8_t MUX_A0 = 0;
const uint8_t MUX_A1 = 2;
const uint8_t MUX_A2 = 1;

const uint8_t PWMA = 3;                // need to be next to each other
const uint8_t PWMB = 4;                // need to be next to each other
const uint8_t MEAS = 5;
const uint8_t COMP = 6;

const uint64_t mainsPeriodus = 1000000/MAINS_FREQ;

#if MAINS_FREQ == 50
    const uint32_t MScyclesPerPLC = 6000;
#elif MAINS_FREQ == 60
    const uint32_t MScyclesPerPLC = 5000;
#endif

// MCP3202 ADC
#define CS   13
#define CLK  10
#define MOSI 11
#define MISO 12

double constant1 = 14.0;
double constant2 = 0.00333;

enum muxState
{
    MUX_IN, 
    MUX_RAW,
    MUX_GND,
    MUX_POSREF,  // Implement if input resistor is > 10kΩ
    MUX_NEGREF   // Same as above
};

void setMuxState(enum muxState inputState)
{
    switch(inputState)
    {
        case MUX_IN:
            gpio_put(MUX_A0, false);
            gpio_put(MUX_A1, false);
            gpio_put(MUX_A2, false);
            break;

        case MUX_RAW:
            gpio_put(MUX_A0, true);
            gpio_put(MUX_A1, false);
            gpio_put(MUX_A2, false);
            break;
        
        case MUX_GND:
            gpio_put(MUX_A0, false);
            gpio_put(MUX_A1, false);
            gpio_put(MUX_A2, true);
            break;
    }
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
uint residueSM;

uint16_t picoADC_before;
uint16_t picoADC_after;

double getReading()
{
    double result = (double)0.0;
    uint32_t counts = get_counts(pio, multislopeSM, pwmCycles); //Multisloping for 20ms
    fistReading = true;
    double approximate_voltage = 60000.0 - (2.0 * (double)counts);
    approximate_voltage = approximate_voltage / 60000.0;
    approximate_voltage = approximate_voltage * constant1;
    double residue_voltage = (constant2) * 0.00005;
    residue_voltage = residue_voltage * (picoADC_before - picoADC_after);
    result = approximate_voltage + residue_voltage;
    int difference = pwmCycles - (2*counts);
    float voltage = (difference * 0.001708815) + 0.00;

    printf("%d, %d, %d, %d\n", difference, resultPreMultislope, resultPostMultislope, (resultPostMultislope - resultPreMultislope));

    return result;
}

int main() 
{
    set_sys_clock_khz(96000, true);  
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    sleep_ms(1000);
    gpio_put(PICO_DEFAULT_LED_PIN, false);

    gpio_init(MUX_A0);
    gpio_init(MUX_A1);
    gpio_init(MUX_A2);

    gpio_set_dir(MUX_A0, GPIO_OUT);
    gpio_set_dir(MUX_A1, GPIO_OUT);
    gpio_set_dir(MUX_A2, GPIO_OUT);
    
    sleep_us(10);

    setMuxState(MUX_GND);

    // initialise multislope PIO
    pio = pio0;
    multislopeSM = pio_claim_unused_sm(pio, true);
    uint multislopeOffset = pio_add_program(pio, &ms_program);

    ms_program_init(pio, multislopeSM, multislopeOffset, PWMA, COMP, div, MEAS);

    residueSM = pio_claim_unused_sm(pio, true);
    uint residueOffset = pio_add_program(pio, &residue_program);

    residue_program_init(pio, residueSM, residueOffset, CLK, 40.0f);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, multislopeSM, true);

    pio_sm_set_enabled(pio, residueSM, true);

    get_counts(pio, multislopeSM, 1);

    int chr = 0;
    
    while(true)
    {
        sleep_ms(10);

        double zero = getReading();

    }
    
    return 0;
}