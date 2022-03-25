
#include "procedures.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char ok_resp[] = "OK";
static const char error_resp[] = "ERROR";
static const char int_vol_failure_resp[] = "INT_VOL_FAILURE";
static const char true_resp[] = "TRUE";
static const char false_resp[] = "FALSE";
static const char out_of_range_resp[] = "OUT_OF_RANGE";
static const char no_conf_selected_resp[] = "NO_CONF_SELECTED";
static const char meas_errors_cleared_resp[] = "MEAS_ERRORS_CLEARED";

static const char none_value[] = "NONE";

#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#define EEPROM_DATA_ADDR ((void*)0)

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

static inline uint16_t
adc_read_oversampled_value(uint8_t samples_count)
{
    uint16_t result = 0;
    for(uint8_t i = 0; i != samples_count; i++) {
        result += adc_read_value();
    }
    result /= samples_count;
    return result;
}

inline static bool
procedures_is_internal_voltage_high_enough(ProceduresData* data)
{
    adc_set_channel(ADC_CHANNEL_INTERNAL_VBG);
    uint16_t adc_result = adc_read_value();
    adc_set_channel(ADC_CHANNEL_EXTERNAL_ADC3);
    return adc_result <= data->internal_vol_data.value + 4;
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
    eeprom_read_block(data->calib_data, EEPROM_DATA_ADDR + offsetof(EepromData, calib_data), sizeof(data->calib_data));
    eeprom_read_block(&data->internal_vol_data, EEPROM_DATA_ADDR + offsetof(EepromData, internal_vol_data), sizeof(data->internal_vol_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
    data->buffer = buffer;
    data->buffer_length = buff_len;
    data->proc_state = PROC_STATE_DEFAULT;
    data->measure_int_vol_counter = -1;
    data->selected_conf = UINT8_MAX;
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
        case PROC_STATE_AWAIT_NOP:
            move_to_state(data, PROC_STATE_DEFAULT);
        case PROC_STATE_DEFAULT:
            prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
            break;
        default:
            break;
    }
}

static inline uint8_t
get_semicolon_idx(const char* data)
{
    uint8_t semicolon_idx = 0;
    while(data[semicolon_idx] != ':') {semicolon_idx++;}
    return semicolon_idx;
}

static bool inline
procedures_is_selected_calib_data_valid(ProceduresData* data)
{
    CalibData* calib_data = &data->calib_data[data->selected_conf];
    if(!(calib_data->flags & CALIB_DATA_GAIN_ERROR_PRESENT)) {
        return false;
    }
    if(!(calib_data->flags & CALIB_DATA_ZERO_ERROR_PRESENT)) {
        return false;
    }
    if(!(calib_data->flags & CALIB_DATA_WAVELENGTH_PRESENT)) {
        return false;
    }
    return true;
}

static void
prepare_next_adc_value(ProceduresData* data)
{
    if(data->is_measurement_error) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    CalibData* calib_data = &data->calib_data[data->selected_conf];
    if(data->measure_int_vol_counter != -1) {
        if(data->measure_int_vol_counter == 0 && !procedures_is_internal_voltage_high_enough(data)) {
            prepare_next_resp(data, int_vol_failure_resp, ARR_SIZE(int_vol_failure_resp));
            data->is_measurement_error = true;
            return;
        }
        data->measure_int_vol_counter = (data->measure_int_vol_counter == 0) ?
            10 : (data->measure_int_vol_counter - 1);
    }
    uint16_t adc_val = adc_read_value();
    int16_t value = calib_data->zero_error + adc_val;
    double coeff = calib_data->gain_error;
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
procedures_handle_meas_start(ProceduresData* data, const char* incoming_data)
{
    data->is_measurement_error = false;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        data->is_measurement_error = true;
        return;
    }
    move_to_state(data, PROC_STATE_MEASUREMENT);
    if(data->selected_conf == UINT8_MAX) {
        prepare_next_resp(data, no_conf_selected_resp, ARR_SIZE(no_conf_selected_resp) - 1);
        data->is_measurement_error = true;
        return;
    }
    if (!procedures_is_selected_calib_data_valid(data)) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        data->is_measurement_error = true;
        return;
    }

    adc_enable(ADC_CHANNEL_EXTERNAL_ADC3);
    prepare_next_adc_value(data);
}

static void
procedures_handle_meas_stop(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_MEASUREMENT) {
        move_to_state(data, PROC_STATE_DEFAULT);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_DEFAULT);
    adc_disable();
    data->measure_int_vol_counter = -1;
    if(data->is_measurement_error) {
        data->is_measurement_error = false;
        prepare_next_resp(data, meas_errors_cleared_resp, ARR_SIZE(meas_errors_cleared_resp) - 1);
        return;
    }
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_meas_get_val(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_MEASUREMENT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_adc_value(data);
}

static void
procedures_handle_conf_set_gain_error(ProceduresData* data, const char* incloming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t first_arg_idx = get_semicolon_idx(incloming_data) + 1;
    char* second_arg = NULL;
    uint8_t calib_index = strtol(&incloming_data[first_arg_idx], &second_arg, 10);
    if(second_arg == NULL || second_arg[0] != ':') {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if (calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }

    ++second_arg;
    if(0 == strcmp(none_value, second_arg)) {
        data->calib_data[calib_index].flags &= ~CALIB_DATA_GAIN_ERROR_PRESENT;
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    char* end_of_args = NULL;
    double value = strtod(second_arg, &end_of_args);
    if (!end_of_args) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    data->calib_data[calib_index].flags |= CALIB_DATA_GAIN_ERROR_PRESENT;
    data->calib_data[calib_index].gain_error = value;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) -1);
}

static void
procedures_handle_conf_set_zero_error(ProceduresData* data, const char* incloming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t first_arg_idx = get_semicolon_idx(incloming_data) + 1;
    char* second_arg = NULL;
    uint8_t calib_index = strtol(&incloming_data[first_arg_idx], &second_arg, 10);
    if(second_arg == NULL || second_arg[0] != ':') {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if (calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    ++second_arg;
    if(0 == strcmp(none_value, second_arg)) {
        data->calib_data[calib_index].flags &= ~CALIB_DATA_ZERO_ERROR_PRESENT;
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    char* end_of_args = NULL;
    long value = strtol(second_arg, &end_of_args, 10);
    if (!end_of_args || value > INT16_MAX || value < INT16_MIN) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    data->calib_data[calib_index].flags |= CALIB_DATA_ZERO_ERROR_PRESENT;
    data->calib_data[calib_index].zero_error = value;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) -1);
}

static void
procedures_handle_conf_set_wavelength(ProceduresData* data, const char* incloming_data)
{
    
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t first_arg_idx = get_semicolon_idx(incloming_data) + 1;
    char* second_arg = NULL;
    uint8_t calib_index = strtol(&incloming_data[first_arg_idx], &second_arg, 10);
    if(second_arg == NULL || second_arg[0] != ':') {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if (calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    ++second_arg;
    if(0 == strcmp(none_value, second_arg)) {
        data->calib_data[calib_index].flags &= ~CALIB_DATA_WAVELENGTH_PRESENT;
        prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
        return;
    }
    char* end_of_args = NULL;
    uint16_t value = strtol(second_arg, &end_of_args, 10);
    if (!end_of_args || value > UINT16_MAX || value < 0) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    data->calib_data[calib_index].flags |= CALIB_DATA_WAVELENGTH_PRESENT;
    data->calib_data[calib_index].wavelength = value;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) -1);
}

static void
procedures_handle_conf_get_gain_error(ProceduresData* data, const char* incoming_data)
{
    
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    char* end_of_string = NULL;
    uint8_t calib_index = strtol(&incoming_data[calib_coeff_idx], &end_of_string, 10);
    if(!end_of_string) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    memset(data->buffer, 0, data->buffer_length - 1);
    if (!(data->calib_data[calib_index].flags & CALIB_DATA_GAIN_ERROR_PRESENT)) {
        prepare_next_resp(data, none_value, ARR_SIZE(none_value) - 1);
        return;
    }
    double value = data->calib_data[calib_index].gain_error;
    int count = snprintf(data->buffer, data->buffer_length, "%lf", value);
    if (count <= 0 || count >= data->buffer_length) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, count);
}

static void
procedures_handle_conf_get_zero_error(ProceduresData* data, const char* incoming_data)
{
    
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    char* end_of_string = NULL;
    uint8_t calib_index = strtol(&incoming_data[calib_coeff_idx], &end_of_string, 10);
    if(!end_of_string) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    memset(data->buffer, 0, data->buffer_length - 1);
    if (!(data->calib_data[calib_index].flags & CALIB_DATA_ZERO_ERROR_PRESENT)) {
        prepare_next_resp(data, none_value, ARR_SIZE(none_value) - 1);
        return;
    }
    int16_t value = data->calib_data[calib_index].zero_error;
    int count = snprintf(data->buffer, data->buffer_length, "%d", (int)value);
    if (count <= 0 || count >= data->buffer_length) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, count);
}

static void
procedures_handle_conf_measure_zero_error(ProceduresData* data, const char* incoming_data)
{
    
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    char* end_of_string = NULL;
    uint8_t calib_index = strtol(&incoming_data[calib_coeff_idx], &end_of_string, 10);
    if(!end_of_string) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    adc_enable(ADC_CHANNEL_EXTERNAL_ADC3);
    data->calib_data[calib_index].zero_error = -adc_read_oversampled_value(4);
    data->calib_data[calib_index].flags |= CALIB_DATA_ZERO_ERROR_PRESENT;
    adc_disable();
    memset(data->buffer, 0, data->buffer_length - 1);
    int16_t value = data->calib_data[calib_index].zero_error;
    int count = snprintf(data->buffer, data->buffer_length, "%d", (int)value);
    if (count <= 0 || count >= data->buffer_length) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, count);
}

static void
procedures_handle_conf_get_wavelength(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    char* end_of_string = NULL;
    uint8_t calib_index = strtol(&incoming_data[calib_coeff_idx], &end_of_string, 10);
    if(!end_of_string) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    memset(data->buffer, 0, data->buffer_length - 1);
    if (!(data->calib_data[calib_index].flags & CALIB_DATA_WAVELENGTH_PRESENT)) {
        prepare_next_resp(data, none_value, ARR_SIZE(none_value) - 1);
        return;
    }
    uint16_t value = data->calib_data[calib_index].wavelength;
    int count = snprintf(data->buffer, data->buffer_length, "%u", (unsigned)value);
    if (count <= 0 || count >= data->buffer_length) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    prepare_next_resp(data, data->buffer, count);
}

static void
procedures_handle_conf_select(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    char* end_of_string = NULL;
    uint8_t calib_index = strtol(&incoming_data[calib_coeff_idx], &end_of_string, 10);
    if(!end_of_string) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    data->selected_conf = calib_index;
    prepare_next_resp(data, ok_resp, ARR_SIZE(error_resp) - 1);
}

static void
procedures_handle_conf_commit(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    uint8_t calib_coeff_idx = get_semicolon_idx(incoming_data) + 1;
    char* end_of_string = NULL;
    uint8_t calib_index = strtol(&incoming_data[calib_coeff_idx], &end_of_string, 10);
    if(!end_of_string) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(calib_index >= CALIB_DATA_ELEMENTS_COUNT) {
        prepare_next_resp(data, out_of_range_resp, ARR_SIZE(out_of_range_resp) - 1);
        return;
    }
    CalibData* calib_data = &data->calib_data[calib_index];
    ptrdiff_t offset = offsetof(EepromData, calib_data) + sizeof(*calib_data) * calib_index;
    eeprom_write_block(calib_data, EEPROM_DATA_ADDR + offset, sizeof(*calib_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(error_resp) - 1);
}

static void
procedures_handle_int_ref_enable(ProceduresData* data, const char* incoming_data)
{
    
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    if(!data->internal_vol_data.has_data) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    data->measure_int_vol_counter = 0;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_int_ref_calibrate(ProceduresData* data, const char* incoming_data)
{
    
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    adc_enable(ADC_CHANNEL_INTERNAL_VBG);
    data->internal_vol_data.has_data = true;
    data->internal_vol_data.value = adc_read_oversampled_value(4);
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
    adc_disable();
}

static void
procedures_handle_int_ref_clear(ProceduresData* data, const char* incoming_data)
{
    if(data->proc_state != PROC_STATE_DEFAULT) {
        move_to_state(data, PROC_STATE_AWAIT_NOP);
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    move_to_state(data, PROC_STATE_AWAIT_NOP);
    data->internal_vol_data.has_data = false;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}


static void
procedures_handle_int_ref_commit(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    eeprom_write_block(&data->internal_vol_data, EEPROM_DATA_ADDR + offsetof(EepromData, internal_vol_data), sizeof(data->internal_vol_data));
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
}

static void
procedures_handle_int_ref_is_calibrated(ProceduresData* data, const char* incoming_data)
{
    (void)incoming_data;
    if(data->proc_state != PROC_STATE_DEFAULT) {
        prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
        return;
    }
    if(data->internal_vol_data.has_data) {
        prepare_next_resp(data, true_resp, ARR_SIZE(true_resp) - 1);
        return;
    }
    prepare_next_resp(data, false_resp, ARR_SIZE(false_resp) - 1);
}

static void
procedures_handle_int_ref_disable(ProceduresData* data, const char* incoming_data)
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
    data->measure_int_vol_counter = -1;
    prepare_next_resp(data, ok_resp, ARR_SIZE(ok_resp) - 1);
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
    MAKE_HANDLER_DESCR("meas_start", &procedures_handle_meas_start),
    MAKE_HANDLER_DESCR("meas_stop", &procedures_handle_meas_stop),
    MAKE_HANDLER_DESCR("meas_get_val", &procedures_handle_meas_get_val),
    MAKE_HANDLER_DESCR("conf_set_gain_error:", &procedures_handle_conf_set_gain_error),
    MAKE_HANDLER_DESCR("conf_get_gain_error:", &procedures_handle_conf_get_gain_error),
    MAKE_HANDLER_DESCR("conf_set_zero_error:", &procedures_handle_conf_set_zero_error),
    MAKE_HANDLER_DESCR("conf_get_zero_error:", &procedures_handle_conf_get_zero_error),
    MAKE_HANDLER_DESCR("conf_measure_zero_error:", &procedures_handle_conf_measure_zero_error),
    MAKE_HANDLER_DESCR("conf_set_wavelength:", &procedures_handle_conf_set_wavelength),
    MAKE_HANDLER_DESCR("conf_get_wavelength:", &procedures_handle_conf_get_wavelength),
    MAKE_HANDLER_DESCR("conf_commit:", &procedures_handle_conf_commit),
    MAKE_HANDLER_DESCR("conf_select:", &procedures_handle_conf_select),
    MAKE_HANDLER_DESCR("int_ref_enable", &procedures_handle_int_ref_enable),
    MAKE_HANDLER_DESCR("int_ref_disable", &procedures_handle_int_ref_disable),
    MAKE_HANDLER_DESCR("int_ref_commit", &procedures_handle_int_ref_commit),
    MAKE_HANDLER_DESCR("int_ref_calibrate", &procedures_handle_int_ref_calibrate),
    MAKE_HANDLER_DESCR("int_ref_clear", &procedures_handle_int_ref_clear),
    MAKE_HANDLER_DESCR("int_ref_is_calibrated", &procedures_handle_int_ref_is_calibrated)
};

void procedures_handle_incoming_message(ProceduresData* data, const char* incoming_message)
{
    uint8_t handler_idx = 0;
    while(handler_idx != ARR_SIZE(handler_descriptions)) {
        const struct HandlerDescription* handler_descr = &handler_descriptions[handler_idx];
        if(0 == strncmp(handler_descr->command, incoming_message, handler_descr->cmd_length)) {
            handler_descr->handler(data, incoming_message);
            memset(data->buffer, 0, data->buffer_length - 1);
            return;
        }
        ++handler_idx;
    }
    prepare_next_resp(data, error_resp, ARR_SIZE(error_resp) - 1);
    memset(data->buffer, 0, data->buffer_length - 1);
}
