#define F_CPU 12000000UL
#include <Nrf24L01.h>
#include <Nrf24L01Registers.h>
#include <avr/io.h>
#include <stdbool.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "interrupts.h"
#include "procedures.h"


static void
enable_spi_for_nrf(void)
{
    DDRC |= (1 << 2) | (1 << 1);
    DDRE |= (1 << 2) | (1 << 3);
    SPSR1 |= (1 << SPI2X);
    SPCR1 = (1 << MSTR1) | (1 << SPE1);
}

// warning this function assumes that us_time is no more than 13ms (20MHz)
static void
delay_us_dfn_for_nrf(void* data, double us_time)
{
    (void)data;
    double us_time_double = us_time;
    double delay_1_val = ((F_CPU) / 3e6) * us_time_double;
    double delay_2_val = ((F_CPU) / 4e6) * us_time_double;
    // check if we can use precise timer
    if(delay_1_val > 255) {
        _delay_loop_2((uint16_t)delay_2_val);
        return;
    }
    uint8_t delay_1_ticks = (delay_1_val < 1.0)? 1 : delay_1_val;
    _delay_loop_1(delay_1_ticks);
}

static uint8_t
exchange_byte_dfn_for_nrf(void* data, uint8_t byte_to_send)
{
    (void)data;
    SPDR1 = byte_to_send;
    while (!(SPSR1 & (1 << SPIF1)));
    return SPDR1;
}

static void
set_ce_pin_dfn_for_nrf(void* data, uint8_t value)
{
    (void)data;
    if (value) {
        PORTC |= (1 << 2);
        return;
    }
    PORTC &= ~(1 << 2);
}

static void
set_csn_pin_dfn_for_nrf(void* data, uint8_t value)
{
    (void)data;
    if (value) {
        PORTE |= (1 << 2);
        return;
    }
    PORTE &= ~(1 << 2);
}

static void
run_cpu_sleep_sequence(void)
{
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}

static NrfHardwareInterface nrf_hw_interface = {
    .delay_us = delay_us_dfn_for_nrf,
    .exchange_byte = exchange_byte_dfn_for_nrf,
    .set_ce_pin = set_ce_pin_dfn_for_nrf,
    .set_csn_pin = set_csn_pin_dfn_for_nrf,
};

static const uint8_t address_to_write[] = "54321";
static const uint8_t address_to_read[] = "65432";
static const uint8_t address_length = 5;

static char text_buffer[33] = {};
static const uint8_t text_buffer_len = sizeof(text_buffer)/sizeof(*text_buffer); 


int main(void)
{
    enable_spi_for_nrf();
    const uint8_t timer_seconds_to_timeout = 20;
    interrupts_init(INTERRUPTS_F_CPU_TO_TIMER_TICKS(F_CPU), timer_seconds_to_timeout);
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    NrfController* nrf_ctrl = nrf_controller_new(&nrf_hw_interface, NULL);
    nrf_controller_begin(nrf_ctrl);
    uint8_t config_reg = nrf_controller_read_byte_register(nrf_ctrl, NRF_CONFIG_REG);
    config_reg &= ~(1 << NRF_CONFIG_BIT_MASK_RX_DR);
    config_reg |= (1 << NRF_CONFIG_BIT_MASK_TX_DS);
    config_reg |= (1 << NRF_CONFIG_BIT_MASK_MAX_RT);
    nrf_controller_write_byte_register(nrf_ctrl, NRF_CONFIG_REG, config_reg);
    
    nrf_controller_set_ack_payloads(nrf_ctrl, NRF_CTRL_ACK_PAYLOAD_ENABLED);
    nrf_controller_open_writing_pipe(nrf_ctrl, address_to_write, address_length);
    nrf_controller_open_reading_pipe(nrf_ctrl, 1, address_to_read, address_length);
    ProceduresData data;
    procedures_data_init(&data, nrf_ctrl, (char*)text_buffer, text_buffer_len);
    nrf_controller_start_listening(nrf_ctrl);
    while (1) 
    {
        uint8_t pipe_number;
        if(nrf_controller_is_message_available(nrf_ctrl, &pipe_number)) {
            
            uint8_t payload_size = nrf_controller_get_dynamic_payload_size(nrf_ctrl);
            payload_size = (payload_size < text_buffer_len - 1) ? payload_size : text_buffer_len - 1;
            nrf_controller_read_incoming(nrf_ctrl, (uint8_t*)text_buffer, payload_size);
            procedures_handle_incoming_message(&data, (const char*)text_buffer);
        }
        if (interrupts_read_zero_interrupt_and_clear()) {
            interrupts_reset_timer();
        }
        if (interrupts_read_timeout_and_clear()) {
            run_cpu_sleep_sequence();
            interrupts_init(INTERRUPTS_F_CPU_TO_TIMER_TICKS(F_CPU), timer_seconds_to_timeout);
            interrupts_reset_timer();
        }
    }
    nrf_controller_free(nrf_ctrl);
    procedures_data_destroy(&data);
}

