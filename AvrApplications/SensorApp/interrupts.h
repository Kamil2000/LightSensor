/*
 * interrupts.h
 *
 * Created: 4/27/2021 10:58:05 PM
 *  Author: Kamil
 */ 

#include <stdbool.h>
#include <stdint.h>
#include <avr/interrupt.h>


#ifndef INTERRUPTS_H_
#define INTERRUPTS_H_

bool
interrupts_read_zero_interrupt_and_clear(void);

bool
interrupts_read_timeout_and_clear(void);

void
interrupts_reset_timer(void);

void
interrupts_init(uint16_t ticks_for_one_second, uint8_t timeout_seconds);

#define INTERRUPTS_F_CPU_TO_TIMER_TICKS(value) ((value)/1024UL)

#define SEI() sei()
#define CLI() cli()


#endif /* INTERRUPTS_H_ */