#ifndef _FP8_LIB_H_
#define _FP8_LIB_H_

/*x[i]*s*/
void vector_lib_vmul_scalar(
    uint32_t v_addr,
    uint32_t s_addr,
    uint32_t vlen)
{
    uint32_t avl;
    asm volatile("fld fa5, (%0)" ::"r"(s_addr));
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfmul.vf v8, v8, fa5");
        asm volatile("vse8.v v8,  (%0)" ::"r"(v_addr));
        vlen -= avl;
        v_addr += avl;
    }
}


/*RMSNorm*/
void vector_lib_rmsnorm(
    uint32_t v_addr_all,
    uint32_t vlen_all)
{
    uint32_t v_addr = v_addr_all;
    uint32_t avl;
    uint32_t vlen = vlen_all;
    asm volatile("fcvt.s.w f6, %0" :: "r"(vlen));
    asm volatile(".word %0\n"::"i"(0x46030353)) /*fcvt.b.s f6, f6*/;
    asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(1));
    asm volatile("vmv.v.i v0, 0");
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfmul.vv v16, v8, v8");
        asm volatile("vfredusum.vs v0, v16, v0");
        vlen -= avl;
        v_addr += avl;
    }
    asm volatile("vfmv.f.s f5, v0");
    asm volatile(".word %0\n"::"i"(0x1E5302D3)) /*fdiv.b  f5, f6, f5*/;
    asm volatile(".word %0\n"::"i"(0x5e0282d3)) /*fsqrt.b f5, f5*/;
    v_addr = v_addr_all;
    vlen = vlen_all;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfmul.vf v8, v8, f5");
        asm volatile("vse8.v v8,  (%0)" ::"r"(v_addr));
        vlen -= avl;
        v_addr += avl;
    }
}


/*RoPE*/
void vector_lib_rope(
    uint32_t i_addr,
    uint32_t o_addr,
    uint32_t c_addr,
    uint32_t s_addr,
    uint32_t vlen_whole)
{
    uint32_t avl;
    uint32_t vlen = vlen_whole / 2;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m4, ta, ma" : "=r"(avl) : "r"(vlen));
        // i[0, 2, 4, 6, ..., n-2]  -> v0   | A
        asm volatile("vlse8.v v0,  (%0), %1" ::"r"(i_addr), "r"(2));

        // i[1, 3, 5, 7, ..., n-1]  -> v4   | B
        asm volatile("vlse8.v v4,  (%0), %1" ::"r"(i_addr + 1), "r"(2));

        // cosine table             -> v8   | C
        asm volatile("vle8.v  v8,  (%0)" ::"r"(c_addr));

        // sine table               -> v12  | S
        asm volatile("vle8.v  v12, (%0)" ::"r"(s_addr));

        // v0  * v8                 -> v16  | AC
        asm volatile("vfmul.vv v16, v0, v8");

        // v4  * v8                 -> v20  | BC
        asm volatile("vfmul.vv v20, v4, v8");

        // v0  * v12                -> v24  | AS
        asm volatile("vfmul.vv v24, v0, v12");

        // v4  * v12                -> v28  | BS
        asm volatile("vfmul.vv v28, v4, v12");

        // v16 - v28                -> v0   | AC - BS
        asm volatile("vfsub.vv v0,  v16, v28");

        // v24 + v20                -> v4   | AS + BC
        asm volatile("vfadd.vv v4,  v24, v20");

        // o[0, 2, 4, 6, ..., n-2]  <- v0   | P1
        asm volatile("vsse8.v v0,  (%0), %1" ::"r"(o_addr), "r"(2));

        // o[1, 3, 5, 7, ..., n-1]  <- v4   | P2
        asm volatile("vsse8.v v4,  (%0), %1" ::"r"(o_addr + 1), "r"(2));

        vlen -= avl;
        i_addr += avl;
        o_addr += avl;
        c_addr += avl;
        s_addr += avl;
    }
}

/*SoftMax*/
void vector_lib_softmax(
    uint32_t v_addr_all,
    uint32_t vlen_all,
    uint32_t dim)
{
    uint32_t v_addr = v_addr_all;
    uint32_t avl;
    uint32_t vlen = vlen_all;

    //Scaling
    asm volatile("fcvt.s.w f6, %0" :: "r"(dim));
    asm volatile(".word %0\n"::"i"(0x46030353)) /*fcvt.b.s f6, f6*/;
    asm volatile(".word %0\n"::"i"(0x5E030353)) /*fsqrt.b f6, f6*/;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfdiv.vf v8, v8, f6");
        asm volatile("vse8.v v8,  (%0)" ::"r"(v_addr));
        vlen -= avl;
        v_addr += avl;
    }

    //exp(x-max)
    asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(1));
    asm volatile("vmv.v.i v0, 0");
    v_addr = v_addr_all;
    vlen = vlen_all;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfredmax.vs v0, v8, v0");
        vlen -= avl;
        v_addr += avl;
    }
    asm volatile("vfmv.f.s f5, v0");
    v_addr = v_addr_all;
    vlen = vlen_all;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfsub.vf v8, v8, f5");
        asm volatile(".word %0\n"::"i"(0x32041857)) /*vfexp.vv v16, v8*/;
        asm volatile("vse8.v v16,  (%0)" ::"r"(v_addr));
        vlen -= avl;
        v_addr += avl;
    }

    //Normalization
    asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(1));
    asm volatile("vmv.v.i v0, 0");
    v_addr = v_addr_all;
    vlen = vlen_all;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfredusum.vs v0, v8, v0");
        vlen -= avl;
        v_addr += avl;
    }
    asm volatile("vfmv.f.s f5, v0");
    v_addr = v_addr_all;
    vlen = vlen_all;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr));
        asm volatile("vfdiv.vf v8, v8, f5");
        asm volatile("vse8.v v8,  (%0)" ::"r"(v_addr));
        vlen -= avl;
        v_addr += avl;
    }
}

/*Dotp*/
void vector_lib_dotp(
    uint32_t v_addr_a,
    uint32_t v_addr_b,
    uint32_t s_addr_c,
    uint32_t vlen)
{
    uint32_t avl;
    asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(1));
    asm volatile("vmv.v.i v0, 0");
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr_a));
        asm volatile("vle8.v v16, (%0)" ::"r"(v_addr_b));
        asm volatile("vfmul.vv v16, v16, v8");
        asm volatile("vfredusum.vs v0, v16, v0");
        vlen -= avl;
        v_addr_a += avl;
        v_addr_b += avl;
    }
    asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(1));
    asm volatile("vse8.v v0,  (%0)" ::"r"(s_addr_c));
}

/*Add*/
void vector_lib_add(
    uint32_t v_addr_a,
    uint32_t v_addr_b,
    uint32_t v_addr_c,
    uint32_t vlen)
{
    uint32_t avl;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr_a));
        asm volatile("vle8.v v16, (%0)" ::"r"(v_addr_b));
        asm volatile("vfadd.vv v0, v16, v8");
        asm volatile("vse8.v v0,  (%0)" ::"r"(v_addr_c));
        vlen -= avl;
        v_addr_a += avl;
        v_addr_b += avl;
        v_addr_c += avl;
    }
}

int scalar_top_index(const uint8_t *arr, int length) {
    int max_idx = 0;
    float res = 0;

    for (int i = 1; i < length; i++) {
        if (arr[i] > arr[max_idx]) {
            max_idx = i;
        }
    }

    return max_idx;
}

/*Sigmoid*/
void vector_lib_sigmoid(
    uint32_t v_addr_a,
    uint32_t vlen)
{
    uint32_t avl;
    asm volatile("fcvt.s.w f6, %0" :: "r"(-1));
    asm volatile(".word %0\n"::"i"(0x46030353)) /*fcvt.b.s f6, f6*/;
    while(vlen > 0){
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr_a));
        asm volatile("vfmul.vf v8, v8, f6"); // -x
        asm volatile(".word %0\n"::"i"(0x32041857)) /*vfexp.vv v16, v8*/; // e^-x
        asm volatile("vfdiv.vv v0, v16, v16"); // 1
        asm volatile("vfadd.vv v16, v16, v0"); // 1 + e^-x
        asm volatile("vfdiv.vv v8, v0, v16"); // 1/(1 + e^-x)
        asm volatile("vse8.v v8,  (%0)" ::"r"(v_addr_a));
        vlen -= avl;
        v_addr_a += avl;
    }
}

#endif
