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
uint32_t pwmCycles = 6000;

const uint8_t MUX_A0 = 0;
const uint8_t MUX_A1 = 2;
const uint8_t MUX_A2 = 1;

const uint8_t PWMA = 3;                // need to be next to each other
const uint8_t PWMB = 4;                // need to be next to each other
const uint8_t MEAS = 5;
const uint8_t COMP = 6;

PIO pio;

uint multislopeSM;
uint residueSM;

const uint32_t residueConfig = 53248;

uint32_t counts = 0;
uint32_t residueBefore = 0;
uint32_t residueAfter = 0;
uint32_t residueCal = 0;

#if MAINS_FREQ == 50
    const uint32_t MScyclesPerPLC = 6000;
#elif MAINS_FREQ == 60
    const uint32_t MScyclesPerPLC = 5000;
#endif

// MCP3202 ADC
#define CS   13
#define CLK  12
#define MOSI 11
#define MISO 10

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

void get_counts(uint32_t countNum)
{
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    pio_sm_put_blocking(pio, multislopeSM, countNum - 1);         // write number of PWM cycles to runup state machine
    pio_sm_put_blocking(pio, residueSM, (residueConfig << 16));   // write config data to residue ADC
    residueBefore = pio_sm_get_blocking(pio, residueSM);          // read pre-runup integrator state
    counts = ~pio_sm_get_blocking(pio, multislopeSM);             // read runup counts
    pio_sm_put_blocking(pio, residueSM, (residueConfig << 16));   // write config data to residue ADC
    residueAfter = pio_sm_get_blocking(pio, residueSM);           // read post-runup integrator state

    // printf("%d, %d, %d, %d\n", counts, residueBefore, residueAfter, (residueAfter - residueBefore));

    gpio_put(PICO_DEFAULT_LED_PIN, false);
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

    gpio_init(13);
    gpio_set_dir(13, GPIO_OUT);
    gpio_put(13, false);

    // setMuxState(MUX_RAW);
    // setMuxState(MUX_RAW);

    // initialise multislope PIO
    pio = pio0;
    multislopeSM = pio_claim_unused_sm(pio, true);
    uint multislopeOffset = pio_add_program(pio, &ms_program);

    residueSM = pio_claim_unused_sm(pio, true);
    uint residueOffset = pio_add_program(pio, &residue_program);

    ms_program_init(pio, multislopeSM, multislopeOffset, PWMA, COMP, div, MEAS);
    residue_program_init(pio, residueSM, residueOffset, MISO, div);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, multislopeSM, true);

    pio_sm_set_enabled(pio, residueSM, true);

    get_counts(12); // start dithering

    int chr = 0;

    uint32_t newInput = 0;
    char inputBuffer[32] = {0};



    while(true)
    {
        // sleep_ms(500);
        newInput = scanf("%s", &inputBuffer, 31);         // Read input from serial port

        // calibration of residue ADC
        setMuxState(MUX_GND);
        sleep_us(100);
        get_counts(3);
        residueCal = (residueAfter > residueBefore) ? (residueAfter - residueBefore) : (residueBefore - residueAfter);

        setMuxState(MUX_GND);
        sleep_us(100);
        get_counts(pwmCycles);
        uint32_t countGnd = ((residueCal * ((2 * counts) - pwmCycles)) + (residueAfter - residueBefore));
        sleep_us(100);

        setMuxState(MUX_RAW);
        sleep_us(100);
        get_counts(pwmCycles);
        uint32_t countRaw = ((residueCal * ((2 * counts) - pwmCycles)) + (residueAfter - residueBefore));
        sleep_us(100);

        setMuxState(MUX_IN);
        sleep_us(100);
        get_counts(pwmCycles);
        uint32_t countIn = ((residueCal * ((2 * counts) - pwmCycles)) + (residueAfter - residueBefore));
        sleep_us(100);

        setMuxState(MUX_GND);

        printf("%d, %d, %d\n", countGnd, countRaw, countIn);
    }
    
    return 0;
}