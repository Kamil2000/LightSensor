import serial

class StreamWrapper:

    def __init__(self, stream, buffer_size):
        self.stream = stream
        self.buffer_limit = buffer_size
        self.buffer = b''

    def _readline_impl(self):
        read_data = b''
        try:
            read_data = self.stream.read(100)
        except serial.SerialTimeoutException as _:
            pass
        if read_data == b'':
            return b''
        index_nl = None
        try:
            index_nl = read_data.index(b'\n')
        except ValueError as _:
            self.buffer += read_data
            return b''
        result = self.buffer + read_data[:index_nl+1]
        self.buffer = read_data[index_nl+1:]
        return result

    def readline(self):
        result = self._readline_impl()
        if len(self.buffer) > self.buffer_limit:
            raise ValueError("buffer limit reached")
        return result
        
    def get_stream(self):
        return self.stream

if __name__ == "__main__":
    class MockStream:
        def __init__(self):
            self.readline_results = []
            self.index = 0
            pass

        def set_readline_results(self, value):
            self.readline_results = value

        def get_readline_results(self):
            return self.readline_results

        def get_index(self):
            return self.index

        def set_index(self, value):
            self.index = value

        def read(self, count):
            count = count
            self.index += 1
            return  self.readline_results[self.index-1]



    def verify_readline():
        mock_stream = MockStream()
        list_of_str = [
            b'1', b'2', b'23', b'11\n11',
            b'128\n', b'23465\n', b'2222', b'\n'
        ]
        mock_stream.set_readline_results(list_of_str)
        stream_reader = StreamWrapper(mock_stream, 2000)
        assert stream_reader.readline() == b''
        assert stream_reader.readline() == b''
        assert stream_reader.readline() == b''
        assert stream_reader.readline() == b'122311\n'
        assert stream_reader.readline() == b'11128\n'
        assert stream_reader.readline() == b'23465\n'
        assert stream_reader.readline() == b''
        assert stream_reader.readline() == b'2222\n'
        print("test passed")

    verify_readline()








