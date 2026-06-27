#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    // Set PB1 (OC1A) as output
    DDRB |= (1 << PB1);

    // Timer1: Fast PWM, 8-bit, non-inverting on OC1A
    TCCR1A = (1 << COM1A1) | (1 << WGM10);
    TCCR1B = (1 << WGM12) | (1 << CS11);  // prescaler=8

    // Ramp up and down
    uint8_t duty = 0;
    int8_t dir = 1;
    while (1) {
        OCR1A = duty;
        _delay_ms(2);
        duty += dir;
        if (duty == 255) dir = -1;
        if (duty == 0)   dir = 1;
    }
}
