"""ctypes layouts matching the current firmware peak and DSP interfaces."""
import ctypes as c

class DspPeak(c.Structure):
    _fields_ = [('position', c.c_float * 3), ('quality', c.c_uint32)]


class DspPeaks(c.Structure):
    _fields_ = [('peak', DspPeak * 3), ('count', c.c_uint32), ('overflow', c.c_uint32)]


class Peak(c.Structure):
    _fields_ = [('offset', c.c_int32 * 3), ('quality', c.c_uint32)]


class Peaks(c.Structure):
    _fields_ = [('peak', Peak * 3), ('count', c.c_uint32), ('overflow', c.c_uint32)]


