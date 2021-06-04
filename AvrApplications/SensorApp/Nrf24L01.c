#include "Nrf24L01Registers.h"
#include "Nrf24L01.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

struct NrfController
{
    NrfHardwareInterface* hw_iface;
    void* hw_iface_udata;
    bool ack_payload_enabled;
    bool dynamic_payloads_enabled;
    bool wide_band;
    uint8_t payload_size;

};

static const uint8_t child_pipes[] = {
    NRF_RX_ADDR_P0_REG,
    NRF_RX_ADDR_P1_REG,
    NRF_RX_ADDR_P2_REG,
    NRF_RX_ADDR_P3_REG,
    NRF_RX_ADDR_P4_REG,
    NRF_RX_ADDR_P5_REG,
};

static const uint8_t child_pipe_enablers[] = {
    NRF_EN_RXADDR_BIT_ERX_P0,
    NRF_EN_RXADDR_BIT_ERX_P1,
    NRF_EN_RXADDR_BIT_ERX_P2,
    NRF_EN_RXADDR_BIT_ERX_P3,
    NRF_EN_RXADDR_BIT_ERX_P4,
    NRF_EN_RXADDR_BIT_ERX_P5,
};

const static uint8_t rx_payload_width_registers[] = {
    NRF_RX_PW_P0_REG,
    NRF_RX_PW_P1_REG,
    NRF_RX_PW_P2_REG,
    NRF_RX_PW_P3_REG,
    NRF_RX_PW_P4_REG,
    NRF_RX_PW_P5_REG
};

static const uint8_t maximum_allowed_pipe_num = sizeof(child_pipes)/sizeof(child_pipes[0]);

#pragma GCC push_options
#pragma GCC optimize ("O0")

static inline void
set_csn_pin(NrfController* nrf, uint8_t value)
{
    nrf->hw_iface->set_csn_pin(nrf->hw_iface_udata, value);
}

static inline void
set_ce_pin(NrfController* nrf, uint8_t value)
{
    nrf->hw_iface->set_ce_pin(nrf->hw_iface_udata, value);
}

static inline uint8_t
exchange_byte(NrfController* nrf, uint8_t byte_value)
{
    return nrf->hw_iface->exchange_byte(nrf->hw_iface_udata, byte_value);
}

static inline void
delay_us(NrfController* nrf, double delay)
{
    nrf->hw_iface->delay_us(nrf->hw_iface_udata, delay);
}

NrfController*
nrf_controller_new(NrfHardwareInterface* low_level_interface, void* user_data)
{
    NrfController* result = calloc(1,sizeof(NrfController));
    result->hw_iface = low_level_interface;
    result->hw_iface_udata = user_data;
    result->ack_payload_enabled = false;
    result->dynamic_payloads_enabled = false;
    result->payload_size = 32;
    return result;
    
}

uint8_t
nrf_controller_read_byte_register(NrfController* nrf, uint8_t register_val)
{
    set_csn_pin(nrf, 0);
    exchange_byte(nrf, NRF_R_REGISTER | ( NRF_REGISTER_MASK & register_val));
    uint8_t result = exchange_byte(nrf, NRF_NOP_INST);
    set_csn_pin(nrf, 1);
    return result;
}


uint8_t
nrf_controller_read_register(NrfController* nrf, uint8_t register_val, uint8_t* buffer, uint8_t length)
{
    set_csn_pin(nrf, 0);
    uint8_t status = exchange_byte(nrf, NRF_R_REGISTER | (NRF_REGISTER_MASK & register_val ));
    while (length--) {
        *buffer = exchange_byte(nrf, NRF_NOP_INST);
        ++buffer;
    }
    set_csn_pin(nrf, 1);
    return status;
}

uint8_t
nrf_controller_write_byte_register(NrfController* nrf, uint8_t register_val, uint8_t value)
{
    set_csn_pin(nrf, 0);
    uint8_t status = exchange_byte(nrf, NRF_W_REGISTER | (NRF_REGISTER_MASK & register_val));
    exchange_byte(nrf, value);
    set_csn_pin(nrf, 1);
    return status;
}

uint8_t
nrf_controller_write_register(NrfController* nrf, uint8_t register_val, const uint8_t* buffer, uint8_t length)
{
    set_csn_pin(nrf, 0);
    uint8_t	status = exchange_byte(nrf, NRF_W_REGISTER | (NRF_REGISTER_MASK & register_val));
    while (length--) {
        exchange_byte(nrf, *buffer);
        ++buffer;
    }
    set_csn_pin(nrf, 1);
    return status;
}


uint8_t
nrf_controller_exec_byte_command(NrfController* nrf, uint8_t command)
{
    set_csn_pin(nrf, 0);
    uint8_t status = exchange_byte(nrf, command);
    set_csn_pin(nrf, 1);
    return status;
}

uint8_t
nrf_controller_exec_byte_command_with_resp(NrfController* nrf, uint8_t command, uint8_t* resp)
{
    set_csn_pin(nrf, 0);
    uint8_t status = exchange_byte(nrf, command);
    *resp = exchange_byte(nrf, NRF_NOP_INST);
    set_csn_pin(nrf, 1);
    return status;
}

void
nrf_controller_begin(NrfController* nrf)
{
    set_csn_pin(nrf, 1);
    set_ce_pin(nrf, 0);
    // delay measured for nrf24l01+
    delay_us(nrf, 5000);
    uint8_t config_reg = nrf_controller_read_byte_register(nrf, NRF_CONFIG_REG);
    config_reg &= ~(1 << NRF_CONFIG_BIT_PWR_UP); // proposed fix - clear power up flag
    nrf_controller_write_byte_register(nrf, NRF_CONFIG_REG, config_reg);
    // retransmissions setup
    uint8_t retr_settings = 0x0;
    const uint8_t max_retr_count_on_fail = 15;
    const uint8_t retr_delay_2250_us = 0x8;
    retr_settings |= (max_retr_count_on_fail << NRF_SETUP_RETR_BIT_ARC);
    retr_settings |= (retr_delay_2250_us << NRF_SETUP_RETR_BIT_ARD);
    nrf_controller_write_byte_register(nrf, NRF_SETUP_RETR_REG, retr_settings);
    // power tx configuration
    uint8_t rf_setup = nrf_controller_read_byte_register(nrf, NRF_RF_SETUP_REG);
    const uint8_t max_power = 0x3;
    const uint8_t speed_1mbps = ~(1 << NRF_RF_SETUP_BIT_RF_DR);
    rf_setup |= (max_power << NRF_RF_SETUP_BIT_RF_PWR);
    rf_setup &= speed_1mbps;
    nrf_controller_write_byte_register(nrf, NRF_RF_SETUP_REG, rf_setup);
    // crc settings
    config_reg = nrf_controller_read_byte_register(nrf, NRF_CONFIG_REG);
    config_reg |= (1 << NRF_CONFIG_BIT_CRCO);
    config_reg |= (1 << NRF_CONFIG_BIT_EN_CRC);
    nrf_controller_write_byte_register(nrf, NRF_CONFIG_REG, config_reg);
    // disable dynamic payload
    nrf_controller_write_byte_register(nrf, NRF_DYNPD_REG, 0x0);
    //clear flags
    uint8_t status_reg = 0x0;
    status_reg |= (1 << NRF_STATUS_BIT_RX_DR);
    status_reg |= (1 << NRF_STATUS_BIT_TX_DS);
    status_reg |= (1 << NRF_STATUS_BIT_MAX_RT);
    nrf_controller_write_byte_register(nrf, NRF_STATUS_REG, status_reg);

    nrf_controller_write_byte_register(nrf, NRF_RF_CH_REG, 76);
    nrf_controller_write_byte_register(nrf, NRF_EN_AA_REG, 0x3F);

    nrf_controller_exec_byte_command(nrf, NRF_FLUSH_RX_INST);
    nrf_controller_exec_byte_command(nrf, NRF_FLUSH_TX_INST);
}

static inline void
calculate_payload_lengths(NrfController* nrf, uint8_t requested_length, uint8_t* data_len, uint8_t* blank_len)
{
    *data_len = (requested_length > nrf->payload_size) ? nrf->payload_size : requested_length;
    *blank_len = nrf->dynamic_payloads_enabled ? 0 : nrf->payload_size - *data_len;
}


uint8_t
nrf_controller_write_payload(NrfController* nrf, const uint8_t* buffer, uint8_t length)
{
    uint8_t data_len, blank_len;
    calculate_payload_lengths(nrf, length, &data_len, &blank_len);
    set_csn_pin(nrf, 0);
    uint8_t status = exchange_byte(nrf, NRF_W_TX_PAYLOAD_INST);
    while (data_len--) {
        exchange_byte(nrf, *buffer);
        ++buffer;
    }
    while (blank_len--) {
        exchange_byte(nrf, 0);
    }
    set_csn_pin(nrf, 1);
    return status;
}

uint8_t
nrf_controller_read_payload(NrfController* nrf, uint8_t* buffer, uint8_t length)
{
    uint8_t data_len, blank_len;
    calculate_payload_lengths(nrf, length, &data_len, &blank_len);
    set_csn_pin(nrf, 0);
    uint8_t status = exchange_byte(nrf, NRF_R_RX_PAYLOAD_INST);
    while (data_len--) {
        *buffer = exchange_byte(nrf, NRF_NOP_INST);
        ++buffer;
    }
    while (blank_len--) {
        exchange_byte(nrf, NRF_NOP_INST);
    }
    set_csn_pin(nrf, 1);
    return status;
}

void
nrf_controller_start_write(NrfController* nrf, const uint8_t* buffer, uint8_t length)
{
    uint8_t config = nrf_controller_read_byte_register(nrf, NRF_CONFIG_REG);
    config |= (1 << NRF_CONFIG_BIT_PWR_UP);
    config &= ~(1 << NRF_CONFIG_BIT_PRIM_RX);
    nrf_controller_write_byte_register(nrf, NRF_CONFIG_REG, config);
    delay_us(nrf, 150);
    nrf_controller_write_payload(nrf, buffer, length);
    
    set_ce_pin(nrf, 1);
}


bool
nrf_controller_finish_write_sync(NrfController* nrf)
{
    uint16_t attempts_count = 9000;
    while(attempts_count) {
        uint8_t observe_tx;
        uint8_t status = nrf_controller_read_register(nrf, NRF_OBSERVE_TX_REG, &observe_tx, 1);
        if ( status & ((1 << NRF_STATUS_BIT_TX_DS) | (1 << NRF_STATUS_BIT_MAX_RT))) {
            break;
        }
        delay_us(nrf, 2);
        --attempts_count;
    }
    set_ce_pin(nrf, 0);
    const uint8_t status_res_val = (1 << NRF_STATUS_BIT_MAX_RT) 
            | (1 << NRF_STATUS_BIT_TX_DS);
    uint8_t status = nrf_controller_write_byte_register(nrf, NRF_STATUS_REG, status_res_val);
    bool is_transmisstion_ok = ( status & (1 << NRF_STATUS_BIT_TX_DS) && (attempts_count != 0));
    uint8_t config = nrf_controller_read_byte_register(nrf, NRF_CONFIG_REG);
    config &= ~(1 << NRF_CONFIG_BIT_PWR_UP);
    nrf_controller_write_byte_register(nrf, NRF_CONFIG_REG, config);
    nrf_controller_exec_byte_command(nrf, NRF_FLUSH_TX_INST);
    return is_transmisstion_ok;
}

void
nrf_controller_open_writing_pipe(NrfController* nrf,  const uint8_t* addr)
{
    nrf_controller_write_register(nrf, NRF_RX_ADDR_P0_REG, addr, 5);
    nrf_controller_write_register(nrf, NRF_TX_ADDR_REG, addr, 5);
    const uint8_t used_payload_size = (nrf->payload_size > 32) ? 32 : nrf->payload_size;
    nrf_controller_write_byte_register(nrf, NRF_RX_PW_P0_REG, used_payload_size);
}

bool
nrf_controller_is_message_available(NrfController* nrf, uint8_t* pipe_number)
{
    uint8_t status = nrf_controller_exec_byte_command(nrf, NRF_NOP_INST);
    if (!( status & (1 << NRF_STATUS_BIT_RX_DR))) {
        return false;
    }
    uint8_t pipe_number_received = ((status >> NRF_STATUS_BIT_RX_P_NO) & 0x07);
    if (pipe_number_received >= maximum_allowed_pipe_num ) {
        return false;
    }
    if(pipe_number != NULL) {
        *pipe_number = pipe_number_received;
    }
    return true;
}

bool 
nrf_controller_open_reading_pipe(NrfController* nrf, uint8_t child, const uint8_t* addr)
{
    if (child >= maximum_allowed_pipe_num) {
        return false;
    }
    uint8_t length = (child < 2)? 5 : 1;
    nrf_controller_write_register(nrf, child_pipes[child], addr, length);
    uint8_t rxaddr_enabled = nrf_controller_read_byte_register(nrf, NRF_EN_RXADDR_REG);
    rxaddr_enabled |= (1 << child_pipe_enablers[child]);
    nrf_controller_write_byte_register(nrf, NRF_EN_RXADDR_REG, rxaddr_enabled);
    const uint8_t used_payload_size = (nrf->payload_size > 32) ? 32 : nrf->payload_size;
    nrf_controller_write_byte_register(nrf, rx_payload_width_registers[child], used_payload_size);
    return true;
}

void 
nrf_controller_close_reading_pipe(NrfController* nrf, uint8_t child)
{
    uint8_t rxaddr_enabled = nrf_controller_read_byte_register(nrf, NRF_EN_RXADDR_REG);
    rxaddr_enabled &= ~(1 << child_pipe_enablers[child]);
    nrf_controller_write_byte_register(nrf, NRF_EN_RXADDR_REG, rxaddr_enabled);
}

void
nrf_controller_read_incoming(NrfController* nrf, uint8_t* buffer, uint8_t length)
{
    nrf_controller_read_payload(nrf, buffer, length);
    nrf_controller_write_byte_register(nrf, NRF_STATUS_REG, (1 << NRF_STATUS_BIT_RX_DR));
    
}
void 
nrf_controller_set_dynamic_payload(NrfController* nrf, NrfCtrlDynamicPayloadState is_enabled)
{
    if (is_enabled && !nrf->dynamic_payloads_enabled) {
        uint8_t feature_reg = nrf_controller_read_byte_register(nrf, NRF_FEATURE_REG);
        feature_reg |= (1 << NRF_FEATURE_BIT_EN_DPL);
        nrf->dynamic_payloads_enabled = true;
        nrf_controller_write_byte_register(nrf, NRF_FEATURE_REG, feature_reg);
        uint8_t dynpd = nrf_controller_read_byte_register(nrf, NRF_DYNPD_REG);
        dynpd |= (1 << NRF_DYNPD_BIT_DPL_P0);
        dynpd |= (1 << NRF_DYNPD_BIT_DPL_P1);
        dynpd |= (1 << NRF_DYNPD_BIT_DPL_P2);
        dynpd |= (1 << NRF_DYNPD_BIT_DPL_P3);
        dynpd |= (1 << NRF_DYNPD_BIT_DPL_P4);
        dynpd |= (1 << NRF_DYNPD_BIT_DPL_P5);
        nrf_controller_write_byte_register(nrf, NRF_DYNPD_REG, dynpd);
    }
    if (!is_enabled && nrf->dynamic_payloads_enabled) {
        uint8_t feature_reg = nrf_controller_read_byte_register(nrf, NRF_FEATURE_REG);
        feature_reg = 0; // disabling dynamic payloads disables also ack payloads
        nrf->dynamic_payloads_enabled = false;
        nrf->ack_payload_enabled = false;
        nrf_controller_write_byte_register(nrf, NRF_FEATURE_REG, feature_reg);
        nrf_controller_write_byte_register(nrf, NRF_DYNPD_REG, 0);
    }
}

void 
nrf_controller_set_ack_payloads(NrfController* nrf, NrfCtrlAckPayloadState is_enabled)
{
    if(is_enabled && !nrf->ack_payload_enabled) {
        if(!nrf->dynamic_payloads_enabled) {
            nrf_controller_set_dynamic_payload(nrf, true);
        }
        uint8_t feature_reg = nrf_controller_read_byte_register(nrf, NRF_FEATURE_REG);
        feature_reg |= (1 << NRF_FEATURE_BIT_EN_ACK_PAY);
        nrf_controller_write_byte_register(nrf, NRF_FEATURE_REG, feature_reg);
        nrf->ack_payload_enabled = true;
    }
    if(!is_enabled && nrf->ack_payload_enabled) {
        uint8_t feature_reg = nrf_controller_read_byte_register(nrf, NRF_FEATURE_REG);
        feature_reg &= ~(1 << NRF_FEATURE_BIT_EN_ACK_PAY);
        nrf_controller_write_byte_register(nrf, NRF_FEATURE_REG, feature_reg);
    }
}

bool
nrf_controller_write_ack_payload(NrfController* nrf, uint8_t pipe, const uint8_t* buffer, uint8_t length)
{
    if(! nrf->ack_payload_enabled) {
        return false;
    }
    if (pipe >= maximum_allowed_pipe_num) {
        return false;
    }
    uint8_t data_len, blank_len;
    calculate_payload_lengths(nrf, length, &data_len, &blank_len);
    set_csn_pin(nrf, 0);
    exchange_byte(nrf, NRF_W_ACK_PAYLOAD_INST | (pipe & 0x07));
    while (data_len--) {
        exchange_byte(nrf, *buffer);
        ++buffer;
    }
    while (blank_len--) {
        exchange_byte(nrf, 0);
    }
    set_csn_pin(nrf, 1);
    return true;
}

uint8_t
nrf_controller_get_dynamic_payload_size(NrfController* nrf)
{
    if (!nrf->dynamic_payloads_enabled) {
        return 0;
    }
    uint8_t payload_len;
    nrf_controller_exec_byte_command_with_resp(nrf, NRF_R_RX_PL_WID_INST, &payload_len);
    if (payload_len > 32) {
        nrf_controller_exec_byte_command(nrf, NRF_FLUSH_RX_INST);
        delay_us(nrf, 2000);
        return 0;
    }
    return payload_len;
}

void
nrf_controller_start_listening(NrfController* nrf)
{
    uint8_t config = nrf_controller_read_byte_register(nrf, NRF_CONFIG_REG);
    config |= (1 << NRF_CONFIG_BIT_PWR_UP);
    config |= (1 << NRF_CONFIG_BIT_PRIM_RX);
    nrf_controller_write_byte_register(nrf, NRF_CONFIG_REG, config);
    uint8_t reset_status_flags = (1 << NRF_STATUS_BIT_RX_DR)|(1 << NRF_STATUS_BIT_MAX_RT)|(1 << NRF_STATUS_BIT_TX_DS);
    nrf_controller_write_byte_register(nrf, NRF_STATUS_REG, reset_status_flags);
    set_ce_pin(nrf, 1);
}

void
nrf_controller_stop_listening(NrfController* nrf)
{
    set_ce_pin(nrf, 0);
    delay_us(nrf, 100);
    if(nrf->ack_payload_enabled) {
        nrf_controller_exec_byte_command(nrf, NRF_FLUSH_TX_INST);
    }
    uint8_t config = nrf_controller_read_byte_register(nrf, NRF_CONFIG_REG);
    config &= ~(1 << NRF_CONFIG_BIT_PWR_UP);
    config &= ~(1 << NRF_CONFIG_BIT_PRIM_RX);
    nrf_controller_write_byte_register(nrf, NRF_CONFIG_REG, config);
    // do we need to restore some EN_RXADDR
}


void
nrf_controller_free(NrfController* controller)
{
    free(controller);
}

#pragma GCC pop_options
