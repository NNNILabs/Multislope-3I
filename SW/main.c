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
uint calibrationSM;

const uint32_t residueConfig = 53248;

int32_t counts = 0;
int32_t residueBefore = 0;
int32_t residueAfter = 0;
int32_t RUU = 0;
int32_t RUD = 0;

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

void get_cal()
{
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    uint32_t countOne = 0;
    uint32_t countTwo = 0;
    uint32_t countThree = 0;
    uint32_t countFour = 0;
    while(countFour == 0)
    {
        pio_sm_put_blocking(pio, calibrationSM, 1);
        sleep_us(15);
        pio_sm_put_blocking(pio, residueSM, (residueConfig << 16));
        countOne = pio_sm_get_blocking(pio, residueSM);
        sleep_us(15);
        pio_sm_put_blocking(pio, residueSM, (residueConfig << 16));
        countTwo = pio_sm_get_blocking(pio, residueSM);
        sleep_us(15);
        pio_sm_put_blocking(pio, residueSM, (residueConfig << 16));
        countThree = pio_sm_get_blocking(pio, residueSM);
        sleep_us(15);
        pio_sm_put_blocking(pio, residueSM, (residueConfig << 16));
        countFour = pio_sm_get_blocking(pio, residueSM);
        sleep_ms(1);
    }

    pio_sm_clear_fifos(pio, calibrationSM);
    pio_sm_clear_fifos(pio, residueSM);

    uint32_t R1 = (countTwo - countOne);
    uint32_t R2 = (countThree - countTwo);
    uint32_t R3 = (countFour - countThree);
    
    RUD = (R2 > R1)? (R2 - R1) : (R1 - R2);
    RUU = (R3 > R2)? (R3 - R2) : (R2 - R3);

    // printf("%d, %d, %d, %d\n", countOne, countTwo, countThree, countFour);
    // printf("%d, %d, %d\n", R1, R2, R3);
    // printf("%d, %d\n", RUD, RUU);

    gpio_put(PICO_DEFAULT_LED_PIN, false);

}

int main() 
{
    set_sys_clock_khz(96000, true);  
    stdio_init_all();

    sleep_ms(1000);

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

    pio = pio0;

    residueSM = pio_claim_unused_sm(pio, true);
    uint residueOffset = pio_add_program(pio, &residue_program);

    calibrationSM = pio_claim_unused_sm(pio, true);
    uint calibrationOffset = pio_add_program(pio, &calibration_program);

    residue_program_init(pio, residueSM, residueOffset, MISO, div);
    calibration_program_init(pio, calibrationSM, calibrationOffset, PWMA, COMP, div, MEAS);

    pio_sm_clear_fifos(pio, residueSM);
    pio_sm_clear_fifos(pio, calibrationSM);

    pio_sm_set_enabled(pio, residueSM, true);
    pio_sm_set_enabled(pio, calibrationSM, true);

    get_cal();
    get_cal();

    pio_sm_set_enabled(pio, residueSM, false);
    pio_sm_set_enabled(pio, calibrationSM, false);

    pio_remove_program(pio, &calibration_program, calibrationOffset);
    pio_remove_program(pio, &residue_program, residueOffset);

    multislopeSM = pio_claim_unused_sm(pio, true);
    uint multislopeOffset = pio_add_program(pio, &ms_program);

    residueSM = pio_claim_unused_sm(pio, true);
    residueOffset = pio_add_program(pio, &residue_program);

    ms_program_init(pio, multislopeSM, multislopeOffset, PWMA, COMP, div, MEAS);
    residue_program_init(pio, residueSM, residueOffset, MISO, div);

    pio_sm_set_enabled(pio, multislopeSM, true);
    pio_sm_set_enabled(pio, residueSM, true);

    get_counts(50);

    int chr = 0;

    uint32_t newInput = 0;
    char inputBuffer[32] = {0};

    setMuxState(MUX_GND);
    sleep_us(10);
    get_counts(pwmCycles);
    int32_t finalGround = (counts * RUU) - ((6000 - counts) * RUD) + (residueAfter - residueBefore);

    setMuxState(MUX_RAW);
    sleep_us(10);
    get_counts(pwmCycles);
    int32_t finalRaw = (counts * RUU) - ((6000 - counts) * RUD) + (residueAfter - residueBefore);

    setMuxState(MUX_GND);

    while(true)
    {
        // sleep_ms(500);
        newInput = scanf("%s", &inputBuffer, 31);         // Read input from serial port

        sleep_us(10);
        get_counts(pwmCycles);
        int32_t finalIn = (counts * RUU) - ((6000 - counts) * RUD) + (residueAfter - residueBefore);

        double voltage = 6.85f * (double)(finalIn - finalGround)/(double)(finalRaw - finalGround);

        printf("%d, %d, %d, %012lf\n", finalGround, finalRaw, finalIn, voltage);
    }
    
    return 0;
}