# Velocity DMA Read Test

This test verifies the two-phase remote-read protocol. Each cluster reads one
word from the next cluster and checks that the response packet writes the data
back into local TCDM.

Run from the repository root:

```bash
make sw app=velocity/tests/DMA/read/sw run
```

To stress `IO_REQ_DENIED`/grant handling with a tiny DMA interconnect queue:

```bash
make hw cfg=velocity/tests/DMA/read/arch/velocity_arch_denied.py
make sw app=velocity/tests/DMA/read/sw run
```
