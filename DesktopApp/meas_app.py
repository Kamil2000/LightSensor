import serial as pyserial
import time

def readSerial(serial):
    time.sleep(0.05)
    return serial.readline().decode("utf-8").strip()

def expectSerial(serial, message):
    msg = readSerial(serial)
    if not message in msg:
        raise ValueError("value is not found")
    return
    

serial = pyserial.Serial()
serial.port = '/dev/ttyUSB0'
serial.baudrate=9600
serial.timeout=1
serial.open()
serial.read(1000)

serial.write(b'nop\r\n')
expectSerial(serial, 'OK')

serial.write(b'conf_select:0\r\n')
expectSerial(serial, 'OK')
serial.write(b'nop\r\n')
expectSerial(serial, 'OK')

serial.write(b'meas_start\r\n')
expectSerial(serial, 'OK')

while True:
    serial.write(b'meas_get_val\r\n')
    print("MeasVal: " + readSerial(serial))
    
serial.write(b'meas_stop\r\n')
expectSerial(serial, 'OK')
serial.write(b'nop\r\n')
expectSerial(serial, 'OK')

serial.close()
    



