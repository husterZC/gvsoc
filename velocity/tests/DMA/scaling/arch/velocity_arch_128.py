class VelocityArch:
    def __init__(self):
        self.num_cluster             = 128
        self.cluster_num_lane        = 32
        self.cluster_lane_width      = 4
        self.dma_reg_offset          = 0x00000100
        self.dma_reg_size            = 0x00000100
        self.dma_bus_width           = 16
        self.dma_read_buffer_size    = 4096
        self.dma_write_buffer_size   = 4096
        self.dma_max_inflight_txn    = 16
        self.dma_base_latency        = 1
        self.dma_cluster_stride      = 0x00010000
        self.cluster_tcdm_base       = 0x00000000
        self.cluster_tcdm_size       = 0x00100000
        self.cluster_stack_base      = 0x10000000
        self.cluster_stack_size      = 0x00020000
        self.cluster_zomem_base      = 0x18000000
        self.cluster_zomem_size      = 0x00020000
        self.cluster_reg_base        = 0x20000000
        self.cluster_reg_size        = 0x00000200
        self.instruction_mem_base    = 0x80000000
        self.instruction_mem_size    = 0x00010000
        self.soc_register_base       = 0x70000000
        self.soc_register_size       = 0x00010000
        self.soc_register_eoc        = 0x70000000

