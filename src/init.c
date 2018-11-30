/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>

#include <cpuinfo.h>
#include <qnnpack.h>
#include <qnnpack/log.h>
#include <qnnpack/params.h>
#include <qnnpack/q8add.h>
#include <qnnpack/q8conv.h>
#include <qnnpack/q8dw.h>
#include <qnnpack/q8gavgpool.h>
#include <qnnpack/q8gemm.h>
#include <qnnpack/x8zip.h>

static pthread_once_t init_guard = PTHREAD_ONCE_INIT;

struct qnnp_parameters qnnp_params = {
  .initialized = false
};

static void init(void) {
#if CPUINFO_ARCH_ARM
  if (!cpuinfo_has_arm_neon()) {
    qnnp_log_error("QNNPACK initialization failed: NEON is not supported");
    return;
  }
  qnnp_params.q8conv = (struct q8conv_parameters) {
      .gemm = q8gemm_ukernel_4x8__aarch32_neon,
      .conv = q8conv_ukernel_4x8__aarch32_neon,
      .mr = 4,
      .nr = 8,
      .kr = 1,
  };
  qnnp_params.q8conv_xzp = (struct q8conv_xzp_parameters) {
      .gemm = q8gemm_xzp_ukernel_4x8c2__aarch32_neon,
      .mr = 4,
      .nr = 8,
      .kr = 2,
      .kc = 8,
      .kthreshold = SIZE_MAX,
  };
  /* setup xzp threshold based on measurements */
  switch (cpuinfo_get_core(0)->uarch) {
    case cpuinfo_uarch_cortex_a72:
      qnnp_params.q8conv_xzp.kthreshold = 64;
      break;
    case cpuinfo_uarch_cortex_a73:
      qnnp_params.q8conv_xzp.kthreshold = 256;
      break;
    case cpuinfo_uarch_cortex_a75:
      qnnp_params.q8conv_xzp.kthreshold = 32;
      break;
    default:
      break;
  }
  qnnp_params.q8dw9 = (struct q8updw_parameters) {
      .updw = q8updw_ukernel_9c8__aarch32_neon,
      .cr = 8,
  };
  qnnp_params.q8dw25 = (struct q8mpdw_parameters) {
      .mpdw = q8mpdw_ukernel_25c8__neon,
      .cr = 8,
  };
  qnnp_params.q8sum_rows = (struct q8sum_rows_parameters) {
      .sum_rows = q8sumrows_ukernel_4x__neon,
      .m = 4,
  };
  qnnp_params.q8add = (struct q8add_parameters) {
      .uvadd = q8uvadd_ukernel__neon,
  };
  qnnp_params.q8gavgpool = (struct q8gavgpool_parameters) {
      .ltnr = q8gavgpool_ukernel_up8xm__neon,
      .genr_lemr = q8gavgpool_ukernel_up8x7__neon,
      .genr_gtmr = q8gavgpool_ukernel_mp8x7__neon,
      .mr = 7,
      .nr = 8,
  };
  qnnp_params.x8zip = (struct x8zip_parameters) {
      .x2 = qnnp_x8zip_x2__neon,
      .x3 = qnnp_x8zip_x3__neon,
      .x4 = qnnp_x8zip_x4__neon,
      .xm = qnnp_x8zip_xm__neon,
  };
#elif CPUINFO_ARCH_ARM64
  qnnp_params.q8conv = (struct q8conv_parameters) {
      .gemm = q8gemm_ukernel_8x8__aarch64_neon,
      .conv = q8conv_ukernel_8x8__aarch64_neon,
      .mr = 8,
      .nr = 8,
      .kr = 1,
  };
  qnnp_params.q8conv_xzp = (struct q8conv_xzp_parameters) {
      .kthreshold = SIZE_MAX,
  };
  qnnp_params.q8dw9 = (struct q8updw_parameters) {
      .updw = q8updw_ukernel_9c8__neon,
      .cr = 8,
  };
  qnnp_params.q8dw25 = (struct q8mpdw_parameters) {
      .mpdw = q8mpdw_ukernel_25c8__neon,
      .cr = 8,
  };
  qnnp_params.q8add = (struct q8add_parameters) {
      .uvadd = q8uvadd_ukernel__neon,
  };
  qnnp_params.q8gavgpool = (struct q8gavgpool_parameters) {
      .ltnr = q8gavgpool_ukernel_up8xm__neon,
      .genr_lemr = q8gavgpool_ukernel_up8x7__neon,
      .genr_gtmr = q8gavgpool_ukernel_mp8x7__neon,
      .mr = 7,
      .nr = 8,
  };
  qnnp_params.x8zip = (struct x8zip_parameters) {
      .x2 = qnnp_x8zip_x2__neon,
      .x3 = qnnp_x8zip_x3__neon,
      .x4 = qnnp_x8zip_x4__neon,
      .xm = qnnp_x8zip_xm__neon,
  };
#elif CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
  if (!cpuinfo_has_x86_sse2()) {
    qnnp_log_error("QNNPACK initialization failed: SSE2 is not supported");
    return;
  }
  qnnp_params.q8conv = (struct q8conv_parameters){
      .gemm = q8gemm_ukernel_4x4c2__sse2,
      .conv = q8conv_ukernel_4x4c2__sse2,
      .mr = 4,
      .nr = 4,
      .kr = 2,
  };
  qnnp_params.q8conv_xzp = (struct q8conv_xzp_parameters) {
      .kthreshold = SIZE_MAX,
  };
  qnnp_params.q8dw9 = (struct q8updw_parameters) {
      .updw = q8updw_ukernel_9c8__sse2,
      .cr = 8,
  };
  qnnp_params.q8dw25 = (struct q8mpdw_parameters) {
      .mpdw = q8mpdw_ukernel_25c8__sse2,
      .cr = 8,
  };
  qnnp_params.q8add = (struct q8add_parameters) {
      .uvadd = q8uvadd_ukernel__sse2,
  };
  qnnp_params.q8gavgpool = (struct q8gavgpool_parameters) {
      .ltnr = q8gavgpool_ukernel_up8xm__sse2,
      .genr_lemr = q8gavgpool_ukernel_up8x7__sse2,
      .genr_gtmr = q8gavgpool_ukernel_mp8x7__sse2,
      .mr = 7,
      .nr = 8,
  };
  qnnp_params.x8zip = (struct x8zip_parameters) {
      .x2 = qnnp_x8zip_x2__sse2,
      .x3 = qnnp_x8zip_x3__sse2,
      .x4 = qnnp_x8zip_x4__sse2,
      .xm = qnnp_x8zip_xm__sse2,
  };
#else
  #error "Unsupported architecture"
#endif
  qnnp_params.initialized = true;
}

enum qnnp_status qnnp_initialize(void) {
  if (!cpuinfo_initialize()) {
    return qnnp_status_out_of_memory;
  }
  pthread_once(&init_guard, &init);
  if (qnnp_params.initialized) {
    return qnnp_status_success;
  } else {
    return qnnp_status_unsupported_hardware;
  }
}

enum qnnp_status qnnp_deinitialize(void) {
  cpuinfo_deinitialize();
  return qnnp_status_success;
}
