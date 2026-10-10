#!/usr/bin/env python3
"""Apply the reproducible output extension after the pinned MXFP4 RTL fixes."""
from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('fp4_prepare', ROOT / 'pulp/tests/mxcore_fp4/rtl/prepare.py')
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
REVISION = legacy.REVISION


def prepare(root):
    legacy.prepare(root)
    hwpe = root / 'mxcore-rtl/src/hwpe'
    edits = {
        'mxcore_hwpe_package.sv': [('    logic                     quantize;',
                                  '    logic [2:0]               result_format; // CTRL_ENGINE[25:23]\n    logic                     quantize;')],
        'mxcore_hwpe_ctrl.sv': [('    ctrl_engine_o.quantize    =',
                               '    ctrl_engine_o.result_format = reg_file.hwpe_params[MXCoreRegCtrlEngine][25:23];\n    ctrl_engine_o.quantize    =')],
    }
    for name, pairs in edits.items():
        path = hwpe / name
        data = path.read_text()
        for before, after in pairs:
            if after not in data:
                if before not in data:
                    raise RuntimeError(f'Missing patch context: {name}')
                data = data.replace(before, after)
        path.write_text(data)
    path = hwpe / 'mxcore_hwpe_top.sv'
    data = path.read_text()
    if '// MXCoreFP4 selectable output extension' in data:
        return
    data = data.replace('assign engine_result_to_fifo.valid      = engine_ctrl.quantize ? 1\'b0 : mxcore_engine_result.valid;',
                        "assign engine_result_to_fifo.valid      = engine_ctrl.result_format == 1 ? mxcore_engine_result.valid : 1'b0;")
    data = data.replace('engine_result_to_quantizer.ready : engine_result_to_fifo.ready;',
                        'engine_result_to_quantizer.ready : (engine_ctrl.result_format == 1 ? engine_result_to_fifo.ready : narrow_ready);')
    begin = data.index('  assign mxcore_result.valid ')
    end = data.index('\n', data.index('  assign mxcore_fp32_result.ready ', begin))
    extension = '''  // MXCoreFP4 selectable output extension: 0=MXFP4, 1=FP32,
  // 2=BF16, 3=FP16, 4=E4M3, 5=E5M2. Accumulation remains FP32.
  logic narrow_ready;
  logic [1:0] narrow_selected, narrow_input_ready, narrow_valid;
  logic [1:0][MXCoreTCDMDataWidth-1:0] narrow_data;
  logic [1:0][MXCoreTCDMDataWidth/8-1:0] narrow_strb;
  assign narrow_selected[0] = engine_ctrl.result_format == 2 || engine_ctrl.result_format == 3;
  assign narrow_selected[1] = engine_ctrl.result_format == 4 || engine_ctrl.result_format == 5;
  assign narrow_ready = |(narrow_selected & narrow_input_ready);
  for (genvar f = 0; f < 2; f++) begin : narrow_output
    localparam int Width = f == 0 ? 16 : 8;
    hwpe_stream_intf_stream #(.DATA_WIDTH(NPE*32)) cast_input (.clk(clk_i));
    hwpe_stream_intf_stream #(.DATA_WIDTH(NPE*Width)) cast_output (.clk(clk_i));
    hwpe_stream_intf_stream #(.DATA_WIDTH(MXCoreTCDMDataWidth)) buffered (.clk(clk_i));
    assign cast_input.valid = mxcore_engine_result.valid && narrow_selected[f];
    assign cast_input.data = mxcore_engine_result.data;
    assign cast_input.strb = mxcore_engine_result.strb;
    assign narrow_input_ready[f] = cast_input.ready;
    mxcore_output_cast #(.N(NPE), .Width(Width)) i_cast (
      .bf16_i(engine_ctrl.result_format == 2 || engine_ctrl.result_format == 4),
      .fp32_i(cast_input.sink), .result_o(cast_output.source)
    );
    mxcore_hwpe_result_fifo_buffer #(
      .InputDataWidth(NPE*Width), .OutputDataWidth(MXCoreTCDMDataWidth), .FifoDepth(2)
    ) i_buffer (
      .clk_i(clk_i), .rst_ni(rst_ni), .clear_i(clear),
      .data_i(cast_output.sink), .data_o(buffered.source)
    );
    assign buffered.ready = narrow_selected[f] && mxcore_result.ready;
    assign narrow_valid[f] = buffered.valid;
    assign narrow_data[f] = buffered.data;
    assign narrow_strb[f] = buffered.strb;
  end
  assign mxcore_result.valid = engine_ctrl.quantize ? mxcore_quantized_result.valid :
    (engine_ctrl.result_format == 1 ? mxcore_fp32_result.valid : |(narrow_valid & narrow_selected));
  assign mxcore_result.data = engine_ctrl.quantize ? mxcore_quantized_result.data :
    (engine_ctrl.result_format == 1 ? mxcore_fp32_result.data : narrow_data[narrow_selected[1]]);
  assign mxcore_result.strb = engine_ctrl.quantize ? mxcore_quantized_result.strb :
    (engine_ctrl.result_format == 1 ? mxcore_fp32_result.strb : narrow_strb[narrow_selected[1]]);
  assign mxcore_quantized_result.ready = engine_ctrl.quantize && mxcore_result.ready;
  assign mxcore_fp32_result.ready = engine_ctrl.result_format == 1 && mxcore_result.ready;'''
    path.write_text(data[:begin] + extension + data[end:])
