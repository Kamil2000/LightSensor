
#ifndef NRF24L01_H_
#define NRF24L01_H_
#include <stdint.h>
#include <stdbool.h>


typedef struct NrfHardwareInterface
{
	uint8_t (*exchange_byte)(void*, uint8_t send);
	void (*set_ce_pin)(void*, uint8_t);
	void (*set_csn_pin)(void*, uint8_t);
	void (*delay_us)(void*, double);
} NrfHardwareInterface;

struct NrfController;
typedef struct NrfController NrfController;

typedef enum {
    NRF_CTRL_ACK_PAYLOAD_DISABLED = 0,
    NRF_CTRL_ACK_PAYLOAD_ENABLED = 1
} NrfCtrlAckPayloadState;

typedef enum {
    NRF_CTRL_DYNAMIC_PAYLOAD_DISABLED = 0,
    NRF_CTRL_DYNAMIC_PAYLOAD_ENABLED = 1
} NrfCtrlDynamicPayloadState;

#define NRF_CTRL_ANY_PIPE ((uint8_t*)0)

NrfController*
nrf_controller_new(NrfHardwareInterface* hw_iface, void* user_data);

uint8_t
nrf_controller_read_byte_register(NrfController* nrf, uint8_t register_val);

uint8_t
nrf_controller_read_register(NrfController* nrf, uint8_t register_val, uint8_t* buffer, uint8_t length);

uint8_t
nrf_controller_write_byte_register(NrfController* nrf, uint8_t register_val, uint8_t value);

uint8_t
nrf_controller_write_register(NrfController* nrf, uint8_t register_val, const uint8_t* buffer, uint8_t length);

uint8_t
nrf_controller_exec_byte_command(NrfController* nrf, uint8_t command);

uint8_t
nrf_controller_exec_byte_command_with_resp(NrfController* nrf, uint8_t command, uint8_t* resp);

void
nrf_controller_begin(NrfController* nrf);

void
nrf_controller_free(NrfController* controller);

uint8_t
nrf_controller_write_payload(NrfController* nrf, const uint8_t* buffer, uint8_t length);

uint8_t
nrf_controller_read_payload(NrfController* nrf, uint8_t* buffer, uint8_t length);

void
nrf_controller_start_write(NrfController* nrf, const uint8_t* buffer, uint8_t length);

bool
nrf_controller_finish_write_sync(NrfController* nrf);

void
nrf_controller_open_writing_pipe(NrfController* nrf,  const uint8_t* addr);

bool
nrf_controller_is_message_available(NrfController* nrf, uint8_t* pipe_number);

bool
nrf_controller_open_reading_pipe(NrfController* nrf, uint8_t child, const uint8_t* addr);

void
nrf_controller_close_reading_pipe(NrfController* nrf, uint8_t child);

void
nrf_controller_read_incoming(NrfController* nrf, uint8_t* buffer, uint8_t length);

void
nrf_controller_set_dynamic_payload(NrfController* nrf, NrfCtrlDynamicPayloadState is_enabled);

void
nrf_controller_set_ack_payloads(NrfController* nrf, NrfCtrlAckPayloadState is_enabled);

bool
nrf_controller_write_ack_payload(NrfController* nrf, uint8_t pipe, const uint8_t* buffer, uint8_t length);

uint8_t
nrf_controller_get_dynamic_payload_size(NrfController* nrf);

void
nrf_controller_start_listening(NrfController* nrf);

void
nrf_controller_stop_listening(NrfController* nrf);


#endif /* NRF24L01_H_ */