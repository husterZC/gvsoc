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
        max_input_pending_size: int = 0,
    ):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/velocity/unified_interco/router.cpp'])
        self.add_properties({
            'router_id': router_id,
            'radix': radix,
            'num_cluster': num_cluster,
            'cluster_stride': cluster_stride,
            'routes': routes,
            'max_input_pending_size': max_input_pending_size,
        })

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_{port}', signature='io')

    def o_OUTPUT(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'out_{port}', itf, signature='io')
