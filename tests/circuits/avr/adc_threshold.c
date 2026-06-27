#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    DDRB |= (1 << PB0);   // PB0 = LED output
    DDRC &= ~(1 << PC0);  // PC0 = ADC0 input

    // ADC: single-ended, AVcc reference, prescaler=128 (125kHz ADC clock)
    ADMUX  = (1 << REFS0);                         // AVcc ref, channel 0
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

    while (1) {
        ADCSRA |= (1 << ADSC);            // start conversion
        while (ADCSRA & (1 << ADSC));     // wait for completion
        uint16_t adc = ADC;

        if (adc > 512)    // > ~2.5V
            PORTB |= (1 << PB0);
        else
            PORTB &= ~(1 << PB0);

        _delay_ms(1);
    }
}
