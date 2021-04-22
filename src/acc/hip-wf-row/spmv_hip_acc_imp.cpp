//
// Created by genshen on 2021/4/17.
//

#include <iostream>
#include <stdio.h>  // printf
#include <stdlib.h> // EXIT_FAILURE

#include <hip/hip_runtime.h>
#include <hip/hip_runtime_api.h> // hipMalloc, hipMemcpy, etc.

#include "utils.h"

typedef int type_index;
typedef double type_values;

/**
 * calculate: Y = alpha * A*X+beta * y
 * @tparam BLOCK_SIZE block size
 * @tparam WF_SIZE wavefront size
 * @tparam I type of indexing
 * @tparam J type of csr col indexing
 * @tparam T type of data in matrix
 * @param m rows of matrix A.
 * @param alpha alpha value
 * @param beta beta value
 * @param row_offset row offset array of csr matrix A
 * @param csr_col_ind col index of csr matrix A
 * @param csr_val matrix A in csr format
 * @param x  vector x
 * @param y vector y
 * @return
 */
template <unsigned int BLOCK_SIZE, unsigned int WF_SIZE, typename I, typename J, typename T>
static __device__ void csr_spmv_device(J m, T alpha, T beta, const I *row_offset, const J *csr_col_ind,
                                       const T *csr_val, const T *x, T *y) {
  int lid = hipThreadIdx_x & (WF_SIZE - 1); // local id in the wavefront

  J gid = hipBlockIdx_x * BLOCK_SIZE + hipThreadIdx_x; // global thread id
  J nwf = hipGridDim_x * BLOCK_SIZE / WF_SIZE;         // number of wavefront, or step length

  // Loop over rows
  for (J row = gid / WF_SIZE; row < m; row += nwf) {
    // Each wavefront processes one row
    I row_start = row_offset[row];
    I row_end = row_offset[row + 1];

    T sum = static_cast<T>(0);

    // Loop over non-zero elements
    for (I j = row_start + lid; j < row_end; j += WF_SIZE) {
      sum = device_fma(alpha * csr_val[j], device_ldg(x + csr_col_ind[j]), sum);
    }

    // Obtain row sum using parallel reduction
    sum = wfreduce_sum<WF_SIZE>(sum);

    // First thread of each wavefront writes result into global memory
    if (lid == WF_SIZE - 1) {
      if (beta == static_cast<T>(0)) {
        y[row] = sum;
      } else {
        y[row] = device_fma(beta, y[row], sum);
      }
    }
  }
}

__global__ void device_sparse_spmv_acc(int trans, const int alpha, const int beta, int m, int n, const int *rowptr,
                                       const int *colindex, const double *value, const double *x, double *y) {
  csr_spmv_device<1024, 64, int, int, double>(m, alpha, beta, rowptr, colindex, value, x, y);
}

void sparse_spmv(int htrans, const int halpha, const int hbeta, int hm, int hn, const int *hrowptr,
                 const int *hcolindex, const double *hvalue, const double *hx, double *hy) {
  device_sparse_spmv_acc<<<1, 1024>>>(htrans, halpha, hbeta, hm, hn, hrowptr, hcolindex, hvalue, hx, hy);
}