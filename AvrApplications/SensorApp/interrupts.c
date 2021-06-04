#include "interrupts.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/atomic.h>


static volatile bool int_zero_occured = false;
static volatile bool is_timeout = false;
static volatile uint8_t seconds_counter = 0;

static inline bool
read_and_clear_bool_flag(volatile bool* flag)
{
    bool result = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if(*flag) {
            result = true;
            *flag = false;
        }
    }
    return result;
}

bool
interrupts_read_zero_interrupt_and_clear(void)
{
    return read_and_clear_bool_flag(&int_zero_occured);
}

bool
interrupts_read_timeout_and_clear(void)
{
    return read_and_clear_bool_flag(&is_timeout);    
}

void
interrupts_reset_timer(void)
{
    TCNT1L = 0;
    TCNT1H = 0;
    seconds_counter = 0;
}

static uint8_t timeout_sec= 0;

void
interrupts_init(uint16_t ticks_for_one_second, uint8_t timeout_seconds)
{
    timeout_sec = timeout_seconds;
    // configure external interrupt 0
    EICRA |= (1 << ISC01) | (0 << ISC00);
    EIMSK |= (1 << INT0);
    // configure timer
    OCR1AH = (ticks_for_one_second >> 8);
    OCR1AL = ticks_for_one_second;
    const uint8_t prescaller_1024_settings = (1 << CS12) | (0 << CS11) | (1 << CS10);
    const uint8_t clear_timer_on_compare_match_settings = (1 << WGM12);
    TCCR1B |= prescaller_1024_settings | clear_timer_on_compare_match_settings;
    const uint8_t enable_interrupt_on_OCR1A = (1 << OCIE1A);
    TIMSK1 |= enable_interrupt_on_OCR1A;
    SEI();
}

ISR(TIMER1_COMPA_vect)
{
    seconds_counter++;
    if (timeout_sec == seconds_counter) {
        seconds_counter = 0;
        is_timeout = true;
    }
}

ISR(INT0_vect)
{
    int_zero_occured = true;
}