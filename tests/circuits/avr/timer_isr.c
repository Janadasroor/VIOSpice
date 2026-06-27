#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>

/*
 * Timer1 Compare Match A interrupt — toggles PB5 at ~1 Hz
 * (500ms on, 500ms off)
 *
 * Timer1: CTC mode, prescaler=1024
 * Compare value = F_CPU / (prescaler * 2 * freq) - 1
 *              = 16e6 / (1024 * 2 * 1) - 1
 *              = 7812
 *
 * LED on PB5 (ext_id 13) blinks at 1 Hz via interrupt.
 */

ISR(TIMER1_COMPA_vect) {
    PORTB ^= (1 << PB5);
}

int main(void) {
    DDRB |= (1 << PB5);

    // Timer1: CTC mode (WGM12=1), prescaler=1024 (CS12=1, CS10=1)
    TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);
    OCR1A = 7812;    // 1 Hz toggle
    TIMSK1 = (1 << OCIE1A);

    sei();

    while (1) {
        // main loop does nothing — interrupt handles the toggle
        __asm__ __volatile__("nop");
    }
}
