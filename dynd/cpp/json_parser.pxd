from libcpp.string cimport string

from ..config cimport translate_exception
from .type cimport type

cdef extern from 'dynd/json_parser.hpp' namespace 'dynd::ndt::json':
    type discover(string) except +translate_exception
