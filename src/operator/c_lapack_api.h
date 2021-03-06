/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file c_lapack_api.h
 * \brief Unified interface for CPU-based LAPACK calls.
 *  Purpose is to hide the platform specific differences.
 */
#ifndef MXNET_OPERATOR_C_LAPACK_API_H_
#define MXNET_OPERATOR_C_LAPACK_API_H_

// Manually maintained list of LAPACK interfaces that can be used
// within MXNET. Conventions:
//    - We should only import LAPACK-functions that are useful and
//      ensure that we support them most efficiently on CPU/GPU. As an
//      example take "potrs": It can be emulated by two calls to
//      "trsm" (from BLAS3) so not really needed from functionality point
//      of view. In addition, trsm on GPU supports batch-mode processing
//      which is much more efficient for a bunch of smaller matrices while
//      there is no such batch support for potrs. As a result, we may
//      not support "potrs" internally and if we want to expose it to the user as
//      a convenience operator at some time, then we may implement it internally
//      as a sequence of trsm.
//    - Interfaces must be compliant with lapacke.h in terms of signature and
//      naming conventions so wrapping a function "foo" which has the
//      signature
//         lapack_int LAPACKE_foo(int, char, lapack_int, float* , lapack_int)
//      within lapacke.h should result in a wrapper with the following signature
//         int MXNET_LAPACK_foo(int, char, int, float* , int)
//      Note that function signatures in lapacke.h will always have as first
//      argument the storage order (row/col-major). All wrappers have to support
//      that argument. The underlying fortran functions will always assume a
//      column-major layout.
//    - In the (usual) case that a wrapper is called specifying row-major storage
//      order of input/output data, there are two ways to handle this:
//        1) The wrapper may support this without allocating any additional memory
//           for example by exploiting the fact that a matrix is symmetric and switching
//           certain flags (upper/lower triangular) when calling the fortran code.
//        2) The wrapper may cause a runtime error. In that case it should be clearly
//           documented that these functions do only support col-major layout.
//      Rationale: This is a low level interface that is not expected to be called
//      directly from many upstream functions. Usually all calls should go through
//      the tensor-based interfaces in linalg.h which simplify calls to lapack further
//      and are better suited to handle additional transpositions that may be necessary.
//      Also we want to push allocation of temporary storage higher up in order to
//      allow more efficient re-use of temporal storage. And don't want to plaster
//      these interfaces here with additional requirements of providing buffers.
//    - It is desired to add some basic checking in the C++-wrappers in order
//      to catch simple mistakes when calling these wrappers.
//    - Must support compilation without lapack-package but issue runtime error in this case.

#include <dmlc/logging.h>
#include "mshadow/tensor.h"

using namespace mshadow;

extern "C" {

  // Fortran signatures
  #define MXNET_LAPACK_FSIGNATURE1(func, dtype) \
    void func##_(char *uplo, int *n, dtype *a, int *lda, int *info);

  MXNET_LAPACK_FSIGNATURE1(spotrf, float)
  MXNET_LAPACK_FSIGNATURE1(dpotrf, double)
  MXNET_LAPACK_FSIGNATURE1(spotri, float)
  MXNET_LAPACK_FSIGNATURE1(dpotri, double)

  void dposv_(char *uplo, int *n, int *nrhs,
    double *a, int *lda, double *b, int *ldb, int *info);

  void sposv_(char *uplo, int *n, int *nrhs,
    float *a, int *lda, float *b, int *ldb, int *info);
}

#define MXNET_LAPACK_ROW_MAJOR 101
#define MXNET_LAPACK_COL_MAJOR 102

#define CHECK_LAPACK_UPLO(a) \
  CHECK(a == 'U' || a == 'L') << "neither L nor U specified as triangle in lapack call";

inline char loup(char uplo, bool invert) { return invert ? (uplo == 'U' ? 'L' : 'U') : uplo; }


/*!
 * \brief Transpose matrix data in memory
 *
 * Equivalently we can see it as flipping the layout of the matrix
 * between row-major and column-major.
 *
 * \param m number of rows of input matrix a
 * \param n number of columns of input matrix a
 * \param b output matrix
 * \param ldb leading dimension of b
 * \param a input matrix
 * \param lda leading dimension of a
 */
template <typename xpu, typename DType>
inline void flip(int m, int n, DType *b, int ldb, DType *a, int lda);

template <>
inline void flip<cpu, float>(int m, int n,
  float *b, int ldb, float *a, int lda) {
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < n; ++j)
      b[j * ldb + i] = a[i * lda + j];
}

template <>
inline void flip<cpu, double>(int m, int n,
  double *b, int ldb, double *a, int lda) {
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < n; ++j)
      b[j * ldb + i] = a[i * lda + j];
}


#if MXNET_USE_LAPACK

  // These functions can be called with either row- or col-major format.
  #define MXNET_LAPACK_CWRAPPER1(func, dtype) \
  inline int MXNET_LAPACK_##func(int matrix_layout, char uplo, int n, dtype *a, int lda) { \
    CHECK_LAPACK_UPLO(uplo); \
    char o(loup(uplo, (matrix_layout == MXNET_LAPACK_ROW_MAJOR))); \
    int ret(0); \
    func##_(&o, &n, a, &lda, &ret); \
    return ret; \
  }
  MXNET_LAPACK_CWRAPPER1(spotrf, float)
  MXNET_LAPACK_CWRAPPER1(dpotrf, double)
  MXNET_LAPACK_CWRAPPER1(spotri, float)
  MXNET_LAPACK_CWRAPPER1(dpotri, double)

  inline int mxnet_lapack_sposv(int matrix_layout, char uplo, int n, int nrhs,
    float *a, int lda, float *b, int ldb) {
    int info;
    if (matrix_layout == MXNET_LAPACK_ROW_MAJOR) {
      // Transpose b to b_t of shape (nrhs, n)
      float *b_t = new float[nrhs * n];
      flip<cpu, float>(n, nrhs, b_t, n, b, ldb);
      sposv_(&uplo, &n, &nrhs, a, &lda, b_t, &n, &info);
      flip<cpu, float>(nrhs, n, b, ldb, b_t, n);
      delete [] b_t;
      return info;
    }
    sposv_(&uplo, &n, &nrhs, a, &lda, b, &ldb, &info);
    return info;
  }

  inline int mxnet_lapack_dposv(int matrix_layout, char uplo, int n, int nrhs,
    double *a, int lda, double *b, int ldb) {
    int info;
    if (matrix_layout == MXNET_LAPACK_ROW_MAJOR) {
      // Transpose b to b_t of shape (nrhs, n)
      double *b_t = new double[nrhs * n];
      flip<cpu, double>(n, nrhs, b_t, n, b, ldb);
      dposv_(&uplo, &n, &nrhs, a, &lda, b_t, &n, &info);
      flip<cpu, double>(nrhs, n, b, ldb, b_t, n);
      delete [] b_t;
      return info;
    }
    dposv_(&uplo, &n, &nrhs, a, &lda, b, &ldb, &info);
    return info;
  }

#else

  // use pragma message instead of warning
  #pragma message("Warning: lapack usage not enabled, linalg-operators will not be available." \
     " Ensure that lapack library is installed and build with USE_LAPACK=1 to get lapack" \
     " functionalities.")

  // Define compilable stubs.
  #define MXNET_LAPACK_CWRAPPER1(func, dtype) \
  inline int MXNET_LAPACK_##func(int matrix_layout, char uplo, int n, dtype* a, int lda) { \
    LOG(FATAL) << "MXNet build without lapack. Function " << #func << " is not available."; \
    return 1; \
  }

  #define MXNET_LAPACK_UNAVAILABLE(func) \
  inline int mxnet_lapack_##func(...) { \
    LOG(FATAL) << "MXNet build without lapack. Function " << #func << " is not available."; \
    return 1; \
  }

  MXNET_LAPACK_CWRAPPER1(spotrf, float)
  MXNET_LAPACK_CWRAPPER1(dpotrf, double)
  MXNET_LAPACK_CWRAPPER1(spotri, float)
  MXNET_LAPACK_CWRAPPER1(dpotri, double)

  MXNET_LAPACK_UNAVAILABLE(sposv)
  MXNET_LAPACK_UNAVAILABLE(dposv)

#endif

template <typename DType>
inline int MXNET_LAPACK_posv(int matrix_layout, char uplo, int n, int nrhs,
  DType *a, int lda, DType *b, int ldb);

template <>
inline int MXNET_LAPACK_posv<float>(int matrix_layout, char uplo, int n,
  int nrhs, float *a, int lda, float *b, int ldb) {
  return mxnet_lapack_sposv(matrix_layout, uplo, n, nrhs, a, lda, b, ldb);
}

template <>
inline int MXNET_LAPACK_posv<double>(int matrix_layout, char uplo, int n,
  int nrhs, double *a, int lda, double *b, int ldb) {
  return mxnet_lapack_dposv(matrix_layout, uplo, n, nrhs, a, lda, b, ldb);
}

#endif  // MXNET_OPERATOR_C_LAPACK_API_H_
