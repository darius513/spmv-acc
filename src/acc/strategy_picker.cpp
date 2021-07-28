//
// Created by genshen on 2021/7/15.
//

#include "api/spmv.h"
#include "building_config.h"

#include "hip-adaptive/adaptive.h"
#include "hip-block-row-ordinary/spmv_hip_acc_imp.h"
#include "hip-flat/spmv_hip_acc_imp.h"
#include "hip-light/spmv_hip_acc_imp.h"
#include "hip-line/line_strategy.h"
#include "hip-thread-row/thread_row.h"
#include "hip-vector-row/vector_row.h"
#include "hip-wf-row/spmv_hip.h"
#include "hip/spmv_hip_acc_imp.h"

void sparse_spmv(int trans, const int alpha, const int beta, int m, int n, const int *rowptr, const int *colindex,
                 const double *value, const double *x, double *y) {
#ifdef KERNEL_STRATEGY_DEFAULT
  default_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_ADAPTIVE
  adaptive_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_BLOCK_ROW_ORDINARY
  block_row_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_FLAT
  flat_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_LIGHT
  light_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_LINE
  line_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_THREAD_ROW
  thread_row_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_VECTOR_ROW
  vec_row_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif

#ifdef KERNEL_STRATEGY_WAVEFRONT_ROW
  wf_row_sparse_spmv(trans, alpha, beta, m, n, rowptr, colindex, value, x, y);
#endif
}
