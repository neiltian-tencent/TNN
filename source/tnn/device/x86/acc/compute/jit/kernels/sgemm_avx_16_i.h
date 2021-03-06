// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the 
// specific language governing permissions and limitations under the License.

#ifndef TNN_SGEMM_AVX_16xI_H_
#define TNN_SGEMM_AVX_16xI_H_

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <fstream>
#include <immintrin.h>
#include <xmmintrin.h>
#include <exception>
#include <utility>

#include <xbyak/xbyak.h>

#include "tnn/device/x86/acc/compute/jit/common/type_def.h"
#include "tnn/device/x86/acc/compute/jit/common/abi_info.h"
#include "tnn/device/x86/acc/compute/jit/common/asm_common.h"
#include "tnn/device/x86/acc/compute/jit/utils/macro.h"
#include "tnn/device/x86/acc/compute/jit/kernels/base_jit_kernel.h"

namespace TNN_NS {
namespace jit {

template<int I, int M_BLOCK_SIZE, int N_BLOCK_SIZE>
class sgemm_avx_16xi: public base_jit_kernel {

public:
    static void naive_impl(const dim_t K, const float * alpha_ptr,
                           const float * src_a, const dim_t lda,
                           const float * src_b, dim_t ldb, const float * beta_ptr,
                           float * dst, dim_t ldc) {}

    using func_ptr_t = decltype(&sgemm_avx_16xi::naive_impl);

    virtual std::string get_kernel_name() {
        std::stringstream buf;
        buf << JIT_KERNEL_NAME(sgemm_avx_16) << "_" << I << "_" << M_BLOCK_SIZE << "_" << N_BLOCK_SIZE;
        return buf.str();
    }

public:
    sgemm_avx_16xi() {

#ifdef XBYAK64
        constexpr int N_r = MIN_(6, I);

        declare_param<const dim_t>();       // 0. K
        declare_param<const float *>();     // 1. alpha_ptr
        declare_param<const float *>();     // 2. src_a
        declare_param<const dim_t>();       // 3. lda
        declare_param<const float *>();     // 4. src_b
        declare_param<const dim_t>();       // 5. ldb
        declare_param<const float *>();     // 6. beta_ptr
        declare_param<float *>();           // 7. dst
        declare_param<const dim_t>();       // 8. ldc

        abi_prolog();

        stack_var K         = get_arguement_to_stack(0);
        reg_var alpha_ptr   = get_arguement(1);
        reg_var src_a       = get_arguement(2);
        reg_var lda         = get_arguement(3);
        reg_var src_b       = get_arguement(4);
        reg_var ldb         = get_arguement(5);
        reg_var beta_ptr    = get_arguement(6);
        reg_var dst         = get_arguement(7);
        reg_var ldc         = get_arguement(8);

        reg_var c[3] = {REG_VAR_ARRAY_3};
        vreg_var v_alpha(this), v_beta(this);
        vreg_var c_data[2][6] = {{VREG_VAR_ARRAY_6}, {VREG_VAR_ARRAY_6}};
        vreg_var a_data[2] = {VREG_VAR_ARRAY_2};
        vreg_var b_data[2] = {VREG_VAR_ARRAY_2};

        v_beta.aquire();
        vbroadcastss(v_beta, dword[beta_ptr.restore()]);
        beta_ptr.release();
        
        ldc.restore();
        mov(c[0].aquire(), dst.restore());
        lea(c[1].aquire(), byte[dst + (ldc * 8)]);
        lea(c[2].aquire(), byte[c[1]+ (ldc * 8)]);
        dst.release();

        Xbyak::RegExp c_addr[6] = {
            Xbyak::RegExp(c[0]),
            Xbyak::RegExp(c[0] + (ldc * 4)),
            Xbyak::RegExp(c[1]),
            Xbyak::RegExp(c[1] + (ldc * 4)),
            Xbyak::RegExp(c[2]),
            Xbyak::RegExp(c[2] + (ldc * 4)),
        };

        for(int i=0;i<N_r;i++) {
            vmovups(c_data[0][i].aquire(), yword[c_addr[i]]);
            vmovups(c_data[1][i].aquire(), yword[c_addr[i] + 8 * 4]);
        }

        for(int i=0;i<N_r;i++) {
            vmulps(c_data[0][i], c_data[0][i], v_beta);
            vmulps(c_data[1][i], c_data[1][i], v_beta);
        }
        v_beta.release();

        src_a.restore();
        src_b.restore();

        LOOP_STACK_VAR(K, SGEMM_AVX_8X6_K) 
        {
            vmovaps(a_data[0].aquire(), yword[src_a]);
            vmovaps(a_data[1].aquire(), yword[src_a + 8 * 4]);

            for(int i=0;i<N_r;i+=2) {
                vbroadcastss(b_data[0].aquire(), yword[src_b + i * 4]);
                vfmadd231ps(c_data[0][i],   a_data[0], b_data[0]);
                vfmadd231ps(c_data[1][i],   a_data[1], b_data[0].release());

                if (i + 1 < N_r) {
                    vbroadcastss(b_data[1].aquire(), yword[src_b + i * 4 + 4]);
                    vfmadd231ps(c_data[0][i+1], a_data[0], b_data[1]);
                    vfmadd231ps(c_data[1][i+1], a_data[1], b_data[1].release());
                }
            }

            a_data[0].release();
            a_data[1].release();

            lea(src_a, byte[src_a + M_BLOCK_SIZE * 4]);
            lea(src_b, byte[src_b + N_BLOCK_SIZE * 4]);
        }

        src_a.release();
        src_b.release();

        v_alpha.aquire();
        vbroadcastss(v_alpha, dword[alpha_ptr.restore()]); alpha_ptr.release();
        for(int i=0;i<N_r;i++) {
            vmulps(c_data[0][i], c_data[0][i], v_alpha);
            vmulps(c_data[1][i], c_data[1][i], v_alpha);
        }
        v_alpha.release();

        for(int i=0;i<N_r;i++) {
            vmovups(yword[c_addr[i]],         c_data[0][i]);
            vmovups(yword[c_addr[i] + 8 * 4], c_data[1][i]);
        }

        abi_epilog();
#endif // XBYAK64
        ret();
    }

    virtual ~sgemm_avx_16xi() {

    }

private:

};

} // namespace jit
} // namespace tnn

#endif // TNN_SGEMM_AVX_16xI_H_
