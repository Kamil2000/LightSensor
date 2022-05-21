#define F_CPU 16000000UL
#include <Nrf24L01.h>
#include <Nrf24L01Registers.h>
#include <avr/io.h>
#include <stdbool.h>
#include <util/delay.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static void
enable_spi_for_nrf(void)
{
    DDRD |= (1 << 7);
    DDRB |= (1 << 2) | (1 << 3) | (1 << 5);
    //SPSR |= (1 << SPI2X);
    SPCR = (1 << MSTR) | (1 << SPE);
}

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
    SPDR = byte_to_send;
    while (!(SPSR & (1 << SPIF)));
    return SPDR;
}

static void
set_ce_pin_dfn_for_nrf(void* data, uint8_t value)
{
    (void)data;
    if(value) {
        PORTD |= (1 << 7);
        return;
    }
    PORTD &= ~(1 << 7);
}

static void
set_csn_pin_dfn_for_nrf(void* data, uint8_t value)
{
    (void)data;
    if (value) {
        PORTB |= (1 << 2);
        return;
    }
    PORTB &= ~(1 << 2);
}

static void
uart_init(unsigned int ubrr)
{
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr);
    const uint8_t disable_parity_conf = (0 << UPM00);
    const uint8_t async_mode_conf = (0 << UMSEL00);
    const uint8_t one_stop_bit_conf = (0 << USBS0);
    const uint8_t eigth_bit_data_conf = (3 << UCSZ00);
    UCSR0C =  disable_parity_conf | async_mode_conf | one_stop_bit_conf | eigth_bit_data_conf;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);
}

static void
uart_send_byte(uint8_t data)
{
    while(!(UCSR0A & (1 << UDRE0)));
    UDR0 = data;    
}
static void
uart_send_byte_seq(const uint8_t* data, uint8_t length)
{
    while(length) {
        uart_send_byte(*data);
        ++data;
        --length;
    }
}
static inline void
uart_send_str(const char* string)
{
    uart_send_byte_seq((const uint8_t*)string, strlen(string));
}

static inline uint8_t
uart_receive_byte()
{
    while(!(UCSR0A & (1 << RXC0)));
    return UDR0;   
}

static inline uint8_t
uart_receive_command(uint8_t* command, uint8_t buffer_length)
{
    uint8_t index = 0;
    while (index != buffer_length - 1) {
        uint8_t received_byte = uart_receive_byte();
        if (received_byte == '\r' || received_byte == '\n') {
            break;
        }
        command[index] = received_byte;
        index++;
    }
    command[index] = 0;
    return index;
}

static NrfHardwareInterface nrf_hw_iface = {
    .set_ce_pin = &set_ce_pin_dfn_for_nrf,
    .set_csn_pin = &set_csn_pin_dfn_for_nrf,
    .delay_us = &delay_us_dfn_for_nrf,
    .exchange_byte = &exchange_byte_dfn_for_nrf,
};

static const uint8_t address_to_write[] = "00002";
static const uint8_t address_to_read[] = "00001";
static const uint8_t address_length = 5;
static char buffer[33] = {};


#define UART_BAUDRATE_TO_UBRR(baud) ((F_CPU)/(16UL*(baud)) - 1)
#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#define STRING_STARTSWITH(var,literal) (strncmp(var, literal,ARR_SIZE(literal)-1) == 0)
int main(void)
{
    enable_spi_for_nrf();
    uart_init(UART_BAUDRATE_TO_UBRR(9600UL));
    NrfController* nrf_ctrl = nrf_controller_new(&nrf_hw_iface, NULL);
    nrf_controller_begin(nrf_ctrl);
    nrf_controller_set_ack_payloads(nrf_ctrl, NRF_CTRL_ACK_PAYLOAD_ENABLED);
    nrf_controller_open_writing_pipe(nrf_ctrl, address_to_write);
    nrf_controller_open_reading_pipe(nrf_ctrl, 1, address_to_read);
    uart_send_str("Waiting for commands\r\n");
    uart_send_str("Write command:\r\n");
    while (1) {
        uint8_t command_length = uart_receive_command((uint8_t*)buffer, ARR_SIZE(buffer)-1);
        if(command_length == 0) {
            continue;
        }
        uint8_t write_cycles = 35;
        bool has_write_succeed = false;
        while(write_cycles && !has_write_succeed) {
            nrf_controller_start_write(nrf_ctrl, (const uint8_t*)buffer, command_length);
            has_write_succeed = nrf_controller_finish_write_sync(nrf_ctrl);
            --write_cycles;
        }
        memset(buffer, 0 , command_length);
        bool has_available_ack = nrf_controller_is_message_available(nrf_ctrl, NRF_CTRL_ANY_PIPE);
        if(has_write_succeed && has_available_ack) {
            uint8_t payload_size = nrf_controller_get_dynamic_payload_size(nrf_ctrl);
            nrf_controller_read_incoming(nrf_ctrl, (uint8_t*)buffer, payload_size);
            uart_send_str(buffer);
            uart_send_str("\r\n");
            memset(buffer, 0, payload_size);
            
        } else {
            uart_send_str("ERROR\r\n");
            memset(buffer, 0, 33);
        }
        
    }
}

