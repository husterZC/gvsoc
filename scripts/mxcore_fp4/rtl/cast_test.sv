// SPDX-License-Identifier: Apache-2.0
module cast_test (
  input logic clk_i, valid_i, ready_i,
  input logic [31:0] value_i,
  input logic [2:0] format_i,
  output logic valid_o, ready_o,
  output logic [15:0] value_o
);
  hwpe_stream_intf_stream #(.DATA_WIDTH(32)) input16 (.clk(clk_i));
  hwpe_stream_intf_stream #(.DATA_WIDTH(32)) input8 (.clk(clk_i));
  hwpe_stream_intf_stream #(.DATA_WIDTH(16)) output16 (.clk(clk_i));
  hwpe_stream_intf_stream #(.DATA_WIDTH(8)) output8 (.clk(clk_i));
  assign input16.valid = valid_i;
  assign input8.valid = valid_i;
  assign input16.data = value_i;
  assign input8.data = value_i;
  assign input16.strb = '1;
  assign input8.strb = '1;
  assign output16.ready = ready_i;
  assign output8.ready = ready_i;
  mxcore_output_cast #(.N(1), .Width(16)) cast16 (
    .bf16_i(format_i == 2), .fp32_i(input16.sink), .result_o(output16.source));
  mxcore_output_cast #(.N(1), .Width(8)) cast8 (
    .bf16_i(format_i == 4), .fp32_i(input8.sink), .result_o(output8.source));
  assign value_o = format_i < 4 ? output16.data : {8'b0, output8.data};
  assign valid_o = format_i < 4 ? output16.valid : output8.valid;
  assign ready_o = format_i < 4 ? input16.ready : input8.ready;
endmodule
