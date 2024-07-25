// I2C slave controller
// Code adapted from  : https://forums.raspberrypi.com/viewtopic.php?t=304074#p1825651
// Coding assisted by : https://github.com/diminDDL

// Add to CMakeLists.txt: AUX_SOURCE_DIRECTORY(lib SUB_SOURCES)
// Add to CMakeLists.txt: target_sources(${PROJECT_NAME} PRIVATE ${SUB_SOURCES})
// Add to main.cpp: #include "lib/i2c_slave.h"
// Add to main.cpp (main function): i2c_init();

#pragma once

#include "hardware/i2c.h"
#include "hardware/irq.h"

#include "pico/stdlib.h"

// define I2C addresses to be used for this peripheral
#define I2C0_PERIPHERAL_ADDR 0x2B

// GPIO pins to use for I2C
#define GPIO_SDA0 8
#define GPIO_SCK0 9

enum
{
    CONV_STAT_REG = 0,
    INPUT_REG = 1,
    OUTPUT_REG = 5,
    CUR_REG = 13
};

volatile struct registerMap
{
    uint8_t conversionStatus = 0;
    uint32_t input = 0;
    double output = 0.0;
    uint8_t currentRegister = 0;
}__attribute__((packed))regs;

void setRegister(volatile registerMap *reg, uint8_t regNum, uint8_t value, uint8_t offset)
{
    uint8_t *ptr = (uint8_t *)reg;
    if((regNum + offset) >= sizeof(registerMap))
    {
        return;
    }
    ptr += regNum + offset;
    *ptr = value;
}

uint8_t getRegister(volatile registerMap *reg, uint8_t regNum, uint8_t offset)
{
    uint8_t *ptr = (uint8_t *)reg;
    if((regNum + offset) >= sizeof(registerMap))
    {
        return 0;
    }
    ptr += regNum + offset;
    return *ptr;
}

// Interrupt handler implements the RAM
void i2c0_irq_handler() {

    // Get interrupt status
    uint32_t status = i2c0->hw->intr_stat;

    static uint8_t currentOffset = 0;

    // Check to see if we have received data from the I2C controller
    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {

        // Read the data (this will clear the interrupt)
        uint32_t value = i2c0->hw->data_cmd;

        // Check if this is the 1st byte we have received
        if (value & I2C_IC_DATA_CMD_FIRST_DATA_BYTE_BITS) 
        {
            currentOffset = 0;
            // If so treat it as the address to use
            regs.currentRegister = (uint8_t)(value & I2C_IC_DATA_CMD_DAT_BITS);
        } 
        else 
        {
            // If not 1st byte then store the data in the RAM
            // and increment the address to point to next byte

            // If not last byte, interpret the request
            uint8_t data = (uint8_t)(value & I2C_IC_DATA_CMD_DAT_BITS);
            switch(regs.currentRegister)
            {
                case CONV_STAT_REG:
                    // Use the memory offset for the register + the current register to store the data
                    setRegister(&regs, CONV_STAT_REG, data, 0);
                    break;
                case INPUT_REG:
                    setRegister(&regs, INPUT_REG, data, currentOffset);
                    break;
                case OUTPUT_REG:
                    setRegister(&regs, OUTPUT_REG, data, currentOffset);
                    break;
                case CUR_REG:
                    setRegister(&regs, CUR_REG, data, 0);
                    break;
                default:
                    break;
            }
            currentOffset++;
        }
    }

    // Check to see if the I2C controller is requesting data from the RAM
    if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS)
    {
        // Write the data from the current address in RAM
        // Clear the interrupt
        // Increment the address

        uint8_t data = 0;
        switch(regs.currentRegister)
        {
            case CONV_STAT_REG:
                // Use the memory offset for the register + the current register to store the data
                data = getRegister(&regs, CONV_STAT_REG, 0);
                break;
            case INPUT_REG:
                data = getRegister(&regs, INPUT_REG, currentOffset);
                break;
            case OUTPUT_REG:
                data = getRegister(&regs, OUTPUT_REG, currentOffset);
                break;
            case CUR_REG:
                data = getRegister(&regs, CUR_REG, 0);
                break;
            default:
                break;
        }
        currentOffset++;
        i2c0->hw->data_cmd = (uint32_t)data;
        i2c0->hw->clr_rd_req;
    }
}

void i2c_init()
{
    // Setup I2C0 as slave (peripheral)
    i2c_init(i2c0, 100 * 1000);
    i2c_set_slave_mode(i2c0, true, I2C0_PERIPHERAL_ADDR);

    // Setup GPIO pins to use and add pull up resistors
    gpio_set_function(GPIO_SDA0, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_SCK0, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_SDA0);
    gpio_pull_up(GPIO_SCK0);

    // Enable the I2C interrupts we want to process
    i2c0->hw->intr_mask = (I2C_IC_INTR_MASK_M_RD_REQ_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);

    // Set up the interrupt handler to service I2C interrupts
    irq_set_exclusive_handler(I2C0_IRQ, i2c0_irq_handler);

    // Enable I2C interrupt
    irq_set_enabled(I2C0_IRQ, true);
}