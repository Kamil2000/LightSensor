import serial
import tkinter
import tkinter.ttk as ttk
import tkinter.messagebox as mb
import threading
import pdb
import time
import procedures as proc
import sensor_utils
import tkinter.filedialog as filedialog

class CommunicationException(Exception):
    pass

def open_serial(device):
    serial_port = serial.Serial()
    serial_port.port = device
    serial_port.baudrate=19200
    serial_port.timeout=1
    serial_port.open()
    serial_wrapper = sensor_utils.StreamWrapper(serial_port, 100)
    return (serial_port, serial_wrapper)

class SerialReaderThread(threading.Thread):

    def __init__(self, device, on_close):
        super().__init__()
        self.displayed_value_lock = threading.Lock()
        self.exit_lock = threading.Lock()
        self.save_file_lock = threading.Lock()
        self.should_exit_thread = False
        self.displayed_value = None
        self.device = device
        self.on_close = on_close
        self.recorded_exception = None
        self.save_file = None

    def get_should_exit(self):
        self.exit_lock.acquire()
        try:
            return self.should_exit_thread
        finally:
            self.exit_lock.release()

    def run(self):
        try:
            self.handle_recv()
        except Exception as e:
            self.recorded_exception = e
        finally:
            self.on_close()

    def set_should_exit(self):
        self.exit_lock.acquire()
        try:
            self.should_exit_thread = True
        finally:
            self.exit_lock.release()

    def set_displayed_value(self, value):
        self.displayed_value_lock.acquire()
        try:
            self.displayed_value = value
        finally:
            self.displayed_value_lock.release()

    def get_displayed_value(self):
        self.displayed_value_lock.acquire()
        try:
            return self.displayed_value
        finally:
            self.displayed_value_lock.release()
     
    def get_save_file(self):
        self.save_file_lock.acquire()
        try:
            return self.save_file
        finally:
            self.save_file_lock.release()
    
    def set_save_file(self, file):
        self.save_file_lock.acquire()
        try:
            self.save_file = file
        finally:
            self.save_file_lock.release()
    
    def try_write_value_to_save_file(self, value):
        if value == None:
            return
        str_value = str(value)
        self.save_file_lock.acquire()
        try:
            if self.save_file == None:
                return
            self.save_file.write(str_value + '\n')
        finally:
            self.save_file_lock.release()

    def handle_recv(self):
        #pdb.set_trace()
        serial_port, serial_wrapper = open_serial(self.device)
        should_clear_meas = False
        try:
            if not proc.set_prompt_off(serial_wrapper):
                raise CommunicationException("failed to unset prompt")
            if not proc.nop_ping(serial_wrapper):
                raise CommunicationException("failed to init ping")
            if not proc.start_measurement(serial_wrapper):
                raise CommunicationException("fialed to start measurement")
            should_clear_meas = True
            while not self.get_should_exit():
                value = proc.get_measured_value(serial_wrapper)
                print("Received value: " + str(value))
                if value == None:
                    raise CommunicationException("Receiving incorrect values")
                self.set_displayed_value(value)
                self.try_write_value_to_save_file(value)
                
        finally:
            if should_clear_meas:
                proc.stop_measurement(serial_wrapper)
            serial_port.close()
            print("Closing port")

class MainFrame(ttk.Frame):
    def __init__(self, root):
        super().__init__()
        self.root = root
        self.initialize_ui()
        self.reader_thread = None
        self.measurement_file = None

    def initialize_ui(self):
        self.master.title("LightSensor")
        self.pack(fill=tkinter.BOTH, expand=True)
        
        # device port selection
        device_container = ttk.Frame(self)
        device_container.pack(fill = tkinter.X, pady=2)

        device_frame = ttk.Frame(device_container)
        device_frame.pack(side=tkinter.LEFT, fill=tkinter.X, expand=True)

        label_serial_dev = ttk.Label(device_frame, text="Serial device:")
        label_serial_dev.pack(side=tkinter.LEFT, )

        self.entry_serial_dev = ttk.Entry(device_frame)
        self.entry_serial_dev.pack(fill=tkinter.X, expand=True, padx=2)

        self.btn_conn = ttk.Button(device_container,
                text="Check Connection",
                command=self.handle_check_conn_button)
        self.btn_conn.pack(padx=2)
        
        # Prepare row to enter offset calibration
        zero_container = ttk.Frame(self)
        zero_container.pack(fill = tkinter.X, pady=2)
        zero_frame_for_entry = ttk.Frame(zero_container)
        zero_frame_for_entry.pack(side=tkinter.LEFT,fill=tkinter.X,expand=True)
        zero_frame_for_buttons = ttk.Frame(zero_container)
        zero_frame_for_buttons.pack(side=tkinter.RIGHT, expand=False)
        
        label_zero = ttk.Label(zero_frame_for_entry, text="Offset error:")
        label_zero.pack(side=tkinter.LEFT, expand=False)
        self.entry_zero = ttk.Entry(zero_frame_for_entry)
        self.entry_zero.pack(side=tkinter.RIGHT, fill=tkinter.X, expand=True, padx=2)
        btn_write_zero = ttk.Button(zero_frame_for_buttons, text="Write", command=self.handle_write_zero)
        btn_write_zero.pack(side=tkinter.LEFT, padx=2)
        btn_read_zero = ttk.Button(zero_frame_for_buttons, text="Read", command=self.handle_read_zero)
        btn_read_zero.pack(side=tkinter.RIGHT, padx=2)
        
        # row to enter gain calculation
        gain_container = ttk.Frame(self)
        gain_container.pack(fill = tkinter.X, pady=2)
        gain_frame_for_entry = ttk.Frame(gain_container)
        gain_frame_for_entry.pack(side=tkinter.LEFT, fill=tkinter.X, expand=True)
        gain_frame_for_buttons = ttk.Frame(gain_container)
        gain_frame_for_buttons.pack(side=tkinter.RIGHT, expand=False)
        
        label_gain = ttk.Label(gain_frame_for_entry, text="Gain coefficient:")
        label_gain.pack(side=tkinter.LEFT, expand=False)
        self.entry_gain = ttk.Entry(gain_frame_for_entry)
        self.entry_gain.pack(side=tkinter.RIGHT, fill=tkinter.X, expand=True, padx=2)
        btn_write_gain = ttk.Button(gain_frame_for_buttons, text="Write", command=self.handle_write_gain)
        btn_write_gain.pack(side=tkinter.LEFT, padx=2)
        btn_read_gain = ttk.Button(gain_frame_for_buttons, text="Read", command=self.handle_read_gain)
        btn_read_gain.pack(side=tkinter.RIGHT, padx=2)
        
        self.btn_save_parameters = ttk.Button(self, text="Save parameters", command=self.handle_save_parameters)
        self.btn_save_parameters.pack(fill = tkinter.X, pady=2, padx=2)        
        self.btn_measurement = ttk.Button(self, text="Start measurement", command=self.handle_measurement_button)
        self.btn_measurement.pack(fill = tkinter.X, pady=2, padx=2)
        self.has_measurement_ongoing = False
        
        self.should_check_int_vol = tkinter.IntVar(value=1)
        check_int_val = ttk.Checkbutton(self, text="Verify supply voltage before measurement", variable=self.should_check_int_vol)
        check_int_val.pack(fill = tkinter.X, pady=2, padx=2)
        self.btn_calib_int_vol = ttk.Button(self, text="Calibrate supply voltage verification", command=self.handle_calibrate_int_vol)
        self.btn_calib_int_vol.pack(fill = tkinter.X, pady=2, padx=2)
        
        
        stats_container = ttk.Frame(self)
        stats_container.pack(fill=tkinter.BOTH, expand=True)

        stats_container.columnconfigure(0, pad = 6)
        stats_container.columnconfigure(1, weight = 1, pad = 6)
        stats_container.columnconfigure(2, weight = 1, pad = 6)
        stats_container.rowconfigure(0, weight = 1)
        stats_container.rowconfigure(1, pad = 3)
        stats_container.rowconfigure(2, pad = 3)
        stats_container.rowconfigure(3, weight = 1, pad = 10)

        lbl_value_desc = ttk.Label(stats_container, text="Current Value: ")
        lbl_value_desc.grid(row=1, column=0)

        self.lbl_value = tkinter.Label(stats_container, text="N/A", fg="green" ,font=('Arial', 32))
        self.lbl_value.grid(row=1, column=1, columnspan=2)

        lbl_status_desc = ttk.Label(stats_container, text="Status: ")
        lbl_status_desc.grid(row = 2, column = 0)

        self.lbl_status = tkinter.Label(
                stats_container,
                text="Disconnected",
                fg="red",
                font=('Arial', 32))
        self.lbl_status.grid(row=2, column=1, columnspan=2)
        
        file_container = ttk.Frame(self)
        file_container.pack(fill=tkinter.X, pady=2, padx=2)
        self.file_entry = ttk.Entry(file_container)
        self.file_entry.pack(side=tkinter.RIGHT, fill=tkinter.X, expand=True)
        btn_select_path = ttk.Button(file_container, text="Select path", command=self.handle_select_path)
        btn_select_path.pack(side=tkinter.RIGHT)
        self.btn_capture = ttk.Button(self, text="Start capturing", command=self.handle_capture_button)
        self.btn_capture.pack(fill = tkinter.X, pady=2, padx=2)
        self.has_capturing_ongoing = False
        self.root.protocol("WM_DELETE_WINDOW", self.handle_window_close)
        
    def handle_calibrate_int_vol(self):
        serial_dev = self.entry_serial_dev.get()
        serial, serial_wrapper = open_serial(serial_dev)
        if not proc.calib_int_ref(serial_wrapper):
            self.lbl_status.configure(text="Error during calibration")
        self.lbl_status.configure(text="Ref voltage calibrated")
       
    def handle_select_path(self):
        data= [('All tyes(*.*)', '*.*')]
        file = filedialog.asksaveasfile(filetypes = data, defaultextension=data)
        self.file_entry.delete(0, tkinter.END)
        self.file_entry.insert(0, str(file_path))
        
    def handle_save_parameters(self):
        serial_dev = self.entry_serial_dev.get()
        serial, serial_wrapper = open_serial(serial_dev)
        try:
            if not proc.set_prompt_off(serial_wrapper):
                self.lbl_status.configure(text="Saving Error")
            if not proc.commit_calib_data(serial_wrapper):
                self.lbl_status.configure(text="Saving Error")
            if not proc.commit_zero_data(serial_wrapper):
                self.lbl_status.configure(text="Saving Error")
            if not proc.commit_int_ref_calib(serial_wrapper):
                self.lbl_status.configure(text="Saving Error")
        finally:
            serial.close()
        self.lbl_status.configure(text="Savnig Successful")
        
    
    def handle_read_gain(self):
        device_name = self.entry_serial_dev.get()
        serial_port, serial_wrapper = open_serial(device_name)
        try:
            if not proc.set_prompt_off(serial_wrapper):
                self.lbl_status.configure(text="Reading gain failed")
                return
            value, read_status = proc.get_calib_data(serial_wrapper)
            if read_status == proc.DATA_READ_NO_VALUE:
                self.lbl_status.configure(text="No gain in device memory")
                return
            if read_status == proc.DATA_READ_FAILED:
                self.lbl_status.configure(text="Reading gain failed")
                return
            self.entry_gain.delete(0, tkinter.END)
            self.entry_gain.insert(0, str(value))
        finally:
            serial_port.close()
        self.lbl_status.configure(text="OK - Gain read")

    def handle_write_gain(self):
        device_name = self.entry_serial_dev.get()
        serial_port, serial_wrapper = open_serial(device_name)
        value = float(self.entry_gain.get())
        try:
            if not proc.set_prompt_off(serial_wrapper):
                self.lbl_status.configure(text="Reading gain failed")
                return
            write_status = proc.set_calib_data(serial_wrapper, value)
            if write_status != True:
                self.lbl_status.configure(text="Writting gain failed")
                return
        finally:
            serial_port.close()
        self.lbl_status.configure(text="OK - Gain wrote")
    
    def handle_read_zero(self):
        device_name = self.entry_serial_dev.get()
        serial_port, serial_wrapper = open_serial(device_name)
        try:
            if not proc.set_prompt_off(serial_wrapper):
                self.lbl_status.configure(text="Reading offset failed")
                return
            value, read_status = proc.get_zero_data(serial_wrapper)
            if read_status == proc.DATA_READ_FAILED:
                self.lbl_status.configure(text="Reading offset failed")
                return
            if read_status == proc.DATA_READ_NO_VALUE:
                self.lbl_status.configure(text="No offset in device memory")
                return
            self.entry_zero.delete(0, tkinter.END)
            self.entry_zero.insert(0, str(value))
        finally:
            serial_port.close()
        self.lbl_status.configure(text="OK - Offset read")
       
    def handle_write_zero(self):
        device_name = self.entry_serial_dev.get()
        serial_port, serial_wrapper = open_serial(device_name)
        value = float(self.entry_zero.get())
        try:
            if not proc.set_prompt_off(serial_wrapper):
                self.lbl_status.configure(text="Reading offset failed")
                return
            write_status = proc.set_zero_data(serial_wrapper, value)
            if write_status != True:
                self.lbl_status.configure(text="Writting offset failed")
                return
        finally:
            serial_port.close()
        self.lbl_status.configure(text="OK - Offset wrote")
    
    def is_supply_voltage_high_enough(self):
        if self.should_check_int_vol.get() == 0:
            return True
        device_name = self.entry_serial_dev.get()
        serial_port, serial_wrapper = open_serial(device_name)
        try:
            result = proc.check_int_ref(serial_wrapper)
            if result == proc.CHECK_UNSPECIFIED_RESULT:
                self.lbl_status.configure(text="Cannot verify supply voltage")
                return False
            if result == proc.CHECK_FAILED_RESULT:
                self.lbl_status.configure(text="Supply voltage droped below acceptable limit")
                return False
            if result == proc.CHECK_SUCCESSFUL_RESULT:
                return True
        except Exception as exc:
            self.lbl_status.configure(text = "Connection ERROR")
            return
        finally:
            serial_port.close()
        self.lbl_status.configure(text="Cannot verify supply voltage")
        return False
        
    
    def handle_measurement_button(self):
        if self.has_measurement_ongoing:
            self.btn_measurement.configure(text="Start Measurement")
            self.stop_measurement()
            self.has_measurement_ongoing = False
            return
        if not self.is_supply_voltage_high_enough():
            return
        self.btn_measurement.configure(text="Stop Measurement")
        self.start_measurement()
        self.has_measurement_ongoing = True
    
    def stop_measurement(self):
        self.stop_capture()
        self.should_continue_read = False
        if self.reader_thread != None:
            self.reader_thread.set_should_exit()

    def start_measurement(self):
        device_name = self.entry_serial_dev.get()
        self.reader_thread = SerialReaderThread(device_name, self.on_reader_thread_close)
        self.reader_thread.start()
        self.should_continue_read = True
        self.root.after(50, self.update_measurement_value)
        
    def update_measurement_value(self):
        if not self.should_continue_read:
            return
        value = self.reader_thread.get_displayed_value()
        if value != None:
            self.lbl_value.configure(text=str(value))
        self.root.after(400, self.update_measurement_value)
    
    def on_reader_thread_close(self):
        def content(self_obj):
            self_obj.lbl_value.configure(text="N/A")
            self_obj.stop_measurement() # to handle exceptional situation
            exception = self_obj.reader_thread.recorded_exception
            if exception != None:
                mb.showerror('Exception occured', 'During reading values from device exception occured: ' + str(exception))
            self_obj.reader_thread = None
        self.root.after(0, lambda: content(self))
    
    def handle_capture_button(self):
        if self.has_capturing_ongoing:
            self.btn_capture.configure(text="Start Capturing")
            self.stop_capture()
            self.has_capturing_ongoing = False
            return
        self.btn_capture.configure(text="Stop Capturing")
        self.start_capture()
        self.has_capturing_ongoing = True
    
    def start_capture(self):
        #file_path = self.file_entry.get()
        #self.measurement_file = open(file_path, "w")
        data= [('All tyes(*.*)', '*.*')]
        self.measurement_file = filedialog.asksaveasfile(filetypes = data, defaultextension=data)
        self.reader_thread.set_save_file(self.measurement_file)
    
    def stop_capture(self):
        if self.measurement_file == None:
            return
        self.reader_thread.set_save_file(None)
        self.measurement_file.close()
        self.measurement_file = None
        
        
    
    def handle_window_close(self):
        if self.reader_thread == None:
            self.root.destroy()
            return
        self.reader_thread.set_should_exit()
        self.reader_thread.join()
        self.root.destroy()

    def handle_check_conn_button(self):
        device_name = self.entry_serial_dev.get()
        serial_port, serial_wrapper = open_serial(device_name)
        try:
            if not proc.set_prompt_off(serial_wrapper):
                self.lbl_status.configure(text="Connection ERROR")
                return
            if not proc.nop_ping(serial_wrapper):
                self.lbl_status.configure(text="Connection ERROR")
                return
        except Exception as e:
            self.lbl_status.configure(text = "Connection ERROR")
            return
        finally:
            serial_port.close()

        self.lbl_status.configure(text = "Connection OK")



root = tkinter.Tk()
root.geometry("600x400")
main_frame = MainFrame(root)
root.mainloop()
