# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""ctypes mirrors of the public grain structures shared by the cross-check scripts."""
import ctypes as C
import struct


class Rect(C.Structure):
    _fields_ = [('x', C.c_double), ('y', C.c_double), ('width', C.c_double), ('height', C.c_double)]


class Point(C.Structure):
    _fields_ = [('x', C.c_float), ('y', C.c_float), ('width', C.c_double),
                ('pressure', C.c_float), ('speed', C.c_float)]


class Mask(C.Structure):
    _fields_ = [('bytes', C.POINTER(C.c_uint8)), ('width', C.c_size_t), ('height', C.c_size_t),
                ('stride', C.c_size_t), ('box', Rect)]


def f32(x):
    """Round a Python float to the nearest binary32 value."""
    return struct.unpack('f', struct.pack('f', x))[0]
