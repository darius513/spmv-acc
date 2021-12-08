//
// Created by genshen on 2021/11/15.
//

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include <hip/hip_runtime.h>
#include <hip/hip_runtime_api.h>

#include "api/types.h"
#include "clipp.h"

#include "csr_binary_reader.hpp"
#include "csr_mtx_reader.hpp"
#include "matrix_market_reader.hpp"
#include "sparse_format.h"
#include "utils.hpp"
#include "utils/statistics_logger.h"

#include "csr_spmv.hpp"

void test_spmv(std::string mtx_path, type_csr h_csr, host_vectors<dtype> h_vectors);

int main(int argc, char **argv) {
  std::string mtx_path = "", fmt = "csr";

  auto cli = (clipp::value("input file", mtx_path),
              clipp::option("-f", "--format")
                      .doc("input matrix format, can be `csr` (default), `mm` (matrix market) or "
                           "`bin` (csr binary)") &
                  clipp::value("format", fmt));

  if (!parse(argc, argv, cli)) {
    std::cout << clipp::make_man_page(cli, argv[0]);
    return 0;
  }

  type_csr h_csr;
  host_vectors<dtype> h_vectors{};
  if (fmt == "csr") {
    csr_mtx_reader<int, dtype> csr_reader(mtx_path);
    csr_reader.fill_mtx();
    csr_reader.close_stream();

    h_csr.rows = csr_reader.rows();
    h_csr.cols = csr_reader.cols();
    h_csr.nnz = csr_reader.nnz();

    // don't allocate new memory, just reuse memory in file parsing.
    // array data in `h_csr` is keep in instance `csr_reader`.
    csr_reader.as_raw_ptr(h_csr.values, h_csr.col_index, h_csr.row_ptr, h_vectors.hX);
    create_host_data(h_csr, h_vectors);
    test_spmv(mtx_path, h_csr, h_vectors);
    destroy_host_data(h_vectors);
  } else if (fmt == "bin") {
    csr_binary_reader<int32_t, dtype> csr_bin_reader;
    csr_bin_reader.load_mat(mtx_path);
    csr_bin_reader.close_stream();

    h_csr.rows = csr_bin_reader.rows();
    h_csr.cols = csr_bin_reader.cols();
    h_csr.nnz = csr_bin_reader.nnz();
    // just reuse memory in file reading step.
    csr_bin_reader.as_raw_ptr(h_csr.values, h_csr.col_index, h_csr.row_ptr);

    create_host_data(h_csr, h_vectors, true);
    test_spmv(mtx_path, h_csr, h_vectors);
    destroy_host_data(h_vectors);
  } else {
    matrix_market_reader<int, dtype> mm_reader;
    matrix_market<int, dtype> mm = mm_reader.load_mat(mtx_path);
    h_csr = mm.to_csr();

    create_host_data(h_csr, h_vectors, true);
    test_spmv(mtx_path, h_csr, h_vectors);
    destroy_host_data(h_vectors);
  }
}

void test_spmv(std::string mtx_path, type_csr h_csr, host_vectors<dtype> h_vectors) {
  hipSetDevice(0);
  dtype *dev_x, *dev_y;
  type_csr d_csr = create_device_data(h_csr, h_vectors.hX, h_vectors.temphY, dev_x, dev_y);
  // set parameters
  enum sparse_operation operation = operation_none;
  dtype alpha = 1.0;
  dtype beta = 1.0;
  // spmv-acc
//  SpMVAccDefault spmv_acc_default;
  SpMVAccAdaptive spmv_acc_adaptive;
  SpMVAccBlockRow spmv_acc_block_row;
  SpMVAccFlat spmv_acc_flat;
  SpMVAccLight spmv_acc_light;
  SpMVAccLine spmv_acc_line;
  SpMVAccThreadRow spmv_acc_thread_row;
  SpMVAccVecRow spmv_acc_vec_row;
  SpMVAccWfRow spmv_acc_wf_row;
  SpMVAccLineEnhance spmv_acc_line_enhance;

  statistics::print_statistics_header();

//  spmv_acc_default.test(mtx_path, "spmv-acc-default", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
  spmv_acc_adaptive.test(mtx_path, "spmv-acc-adaptive", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
  spmv_acc_block_row.test(mtx_path, "spmv-acc-block-row", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x,
                          dev_y);
  spmv_acc_flat.test(mtx_path, "spmv-acc-flat", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
  spmv_acc_light.test(mtx_path, "spmv-acc-light", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
  spmv_acc_line.test(mtx_path, "spmv-acc-line", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
  spmv_acc_thread_row.test(mtx_path, "spmv-acc-thread-row", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x,
                           dev_y);
  spmv_acc_vec_row.test(mtx_path, "spmv-acc-vector-row", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
  spmv_acc_wf_row.test(mtx_path, "spmv-acc-wavefront-row", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x,
                       dev_y);
  spmv_acc_line_enhance.test(mtx_path, "spmv-acc-line-enhance", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x,
                             dev_y);

#ifdef __HIP_PLATFORM_HCC__
  // rocsparse
  RocSparseVecRow rocsparse_vec_row;
  rocsparse_vec_row.test(mtx_path, "rocSparse-vector-row", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x,
                         dev_y);

  RocSparseAdaptive rocsparse_adaptive;
  rocsparse_adaptive.test(mtx_path, "rocSparse-adaptive", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x,
                          dev_y);
  // hola
  HolaHipSpMV hola_hip_spmv;
  hola_hip_spmv.test(mtx_path, "hip-hola", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);

#endif

#ifndef __HIP_PLATFORM_HCC__
  // cusparse
  CuSparseGeneral cusparse_general;
  cusparse_general.test(mtx_path, "cuSparse", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);

  // cub
  CubDeviceSpMV cub_device_spmv;
  cub_device_spmv.test(mtx_path, "cub", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);

  // hola
  HolaSpMV hola_spmv;
  hola_spmv.test(mtx_path, "hola", operation, alpha, beta, h_csr, d_csr, h_vectors, dev_x, dev_y);
#endif

  destroy_device_data(d_csr, dev_x, dev_y);
}