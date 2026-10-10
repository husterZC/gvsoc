// SPDX-License-Identifier: Apache-2.0
#include "Vcast_test.h"
#include "../../../pulp/pulp/mxcore_fp4/format.hpp"
#include <cstring>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    VerilatedContext context;
    Vcast_test dut{&context};
    std::ifstream input(argv[1]);
    unsigned format, bits, expected, count = 0;
    while (input >> std::hex >> format >> bits >> expected) {
        float value;
        std::memcpy(&value, &bits, 4);
        unsigned actual = mxcore_fp4::encode_output(value, mxcore_fp4::OutputFormat(format));
        dut.format_i = format;
        dut.value_i = bits;
        dut.valid_i = 1;
        dut.ready_i = count & 1;
        dut.eval();
        if (actual != expected || dut.value_o != expected || !dut.valid_o ||
            dut.ready_o != dut.ready_i) {
            std::cerr << std::hex << "Cast mismatch: " << format << ' ' << bits << ' ' << expected
                      << ' ' << actual << ' ' << dut.value_o << '\n';
            return 1;
        }
        ++count;
    }
    std::cout << "CAST_PASS " << count << '\n';
}
