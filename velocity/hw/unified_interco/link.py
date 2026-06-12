import gvsoc.systree


class UnifiedLink(gvsoc.systree.Component):
    def __init__(
        self,
        parent: gvsoc.systree.Component,
        name: str,
        latency: int,
        width: int,
        max_pending_size: int = 0,
    ):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/velocity/unified_interco/link.cpp'])
        self.add_properties({
            'latency': latency,
            'width': width,
            'max_pending_size': max_pending_size,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('output', itf, signature='io')
