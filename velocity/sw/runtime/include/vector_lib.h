#ifndef VELOCITY_VECTOR_LIB_H
#define VELOCITY_VECTOR_LIB_H

#include <stdint.h>

static inline void vector_lib_int8_add(
    uint32_t v_addr_a,
    uint32_t v_addr_b,
    uint32_t v_addr_c,
    uint32_t vlen)
{
    uint32_t avl;

    while (vlen > 0)
    {
        asm volatile("vsetvli %0, %1, e8, m8, ta, ma" : "=r"(avl) : "r"(vlen));
        asm volatile("vle8.v v8,  (%0)" ::"r"(v_addr_a));
        asm volatile("vle8.v v16, (%0)" ::"r"(v_addr_b));
        asm volatile("vadd.vv v0, v16, v8");
        asm volatile("vse8.v v0,  (%0)" ::"r"(v_addr_c));

        vlen -= avl;
        v_addr_a += avl;
        v_addr_b += avl;
        v_addr_c += avl;
    }
}

#endif
