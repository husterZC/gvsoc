import gvsoc.systree


class UnifiedRouter(gvsoc.systree.Component):
    def __init__(
        self,
        parent: gvsoc.systree.Component,
        name: str,
        router_id: int,
        radix: int,
        num_cluster: int,
        cluster_stride: int,
        routes: list[int],
        output_clusters: list[int] | None = None,
        collective_subtree_words: list[int] | None = None,
        collective_subtree_words_per_root: int = 0,
        max_input_pending_size: int = 0,
        collective_buffer_size: int = 65536,
        collective_max_pending: int = 1024,
        collective_alu_count: int = 0,
        collective_alu_latency: int = 1,
    ):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/velocity/unified_interco/router.cpp'])
        self.add_properties({
            'router_id': router_id,
            'radix': radix,
            'num_cluster': num_cluster,
            'cluster_stride': cluster_stride,
            'routes': routes,
            'output_clusters': output_clusters or [-1] * radix,
            'collective_subtree_words': collective_subtree_words or [],
            'collective_subtree_words_per_root': collective_subtree_words_per_root,
            'max_input_pending_size': max_input_pending_size,
            'collective_buffer_size': collective_buffer_size,
            'collective_max_pending': collective_max_pending,
            'collective_alu_count': collective_alu_count,
            'collective_alu_latency': collective_alu_latency,
        })

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_{port}', signature='io')

    def o_OUTPUT(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'out_{port}', itf, signature='io')
