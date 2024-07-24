#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "ms.pio.h"

const float div = 40;
uint32_t pwmCycles = 150000;

const uint8_t MUX_A0 = 0;
const uint8_t MUX_A1 = 2;
const uint8_t MUX_A2 = 1;

const uint8_t PWMA = 3;                // need to be next to each other
const uint8_t PWMB = 4;                // need to be next to each other
const uint8_t MEAS = 5;
const uint8_t COMP = 6;

const double vrefAbs = 6.85793; // Measured using K2000

double voltage = 0.0;

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

int32_t runup_pos = 0;
int32_t runup_neg = 0;
int32_t rundown_pos = 0;
int32_t rundown_neg = 0;
int32_t residue_before = 0;
int32_t residue_after = 0;

int32_t N1, N2, N3;

int32_t result = 0;

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

    pio_sm_clear_fifos(pio, multislopeSM);
    pio_sm_clear_fifos(pio, residueSM);

    pio_sm_put_blocking(pio, multislopeSM, countNum - 1);         // write number of PWM cycles to runup state machine

    residueBefore = pio_sm_get_blocking(pio, residueSM);          // read pre-runup integrator state

    counts = ~pio_sm_get_blocking(pio, multislopeSM);             // read runup counts
    rundownUp = ~pio_sm_get_blocking(pio, multislopeSM);           // read rundown up counts
    rundownDown = ~pio_sm_get_blocking(pio, multislopeSM);         // read rundown down counts

    residueAfter = pio_sm_get_blocking(pio, residueSM);           // read post-runup integrator state

    runup_pos = pwmCycles - counts;
    runup_neg = counts;

    rundown_neg = rundownUp - counts;
    rundown_pos = rundownDown - rundownUp;

    residue_before = residueBefore & 0xFFF;
    residue_after = residueAfter & 0xFFF;

    gpio_put(SIO_INTERP0_CTRL_LANE0_ADD_RAW_ACCESS, false);
}

void get_cal()
{
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

    pio_sm_clear_fifos(pio, calibrationSM);
    pio_sm_clear_fifos(pio, residueSM);

    uint32_t R1 = (countTwo - countOne);
    uint32_t R2 = (countTwo - countThree);

    RUU = R1;
    RUD = R2;

    gpio_put(LED, false);

}

void get_result()
{
    N1 = N2 = N3 = 0;

    // N1 = (runup_neg * (15*RUU + 1*RUD)) - (runup_pos * (1*RUU + 15*RUD)); // Complementary PWM runup
    N1 = (runup_neg * (15*RUU)) - (runup_pos * (15*RUD));
    N2 = (rundown_neg * RUU) - (rundown_pos * RUD);
    N3 = residue_after - residue_before;

    result = N1 + N2 + N3;

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

    // while(true)
    // {
    //     get_cal();
    //     sleep_ms(500);
    //     printf("%d, %d\n", RUU, RUD);
    // }

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

    sleep_ms(1000);

    setMuxState(MUX_GND);
    sleep_us(100);
    get_counts(pwmCycles);
    get_result();

    gndCounts = result;

    setMuxState(MUX_RAW);
    sleep_us(100);
    get_counts(pwmCycles);
    get_result();

    rawCounts = result;

    while(true)
    {
        newInput = scanf("%s", &inputBuffer, 31);         // Read input from serial port

        setMuxState(MUX_GND);
        sleep_us(100);
        get_counts(pwmCycles);
        get_result();

        inCounts = result;

        voltage = ((double)(inCounts - gndCounts)/(double)(rawCounts - gndCounts)) * vrefAbs;

        // printf("%lf\n", voltage);

        // setMuxState(MUX_IN);
        // sleep_ms(20);
        // get_counts(pwmCycles);
        // inCounts = (counts * RUU) - ((pwmCycles - counts) * RUD) + (residueAfter - residueBefore);

        // double voltage = vrefAbs * (double)(inCounts - gndCounts)/(double)(rawCounts - gndCounts);

        // printf("%.17g\n", voltage);

        // printf("%d, %d, %d, %.17g\n", gndCounts, rawCounts, inCounts, voltage);

        // printf("%d, %d, %d, %d, %d, %d\n", counts, residueBefore, residueAfter, (residueAfter - residueBefore), RUU, RUD);

        // printf("%d, %d, %d, %d, %d, %d, %d\n", counts, rundownUp, rundownDown, residueBefore, residueAfter, RUU, RUD);

        // printf("%d, %d, %d, %d, %d, %d, %d, %d\n", runup_pos, runup_neg, rundown_pos, rundown_neg, residue_before, residue_after, RUU, RUD);

        // printf("%d, %d, %d, %d\n", N1, N2, N3, result);

        printf("%d, %d, %d, %g\n", gndCounts, rawCounts, inCounts, voltage);

        // sleep_ms(500);

    }
    
    return 0;
}