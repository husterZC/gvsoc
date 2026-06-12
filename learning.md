# Learning GVSOC Modeling

This note is a compact learning guide for this `codex_velocity` branch. It
summarizes the basic GVSOC model structure, component creation, bindings,
memory-mapped IO, wire interfaces, timing, and synchronous/asynchronous
behavior.

Primary local references:

- `core/docs/developer_manual/component_model.rst`
- `core/docs/developer_manual/interfaces.rst`
- `core/docs/developer_manual/block.rst`
- `core/docs/developer_manual/gvsoc_archi.rst`
- `core/docs/developer_manual/tutorials.rst`
- `velocity/hw/velocity_system.py`
- `velocity/hw/cluster_unit.py`
- `velocity/hw/velocity_ctrl.py`
- `velocity/hw/velocity_ctrl.cpp`

Online context:

- GVSoC paper: <https://arxiv.org/abs/2201.08166>

## 1. Mental Model

GVSOC is a component-based, event-driven simulator. The paper describes it as a
timing-accurate simulator combining C++ models with Python configuration
scripts. In this repo that split is very visible:

- Python files describe the system hierarchy and connections.
- C++ files implement primitive model behavior.
- GVSOC generates a JSON configuration from Python.
- The C++ engine loads the compiled component shared libraries and runs events.

The key idea: hardware blocks do not directly know the whole system. They talk
through named ports with signatures. A Python generator builds the component
tree and binds compatible ports together.

## 2. Components, Composites, And Primitives

GVSOC uses two main component styles.

### Composite Components

A composite is usually a Python class inheriting from `gvsoc.systree.Component`.
It mostly instantiates children and wires them together. It may not have C++
model code.

Example: `velocity/hw/velocity_system.py`

```python
class VelocitySystem(gvsoc.systree.Component):
    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)
        velocity_ctrl = VelocityCtrl(self, 'velocity_ctrl', num_cluster=arch.num_cluster)

        virtual_interco.o_MAP(
            velocity_ctrl.i_INPUT(),
            base=arch.soc_register_base,
            size=arch.soc_register_size,
            rm_base=True,
        )
```

This creates components and connects the virtual interconnect to the Velocity
control register block.

### Primitive Components

A primitive has a Python wrapper plus C++ implementation.

Python wrapper:

```python
class VelocityCtrl(gvsoc.systree.Component):
    def __init__(self, parent, name, num_cluster):
        super().__init__(parent, name)
        self.add_sources(['pulp/chips/velocity/velocity_ctrl.cpp'])
        self.add_properties({'num_cluster': num_cluster})

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
```

C++ implementation:

```cpp
class VelocityCtrl : public vp::Component
{
public:
    VelocityCtrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    vp::IoSlave input_itf;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new VelocityCtrl(config);
}
```

The Python wrapper tells GVSOC what to compile, what properties to put in JSON,
and what ports are exposed. The C++ model implements runtime behavior.

## 3. Python System Generators

Python generators build the simulated hardware.

Common pattern:

```python
child = SomeComponent(self, 'child_name', param=value)
self.bind(master_component, 'master_port', slave_component, 'slave_port')
```

or with helper methods:

```python
core.o_DATA(core_ico.i_INPUT())
core.o_FETCH(instr_router.i_INPUT())
loader.o_START(core.i_FETCHEN())
```

Helper methods make connections self-documenting:

- `i_*` usually returns a slave interface.
- `o_*` usually binds a master interface to a provided slave interface.
- Signatures, such as `io` or `wire<bool>`, must match.

In `velocity/hw/cluster_unit.py`, the cluster builds:

- instruction memory
- instruction router
- Snitch core
- TCDM banks
- stack memory
- routers for core data and vector lanes

Then it binds them:

```python
core.o_DATA(core_ico.i_INPUT())
core.o_FETCH(instr_router.i_INPUT())
core_ico.o_MAP(stack_mem.i_INPUT(), base=arch.stack_base, size=arch.stack_size, rm_base=True)
core_ico.o_MAP(tcdm.i_INPUT(arch.num_lane), base=arch.tcdm_base, size=arch.tcdm_size, rm_base=True)
```

## 4. Ports, Bindings, And Signatures

A binding connects a master port to a slave port. A port has a signature that
defines what methods can be called over that connection.

Important signatures:

- `io`: memory-mapped request/response interface.
- `wire<T>`: typed value notification interface.
- `clock`: clock propagation/control interface.

The Python layer uses signatures to check compatibility. The C++ layer uses
typed port classes to implement the actual calls.

Example IO slave exposed in Python:

```python
def i_INPUT(self) -> gvsoc.systree.SlaveItf:
    return gvsoc.systree.SlaveItf(self, 'input', signature='io')
```

Matching C++ port:

```cpp
vp::IoSlave input_itf;

this->input_itf.set_req_meth(&MyComp::handle_req);
this->new_slave_port("input", &this->input_itf);
```

The string name must match: Python exposes `input`, C++ registers `input`.

## 5. Memory-Mapped IO Interface

The `io` interface exchanges memory accesses with address, size, data pointer,
and read/write direction.

Typical slave handler:

```cpp
vp::IoReqStatus MyComp::handle_req(vp::Block *__this, vp::IoReq *req)
{
    MyComp *_this = (MyComp *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (!is_write && offset == 0 && size == 4)
    {
        *(uint32_t *)data = _this->value;
        return vp::IO_REQ_OK;
    }

    return vp::IO_REQ_INVALID;
}
```

Important request methods:

- `req->get_addr()`: local address/offset after any router address removal.
- `req->get_size()`: access size in bytes.
- `req->get_data()`: pointer to payload buffer.
- `req->get_is_write()`: write if true, read if false.
- `req->inc_latency(cycles)`: add modeled latency to a synchronous response.
- `req->get_resp_port()->resp(req)`: complete a pending request.
- `req->get_resp_port()->grant(req)`: grant a previously denied request.

Velocity example: `velocity/hw/velocity_ctrl.cpp`

- offset `0`: quit simulation.
- offset `4`: increment end-of-cluster counter and print progress.
- offset `8`: capture start time.
- offset `12`: print performance counter period.
- offset `16`: print a character.
- offset `20`: print an integer.

## 6. Synchronous IO Replies

A synchronous reply completes during the same C++ call.

Return values:

- `vp::IO_REQ_OK`: valid access, completed now.
- `vp::IO_REQ_INVALID`: invalid access, completed now as an error.

Example:

```cpp
if (req->get_addr() == 0 && req->get_size() == 4)
{
    *(uint32_t *)req->get_data() = _this->value;
    req->inc_latency(10);
    return vp::IO_REQ_OK;
}

return vp::IO_REQ_INVALID;
```

Use this for simple registers, zero-latency memories, or latency that can be
represented by `inc_latency` without holding the request.

## 7. Asynchronous IO Replies

An asynchronous reply means the request cannot fully complete during the initial
call.

Return values:

- `vp::IO_REQ_PENDING`: request accepted, response will come later.
- `vp::IO_REQ_DENIED`: request not accepted now, master must retry or wait for a grant.

Typical pending pattern:

```cpp
vp::IoReqStatus MyComp::handle_req(vp::Block *__this, vp::IoReq *req)
{
    MyComp *_this = (MyComp *)__this;

    _this->pending_req = req;
    _this->event.enqueue(20);

    return vp::IO_REQ_PENDING;
}

void MyComp::handle_event(vp::Block *__this, vp::ClockEvent *event)
{
    MyComp *_this = (MyComp *)__this;

    *(uint32_t *)_this->pending_req->get_data() = _this->value;
    _this->pending_req->get_resp_port()->resp(_this->pending_req);
}
```

Use pending when the target accepts the transaction but response timing depends
on a modeled event, queue, bus, DMA, memory pipeline, or device state.

Use denied when the target cannot even accept ownership of the request, for
example a full FIFO. Later, after space becomes available, the slave can call:

```cpp
req->get_resp_port()->grant(req);
```

Then the request can continue through the initiator timing model.

## 8. Wire Interfaces

Wire interfaces are typed notifications between components. They are useful for
signals, interrupts, boot enables, state notifications, and small typed payloads.

Python side:

```python
def o_NOTIF(self, itf: gvsoc.systree.SlaveItf):
    self.itf_bind('notif', itf, signature='wire<bool>')

def i_RESULT(self) -> gvsoc.systree.SlaveItf:
    return gvsoc.systree.SlaveItf(self, 'result', signature='wire<MyResult>')
```

C++ side:

```cpp
vp::WireMaster<bool> notif_itf;
vp::WireSlave<MyClass *> result_itf;

this->new_master_port("notif", &this->notif_itf);

this->result_itf.set_sync_meth(&MyComp::handle_result);
this->new_slave_port("result", &this->result_itf);
```

Send a value:

```cpp
_this->notif_itf.sync(true);
```

Receive a value:

```cpp
void MyComp::handle_result(vp::Block *__this, MyClass *result)
{
    printf("Received results %x %x\n", result->value0, result->value1);
}
```

Wire `sync()` is a direct call into the connected component. That means the
receiver may call back into the sender before the sender's original method
returns. Keep internal state coherent before calling any outgoing interface.

## 9. Clock And Time Events

GVSOC models time with event callbacks.

### Clock Events

Clock events are scheduled in cycles of the component clock domain.

```cpp
class MyComp : public vp::Component
{
    static void handle_event(vp::Block *__this, vp::ClockEvent *event);
    vp::ClockEvent event;
};

MyComp::MyComp(vp::ComponentConf &config)
    : vp::Component(config), event(this, MyComp::handle_event)
{
}

_this->event.enqueue(10);
```

Use clock events when the component is clocked and behavior should be expressed
in cycles.

### Time Events

Time events are scheduled directly in simulator time, in picoseconds.

```cpp
vp::TimeEvent event;
event.enqueue(10000);
```

Use time events for asynchronous blocks that are not tied to a clock domain.

## 10. Clock Domains And Frequency

A top-level platform usually creates a `Clock_domain` and binds it to the
system.

Velocity example:

```python
clock = Clock_domain(self, 'clock', frequency=1000000000)
velocity_system = VelocitySystem(self, 'system', parser)
self.bind(clock, 'out', velocity_system, 'clock')
```

Once a component receives a clock, child components can schedule clock events.
GVSOC converts cycles to global timestamps through the clock engine. Cross-domain
bindings may need stubs that synchronize clock engines and convert timing.

## 11. Lifecycle Hooks

`vp::Component` inherits from `vp::Block`, which provides lifecycle hooks:

- `reset(bool active)`: initialize or start behavior on reset assertion/release.
- `start()`: called after system instantiation, before reset.
- `stop()`: called when simulation ends.
- `flush()`: called around interactive actions, useful for flushing output.
- `handle_command(...)`: custom proxy/control commands.

Velocity uses `reset` to print system information:

```cpp
void VelocityCtrl::reset(bool active)
{
    if (active)
    {
        std::cout << "[SystemInfo]: num_cluster = " << this->num_cluster << std::endl;
    }
}
```

## 12. Traces

System traces are for debug visibility.

Declare:

```cpp
vp::Trace trace;
```

Register:

```cpp
this->traces.new_trace("trace", &this->trace, vp::DEBUG);
```

Emit:

```cpp
_this->trace.msg(vp::TraceLevel::DEBUG,
    "Received request at offset 0x%lx\n", req->get_addr());
```

Run with trace selection:

```bash
./install/bin/gvsoc --target=pulp.chips.velocity.velocity_target \
  --binary sw_build/velocity.elf run --trace=velocity_ctrl
```

## 13. How Velocity Is Built

The current branch builds a multi-cluster Velocity platform.

High-level path:

1. `velocity/hw/velocity_target.py` declares the runnable target.
2. `VelocityPlatform` creates a clock domain and `VelocitySystem`.
3. `VelocitySystem` creates `ClusterUnit` instances, a virtual router, debug memory, and `VelocityCtrl`.
4. Each `ClusterUnit` creates memory, routers, one Snitch core, TCDM, stack memory, and vector-lane routers.
5. Software accesses mapped register addresses that reach `VelocityCtrl`.
6. `VelocityCtrl` prints progress and eventually calls `time.get_engine()->quit(0)`.

## 14. Practical Modeling Checklist

When adding a model:

1. Decide if it is a composite or primitive.
2. If primitive, create a Python wrapper and C++ file.
3. In Python, call `add_sources` for C++ files.
4. In Python, call `add_properties` for parameters the C++ needs.
5. In Python, expose interfaces with `i_*` or `o_*` helper methods.
6. In C++, inherit from `vp::Component`.
7. Register ports in the constructor with `new_slave_port` or `new_master_port`.
8. Set slave callbacks with `set_req_meth` for IO or `set_sync_meth` for wire.
9. Implement `gv_new`.
10. Bind the component into a system generator.
11. Add traces before debugging timing.
12. Use synchronous IO for simple immediate behavior.
13. Use `IO_REQ_PENDING` plus clock/time events for delayed accepted requests.
14. Use `IO_REQ_DENIED` for backpressure when the request cannot be accepted.

## 15. Common Pitfalls

- Python interface name and C++ port name must match.
- Signatures must match on both sides of a binding.
- Wire callbacks can be reentrant because `sync()` is a direct method call.
- Do not return `IO_REQ_OK` if the response will be produced later.
- Do not return `IO_REQ_PENDING` unless the request object remains valid until `resp()`.
- Use `rm_base=True` or `remove_offset` intentionally in router mappings; it changes what `req->get_addr()` means inside the target.
- Keep component state coherent before calling outgoing ports.
- If timing is in cycles, use `ClockEvent`; if timing is absolute/asynchronous, use `TimeEvent`.

## 16. Minimal Component Template

Python wrapper:

```python
import gvsoc.systree

class MyReg(gvsoc.systree.Component):
    def __init__(self, parent, name, value):
        super().__init__(parent, name)
        self.add_sources(['my_reg.cpp'])
        self.add_properties({'value': value})

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
```

C++ model:

```cpp
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

class MyReg : public vp::Component
{
public:
    MyReg(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    vp::IoSlave input_itf;
    uint32_t value;
};

MyReg::MyReg(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->value = this->get_js_config()->get_child_int("value");
    this->input_itf.set_req_meth(&MyReg::req);
    this->new_slave_port("input", &this->input_itf);
}

vp::IoReqStatus MyReg::req(vp::Block *__this, vp::IoReq *req)
{
    MyReg *_this = (MyReg *)__this;

    if (!req->get_is_write() && req->get_addr() == 0 && req->get_size() == 4)
    {
        *(uint32_t *)req->get_data() = _this->value;
        return vp::IO_REQ_OK;
    }

    return vp::IO_REQ_INVALID;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new MyReg(config);
}
```

System binding:

```python
reg = MyReg(self, 'my_reg', value=0x12345678)
ico.o_MAP(reg.i_INPUT(), 'my_reg', base=0x20000000, size=0x1000, rm_base=True)
```
