#include <Nrf24L01.h>
#include <Nrf24L01Registers.h>
#ifndef PROCEDURES_H_
#define PROCEDURES_H_

typedef enum {
    PROC_STATE_DEFAULT,
    PROC_STATE_MEASUREMENT,
    PROC_STATE_SET_CALIB_DATA_RESP,
    PROC_STATE_GET_CALIB_DATA_RESP,
    PROC_STATE_GET_ZERO_DATA_RESP,
    PROC_STATE_SET_ZERO_DATA_RESP,
    PROC_STATE_COUNT_OF_STATES    
}ProceduresState;

#define PACKED_ATTRIBUTE __attribute__((packed))

typedef PACKED_ATTRIBUTE struct {
    bool has_data;
    double value;
}CalibData;

typedef PACKED_ATTRIBUTE struct {
    bool has_data;
    int16_t value;
}ZeroData;

typedef PACKED_ATTRIBUTE struct {
    bool has_data;
    uint16_t value;
}InternalVolData;

typedef PACKED_ATTRIBUTE struct {
    CalibData calib_data;
    ZeroData zero_data;
    InternalVolData internal_vol_data;
}EepromData;

typedef struct {
    NrfController* nrf_ctrl;
    char* buffer;
    uint8_t buffer_length;
    uint8_t proc_state;
    CalibData calib_data;
    ZeroData zero_data;
    InternalVolData internal_vol_data;
}ProceduresData;

void
procedures_data_init(ProceduresData* data, NrfController* nrf_ctrl, char* buffer, uint8_t buff_len);

void
procedures_data_destroy(ProceduresData* data);

void procedures_handle_incoming_message(ProceduresData* data, const char* incoming_message);

#endif /* PROCEDURES_H_ */