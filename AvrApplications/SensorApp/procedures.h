#include <Nrf24L01.h>
#include <Nrf24L01Registers.h>
#ifndef PROCEDURES_H_
#define PROCEDURES_H_

typedef enum {
    PROC_STATE_DEFAULT,
    PROC_STATE_MEASUREMENT,
    PROC_STATE_AWAIT_NOP,
    PROC_STATE_COUNT_OF_STATES    
}ProceduresState;

enum {
    CALIB_DATA_ELEMENTS_COUNT = 5,
    CALIB_DATA_WAVELENGTH_PRESENT = 1,
    CALIB_DATA_GAIN_ERROR_PRESENT = 2,
    CALIB_DATA_ZERO_ERROR_PRESENT = 4
};

#define PACKED_ATTRIBUTE __attribute__((packed))

typedef PACKED_ATTRIBUTE struct {
    uint8_t flags;
    uint16_t wavelength;
    double gain_error;
    int16_t zero_error;
}CalibData;

typedef PACKED_ATTRIBUTE struct {
    bool has_data;
    uint16_t value;
}InternalVolData;

typedef PACKED_ATTRIBUTE struct {
    InternalVolData internal_vol_data;
    CalibData calib_data[CALIB_DATA_ELEMENTS_COUNT];
}EepromData;

typedef struct {
    NrfController* nrf_ctrl;
    char* buffer;
    CalibData calib_data[CALIB_DATA_ELEMENTS_COUNT];
    InternalVolData internal_vol_data;
    bool is_measurement_error;
    int8_t measure_int_vol_counter;
    uint8_t selected_conf;
    uint8_t buffer_length;
    uint8_t proc_state;
    
}ProceduresData;

void
procedures_data_init(ProceduresData* data, NrfController* nrf_ctrl, char* buffer, uint8_t buff_len);

void
procedures_data_destroy(ProceduresData* data);

void procedures_handle_incoming_message(ProceduresData* data, const char* incoming_message);

#endif /* PROCEDURES_H_ */