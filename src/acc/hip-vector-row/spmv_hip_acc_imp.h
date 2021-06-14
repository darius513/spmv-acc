#ifndef SPMV_ACC_SPMV_HIP_ACC_IMP_WF_ROW_H
#define SPMV_ACC_SPMV_HIP_ACC_IMP_WF_ROW_H

enum sparse_operation { operation_none = 0, operation_transpose = 1 };

void sparse_spmv(int trans, const int alpha, const int beta, int m, int n, const int *rowptr, const int *colindex,
                 const double *value, const double *x, double *y);

#endif // SPMV_ACC_SPMV_HIP_ACC_IMP_WF_ROW_H