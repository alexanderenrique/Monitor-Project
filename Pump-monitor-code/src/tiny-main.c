#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdint.h>

#define I2C_BAUD 11
#define VREF 3.3f
#define ADC_MAX 1023.0f
#define R_FIXED 10000.0f // CHANGE WITH ACTUAL RESISTOR VALUE

static void clock_init(void);
static void gpio_init(void);
static void adc_init(void);
static void spi_init(void);
static void i2c_init(void);
static uint16_t adc_read(uint8_t channel);
static float ntc_resistance(float adc);
static float adc_to_voltage(uint16_t adc);

static void system_init(void){
    clock_init();
    gpio_init();
    adc_init();
    spi_init();
    i2c_init();

    sei(); //enables global interrupts

}

static void clock_init(void){
    CLKCTRL.OSC20MCTRLA = CCL_ENABLE_bm;

    CCP = CCP_IOREG_gc;
    CLKCTRL.MCLKCTRLA = CLKCTRL_CLKSEL_OSC20M_gc;

    CCP = CCP_IOREG_gc;
    CLKCTRL.MCLKCTRLB = CLKCTRL_PDIV_6X_gc | CLKCTRL_PEN_bm;

}

static void gpio_init(void){

    // setting the ADC Pins
    // DIRCLR clears the bit register, setting these pins as the output
    PORTA.DIRSET = PIN0_bm | PIN1_bm | PIN2_bm;

    // per-pin control register, setting to zero means no pull ups, no invert, no interrupt
    PORTA.PIN0CTRL = 0;
    PORTA.PIN1CTRL = 0;
    PORTA.PIN2CTRL = 0;

    // I2C Pins
    PORTA.DIRCLR = PIN6_bm | PIN7_bm;

    //SPI Pins, this is the slave so MISO is output
    // MISO PA5 as output
    PORTA.DIRSET = PIN5_bm;

    // MOSI, SCLK as inputs
    PORTA.DIRCLR = PIN3_bm | PIN4_bm;

}

static void adc_init(void){
    ADC0.CTRLA = ADC_ENABLE_bm;
    ADC0.CTRLC = ADC_PRESC_DIV16_gc;
    ADC0.CTRLA |= ADC_RESSEL_10BIT_gc;
}

static void spi_init(void){

    // enable the SPI peripheral
    SPI0.CTRLA = SPI_ENABLE_bm;
}

static void i2c_init(void){
    // TWI is Two Wire Interface, ATTiny speak for I2c

    // set I2C Baud rate
    TWI0.MBAUD = I2C_BAUD;

    // enable TWI Master
    TWI0.MCTRLA = TWI_ENABLE_bm;

    // force bus to idle state
    TWI0.MSTATUS = TWI_BUSSTATE_IDLE_gc;

}

static uint16_t adc_read(uint8_t channel){
    ADC0.MUXPOS = channel; //Selects the analog pin
    ADC0.COMMAND = ADC_STCONV_bm; // Starts the conversion
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) // waits to make sure the hardware changes have taken effect
    ;
    ADC0.INTFLAGS = ADC_RESRDY_bm; //clears the interrupt flag
    return ADC0.RES; //returns the result of the conversion

}

void sample_all_adc(void){
    uint16_t adc_raw[3];
    adc_raw[0] = adc_read(ADC_MUXPOS_AIN0_gc);
    adc_raw[1] = adc_read(ADC_MUXPOS_AIN1_gc);
    adc_raw[2] = adc_read(ADC_MUXPOS_AIN2_gc);
}

float ntc_resistance(float adc){
    return (R_FIXED * VREF/ adc_to_voltage(adc) - 1.0f);
}

float adc_to_voltage(uint16_t adc){
    return (adc*VREF)/ ADC_MAX;
}

int main(void){
    system_init();

    while (1){


    }

    return 0;
}