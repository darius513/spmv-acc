//
// Created by chu genshen on 2021/10/2.
//

#include <hip/hip_runtime.h>
#include <hip/hip_runtime_api.h>

#include "building_config.h"

template <int WF_SIZE, int ROWS_PER_BLOCK, int R, int THREADS, typename I, typename T>
__global__ void line_enhance_kernel(int m, const T alpha, const T beta, const I *__restrict__ row_offset,
                                    const I *__restrict__ csr_col_ind, const T *__restrict__ csr_val,
                                    const T *__restrict__ x, T *__restrict__ y) {
  const int g_tid = threadIdx.x + blockDim.x * blockIdx.x; // global thread id
  const int g_bid = blockIdx.x;                            // global block id
  const int tid_in_block = g_tid % THREADS;                // local thread id in current block

  constexpr int shared_len = THREADS * R;
  __shared__ T shared_val[shared_len];

  const I block_row_begin = g_bid * ROWS_PER_BLOCK;
  const I block_row_end = min(block_row_begin + ROWS_PER_BLOCK, m);
  const I block_row_idx_start = row_offset[block_row_begin];
  const I block_row_idx_end = row_offset[block_row_end];

  // load reduce row bound
  const I reduce_row_id = block_row_begin + tid_in_block;
  I reduce_row_idx_begin = 0;
  I reduce_row_idx_end = 0;
  if (reduce_row_id < block_row_end) {
    reduce_row_idx_begin = row_offset[reduce_row_id];
    reduce_row_idx_end = row_offset[reduce_row_id + 1];
  }

  T sum = static_cast<T>(0);
  const int rounds = (block_row_idx_end - block_row_idx_start) / (R * THREADS) +
                     ((block_row_idx_end - block_row_idx_start) % (R * THREADS) == 0 ? 0 : 1);
  for (int r = 0; r < rounds; r++) {
    // start and end data index in each round
    const I block_round_inx_start = block_row_idx_start + r * R * THREADS;
    const I block_round_inx_end = min(block_round_inx_start + R * THREADS, block_row_idx_end);
    I i = block_round_inx_start + tid_in_block;

    __syncthreads();
// in each inner loop, it processes R*THREADS element at max
#pragma unroll
    for (int k = 0; k < R; k++) {
      if (i < block_row_idx_end) {
        const T tmp = csr_val[i] * x[csr_col_ind[i]];
        shared_val[i - block_round_inx_start] = tmp;
      }
      i += THREADS;
    }

    __syncthreads();
    // reduce
    if (reduce_row_id < block_row_end) {
      if (reduce_row_idx_begin < block_round_inx_end && reduce_row_idx_end > block_round_inx_start) {
        const I reduce_start = max(reduce_row_idx_begin, block_round_inx_start);
        const I reduce_end = min(reduce_row_idx_end, block_round_inx_end);
        for (I j = reduce_start; j < reduce_end; j++) {
          sum += shared_val[j - block_round_inx_start];
        }
      }
    }
  }
  // store result
  if (reduce_row_id < block_row_end) {
    y[reduce_row_id] = alpha * sum + y[reduce_row_id];
  }
}

#define LINE_ENHANCE_KERNEL_WRAPPER(ROWS_PER_BLOCK, R, BLOCKS, THREADS)                                                \
  (line_enhance_kernel<__WF_SIZE__, ROWS_PER_BLOCK, R, THREADS, int, double>)<<<BLOCKS, THREADS>>>(                    \
      m, alpha, beta, rowptr, colindex, value, x, y)
