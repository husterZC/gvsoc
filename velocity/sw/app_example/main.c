#include "velocity_runtime.h"
#include "fp8_lib.h"

int main()
{
    uint32_t vlen = 128;
    vector_lib_rmsnorm(
        0/*v_addr_all*/,
        vlen/*vlen_all*/);
    // vector_lib_vmul_scalar(
    //     0/*v_addr*/,
    //     0/*s_addr*/,
    //     vlen/*vlen*/);
    // vector_lib_rope(
    //     0/*i_addr*/,
    //     0/*o_addr*/,
    //     0/*c_addr*/,
    //     0/*s_addr*/,
    //     vlen/*vlen_whole*/);
    // vector_lib_softmax(
    //     0/*v_addr_all*/,
    //     vlen/*vlen_all*/,
    //     64/*dim*/);
    flex_eoc_all(flex_get_core_id());
    return 0;
}