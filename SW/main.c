#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "ms.pio.h"

const float div = 80;
uint32_t pwmCycles = 1250;

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
uint rundownSM;

int32_t counts = 0;
int32_t residueBefore = 0;
int32_t residueAfter = 0;
int32_t RUU = 1540;
int32_t RUD = 1540;
int32_t rundownUp = 0;
int32_t rundownDown = 0;
int32_t gndCounts = 0;
int32_t rawCounts = 0;
int32_t inCounts = 0;

uint32_t newInput = 0;
char inputBuffer[32] = {0};

// MCP3202 ADC
#define CS   13
#define CLK  12
#define MOSI 11
#define MISO 10

#define LED  25

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
    gpio_put(LED, true);

    pio_sm_put_blocking(pio, multislopeSM, countNum - 1);         // write number of PWM cycles to runup state machine

    residueBefore = pio_sm_get_blocking(pio, residueSM);          // read pre-runup integrator state

    counts = ~pio_sm_get_blocking(pio, multislopeSM);             // read runup counts
    rundownUp = ~pio_sm_get_blocking(pio, multislopeSM);           // read rundown up counts
    rundownDown = ~pio_sm_get_blocking(pio, multislopeSM);         // read rundown down counts

    residueAfter = pio_sm_get_blocking(pio, residueSM);           // read post-runup integrator state

    // printf("%d, %d, %d, %d\n", counts, residueBefore, residueAfter, (residueAfter - residueBefore));

    gpio_put(SIO_INTERP0_CTRL_LANE0_ADD_RAW_ACCESS, false);
}

void get_cal()
{
    // newInput = scanf("%s", &inputBuffer, 31);         // Read input from serial port
    sleep_ms(100);

    gpio_put(LED, true);

    uint32_t countOne = 0;
    uint32_t countTwo = 0;
    uint32_t countThree = 0;
    uint32_t countFour = 0;

    pio_sm_put_blocking(pio, calibrationSM, 1);

    countOne = pio_sm_get_blocking(pio, residueSM);
    countTwo = pio_sm_get_blocking(pio, residueSM);
    countThree = pio_sm_get_blocking(pio, residueSM);

    // pio_sm_clear_fifos(pio, calibrationSM);
    // pio_sm_clear_fifos(pio, residueSM);

    uint32_t R1 = (countOne - countTwo);
    uint32_t R2 = (countThree - countTwo);
    // uint32_t R3 = (countThree - countFour);
    
    // RUD = (R2 > R1)? (R2 - R1) : (R1 - R2);
    // RUU = (R3 > R2)? (R3 - R2) : (R2 - R3);

    RUU = R1;
    RUD = R2;

    // printf("%d, %d\n", R1, R2);

    gpio_put(LED, false);

}

int main() 
{
    set_sys_clock_khz(96000, true);  
    stdio_init_all();

    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);

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

    residueSM = 0; // pio_claim_unused_sm(pio, true);
    uint residueOffset = pio_add_program(pio, &residue_program);

    calibrationSM = 1; // pio_claim_unused_sm(pio, true);
    uint calibrationOffset = pio_add_program(pio, &calibration_program);

    residue_program_init(pio, residueSM, residueOffset, MISO, div);
    calibration_program_init(pio, calibrationSM, calibrationOffset, PWMA, COMP, div, MEAS);

    pio_sm_set_enabled(pio, calibrationSM, true);
    pio_sm_set_enabled(pio, residueSM, true);

    get_cal();

    sleep_ms(100);

    get_cal();

    while(true)
    {
        get_cal();
        sleep_ms(500);
        printf("%d, %d\n", RUU, RUD);
    }

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

    setMuxState(MUX_GND);
    sleep_ms(20);
    get_counts(pwmCycles);
    gndCounts = (counts * RUU) - ((pwmCycles - counts) * RUD) + (residueAfter - residueBefore);

    setMuxState(MUX_RAW);
    sleep_ms(20);
    get_counts(pwmCycles);
    rawCounts = (counts * RUU) - ((pwmCycles - counts) * RUD) + (residueAfter - residueBefore);

    setMuxState(MUX_RAW);

    while(true)
    {
        // newInput = scanf("%s", &inputBuffer, 31);         // Read input from serial port

        get_counts(100);
        sleep_ms(500);

        // setMuxState(MUX_IN);
        // sleep_ms(20);
        // get_counts(pwmCycles);
        // inCounts = (counts * RUU) - ((pwmCycles - counts) * RUD) + (residueAfter - residueBefore);

        // double voltage = 6.85f * (double)(inCounts - gndCounts)/(double)(rawCounts - gndCounts);

        // printf("%.17g\n", voltage);

        // printf("%d, %d, %d, %.17g\n", gndCounts, rawCounts, inCounts, voltage);

        // printf("%d, %d, %d, %d, %d, %d\n", counts, residueBefore, residueAfter, (residueAfter - residueBefore), RUU, RUD);

        printf("%d, %d, %d, %d, %d, %d, %d\n", counts, rundownUp, rundownDown, residueBefore, residueAfter, (residueAfter - residueBefore), (rundownDown - rundownUp));

    }
    
    return 0;
}