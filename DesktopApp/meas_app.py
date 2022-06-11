import serial as pyserial
import time

def readSerial(serial):
    time.sleep(0.2)
    return serial.readline().decode("utf-8").strip()

def expectSerial(serial, message):
    msg = readSerial(serial)
    if not message in msg:
        raise ValueError("value is not found")
    return
    

serial = pyserial.Serial()
serial.port = 'COM3'
serial.baudrate=9600
serial.timeout=1
serial.open()
serial.read(1000)

serial.write(b'nop\r\n')
expectSerial(serial, 'OK')


while True:
    serial.write(b'conf_get_wavelength:0\r\n')
    print("Serial req resp: " + readSerial(serial))
    serial.write(b'nop\r\n')
    print("Serial nop resp: " + readSerial(serial))
    serial.write(b'conf_get_gain_error::0\r\n')
    print("Serial req resp: " + readSerial(serial))
    serial.write(b'nop\r\n')
    print("Serial nop resp: " + readSerial(serial))
    serial.write(b'conf_get_zero_error::0\r\n')
    print("Serial req resp: " + readSerial(serial))
    serial.write(b'nop\r\n')
    print("Serial nop resp: " + readSerial(serial))
    

serial.close()
    



