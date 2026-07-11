#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

#include "SPI.pio.h"

#define LED    25
#define SYNC    0
#define TEST    1

#define MUX_A0 26
#define MUX_A1 22

#define PWM_N  18
#define PWM_P  19
#define RST    20
#define MEAS   21

#define CS      6
#define DO      7
#define CLK     8

#define PWM_PERIOD       50
#define PWM_COM           5
#define VREFN_OFF_PERIOD  0
#define VREFP_OFF_PERIOD 50
#define VREFN_ON_PERIOD  50
#define VREFP_ON_PERIOD   0

#define JPWM

#define RESET_ON  pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), PWM_PERIOD)
#define RESET_OFF pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), 0)

#define INPUT_ON  pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), PWM_PERIOD)
#define INPUT_OFF pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), 0)

// Notes: CPWM is much 'noisier' than JPWM, might have something to do with the 'spikes' being read by the fast ADC

#ifdef CPWM
// CPWM
#define VREFN_HIGH pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), PWM_PERIOD - PWM_COM)
#define VREFN_LOW  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), PWM_COM)
#define VREFN_OFF  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), VREFN_OFF_PERIOD)

#define VREFP_HIGH pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), PWM_COM)
#define VREFP_LOW  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), PWM_PERIOD - PWM_COM)
#define VREFP_OFF  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), VREFP_OFF_PERIOD)
#endif

#ifdef JPWM
// JPWM
#define VREFN_HIGH pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), PWM_PERIOD - PWM_COM)
#define VREFN_LOW  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), PWM_COM)
#define VREFN_OFF  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), VREFN_OFF_PERIOD)
#define VREFN_ON   pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), PWM_PERIOD)

#define VREFP_HIGH pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), PWM_PERIOD - PWM_COM)
#define VREFP_LOW  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), PWM_COM)
#define VREFP_OFF  pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), VREFN_OFF_PERIOD)
#define VREFP_ON   pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), PWM_PERIOD)
#endif

#define FAST_RUNDOWN_ON 

#define PWM_PER_PLC   8
#define NPLC          1

PIO pio;
uint adc_sm;

uint adc_reading = 0;
uint phase = 10;
uint sm_counter = 0;

uint32_t pos_counts = 0;
uint32_t neg_counts = 0;

uint32_t neg_rundown = 0;
uint32_t pos_rundown = 0;

uint32_t counts = 0;

uint32_t adc_readings[5] = {0};
uint adc_reading_index = 0;

bool reading = false;
bool finished = true;
bool rundown_up = false;
bool rundown_down = false;
bool rundown_finished = false;

// S1: IN
// S2: AGND
// S3: -
// S4: RAW

enum muxState
{
    MUX_IN, 
    MUX_RAW,
    MUX_GND,
};

enum sm_state
{
    SM_IDLE,
    SM_START,
    SM_READING,
    SM_RUNNING,
    SM_STOP
};

enum multicore_state
{
    MC_SAMPLE,
    MC_IDLE
};

sm_state state = SM_IDLE;

void set_mux_state(muxState inputState);
void config_pwm();
void on_pwm_wrap();

void set_mux_state(muxState inputState)
{
    switch(inputState)
    {
        case MUX_IN:
            gpio_put(MUX_A0, false);
            gpio_put(MUX_A1, false);
            break;

        case MUX_GND:
            gpio_put(MUX_A0, true);
            gpio_put(MUX_A1, false);
            break;
        
        case MUX_RAW:
            gpio_put(MUX_A0, true);
            gpio_put(MUX_A1, true);
            break;
    }
}

void config_pwm()
{
    pwm_config config = pwm_get_default_config();

    // Synchronization slice
    // Sysclk: 200MHz, clkdiv: 200, clk: 1MHz, 50 counts
    // SYNC pulse is 5μs (10 counts) wide, one count before rising edge signals start of ISR, falling edge signals start of next PWM cycle
    gpio_set_function(SYNC, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv(&config, 100);
    pwm_config_set_wrap(&config, 49);
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_FREE_RUNNING);
    pwm_config_set_phase_correct(&config, false);
    pwm_config_set_output_polarity(&config, false, false);
    pwm_init(pwm_gpio_to_slice_num(SYNC), &config, false);
    pwm_set_chan_level(pwm_gpio_to_slice_num(SYNC), pwm_gpio_to_channel(SYNC), 1);

    // PWM slice
    // Sysclk: 200MHz, clkdiv: 200, clk: 1MHz, 50 counts 
    gpio_set_function(PWM_N, GPIO_FUNC_PWM);
    gpio_set_function(PWM_P, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv(&config, 100);
    pwm_config_set_wrap(&config, 49);
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_FREE_RUNNING);
    pwm_config_set_phase_correct(&config, false);
    #ifdef CPWM
    pwm_config_set_output_polarity(&config, false, true);
    #endif
    #ifdef JPWM
    pwm_config_set_output_polarity(&config, false, false);
    #endif
    pwm_init(pwm_gpio_to_slice_num(PWM_N), &config, false);
    pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
    pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 50);

    // Reset and measure slice
    // Sysclk: 200MHz, clkdiv: 200, clk: 1MHz, 50 counts 
    gpio_set_function(RST, GPIO_FUNC_PWM);
    gpio_set_function(MEAS, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv(&config, 100);
    pwm_config_set_wrap(&config, 49);
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_FREE_RUNNING);
    pwm_config_set_phase_correct(&config, false);
    pwm_config_set_output_polarity(&config, false, false);
    pwm_init(pwm_gpio_to_slice_num(RST), &config, false);
    pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), 50);
    pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), 0);

    // PWM Sync IRQ
    pwm_clear_irq(pwm_gpio_to_slice_num(SYNC));
    pwm_set_irq_enabled(pwm_gpio_to_slice_num(SYNC), true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void sm_plc(uint reading)
{
    if(sm_counter == 0)
    {
        RESET_OFF;
        INPUT_ON;
        VREFP_HIGH;
        VREFN_LOW;

        pos_counts += 1;
        counts += 1;
    }
    else if(sm_counter < (PWM_PER_PLC * NPLC))
    {
        if(reading > 100)
        {
            VREFP_HIGH;
            VREFN_LOW;

            pos_counts += 1;
            counts += 1;
        }
        else if (reading <= 100)
        {
            VREFN_HIGH;
            VREFP_LOW;

            neg_counts += 1;
            counts -= 1;
        }
    }
    else if(sm_counter == (PWM_PER_PLC * NPLC))
    {
        pwm_set_wrap(pwm_gpio_to_slice_num(SYNC), 3);
        pwm_set_wrap(pwm_gpio_to_slice_num(PWM_N), 3);
        pwm_set_wrap(pwm_gpio_to_slice_num(RST), 3);

        pwm_set_chan_level(pwm_gpio_to_slice_num(SYNC), pwm_gpio_to_channel(SYNC), 1);
        pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), 0);
        pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), 0);
        pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 4);
        pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 4);
    }
    else if(sm_counter > (PWM_PER_PLC * NPLC))
    {
        
    }
    // else if(sm_counter == (PWM_PER_PLC * NPLC))
    // {
    //     pwm_set_wrap(pwm_gpio_to_slice_num(SYNC), 9);
    //     pwm_set_wrap(pwm_gpio_to_slice_num(PWM_N), 9);
    //     pwm_set_wrap(pwm_gpio_to_slice_num(RST), 9);

    //     pwm_set_chan_level(pwm_gpio_to_slice_num(SYNC), pwm_gpio_to_channel(SYNC), 1);
    //     pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), 0);
    //     pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), 0);

    //     if(adc_reading > 4000)
    //     {
    //         pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
    //         pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 10);
    //         rundown_down = true;
    //         pos_rundown += 1;
    //     }
    //     else
    //     {
    //         pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 10);
    //         pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 0);
    //         rundown_up = true;
    //         neg_rundown += 1;
    //     }
    // }
    // else if(sm_counter > (PWM_PER_PLC * NPLC))
    // {
    //     if(rundown_up)
    //     {
    //         if(adc_reading > 4000)
    //         {
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 10);
    //             pos_rundown += 1;
    //             rundown_up = false;
    //             rundown_down = true;
    //         }
    //         else
    //         {
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 10);
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 0);
    //             neg_rundown += 1;
    //         }
    //     }
    //     else if(rundown_down)
    //     {
    //         if(adc_reading < 100)
    //         {
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 0);
    //             rundown_finished = true;
    //         }
    //         else
    //         {
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
    //             pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 10);
    //             pos_rundown += 1;
    //         }
    //     }
    // }

    sm_counter += 1;

    // if(rundown_finished)
    // {
    //     pwm_set_wrap(pwm_gpio_to_slice_num(SYNC), 50);
    //     pwm_set_wrap(pwm_gpio_to_slice_num(PWM_N), 50);
    //     pwm_set_wrap(pwm_gpio_to_slice_num(RST), 50);

    //     pwm_set_chan_level(pwm_gpio_to_slice_num(SYNC), pwm_gpio_to_channel(SYNC), 1);
    //     pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), 50);
    //     pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), 0);
    //     pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
    //     pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 0);

    //     sm_counter = 0;
    //     state = SM_IDLE;
    //     finished = true;
    // }

    if(sm_counter == (PWM_PER_PLC * NPLC + 11))
    {
        
        pwm_set_wrap(pwm_gpio_to_slice_num(SYNC), 49);
        pwm_set_wrap(pwm_gpio_to_slice_num(PWM_N), 49);
        pwm_set_wrap(pwm_gpio_to_slice_num(RST), 49);

        pwm_set_chan_level(pwm_gpio_to_slice_num(SYNC), pwm_gpio_to_channel(SYNC), 1);
        pwm_set_chan_level(pwm_gpio_to_slice_num(RST), pwm_gpio_to_channel(RST), 50);
        pwm_set_chan_level(pwm_gpio_to_slice_num(MEAS), pwm_gpio_to_channel(MEAS), 0);
        pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_N), pwm_gpio_to_channel(PWM_N), 0);
        pwm_set_chan_level(pwm_gpio_to_slice_num(PWM_P), pwm_gpio_to_channel(PWM_P), 0);

        pwm_set_counter(pwm_gpio_to_slice_num(SYNC), 9);

        sm_counter = 0;
        state = SM_IDLE;
        finished = true;
    }

    // if(sm_counter == (PWM_PER_PLC * NPLC + 1))
    // {
    //     RESET_ON;
    //     INPUT_OFF;
    //     VREFP_OFF;
    //     VREFN_OFF;

    //     sm_counter = 0;
    //     state = SM_IDLE;
    //     finished = true;
    // }
}

void on_pwm_wrap()
{
    gpio_put(TEST, 1);

    switch(state)
    {
        case(SM_IDLE):
            if(reading)
            {
                state = SM_READING;
                reading = false;
                finished = false;
                neg_counts = 0;
                pos_counts = 0;
                counts = 0;
            }
            break;
        case(SM_READING):
            // Read fast ADC
            pio_sm_put_blocking(pio, adc_sm, 0);
            adc_reading = pio_sm_get_blocking(pio, adc_sm);
            pio_sm_clear_fifos(pio, adc_sm);
            sm_plc(adc_reading);
            break;
    }

    gpio_put(TEST, 0);

    // Clear interrupt to prevent crashing
    pwm_clear_irq(pwm_gpio_to_slice_num(SYNC));
}

void core1()
{
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);

    gpio_init(TEST);
    gpio_set_dir(TEST, GPIO_OUT);

    gpio_init(MUX_A0);
    gpio_set_dir(MUX_A0, GPIO_OUT);

    gpio_init(MUX_A1);
    gpio_set_dir(MUX_A1, GPIO_OUT);

    // PIO SPI controller for AD7276 fast ADC
    pio = pio0;
    adc_sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &SPI_program);
    SPI_program_init(pio, adc_sm, offset, CS, 1);
    pio_sm_set_enabled(pio, adc_sm, true);

    // Kickstarting the state machine to set the SPI pins to correct states
    pio_sm_put_blocking(pio, adc_sm, 0);
    adc_reading = pio_sm_get_blocking(pio, adc_sm);
    pio_sm_clear_fifos(pio, adc_sm);
    adc_reading = 0;

    config_pwm();

    pwm_set_counter(pwm_gpio_to_slice_num(SYNC), 9);

    pwm_set_mask_enabled(
        (1 << pwm_gpio_to_slice_num(SYNC))  |
        (1 << pwm_gpio_to_slice_num(PWM_N)) |
        (1 << pwm_gpio_to_slice_num(RST))
    );

    // for(int i = 0; i < phase; i += 1)
    // {
    //     pwm_advance_count(pwm_gpio_to_slice_num(SYNC));
    // }

    set_mux_state(MUX_GND);

    // Dummy conversion to set the correct pin states
    sleep_ms(100);
    reading = true;
    while(!finished);

    while(true)
    {
        // I shuffled the order of instructions till things worked lmao
        //sleep_ms(500);
        if(finished)
        {
            reading = true;
        }
        while(!finished);
        // for(int i = 0; i < phase; i += 1)
        // {
        //     pwm_advance_count(pwm_gpio_to_slice_num(SYNC));
        // }
        multicore_fifo_push_blocking(neg_counts - pos_counts);
        sleep_ms(100);
    }
}

int main()
{
    stdio_init_all();

    vreg_set_voltage(VREG_VOLTAGE_1_20);
    set_sys_clock_khz(200000, true);
    
    multicore_launch_core1(core1);

    while (true) 
    {
        uint32_t result = multicore_fifo_pop_blocking();
        printf("%d, %d, %d, %d\n", pos_counts, neg_counts, result, counts);
    }
}
