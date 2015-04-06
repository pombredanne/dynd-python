//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include "numpy_interop.hpp"

#if DYND_NUMPY_INTEROP

#include <numpy/arrayobject.h>
#include <numpy/arrayscalars.h>

#include <dynd/kernels/assignment_kernels.hpp>
#include <dynd/func/elwise.hpp>
#include <dynd/kernels/tuple_assignment_kernels.hpp>
#include <dynd/types/base_struct_type.hpp>

#include "copy_to_numpy_arrfunc.hpp"
#include "copy_to_pyobject_arrfunc.hpp"
#include "utility_functions.hpp"

using namespace std;
using namespace dynd;
using namespace pydynd;

namespace {

struct strided_of_numpy_arrmeta {
  fixed_dim_type_arrmeta sdt[NPY_MAXDIMS];
  copy_to_numpy_arrmeta am;
};

} // anonymous namespace

/**
 * This sets up a ckernel to copy from a dynd array
 * to a numpy array. The destination numpy array is
 * represented by dst_tp being ``void`` and the dst_arrmeta
 * being a pointer to the ``PyArray_Descr *`` of the type for the destination.
 */
intptr_t copy_to_numpy_ck::instantiate(
    const arrfunc_type_data *self_af, const arrfunc_type *af_tp,
    char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
    const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc,
    const ndt::type *src_tp, const char *const *src_arrmeta,
    kernel_request_t kernreq, const eval::eval_context *ectx,
    const nd::array &kwds, const std::map<nd::string, ndt::type> &tp_vars)
{
  if (dst_tp.get_type_id() != void_type_id) {
    stringstream ss;
    ss << "Cannot instantiate arrfunc with signature ";
    ss << af_tp << " with types (";
    ss << src_tp[0] << ") -> " << dst_tp;
    throw type_error(ss.str());
  }

  PyObject *dst_obj = *reinterpret_cast<PyObject *const *>(dst_arrmeta);
  uintptr_t dst_alignment = reinterpret_cast<const uintptr_t *>(dst_arrmeta)[1];

  PyArray_Descr *dtype = reinterpret_cast<PyArray_Descr *>(dst_obj);
  if (!PyDataType_FLAGCHK(dtype, NPY_ITEM_HASOBJECT)) {
    // If there is no object type in the numpy type, get the dynd equivalent
    // type and use it to do the copying
    ndt::type dst_view_tp = ndt_type_from_numpy_dtype(dtype, dst_alignment);
    return make_assignment_kernel(NULL, NULL, ckb, ckb_offset, dst_view_tp,
                                  NULL, src_tp[0], src_arrmeta[0], kernreq,
                                  ectx, nd::array());
  } else if (PyDataType_ISOBJECT(dtype)) {
    const arrfunc_type_data *af =
        static_cast<dynd::nd::arrfunc>(copy_to_pyobject_tuple).get();
    return af->instantiate(
        af, static_cast<dynd::nd::arrfunc>(copy_to_pyobject_tuple).get_type(),
        NULL, ckb, ckb_offset, ndt::make_type<void>(), NULL, nsrc, src_tp,
        src_arrmeta, kernreq, ectx, nd::array(), tp_vars);
  } else if (PyDataType_HASFIELDS(dtype)) {
    if (src_tp[0].get_kind() != struct_kind &&
        src_tp[0].get_kind() != tuple_kind) {
      stringstream ss;
      pyobject_ownref dtype_str(PyObject_Str((PyObject *)dtype));
      ss << "Cannot assign from source dynd type " << src_tp[0]
         << " to numpy type " << pystring_as_string(dtype_str.get());
      throw invalid_argument(ss.str());
    }

    // Get the fields out of the numpy dtype
    vector<PyArray_Descr *> field_dtypes_orig;
    vector<string> field_names_orig;
    vector<size_t> field_offsets_orig;
    extract_fields_from_numpy_struct(dtype, field_dtypes_orig, field_names_orig,
                                     field_offsets_orig);
    intptr_t field_count = field_dtypes_orig.size();
    if (field_count !=
        src_tp[0].extended<base_tuple_type>()->get_field_count()) {
      stringstream ss;
      pyobject_ownref dtype_str(PyObject_Str((PyObject *)dtype));
      ss << "Cannot assign from source dynd type " << src_tp[0]
         << " to numpy type " << pystring_as_string(dtype_str.get());
      throw invalid_argument(ss.str());
    }

    // Permute the numpy fields to match with the dynd fields
    vector<PyArray_Descr *> field_dtypes;
    vector<size_t> field_offsets;
    if (src_tp[0].get_kind() == struct_kind) {
      field_dtypes.resize(field_count);
      field_offsets.resize(field_count);
      for (intptr_t i = 0; i < field_count; ++i) {
        intptr_t src_i =
            src_tp[0].extended<base_struct_type>()->get_field_index(
                field_names_orig[i]);
        if (src_i >= 0) {
          field_dtypes[src_i] = field_dtypes_orig[i];
          field_offsets[src_i] = field_offsets_orig[i];
        } else {
          stringstream ss;
          pyobject_ownref dtype_str(PyObject_Str((PyObject *)dtype));
          ss << "Cannot assign from source dynd type " << src_tp[0]
             << " to numpy type " << pystring_as_string(dtype_str.get());
          throw invalid_argument(ss.str());
        }
      }
    } else {
      // In the tuple case, use position instead of name
      field_dtypes.swap(field_dtypes_orig);
      field_offsets.swap(field_offsets_orig);
    }

    vector<ndt::type> dst_fields_tp(field_count, ndt::make_type<void>());
    vector<copy_to_numpy_arrmeta> dst_arrmeta_values(field_count);
    vector<const char *> dst_fields_arrmeta(field_count);
    for (intptr_t i = 0; i < field_count; ++i) {
      dst_arrmeta_values[i].dst_dtype = field_dtypes[i];
      dst_arrmeta_values[i].dst_alignment = dst_alignment | field_offsets[i];
      dst_fields_arrmeta[i] =
          reinterpret_cast<const char *>(&dst_arrmeta_values[i]);
    }

    const uintptr_t *src_arrmeta_offsets =
        src_tp[0].extended<base_tuple_type>()->get_arrmeta_offsets_raw();
    shortvector<const char *> src_fields_arrmeta(field_count);
    for (intptr_t i = 0; i != field_count; ++i) {
      src_fields_arrmeta[i] = src_arrmeta[0] + src_arrmeta_offsets[i];
    }

    return make_tuple_unary_op_ckernel(
        self_af, af_tp, ckb, ckb_offset, field_count, &field_offsets[0],
        &dst_fields_tp[0], &dst_fields_arrmeta[0],
        src_tp[0].extended<base_tuple_type>()->get_data_offsets(src_arrmeta[0]),
        src_tp[0].extended<base_tuple_type>()->get_field_types_raw(),
        src_fields_arrmeta.get(), kernreq, ectx);
  } else {
    stringstream ss;
    ss << "TODO: implement assign from source dynd type " << src_tp[0]
       << " to numpy type " << pyobject_repr((PyObject *)dtype);
    throw invalid_argument(ss.str());
  }
}

dynd::nd::arrfunc pydynd::copy_to_numpy::make()
{
  return nd::functional::elwise(
      dynd::nd::as_arrfunc<copy_to_numpy_ck>(ndt::type("(Any) -> void"), 0));
}

struct pydynd::copy_to_numpy pydynd::copy_to_numpy;

void pydynd::array_copy_to_numpy(PyArrayObject *dst_arr,
                                 const dynd::ndt::type &src_tp,
                                 const char *src_arrmeta, const char *src_data,
                                 const dynd::eval::eval_context *ectx)
{
  intptr_t dst_ndim = PyArray_NDIM(dst_arr);
  intptr_t src_ndim = src_tp.get_ndim();
  uintptr_t dst_alignment = reinterpret_cast<uintptr_t>(PyArray_DATA(dst_arr));

  strided_of_numpy_arrmeta dst_am_holder;
  const char *dst_am = reinterpret_cast<const char *>(
      &dst_am_holder.sdt[NPY_MAXDIMS - dst_ndim]);
  // Fill in metadata for a multi-dim strided array, corresponding
  // to the numpy array, with a void type at the end for the numpy
  // specific data.
  for (intptr_t i = 0; i < dst_ndim; ++i) {
    fixed_dim_type_arrmeta &am = dst_am_holder.sdt[NPY_MAXDIMS - dst_ndim + i];
    am.stride = PyArray_STRIDE(dst_arr, (int)i);
    dst_alignment |= static_cast<uintptr_t>(am.stride);
    am.dim_size = PyArray_DIM(dst_arr, (int)i);
  }
  ndt::type dst_tp =
      ndt::make_type(dst_ndim, PyArray_SHAPE(dst_arr), ndt::make_type<void>());
  dst_am_holder.am.dst_dtype = PyArray_DTYPE(dst_arr);
  dst_am_holder.am.dst_alignment = dst_alignment;

  const arrfunc_type_data *af = copy_to_numpy.get();
  unary_ckernel_builder ckb;
  af->instantiate(af, copy_to_numpy.get_type(), NULL, &ckb, 0, dst_tp, dst_am,
                  1, &src_tp, &src_arrmeta, kernel_request_single, ectx,
                  nd::array(), std::map<nd::string, ndt::type>());
  ckb((char *)PyArray_DATA(dst_arr), const_cast<char *>(src_data));
}

#endif // DYND_NUMPY_INTEROP
