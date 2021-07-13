
#include "procedures.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <string.h>


static const char ok_resp[] = "OK";
static const char error_resp[] = "ERROR";
static const char na_resp[] = "NA";
static const char check_failed_resp[] = "CHECK_FAILED";

#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#define EEPROM_DATA_ADDR ((void*)0)

#pragma GCC push_options
#pragma GCC optimize ("O0")

#define ADC_CHANNEL_INTERNAL_VBG 0xE
#define ADC_CHANNEL_EXTERNAL_ADC3 3

static inline void
adc_set_channel(uint8_t channel)
{
    const uint8_t reference_avcc_pin = (1 << REFS0);
    const uint8_t channel_enable = (channel & 0x0F);
    ADMUX = reference_avcc_pin | channel_enable;
}

static inline void
adc_enable(uint8_t channel)
{
    PRR0 &= ~(1 << PRADC);
    adc_set_channel(channel);
    const uint8_t adc_prescaler_128 = (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    ADCSRA = (1 << ADEN) | adc_prescaler_128;
}

static inline void
adc_disable(void)
{
    ADCSRA &= ~(1 << ADEN);
}
static inline uint16_t
adc_read_value(void)
{
    ADCSRA |= (1 << ADSC);
    while(ADCSRA & (1 << ADSC));
    uint16_t result = 0;
    result |= ADCL;
    result |= (ADCH << 8);
    return result;
}

static inline void
prepare_next_resp(ProceduresData* data, const char* msg, uint8_t len)
{
    nrf_controller_write_ack_payload(data->nrf_ctrl, 1, (const uint8_t*)msg, len);
}

void
procedures_data_init(ProceduresData* data, NrfController* nrf_ctrl, char* buffer, uint8_t buff_len)
{
    data->nrf_ctrl = nrf_ctrl;
    eeprom_read_block(&data->calib_data, EEPROM_DATA_ADDR + offsetof(EepromData, calib_data), sizeof(data->calib_data));
    eeprom_read_block(&data->zero_data, EEPROM_DATA_ADDR + offsetof(EepromData, zero_data), sizeof(data->zero_data));
    eeprom_read_block(&data->internal_vol_data, EEPROM_DATA_ADDR + offsetof(EepromData, internal_vol_data), sizeof(data->internal_vol_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
    data->buffer = buffer;
    data->buffer_length = buff_len;
    data->proc_state = PROC_STATE_DEFAULT;
}

void
procedures_data_destroy(ProceduresData* data)
{
    memset(data, 0, sizeof(ProceduresData));
}

static inline void
move_to_state(ProceduresData* data, ProceduresState state)
{
    data->proc_state = state;
}

static void
procedures_handle_nop(ProceduresData* data, const char* incoming_data)
{
    switch(data->proc_state) {
        case PROC_STATE_GET_CALIB_DATA_RESP:
        case PROC_STATE_SET_CALIB_DATA_RESP:
        case PROC_STATE_SET_ZERO_DATA_RESP:
        case PROC_STATE_GET_ZERO_DATA_RESP:
            move_to_state(data, PROC_STATE_DEFAULT);
        case PROC_STATE_DEFAULT:
            prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
            break;
        default:
            break;
    }
    
}

static void
prepare_next_adc_value(ProceduresData* data)
{
    if(!data->calib_data.has_data || !data->zero_data.has_data) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    uint16_t adc_val = adc_read_value();
    int16_t value = data->zero_data.value + adc_val;
    double coeff = data->calib_data.value;
    double measured_value = coeff*value;
    memset(data->buffer, 0, data->buffer_length - 1);
    int count = snprintf(data->buffer, data->buffer_length, "%lf", measured_value);
    if(count <= 0 || count >= data->buffer_length) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, count);
}

static void
procedures_handle_start_measurement(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_MEASUREMENT);
    adc_enable(3);
    prepare_next_adc_value(data);
}

static void
procedures_handle_stop_measurement(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_MEASUREMENT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    adc_disable();
    move_to_state(data, PROC_STATE_DEFAULT);
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_get(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_MEASUREMENT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_adc_value(data);
}

static void
procedures_handle_get_meas_calib(ProceduresData* data, const char* incloming_data)
{
    (void)incloming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_GET_CALIB_DATA_RESP);
    const CalibData* const calib_data = &data->calib_data;
    if(!calib_data->has_data) {
        prepare_next_resp(data, na_resp, ARR_SIZE(na_resp) - 1);
        return;
    }
    memset(data->buffer, 0, data->buffer_length - 1);
    
    int chars_count = snprintf(data->buffer, data->buffer_length, "%lf", calib_data->value);
    //int chars_count = sprintf(data->buffer, "%lf", calib_data->value);
    if(chars_count >= data->buffer_length || chars_count <= 0) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, chars_count);
}

static void
procedures_handle_get_meas_zero(ProceduresData* data, const char* incloming_data)
{
    (void)incloming_data;

    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_GET_ZERO_DATA_RESP);
    const ZeroData* const zero_data = &data->zero_data;
    if(!zero_data->has_data) {
        prepare_next_resp(data, na_resp, ARR_SIZE(na_resp) - 1);
        return;
    }
    memset(data->buffer, 0, data->buffer_length - 1);
    int chars_count = snprintf(data->buffer, data->buffer_length, "%d", zero_data->value);
    if(chars_count >= data->buffer_length || chars_count <= 0) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, chars_count);
}

static inline uint8_t
get_semicolon_idx(const char* data)
{
    uint8_t semicolon_idx = 0;
    while(data[semicolon_idx] != ':') {semicolon_idx++;}
    return semicolon_idx;
}

static void
procedures_handle_set_meas_calib(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_SET_CALIB_DATA_RESP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    if(0 == strncmp(incoming_data + calib_coeff_idx, "NONE", ARR_SIZE("NONE") - 1)) {
        data->calib_data.has_data = false;
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    double calib_val = 0.0;
    int result = sscanf(&incoming_data[calib_coeff_idx],"%lf", &calib_val);
    if(1 != result) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }

    data->calib_data.has_data = true;
    data->calib_data.value = calib_val;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}
static void
procedures_handle_set_meas_zero(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_SET_ZERO_DATA_RESP);
    uint8_t zero_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    if(0 == strncmp(incoming_data + zero_coeff_idx, "NONE", ARR_SIZE("NONE") - 1)) {
        data->zero_data.has_data = false;
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    int zero_offset_val = 0;
    if(1 != sscanf(&incoming_data[zero_coeff_idx], "%d", &zero_offset_val)) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if (zero_offset_val < INT16_MIN || zero_offset_val > INT16_MAX) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    data->zero_data.has_data = true;
    data->zero_data.value = zero_offset_val;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_commit_meas_calib(ProceduresData* data, const char* incloming_data)
{
    (void)incloming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    CalibData calib_data;
    eeprom_read_block(&calib_data, EEPROM_DATA_ADDR + offsetof(EepromData, calib_data), sizeof(calib_data));
    if(!calib_data.has_data && !data->calib_data.has_data) {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    if(calib_data.has_data == data->calib_data.has_data && calib_data.value == data->calib_data.value)
    {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    eeprom_write_block(&data->calib_data, EEPROM_DATA_ADDR + offsetof(EepromData, calib_data), sizeof(data->calib_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_commit_meas_zero(ProceduresData* data, const char* incloming_data)
{
    (void)incloming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    ZeroData zero_data;
    eeprom_read_block(&zero_data, EEPROM_DATA_ADDR + offsetof(EepromData, zero_data), sizeof(zero_data));
    if(!zero_data.has_data && !data->zero_data.has_data) {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    if(zero_data.has_data == data->zero_data.has_data && zero_data.value == data->zero_data.value)
    {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    eeprom_write_block(&data->zero_data, EEPROM_DATA_ADDR + offsetof(EepromData, zero_data), sizeof(data->zero_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_calibrate_internal_voltage_ref(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    adc_enable(ADC_CHANNEL_INTERNAL_VBG);
    uint16_t result = 0;
    const uint8_t count_of_samples = 4;
    for(uint8_t i = 0; i != count_of_samples; i++) {
        result += adc_read_value();  
    }
    result /= count_of_samples;
    data->internal_vol_data.has_data = true;
    data->internal_vol_data.value = result;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
    adc_disable();
}
static void
procedures_handle_commit_internal_voltage_ref_calib(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    InternalVolData vol_data;
    eeprom_read_block(&vol_data, EEPROM_DATA_ADDR + offsetof(EepromData, internal_vol_data), sizeof(data->internal_vol_data));
    if(!vol_data.has_data && !data->internal_vol_data.has_data) {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    if(vol_data.has_data == data->internal_vol_data.has_data && vol_data.value == data->internal_vol_data.value) {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    eeprom_write_block(&data->internal_vol_data, EEPROM_DATA_ADDR + offsetof(EepromData, internal_vol_data), sizeof(data->internal_vol_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}


static void
procedures_handle_check_internal_voltage_ref(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(!data->internal_vol_data.has_data) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    adc_enable(ADC_CHANNEL_INTERNAL_VBG);
    uint16_t adc_result = adc_read_value();
    if (adc_result > data->internal_vol_data.value + 4) {
        prepare_next_resp(data, check_failed_resp, ARR_SIZE(check_failed_resp) - 1);
    } else {
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
    }
    adc_disable();
    
}


typedef void (*HandlerFunc)(ProceduresData*, const char*);
struct HandlerDescription
{
    char* command;
    uint8_t cmd_length;
    HandlerFunc handler;
};

#define MAKE_HANDLER_DESCR(command, handler) {(command), sizeof(command)/sizeof(char) - 1, handler}

const static struct HandlerDescription handler_descriptions[] = {
    MAKE_HANDLER_DESCR("nop", &procedures_handle_nop),
    MAKE_HANDLER_DESCR("start_meas", &procedures_handle_start_measurement),
    MAKE_HANDLER_DESCR("stop_meas", &procedures_handle_stop_measurement),
    MAKE_HANDLER_DESCR("get_val", &procedures_handle_get),
    MAKE_HANDLER_DESCR("get_calib", &procedures_handle_get_meas_calib),
    MAKE_HANDLER_DESCR("set_calib:", &procedures_handle_set_meas_calib),
    MAKE_HANDLER_DESCR("commit_calib", &procedures_handle_commit_meas_calib),
    MAKE_HANDLER_DESCR("get_zero", &procedures_handle_get_meas_zero),
    MAKE_HANDLER_DESCR("set_zero:", &procedures_handle_set_meas_zero),
    MAKE_HANDLER_DESCR("commit_zero", &procedures_handle_commit_meas_zero),
    MAKE_HANDLER_DESCR("calib_int_ref", &procedures_handle_calibrate_internal_voltage_ref),
    MAKE_HANDLER_DESCR("check_int_ref", &procedures_handle_check_internal_voltage_ref),
    MAKE_HANDLER_DESCR("commit_int_ref_calib", &procedures_handle_commit_internal_voltage_ref_calib)
};

void procedures_handle_incoming_message(ProceduresData* data, const char* incoming_message)
{
    uint8_t handler_idx = 0;
    while(handler_idx != ARR_SIZE(handler_descriptions)) {
        const struct HandlerDescription* handler_descr = &handler_descriptions[handler_idx];
        if(0 == strncmp(handler_descr->command, incoming_message, handler_descr->cmd_length)) {
            handler_descr->handler(data, incoming_message);
            return;
        }
        ++handler_idx;
    }
    memset(data->buffer, 0, data->buffer_length - 1);
}

#pragma GCC pop_options