import sensor_utils as utils

OK_RESP = "OK"
ERR_RESP = "ERROR"
NA_RESP = "NA"
CHECK_FAILED_RESP = "CHECK_FAILED"
DATA_READ_FAILED = 0
DATA_READ_SUCCESS = 1
DATA_READ_NO_VALUE = 2

CHECK_SUCCESSFUL_RESULT = 0
CHECK_FAILED_RESULT = 1

def set_prompt_off(serial):
    stream = serial.get_stream()
    stream.write(b'prompt_off\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return False
    return True

def get_measured_value(serial):
    stream = serial.get_stream()
    stream.write(b'get_val\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if ERR_RESP in textline:
        return None
    if CHECK_FAILED_RESP in textline:
        return (CHECK_FAILED_RESULT, None)
    return (CHECK_SUCCESSFUL_RESULT, float(textline.strip()))


def start_measurement(serial):
    stream = serial.get_stream()
    stream.write(b'start_meas\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if OK_RESP in textline:
        return True
    return False

def stop_measurement(serial):
    stream = serial.get_stream()
    stream.write(b'stop_meas\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if ERR_RESP in textline:
        return False
    return nop_ping(serial)

def nop_read(serial):
    stream = serial.get_stream()
    stream.write(b'nop\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    return textline

def nop_ping(serial):
    textline = nop_read(serial)
    if OK_RESP in textline:
        return True
    return False

def get_param_data_impl(serial, command, convert_function):
    stream = serial.get_stream()
    stream.write(command + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return (None, CDATA_READ_FAILED)
    result = nop_read(serial)
    if NA_RESP in result:
        return (None, DATA_READ_NO_VALUE)
    return (convert_function(result), DATA_READ_SUCCESS)


def set_param_data_impl(serial, command, value, convert_fun):
    stream = serial.get_stream()
    stream.write(command + convert_fun(value).encode('UTF-8') + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return False
    response = nop_read(serial)
    if OK_RESP in response:
        return True
    elif ERR_RESP in response:
        return False
    return False

def commit_param_data_impl(serial, command):
    stream = serial.get_stream()
    stream.write(command + b'\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return False
    response = nop_read(serial)
    if OK_RESP in response:
        return True
    elif ERR_RESP in response:
        return False
    return False

def check_int_ref_enable(serial):
    stream = serial.get_stream()
    stream.write(b'check_int_ref_enable\r\n')
    textline = serial.readline().decode('UTF-8').strip()
    if not OK_RESP in textline:
        return False
    response = nop_read(serial)
    if OK_RESP in response:
        return True
    return False

def calib_int_ref(serial):
    return commit_param_data_impl(serial, b'calib_int_ref')

def commit_int_ref_calib(serial):
    return commit_param_data_impl(serial, b'commit_int_ref_calib')
 
def get_calib_data(serial):
    return get_param_data_impl(serial, b'get_calib', float)

def set_calib_data(serial, value):
    return set_param_data_impl(serial, b'set_calib:', value, str)

def commit_calib_data(serial):
    return commit_param_data_impl(serial, b'commit_calib')
 
def get_zero_data(serial):
    return get_param_data_impl(serial, b'get_zero', int)

def set_zero_data(serial, value):
    return set_param_data_impl(serial, b'set_zero:', value, str)

def commit_zero_data(serial):
    return commit_param_data_impl(serial, b'commit_zero')
