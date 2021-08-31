//
// Created by genshen on 2021/07/06.
//

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <hip/hip_runtime.h>
#include <hip/hip_runtime_api.h>

#include "../common/utils.h"

template <typename I, typename T, int NNZ_PER_BLOCK, int THREADS, int VECTOR_SIZE>
__device__ __forceinline__ void
flat_reduce_in_vector(const int n_reduce_rows_num, const int tid_in_block, const int bp_index,
                      const I reduce_start_row_id, const I reduce_end_row_id, const T alpha,
                      const I *__restrict__ row_offset, const T *__restrict__ _lds_shared_data, T *__restrict__ y) {
  if (n_reduce_rows_num <= THREADS && tid_in_block < THREADS) {
    // use `vec_num` vectors, each vector can process reduction of one row by involving `vec_size` threads.
    const I vec_size = VECTOR_SIZE;
    const I vec_num = THREADS / vec_size;
    const I vec_id = tid_in_block / vec_size;
    const I tid_in_vec = tid_in_block % vec_size;

    const I reduce_row_id = reduce_start_row_id + vec_id;
    if (reduce_row_id < reduce_end_row_id) {
      const I reduce_start_inx = max(0, row_offset[reduce_row_id] - bp_index * NNZ_PER_BLOCK);
      const I reduce_end_inx = min(NNZ_PER_BLOCK, row_offset[reduce_row_id + 1] - bp_index * NNZ_PER_BLOCK);
      // reduce LDS via vectors.
      T sum = static_cast<T>(0);
      for (int i = reduce_start_inx + tid_in_vec; i < reduce_end_inx; i += vec_size) {
        sum += _lds_shared_data[i];
      }
      // reduce in vector
      for (int i = vec_size >> 1; i > 0; i >>= 1) {
        sum += __shfl_down(sum, i, vec_size);
      }
      // store value
      if (tid_in_vec == 0) {
        atomicAdd(y + reduce_row_id, alpha * sum);
      }
    }
  }
}

constexpr int FLAT_REDUCE_METHOD = 0;

/**
 * We solve SpMV with flat method.
 *
 * @tparam WF_SIZE threads in one wavefront
 * @tparam R rounds, process R*THREADS elements in each loop.
 * @tparam BLOCKS total blocks on one GPU (blocks in one grid).
 * @tparam THREADS threads in each block.
 * @tparam I type of data in matrix index
 * @tparam T type of data in matrix A, vector x, vector y and alpha, beta.
 * @param m rows in matrix A
 * @param alpha alpha value
 * @param beta beta value
 * @param row_offset row offset array of csr matrix A
 * @param csr_col_ind col index of csr matrix A
 * @param csr_val matrix A in csr format
 * @param x vector x
 * @param y vector y
 * @return
 */
template <int WF_SIZE, int R, int BLOCKS, int THREADS, typename I, typename T>
__global__ void spmv_flat_kernel(int m, const T alpha, const T beta, const I *__restrict__ row_offset,
                                 const I *__restrict__ break_points, const I *__restrict__ csr_col_ind,
                                 const T *__restrict__ csr_val, const T *__restrict__ x, T *__restrict__ y) {
  const int global_thread_id = threadIdx.x + THREADS * blockIdx.x;
  constexpr int global_threads_num = BLOCKS * THREADS;

  const int wf_id_in_block = blockIdx.x / WF_SIZE;         // wavefront id in block
  const int block_id = blockIdx.x;                         // block id
  const int tid_in_block = threadIdx.x % THREADS; // thread id in one block

  constexpr unsigned int shared_len = THREADS * R; // 64 * 1024 / (BLOCKS / 64) / sizeof(T); // nnz per block
  __shared__ T shared_val[shared_len];
  constexpr int nnz_per_block = THREADS * R;
  const I last_element_index = row_offset[m];

  I bp_index = block_id;
  // each block process THREADS * R elements.
  for (int k = nnz_per_block * block_id; k < last_element_index; k += BLOCKS * nnz_per_block) {
    __syncthreads();
#pragma unroll
    for (int i = 0; i < R; i++) {
      const I shared_inx = tid_in_block + i * THREADS;
      const I index = min(k + shared_inx, last_element_index - 1);
      shared_val[shared_inx] = csr_val[index] * x[csr_col_ind[index]];
    }
    __syncthreads();

    // reduce via LDS.
    const I reduce_start_row_id = min(break_points[bp_index], m);
    I reduce_end_row_id = min(break_points[bp_index + 1], m);
    // if it is the last block
    if (reduce_end_row_id == 0) {
      reduce_end_row_id = m;
    }
    // if start of the next block cuts some row.
    // or some row cross multiple blocks.
    if (row_offset[reduce_end_row_id] % nnz_per_block != 0 || reduce_end_row_id == reduce_start_row_id) {
      reduce_end_row_id = min(reduce_end_row_id + 1, m); // make sure `reduce_end_row_id` is not large than m
    }

    // reduce and store result to memory
    if (FLAT_REDUCE_METHOD == 0) {
      const I n_reduce_rows_num = reduce_end_row_id - reduce_start_row_id;
      (flat_reduce_in_vector<I, T, nnz_per_block, THREADS, 2>)(n_reduce_rows_num, tid_in_block, bp_index,
                                                                reduce_start_row_id, reduce_end_row_id, alpha,
                                                                row_offset, shared_val, y);
    } else {
      // direct reduction
      I reduce_row_id = reduce_start_row_id + tid_in_block;
      if (reduce_row_id < reduce_end_row_id) {
        T sum = static_cast<T>(0);
        // what if it has a very long row? which means `reduce_start_row_id == reduce_end_row_id`.
        const I reduce_start_inx = max(0, row_offset[reduce_row_id] - bp_index * nnz_per_block);
        const I reduce_end_inx = min(nnz_per_block, row_offset[reduce_row_id + 1] - bp_index * nnz_per_block);
        for (int i = reduce_start_inx; i < reduce_end_inx; i++) {
          sum += shared_val[i];
        }
        atomicAdd(y + reduce_row_id, alpha * sum);
        // y[reduce_row_id] = device_fma(beta, y[reduce_row_id], alpha * sum);
      }
      reduce_row_id += THREADS;
      for (; reduce_row_id < reduce_end_row_id; reduce_row_id += THREADS) {
        T sum = static_cast<T>(0);
        const I reduce_start_inx = max(0, row_offset[reduce_row_id] - bp_index * nnz_per_block);
        const I reduce_end_inx = min(nnz_per_block, row_offset[reduce_row_id + 1] - bp_index * nnz_per_block);
        for (int i = reduce_start_inx; i < reduce_end_inx; i++) {
          sum += shared_val[i];
        }
        atomicAdd(y + reduce_row_id, alpha * sum);
      }
    }

    bp_index += BLOCKS;
  }
}

template <int BREAK_STRIDE, int BLOCKS, typename I>
__global__ void pre_calc_break_point(const I *__restrict__ row_ptr, const I m, I *__restrict__ break_points,
                                     const I bp_len) {
  const int global_thread_id = threadIdx.x + blockDim.x * blockIdx.x;
  const int global_threads_num = blockDim.x * gridDim.x;

  constexpr I break_stride = BREAK_STRIDE;
  if (global_thread_id == 0) {
    break_points[0] = 0; // start row of the block 0 and the first round.
  }

  for (int i = global_thread_id; i < m; i += global_threads_num) {
    // for first element of row i and row i+1, they belong to different blocks.
    if (row_ptr[i] / break_stride != row_ptr[i + 1] / break_stride) { // fixme: step may be not 1
      // record the row id of the first element in the block.
      // note: a row can cross multiple blocks.
      for (int j = row_ptr[i] / break_stride + 1; j <= row_ptr[i + 1] / break_stride; j++) {
        break_points[j] = i;
      }
      if (row_ptr[i + 1] % break_stride == 0) {
        break_points[row_ptr[i + 1] / break_stride] += 1;
      }
    }
  }
}

#define FLAT_KERNEL_WRAPPER(R, BLOCKS, THREADS)                                                                        \
  (spmv_flat_kernel<64, R, BLOCKS, THREADS, int, double>)<<<(BLOCKS), (THREADS)>>>(                                    \
      m, alpha, beta, rowptr, break_points, colindex, value, x, y)
