import sensor_utils as utils

OK_RESP = "OK"
ERR_RESP = "ERROR"
NA_RESP = "NA"
OUT_OF_RANGE_RESP = "OUT_OF_RANGE"
VOL_CHECK_FAILED_RESP = "CHECK_FAILED"
NO_CONF_SELECTED_RESP = "NO_CONF_SELECTED"
TRUE_RESP = "TRUE"
FALSE_RESP = "FALSE"
MEAS_ERRORS_CLEARED_RESP = "MEAS_ERRORS_CLEARED"

DATA_READ_FAILED = 0
DATA_READ_SUCCESS = 1
DATA_READ_NO_VALUE = 2
DATA_READ_CANNOT_CONVERT = 3
DATA_READ_OUT_OF_RANGE = 4

GET_VAL_SUCCESS = 0
GET_VAL_VOL_CHECK_FAILURE = 1
GET_VAL_NO_CONF_SELECTED = 2
GET_VAL_ERROR_OTHER = 3
GET_VAL_NO_VAL = 4

CMD_FAILURE = 0
CMD_SUCCESS = 1

TRUE = 1
FALSE = 0

RETR_COUNT = 12

def _meas_get_val(serial):
    stream = serial.get_stream()
    stream.write(b'meas_get_val\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if textline.isspace() or len(textline) == 0:
    	return (None, GET_VAL_NO_VAL)
    if ERR_RESP in textline:
        return (None, GET_VAL_ERROR_OTHER)
    if VOL_CHECK_FAILED_RESP in textline:
        return (None, GET_VAL_VOL_CHECK_FAILURE)
    if NO_CONF_SELECTED_RESP in textline:
        return (None, GET_VAL_NO_CONF_SELECTED)
    return (float(textline.strip()), GET_VAL_SUCCESS)

def _meas_stop(serial):
    stream = serial.get_stream()
    stream.write(b'meas_stop\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if OK_RESP in textline:
        return CMD_SUCCESS if OK_RESP in _nop_read(serial) else CMD_FAILURE
    textline = _nop_read(serial)
    if MEAS_ERRORS_CLEARED_RESP in textline:
        return CMD_SUCCESS
    return CMD_FAILURE

def _meas_start(serial):
    stream = serial.get_stream()
    stream.write(b'meas_start\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if ERR_RESP in textline:
        return CMD_FAILURE
    return CMD_SUCCESS

def _nop_read(serial):
    stream = serial.get_stream()
    stream.write(b'nop\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    return textline

def _nop_ping(serial):
    for _ in range(RETR_COUNT):
        if OK_RESP in _nop_read(serial):
            return CMD_SUCCESS
    return CMD_FAILURE
    
def _get_indexed_param(serial, command, index, convert_fun):
    stream = serial.get_stream()
    stream.write(command + str(index).encode('UTF-8') + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return (None, DATA_READ_FAILED)
    result = _nop_read(serial)
    try:
        return (convert_fun(result), DATA_READ_SUCCESS)
    except ValueError as _:
        return (None, DATA_READ_CANNOT_CONVERT)

def _get_indexed_param_guarded(serial, command, index, convert_fun):
    result = None
    for _ in range(RETR_COUNT):
        result = _get_indexed_param(serial, command, index, convert_fun)
        if result[1] == DATA_READ_SUCCESS:
            return result
    return result
    
def _set_indexed_param(serial, command, index, encoded_value):
    stream = serial.get_stream()
    stream.write(command + str(index).encode('UTF-8') + b':' + encoded_value + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return CMD_FAILURE
    result = _nop_read(serial)
    if not OK_RESP in result:
        return CMD_FAILURE
    return CMD_SUCCESS

def _set_indexed_param_guarded(serial, command, index, encoded_value):
    for _ in range(RETR_COUNT):
        result = _set_indexed_param(serial, command, index, encoded_value)
        if result == CMD_SUCCESS:
            return CMD_SUCCESS
    return CMD_FAILURE

def _get_param(serial, command, convert_fun):
    stream = serial.get_stream()
    stream.write(command + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return (None, DATA_READ_FAILED)
    result = _nop_read(serial)
    if NA_RESP in result:
        return (None, DATA_READ_NO_VALUE)
    try:
        return (convert_fun(result), DATA_READ_SUCCESS)
    except ValueError as _:
        return (None, DATA_READ_CANNOT_CONVERT)

def _get_param_guarded(serial, command, convert_fun):
    result = None
    for _ in range(RETR_COUNT):
        result = _get_param(serial, command, convert_fun)
        if result[1] == DATA_READ_SUCCESS:
            return result
    return result

def _set_param(serial, command, encoded_value):
    stream = serial.get_stream()
    stream.write(command + encoded_value + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return CMD_FAILURE
    response = _nop_read(serial)
    if OK_RESP in response:
        return CMD_SUCCESS
    elif ERR_RESP in response:
        return CMD_FAILURE
    return CMD_FAILURE

def _set_param_guarded(serial, command, encoded_value):
    for _ in range(RETR_COUNT):
        result = _set_param(serial, command, encoded_value)
        if result == CMD_SUCCESS:
            return CMD_SUCCESS
    return CMD_FAILURE

def _execute_single_command_with_resp(serial, command):
    stream = serial.get_stream()
    stream.write(command + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return CMD_FAILURE
    response = _nop_read(serial)
    if OK_RESP in response:
        return CMD_SUCCESS
    if ERR_RESP in response:
        return CMD_FAILURE
    return CMD_FAILURE

def _execute_single_command_with_resp_guarded(serial, command):
    for _ in range(RETR_COUNT):
        result = _execute_single_command_with_resp(serial, command)
        if CMD_SUCCESS == result:
            return CMD_SUCCESS
    return CMD_FAILURE

def _commit_param(serial, command):
    return _execute_single_command_with_resp_guarded(serial, command)

# actual procedures:

def int_ref_enable(serial):
    return _execute_single_command_with_resp_guarded(serial, b'int_ref_enable')
    
def int_ref_disable(serial):
    return _execute_single_command_with_resp_guarded(serial, b'int_ref_disable')

def int_ref_commit(serial):
    return _commit_param(serial, b'int_ref_commit')
    
def int_ref_calibrate(serial):
    return _execute_single_command_with_resp_guarded(serial, b'int_ref_calibrate')
    
def int_ref_is_calibrated(serial):
    convert = lambda result: True if result == TRUE_RESP else False
    return _get_param_guarded(serial, b'int_ref_is_calibrated', convert)

def conf_get_zero_error_value(serial, index):
    convertFun = lambda x: None if "NONE" in x else int(x)
    return _get_indexed_param_guarded(serial, b'conf_get_zero_error:', index, convertFun)

def conf_measure_zero_error_value(serial, index):
    convertFun = lambda x: None if "NONE" in x else int(x)
    return _get_indexed_param_guarded(serial, b'conf_measure_zero_error:', index, convertFun)
    
def conf_set_zero_error_value(serial, index, value):
    strVal = str(value).encode('UTF-8') if value != None else "NONE".encode('UTF-8')
    return _set_indexed_param_guarded(serial, b'conf_set_zero_error:', index, strVal)

def conf_get_gain_error_value(serial, index):
    convertFun = lambda x: None if "NONE" in x else float(x)
    return _get_indexed_param_guarded(serial, b'conf_get_gain_error:', index, convertFun)

def conf_set_gain_error_value(serial, index, value):
    strVal = str(value).encode('UTF-8') if value != None else "NONE".encode('UTF-8')
    return _set_indexed_param_guarded(serial, b'conf_set_gain_error:', index, strVal)

def conf_get_wavelength_value(serial, index):
    convertFun = lambda x: None if "NONE" in x else int(x)
    return _get_indexed_param_guarded(serial, b'conf_get_wavelength:', index, convertFun)

def conf_set_wavelength_value(serial, index, value):
    strVal = str(value).encode('UTF-8') if value != None else "NONE".encode('UTF-8')
    return _set_indexed_param_guarded(serial, b'conf_set_wavelength:', index, strVal)

def conf_select(serial, index):
    return _set_param_guarded(serial, b'conf_select:', str(index).encode('UTF-8'))

def conf_commit(serial, index):
    return _set_param_guarded(serial, b'conf_commit:', str(index).encode('UTF-8'))

def nop(serial):
    return _nop_ping(serial)
    
def meas_start(serial):
    return _meas_start(serial)

def meas_stop(serial):
    return _meas_stop(serial)

def meas_get_val(serial):
    return _meas_get_val(serial)
 
