//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//
// This header defines some functions to
// interoperate with ctypes
//

#ifndef _DYND__CTYPES_INTEROP_HPP_
#define _DYND__CTYPES_INTEROP_HPP_

#include "config.hpp"

#include <dynd/type.hpp>

namespace pydynd {

/**
 * Struct with data about the _ctypes module.
 */
struct ctypes_info {
  // The _ctypes module (for C-implementation details)
  PyObject *_ctypes;
  // These match the corresponding names within _ctypes.c
  PyObject *PyCData_Type;
  PyObject *PyCStructType_Type;
  PyObject *UnionType_Type;
  PyObject *PyCPointerType_Type;
  PyObject *PyCArrayType_Type;
  PyObject *PyCSimpleType_Type;
  PyObject *PyCFuncPtrType_Type;
};

extern ctypes_info ctypes;

/**
 * Is called by pydynd_init()
 */
void init_ctypes_interop();

/**
 * Constructs a dynd type from a ctypes type object, such
 * as ctypes.c_int, ctypes.c_float, etc.
 */
dynd::ndt::type _type_from_ctypes_cdatatype(PyObject *d);

//////////////////////////////////////////////////////////
// The following emulates a lot of the internal ctypes.h
// API, so we can directly access ctypes data quickly.
//
// TODO: Try this across many versions of Python,
//       tweak it for variations.

union ctypes_value {
  char c[16];
  short s;
  int i;
  long l;
  float f;
  double d;
#ifdef HAVE_LONG_LONG
  PY_LONG_LONG ll;
#endif
  long double D;
};

struct CDataObject {
  PyObject_HEAD char *b_ptr; /* pointer to memory block */
  int b_needsfree;           /* need _we_ free the memory? */
  CDataObject *b_base;       /* pointer to base object or NULL */
  Py_ssize_t b_size;         /* size of memory block in bytes */
  Py_ssize_t b_length;       /* number of references we need */
  Py_ssize_t b_index; /* index of this object into base's b_object list */
  PyObject *
      b_objects; /* dictionary of references we need to keep, or Py_None */
  ctypes_value b_value;
};

struct PyCFuncPtrObject {
  // First part identical to CDataObject
  PyObject_HEAD char *b_ptr; /* pointer to memory block */
  int b_needsfree;           /* need _we_ free the memory? */
  CDataObject *b_base;       /* pointer to base object or NULL */
  Py_ssize_t b_size;         /* size of memory block in bytes */
  Py_ssize_t b_length;       /* number of references we need */
  Py_ssize_t b_index;  /* index of this object into base's b_object list */
  PyObject *b_objects; /* list of references we need to keep */
  ctypes_value b_value;
  /* end of CDataObject, additional fields follow */

  void *thunk;
  PyObject *callable;

  /* These two fields will override the ones in the type's stgdict if
     they are set */
  PyObject *converters;
  PyObject *argtypes;
  PyObject *restype;
  PyObject *checker;
  PyObject *errcheck;
#ifdef _WIN32
  int index;
  void *iid;
#endif
  PyObject *paramflags;
};

struct ctypes_ffi_type {
  size_t size;
  unsigned short alignment;
  unsigned short type;
  ctypes_ffi_type **elements;
};

// These functions correspond to functions or macros in
// CPython's internal "ctypes.h"
inline bool CDataObject_CheckExact(PyObject *v)
{
  return v->ob_type == (PyTypeObject *)ctypes.PyCData_Type;
}
inline bool CDataObject_Check(PyObject *v)
{
  return PyObject_TypeCheck(v, (PyTypeObject *)ctypes.PyCData_Type);
}

/**
 * Gets the signature of the ctypes function pointer object.
 *
 * @cfunc  The ctypes function pointer object
 * @out_returntype  The return type is placed here
 * @out_paramtypes  This is populated with the parameter types
 */
void get_ctypes_signature(PyCFuncPtrObject *cfunc,
                          dynd::ndt::type &out_returntype,
                          std::vector<dynd::ndt::type> &out_paramtypes);

} // namespace pydynd

#endif // _DYND__CTYPES_INTEROP_HPP_
