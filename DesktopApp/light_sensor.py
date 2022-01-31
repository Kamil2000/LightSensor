import serial as pyserial
import tkinter
import tkinter.ttk as ttk
import tkinter.messagebox as mb
import threading
import time
import procedures as proc
import sensor_utils
import tkinter.filedialog as filedialog
import tkinter.messagebox as tkmb
#import traceback
#import pdb

TREEVIEW_VALUE_UNREAD = 'Not Read'
TREEVIEW_VALUE_UNSET = "Unset"

class CommunicationException(Exception):
    pass

class SerialPortException(Exception):
    pass

class IntVolCheckFailedException(Exception):
    pass

def can_convert(strvalue, datatype):
    try:
        _ = datatype(strvalue)
        return True
    except:
        return False

class Parameter():
    UNSET = 0
    UNREAD = 1
    PRESENT = 2
    def __init__(self, value = None, flag = UNREAD):
        self.value = value
        self.flag = flag
        
    def set(self, value):
        self.value = value
        self.flag = Parameter.PRESENT
    
    def get(self):
        return self.value
    
    def is_unset(self):
        return self.flag != Parameter.UNSET
        
    def is_unread(self):
        return self.flag != Parameter.UNREAD
    
    def is_present(self):
        return not (self.is_unset() or self.is_unread())
    
    def mark_unread(self):
        self.flag = Parameter.UNREAD
    
    def mark_unset(self):
        self.flag = Parameter.UNSET

    def to_treeview_cell_value(self):
        if self.flag == Parameter.UNSET:
            return TREEVIEW_VALUE_UNSET
        if self.flag == Parameter.UNREAD:
            return TREEVIEW_VALUE_UNREAD
        return str(self.value)

class ParametersRow:

    def __init__(self, id_val):
        self.id_val = id_val
        self.wavelength = Parameter()
        self.zero_error = Parameter()
        self.gain_error = Parameter()
    
    def get_id(self):
        return self.id_val
    
    def get_treeview_display_id(self):
        return str(self.id_val + 1)

    def get_wavelength(self):
        return self.wavelength 
    
    def get_zero_error(self):
        return self.zero_error 
       
    def get_gain_error(self):
        return self.gain_error 
    
    def to_treeview_row(self):
        wavelength = self.get_wavelength().to_treeview_cell_value()
        zero_error = self.get_zero_error().to_treeview_cell_value()
        gain_error = self.get_gain_error().to_treeview_cell_value()
        return (self.get_treeview_display_id(), wavelength, zero_error, gain_error)
        
        

def open_serial(device):
    serial_port = pyserial.Serial()
    try:
        serial_port.port = device
        serial_port.baudrate=19200
        serial_port.timeout=1
        serial_port.open()
        serial_wrapper = sensor_utils.StreamWrapper(serial_port, 100)
        return (serial_port, serial_wrapper)
    except:
        serial_port.close()
        raise SerialPortException("Cannot connect with serial device: " + str(device))

class MeasurementThread(threading.Thread):

    def __init__(self, device):
        super().__init__()
        self.displayed_value_lock = threading.Lock()
        self.exit_lock = threading.Lock()
        self.should_exit_thread = False
        self.displayed_value = None
        self.device = device

    def get_should_exit(self):
        self.exit_lock.acquire()
        try:
            return self.should_exit_thread
        finally:
            self.exit_lock.release()
    
    def get_recorded_exception(self):
        if hasattr(self, 'recorded_exception'):
            return self.recorded_exception
        return None
        
      
    def run(self):
        try:
            self.handle_recv()
        except Exception as e:
            self.recorded_exception = e

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
        return self.save_file if hasattr(self, 'save_file') else None

    def set_save_file(self, file):
        self.save_file = file
    
    def try_write_value_to_save_file(self, value):
        if self.get_save_file() == None or value == None:
            return
        self.get_save_file().write(str(value) + '\n')
    
    def get_recored_error(self):
        if not hasattr(self, 'recorded_error'):
            return None
        return self.recorded_error
    
    def finish_measurement(self):
        self.set_should_exit()
        self.join()
        if self.get_recored_error() != None:
            return self.get_recored_error()
        elif self.get_recorded_exception() != None:
            return "Exception occured: " + str(self.get_recorded_exception())

    def handle_recv(self):
        #pdb.set_trace()
        (serial, reader) = self.device
        should_clear_meas = False
        try:
            if proc.CMD_SUCCESS != proc.meas_start(reader):
                self.recorded_error = "Cannot start measurement"
                return
            should_clear_meas = True
            while not self.get_should_exit():
                (value, status) = proc.meas_get_val(reader)
                if status == proc.GET_VAL_VOL_CHECK_FAILURE:
                    self.recorded_error = "Voltage check failed. Please, check supply voltage."
                    return
                if status != proc.GET_VAL_SUCCESS or value == None:
                    self.recorded_error = "Unknown error occured during measurement. Check if all parameters are applied"
                    return
                
                self.set_displayed_value(value)
                self.try_write_value_to_save_file(value)
        finally:
            if should_clear_meas:
                proc.meas_stop(reader)
            serial.close()
            if self.get_save_file() != None:
                self.get_save_file().close()
            

        
class ApplicationController:
    def __init__(self):
        self.data_model = [ParametersRow(id_val = i) for i in range(5)]
        self.is_vol_validation_enabled = False
        self.device_name = None
        
    def treeview_rows_count(self):
        return len(self.data_model)
    
    def get_data_row(self, idx):
        return self.data_model[idx]

    def get_treeview_row(self, idx):
        row = self.data_model[idx]
        return (row.get_id(), row.to_treeview_row())
            
    def get_gui(self):
        return self._gui if hasattr(self, '_gui') else None
    
    def guarded_open_serial(self, device_name):
        try:
            (serial, reader) = open_serial(device_name)
            return (serial, reader)
        except SerialPortException:
            self.get_gui().dialog_error("Cannot open device: " + device_name + ". Please, check connection")
            return None
    
    def save_wavelength(self, value, index):
        if not (value == '' or (can_convert(value, int))):
            tkmb.showerror('Error', 'Wavelength field should be left empty or have unsigned integer value')
            return
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            result = proc.conf_set_wavelength_value(reader, index, value if value != '' else None)
            if result != proc.CMD_SUCCESS:
                self.get_gui().dialog_error("Cannot save wavelength value")
                return
            if value == '':
                self.data_model[index].get_wavelength().mark_unset()
            else:
                self.data_model[index].get_wavelength().set(int(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
    
    def read_wavelength(self, strvar, index):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            (value, status) = proc.conf_get_wavelength_value(reader, index)
            if status != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot read wavelength value")
                return
            if value == None:
                self.data_model[index].get_wavelength().mark_unset()
                strvar.set('')
            else:
                self.data_model[index].get_wavelength().set(value)
                strvar.set(str(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
        
    def save_zero_error(self, value, index):
        if not (value == '' or (can_convert(value, int))):
            tkmb.showerror('Error', 'Zero error field should be left empty or have integer value')
            return
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            result = proc.conf_set_zero_error_value(reader, index, value if value != '' else None)
            if result != proc.CMD_SUCCESS:
                self.get_gui().dialog_error("Cannot save zero error value")
                return
            if value == '':
                self.data_model[index].get_zero_error().mark_unset()
            else:
                self.data_model[index].get_zero_error().set(int(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
    
    def read_zero_error(self, strvar, index):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            (value, status) = proc.conf_get_zero_error_value(reader, index)
            if status != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot zero error value")
                return
            if value == None:
                self.data_model[index].get_zero_error().mark_unset()
                strvar.set('')
            else:
                self.data_model[index].get_zero_error().set(value)
                strvar.set(str(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()

    def measure_zero_error(self, strvar, index):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            (value, status) = proc.conf_measure_zero_error_value(reader, index)
            if status != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot measure zero error value")
                return
            if value == None:
                self.data_model[index].get_zero_error().mark_unset()
                strvar.set('')
            else:
                self.data_model[index].get_zero_error().set(value)
                strvar.set(str(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
        
    def read_gain_error(self, strvar, index):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            (value, status) = proc.conf_get_gain_error_value(reader, index)
            if status != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot gain error value")
                return
            if value == None:
                self.data_model[index].get_gain_error().mark_unset()
                strvar.set('')
            else:
                self.data_model[index].get_gain_error().set(value)
                strvar.set(str(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
    
    def save_gain_error(self, value, index):
        if not (value == '' or (can_convert(value, float))):
            tkmb.showerror('Error', 'Gain error field should be left empty or have float value')
            return
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            result = proc.conf_set_gain_error_value(reader, index, value if value != '' else None)
            if result != proc.CMD_SUCCESS:
                self.get_gui().dialog_error("Cannot save gain error value")
                return
            if value == '':
                self.data_model[index].get_gain_error().mark_unset()
            else:
                self.data_model[index].get_gain_error().set(float(value))
            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
    
    def calibrate_internal_voltage(self):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            if proc.CMD_SUCCESS != proc.int_ref_calibrate(reader):
                self.get_gui().dialog_error("Cannot set voltage validation parameter.")
                return
        finally:
            serial.close()
    
    def save_internal_voltage(self):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            if proc.CMD_SUCCESS != proc.int_ref_commit(reader):
                self.get_gui().dialog_error("Cannot set voltage validation parameter.")
                return
        finally:
            serial.close()

    def handle_vol_validation_state_changed(self, state):
        self.is_vol_validation_enabled = True if state else False
    
    def handle_connect(self, device_txt):
        opened_serial = self.guarded_open_serial(device_txt)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            self.device_name = device_txt
            if proc.nop(reader) != proc.CMD_SUCCESS:
                self.get_gui().dialog_error_connection()
                return
            (value, state) = proc.int_ref_is_calibrated(reader)
            if state != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot read calibration status")
                return
            self.get_gui().intvar_vol_validation.set(1 if value else 0)
            for i in range(len(self.data_model)):
                (value, state) = proc.conf_get_wavelength_value(reader, i)
                if state != proc.DATA_READ_SUCCESS:
                    self.get_gui().dialog_error("Cannot read wavelength")
                    return
                if value != None:
                    self.data_model[i].get_wavelength().set(value)
                else:
                    self.data_model[i].get_wavelength().mark_unset()
            self.get_gui().notebook_enable()
        except SerialPortException:
            self.get_gui().dialog_error("Cannot find specified device: " + device_txt + ". Please, check connection.")
        finally:
            serial.close()
        
    def handle_show_parameters(self, index):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            (value, state) = proc.conf_get_zero_error_value(reader, index)
            if state != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot read zero error value.")
            if value != None:
                self.data_model[index].get_zero_error().set(value)
            else:
                self.data_model[index].get_zero_error().mark_unset()
            
            (value, state) = proc.conf_get_gain_error_value(reader, index)
            if state != proc.DATA_READ_SUCCESS:
                self.get_gui().dialog_error("Cannot read gain error value.")
            if value != None:
                self.data_model[index].get_gain_error().set(value)
            else:
                self.data_model[index].get_gain_error().mark_unset()

            self.get_gui().refresh_treeview_row(index)
        finally:
            serial.close()
    
    def handle_commit_config_params(self, index):
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            return
        (serial, reader) = opened_serial
        try:
            if proc.CMD_SUCCESS != proc.conf_commit(reader, index):
                self.get_gui().dialog_error("Cannot save parameters. Check Connection.")
                return
        finally:
            serial.close()
    
    def get_filename(self):
        if not hasattr(self, 'filename'):
            return None
        return self.filename
    
    def get_value_setter(self):
        if not hasattr(self, 'value_setter'):
            return None
        return self.value_setter

    def get_meas_reader(self):
        if not hasattr(self, 'meas_reader'):
            return None
        return self.meas_reader
    
    def clear_measurment(self):
        self.filename = None
        self.value_setter = None
        
    def handle_start_measurement(self, idx, filename, value_setter):
        if self.get_meas_reader() != None:
            return
        self.value_setter = value_setter
        self.filename = filename
        opened_serial = self.guarded_open_serial(self.device_name)
        if opened_serial == None:
            self.clear_measurment()
            return
        (serial, reader) = opened_serial
        try:
            if proc.CMD_SUCCESS != proc.conf_select(reader, idx):
                self.clear_measurment()
                return
        except Exception as e:
            serial.close()
            self.get_gui().dialog_error("Unexpected error occured!! " + str(e))
            return 
        try:
            self.meas_reader = MeasurementThread(opened_serial)
            if filename != None:
                file = open(filename, 'w+')
                self.meas_reader.set_save_file(file)
                
        except IOError:
            self.get_gui().dialog_error("Cannot open filename: " + filename + ". Measurement stopped")
            self.meas_reader = None
            return
        self.meas_reader.start()
        self.get_gui().after(50, self.handle_meas_reader_update)
    
    def handle_meas_reader_update(self):
        if self.get_meas_reader() == None:
            return
        if not self.get_meas_reader().is_alive():
            meas_error = self.get_meas_reader().finish_measurement()
            if meas_error != None:
                self.get_gui().dialog_error(meas_error)
            self.get_value_setter()("--")
            self.meas_reader = None
            return
        self.get_value_setter()(str(self.get_meas_reader().get_displayed_value()))
        self.get_gui().after(100, self.handle_meas_reader_update)
    
    def handle_stop_measurement(self):
        if self.get_meas_reader() == None:
            return
        result = self.get_meas_reader().finish_measurement()
        if result != None:
            self.get_gui().dialog_error("After requesting to finish measurement error has occured: " + str(result))
        self.meas_reader = None
        self.get_value_setter()("--")
    
    def handle_application_close(self):
        if self.get_meas_reader() == None:
            self.get_gui().root.destroy()
            return
        result = self.meas_reader.finish_measurement()
        if result != None:
            self.get_gui().dialog_error("After requesting to finish measurement error has occured: " + str(result))
        self.get_gui().root.destroy()
            
    def is_measurement_running(self):
        return self.get_meas_reader() != None and self.get_meas_reader().is_alive()
       
class MainFrame(ttk.Frame):
    def __init__(self, root, app_controller):
        super().__init__()
        self.app_controller = app_controller
        self.app_controller._gui = self
        self.root = root
        self.master.title("LightSensor")
        self.pack(fill=tkinter.BOTH, expand=True)
        self.root.protocol("WM_DELETE_WINDOW", self.app_controller.handle_application_close)
        self.init_device_selection()
        self.init_notebook()
        self.init_tab_global_presets()
        self.init_tab_internal_voltage_conf()
     
    def init_device_selection(self):
        self.device_selection_frame = ttk.Frame(self)
        self.device_selection_frame.pack(fill = tkinter.X, pady = 2)

        device_selection_label = ttk.Label(self.device_selection_frame, text="Serial device: ")
        device_selection_label.pack(side = tkinter.LEFT, expand = False)
        
        device_selection_button = ttk.Button(self.device_selection_frame, text="Connect")
        
        device_selection_button.pack(side = tkinter.RIGHT, expand = False)
        
        self.device_selection_entry = ttk.Entry(self.device_selection_frame)
        self.device_selection_entry.pack(fill = tkinter.X, expand = True, padx = 2)
        device_selection_button.config(command = lambda: self.app_controller.handle_connect(self.device_selection_entry.get()))
        
    def init_notebook(self):
        self.notebook = ttk.Notebook(self)
        
    
    def notebook_disable(self):
        self.notebook.pack_forget()
        if self.is_tab_single_wavelength_conf_opened():
            self.notebook.forget(self.tab_single_wavelength_conf)
            self.tab_single_wavelength_conf = None
        if self.is_tab_measurement_opened():
            self.notebook.forget(self.tab_measurement)
            self.tab_measurement = None
    
    def notebook_enable(self):
        self.notebook.pack(fill = tkinter.BOTH, expand = True)
        for i in range(len(self.app_controller.data_model)):
            self.refresh_treeview_row(i)
    
    def refresh_treeview_row(self, index):
        (idx, row) = self.app_controller.get_treeview_row(index)
        self.tab_global_conf_treeview.item(idx, values = row)
     
    def init_tab_internal_voltage_conf(self):
        self.tab_internal_voltage_conf = ttk.Frame(self.notebook)
        self.notebook.add(self.tab_internal_voltage_conf, text='Supply voltage validation')
        button_calibrate = ttk.Button(self.tab_internal_voltage_conf, text="Calibrate")
        button_calibrate.config(command = self.app_controller.calibrate_internal_voltage)
        button_calibrate.grid(row = 0, column = 1, padx = 1, pady = 1)
        
        button_save = ttk.Button(self.tab_internal_voltage_conf, text="Save")
        button_save.config(command = self.app_controller.save_internal_voltage)
        button_save.grid(row = 0, column = 2, padx = 1, pady = 1)
           
        checkbox_vol_validation = ttk.Checkbutton(self.tab_internal_voltage_conf, text="Should use voltage validation during measurement")
        self.intvar_vol_validation = tkinter.IntVar(checkbox_vol_validation)
        vol_validation_callback = lambda *args: self.app_controller.handle_vol_validation_state_changed(self.intvar_vol_validation.get())
        self.intvar_vol_validation.trace_add("write", vol_validation_callback)
        self.intvar_vol_validation.set(0)
        checkbox_vol_validation.config(variable = self.intvar_vol_validation)
        checkbox_vol_validation.grid(row = 0, column = 0, padx = 1, pady = 1)
            
        
    def init_tab_global_presets(self):
        self.tab_global_conf = ttk.Frame(self.notebook)
        self.notebook.add(self.tab_global_conf, text='Presets')
        
        self.tab_global_conf_treeview = ttk.Treeview(self.tab_global_conf, selectmode='browse')
        self.tab_global_conf_treeview.bind("<Button-3>", self.handle_global_conf_left_click)
        self.tab_global_conf_treeview.pack(fill = tkinter.BOTH, expand = True)
        self.tab_global_conf_treeview['columns'] = ('id', 'wavelength', 'zero_error', 'gain_error')
        
        self.tab_global_conf_treeview.column('#0', width = 0, stretch = tkinter.NO)
        self.tab_global_conf_treeview.column('id', anchor = tkinter.CENTER, width = 30, stretch = tkinter.NO)
        self.tab_global_conf_treeview.column('wavelength', anchor = tkinter.CENTER, width = 100)
        self.tab_global_conf_treeview.column('zero_error', anchor = tkinter.CENTER, width = 100)
        self.tab_global_conf_treeview.column('gain_error', anchor = tkinter.CENTER, width = 100)
        
        self.tab_global_conf_treeview.heading('#0', text = '', anchor = tkinter.CENTER)
        self.tab_global_conf_treeview.heading('id', text = 'Id', anchor = tkinter.CENTER)
        self.tab_global_conf_treeview.heading('wavelength', text = 'Wavelength', anchor = tkinter.CENTER)
        self.tab_global_conf_treeview.heading('zero_error', text = 'Zero error', anchor = tkinter.CENTER)
        self.tab_global_conf_treeview.heading('gain_error', text = 'Gain error', anchor = tkinter.CENTER)
        
        for i in range(self.app_controller.treeview_rows_count()):
            (row_id, row) = self.app_controller.get_treeview_row(i)
            self.tab_global_conf_treeview.insert(iid=row_id, parent='', index= row_id, values = row)
    
    def is_tab_single_wavelength_conf_opened(self):
        return hasattr(self, 'tab_single_wavelength_conf') and self.tab_single_wavelength_conf != None
    
    def is_tab_measurement_opened(self):
        return hasattr(self, 'tab_measurement') and self.tab_measurement != None
    
    def handle_global_conf_left_click(self, event):
        edit_state = tkinter.NORMAL
        measurement_state = tkinter.NORMAL
        show_params_state = tkinter.NORMAL
        if self.is_tab_single_wavelength_conf_opened():
            edit_state = tkinter.DISABLED
            measurement_state = tkinter.DISABLED

        if self.is_tab_measurement_opened():
            measurement_state = tkinter.DISABLED
            edit_state = tkinter.DISABLED
        
        iid = self.tab_global_conf_treeview.focus()
        
        if iid == '' or int(iid) >= self.app_controller.treeview_rows_count():
            measurement_state = tkinter.DISABLED
            edit_state = tkinter.DISABLED
            show_params_state = tkinter.DISABLED
            
        try:
            menu = tkinter.Menu(root, tearoff = 0)
            edit_cmd = lambda: self.open_single_wavelength_config(int(iid))
            menu.add_command(label = "Edit", command = edit_cmd, state = edit_state)
            menu.add_command(label = "Measurement", command = self.handle_open_mesurement, state = measurement_state)
            show_params_cmd = lambda: self.app_controller.handle_show_parameters(int(iid))
            menu.add_command(label = "Show parameters", command =show_params_cmd, state = show_params_state)
            menu.tk_popup(event.x_root, event.y_root)
            
        finally:
            menu.grab_release()
            
            
    def open_single_wavelength_config(self, iid):
        self.tab_single_wavelength_conf = ttk.Frame(self.notebook)
        self.notebook.add(self.tab_single_wavelength_conf, text = "Configuration {}".format(int(iid) + 1))
        self.tab_single_wavelength_conf.columnconfigure(1, weight=1)
        self.tab_single_wavelength_conf.rowconfigure(3, weight=1)
        self.notebook.select(self.tab_single_wavelength_conf)
        
        label_wavelength = ttk.Label(self.tab_single_wavelength_conf, text='Wavelength: ')
        label_wavelength.grid(row = 0, padx = 1, pady = 1)
        
        entry_wavelength = ttk.Entry(self.tab_single_wavelength_conf)
        strvar_wavelength = tkinter.StringVar(entry_wavelength)
        entry_wavelength.config(textvariable = strvar_wavelength)
        if not self.app_controller.get_data_row(iid).get_wavelength().is_present():
            strvar_wavelength.set("")
        else:
            strvar_wavelength.set(str(self.app_controller.get_data_row(iid).get_wavelength().get()))
        entry_wavelength.grid(row = 0, column = 1, sticky = tkinter.E + tkinter.W, padx = 1, pady = 1)
        button_save_wavelength = ttk.Button(self.tab_single_wavelength_conf, text = "Apply")
        button_save_wavelength.config(command = lambda: self.app_controller.save_wavelength(strvar_wavelength.get(), iid))
        button_save_wavelength.grid(row = 0, column = 2, padx = 1, pady = 1)
        button_read_wavelength = ttk.Button(self.tab_single_wavelength_conf, text = "Read")
        button_read_wavelength.config(command = lambda: self.app_controller.read_wavelength(strvar_wavelength, iid))
        button_read_wavelength.grid(row = 0, column = 3, padx = 1, pady = 1)
        
        label_zero_error = ttk.Label(self.tab_single_wavelength_conf, text='Zero error: ')
        label_zero_error.grid(row = 1, padx = 1, pady = 1)
        entry_zero_error = ttk.Entry(self.tab_single_wavelength_conf)
        strvar_zero_error = tkinter.StringVar(entry_zero_error)
        entry_zero_error.config(textvariable = strvar_zero_error)
        if not self.app_controller.get_data_row(iid).get_zero_error().is_present():
            strvar_zero_error.set("")
        else:
            strvar_zero_error.set(str(self.app_controller.get_data_row(iid).get_zero_error().get()))
        entry_zero_error.grid(row = 1, column = 1, sticky = tkinter.E + tkinter.W, padx = 1, pady = 1)
        button_apply_zero_error = ttk.Button(self.tab_single_wavelength_conf, text = "Apply")
        button_apply_zero_error.config(command = lambda: self.app_controller.save_zero_error(strvar_zero_error.get(), iid))
        button_apply_zero_error.grid(row = 1, column = 2, padx = 1, pady = 1)
        button_read_zero_error = ttk.Button(self.tab_single_wavelength_conf, text = "Read")
        button_read_zero_error.config(command = lambda: self.app_controller.read_zero_error(strvar_zero_error, iid))
        button_read_zero_error.grid(row = 1, column = 3, padx = 1, pady = 1)
        button_measure_zero_error = ttk.Button(self.tab_single_wavelength_conf, text = "Measure")
        button_measure_zero_error.config(command = lambda: self.app_controller.measure_zero_error(strvar_zero_error, iid))
        button_measure_zero_error.grid(row = 1, column = 4, padx = 1, pady = 1)
        
        label_gain_error = ttk.Label(self.tab_single_wavelength_conf, text='Gain error: ')
        label_gain_error.grid(row = 2, padx = 1, pady = 1)
        entry_gain_error = ttk.Entry(self.tab_single_wavelength_conf)
        strvar_gain_error = tkinter.StringVar(entry_gain_error)
        entry_gain_error.config(textvariable = strvar_gain_error)
        if not self.app_controller.get_data_row(iid).get_gain_error().is_present():
            strvar_gain_error.set("")
        else:
            strvar_gain_error.set(str(self.app_controller.get_data_row(iid).get_gain_error().get()))
        entry_gain_error.grid(row = 2, column = 1, sticky = tkinter.E + tkinter.W, padx = 1, pady = 1)
        button_apply_gain_error = ttk.Button(self.tab_single_wavelength_conf, text = "Apply")
        button_apply_gain_error.config(command = lambda: self.app_controller.save_gain_error(strvar_gain_error.get(), iid))
        button_apply_gain_error.grid(row = 2, column = 2, padx = 1, pady = 1)
        button_read_gain_error = ttk.Button(self.tab_single_wavelength_conf, text = "Read")
        button_read_gain_error.config(command = lambda: self.app_controller.read_gain_error(strvar_gain_error, iid))
        button_read_gain_error.grid(row = 2, column = 3, padx = 1, pady = 1)
        
        button_save_exit = ttk.Button(self.tab_single_wavelength_conf, text = "Save and exit")
        button_save_exit.configure(command = lambda: self.handle_save_exit_edit_config(iid))
        button_save_exit.grid(row = 4, column = 2, columnspan=2, padx = 1, pady = 1, sticky = tkinter.E)
        button_save_exit.config(width = 15)
        button_exit = ttk.Button(self.tab_single_wavelength_conf, text = "Exit")
        button_exit.configure(command = self.handle_exit_edit_config)
        button_exit.grid(row = 4, column = 4, padx = 1, pady = 1)
    
    def handle_exit_edit_config(self):
        self.notebook.forget(self.tab_single_wavelength_conf)
        self.tab_single_wavelength_conf = None
    
    def handle_save_exit_edit_config(self, idx):
        self.app_controller.handle_commit_config_params(idx)
        self.handle_exit_edit_config(self)
        
    def handle_open_mesurement(self):
        self.tab_measurement = ttk.Frame(self.notebook)
        self.tab_measurement.rowconfigure(2, weight = 1)
        self.tab_measurement.rowconfigure(4, weight = 1)
        self.tab_measurement.columnconfigure(2, weight = 1)
        iid = self.tab_global_conf_treeview.focus()
        self.notebook.add(self.tab_measurement, text = "Measurement {}".format(int(iid)+1))
        self.notebook.select(self.tab_measurement)
        
        checkbox_save_to_file = ttk.Checkbutton(self.tab_measurement, text="Save to file")
        intvar_save_to_file = tkinter.IntVar(checkbox_save_to_file, value=0)
        checkbox_save_to_file.config(variable = intvar_save_to_file)
        checkbox_save_to_file.grid(row=0, padx = 1, pady = 1)
        entry_file = ttk.Entry(self.tab_measurement)
        strvar_entry_file = tkinter.StringVar(entry_file)
        entry_file.config(textvariable = strvar_entry_file)
        entry_file.grid(row = 1, column = 0, padx = 1, pady = 1, columnspan = 4, sticky = tkinter.E + tkinter.W)
        
        def handle_select_file():
            new_name = filedialog.asksaveasfilename()
            strvar_entry_file.set(new_name)
        button_select_file = ttk.Button(self.tab_measurement, text = "Select file")
        button_select_file.configure(command = handle_select_file)
        button_select_file.grid(row = 1, column = 3, padx = 1, pady = 1)
        
        def save_to_file_callback(*args):
            objects_to_toggle = [button_select_file, entry_file]
            self.toggle_save_to_file_option(intvar_save_to_file.get(), objects_to_toggle)
        intvar_save_to_file.trace_add("write", save_to_file_callback)
        intvar_save_to_file.set(0)
        start_button = ttk.Button(self.tab_measurement, text = "Start")
        
        value_frame = ttk.Frame(self.tab_measurement)
        lbl_value = ttk.Label(value_frame)
        def set_value(value):
            lbl_value.config(text = value)
        
        def start_measurement_callback():
            filename = entry_file.get() if intvar_save_to_file.get() == 1 else None
            self.app_controller.handle_start_measurement(int(iid), filename, set_value)
            
        start_button.config(command = start_measurement_callback)
        start_button.grid(row = 5, column = 0, padx = 1, pady = 1, sticky = tkinter.E)
        stop_button = ttk.Button(self.tab_measurement, text = "Stop")
        stop_button.config(command = self.app_controller.handle_stop_measurement)
        stop_button.grid(row = 5, column = 1, padx = 1, pady = 1, sticky = tkinter.W)
        exit_button = ttk.Button(self.tab_measurement, text = "Exit")
        exit_button.config(command = self.handle_exit_measurement)
        exit_button.grid(row = 5, column = 3, padx = 1, pady = 1, sticky = tkinter.E)
        
        value_frame.grid(row = 3, column = 1, columnspan=2, sticky = tkinter.W+tkinter.E)
        value_frame.columnconfigure(0, weight = 1)
        value_frame.columnconfigure(2, weight = 1)
        
        
        lbl_value.grid(row = 0, column = 1)
        lbl_value.config(font = ("Arial", 24))
        set_value("--")
        
    def toggle_save_to_file_option(self, value, objects_to_toggle):
        for object in objects_to_toggle:
            object.config(state = tkinter.DISABLED if value == 0 else tkinter.NORMAL)
    
    def handle_exit_measurement(self):
        if self.app_controller.is_measurement_running():
            self.app_controller.handle_stop_measurement()
        self.notebook.forget(self.tab_measurement)
        self.tab_measurement = None

    def dialog_ensure_enter_measurement(self):
        result = tkmb.askokcancel(title= "Question",message="Voltage checking is not enabled. Do you really wish to proceed?")
        if result == 'yes':
            return True
        return False
   
    def dialog_error_connection(self):
        tkmb.showerror("Error", "Cannot connect with device")
        self.notebook_disable()
    
    def dialog_error(self, msg):
        tkmb.showerror("Error", msg)
        self.notebook_disable()
        
    

root = tkinter.Tk()
root.geometry("600x400")
main_frame = MainFrame(root, ApplicationController())
root.mainloop()