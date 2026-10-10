// SPDX-License-Identifier: Apache-2.0
#include "../../pulp/pulp/mxcore_fp4/compute.hpp"
#include <fstream>
#include <iterator>
#include <iostream>

int main(int argc, char **argv) {
    if (argc!=7) return 2;
    mxcore_fp4::Compute compute;
    compute.init(std::stoul(argv[1]), std::stoul(argv[2]), std::stoul(argv[3]),
                 mxcore_fp4::OutputFormat(std::stoul(argv[4])));
    std::ifstream file(argv[5],std::ios::binary);
    if (!file) return 2;
    std::vector<uint8_t> memory(std::istreambuf_iterator<char>(file),{});
    unsigned offset=0x1000;
    for (unsigned i=0; i<4; ++i) {
        if (offset+compute.data[i].size()>memory.size()) return 2;
        std::copy_n(memory.begin()+offset, compute.data[i].size(), compute.data[i].begin());
        std::fill(compute.present[i].begin(),compute.present[i].end(),true);
        offset+=compute.data[i].size();
    }
    for (unsigned b=0; b<compute.ready.size(); ++b) compute.block(b);
    std::ofstream out(argv[6],std::ios::binary);
    for (unsigned i=4; i<6; ++i) out.write(reinterpret_cast<char *>(compute.data[i].data()),compute.data[i].size());
    return out ? 0 : 2;
}
