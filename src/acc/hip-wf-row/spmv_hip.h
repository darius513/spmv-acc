//
// Created by genshen on 2021/5/4.
//

#ifndef SPMV_ACC_SPMV_HIP_WF_ROW_H
#define SPMV_ACC_SPMV_HIP_WF_ROW_H


void sparse_spmv(int htrans, const int halpha, const int hbeta, int hm, int hn, const int *hrowptr,
                 const int *hcolindex, const double *hvalue, const double *hx, double *hy);

#endif // SPMV_ACC_SPMV_HIP_WF_ROW_H
