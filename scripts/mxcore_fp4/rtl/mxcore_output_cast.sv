// SPDX-License-Identifier: Apache-2.0
// Combinational FP32 -> IEEE binary floating point, round-to-nearest-even.
// E4M3 here has IEEE infinities/NaNs (not E4M3FN). Subnormals are preserved.
module mxcore_output_cast #(
  parameter int unsigned N = 32,
  parameter int unsigned Width = 16
) (
  input logic bf16_i,
  hwpe_stream_intf_stream.sink fp32_i,
  hwpe_stream_intf_stream.source result_o
);
  function automatic logic [15:0] convert(input logic [31:0] x, input int eb, mb);
    logic [63:0] significand, rounded, remainder, halfway;
    logic [15:0] sign_bit, infinity;
    int exponent, target_exp, shift, bias;
    sign_bit = (x[31] ? 16'b1 : 16'b0) << (eb + mb);
    infinity = ((1 << eb) - 1) << mb;
    bias = (1 << (eb - 1)) - 1;
    if (x[30:23] == 255)
      return sign_bit | infinity | (x[22:0] != 0 ? (1 << (mb-1)) : 0);
    significand = {40'b0, (x[30:23] != 0), x[22:0]};
    exponent = (x[30:23] == 0 ? -126 : int'(x[30:23]) - 127);
    target_exp = exponent + bias;
    shift = 23 - mb + (target_exp <= 0 ? 1 - target_exp : 0);
    if (shift >= 64) return sign_bit;
    rounded = significand >> shift;
    remainder = significand & ((64'b1 << shift) - 1);
    halfway = 64'b1 << (shift - 1);
    if (remainder > halfway || (remainder == halfway && rounded[0])) rounded++;
    if (target_exp <= 0 || x[30:23] == 0) return sign_bit | 16'(rounded);
    if (rounded >= (64'b1 << (mb + 1))) begin
      rounded >>= 1;
      target_exp++;
    end
    if (target_exp >= (1 << eb) - 1) return sign_bit | infinity;
    return sign_bit | 16'(target_exp << mb) | 16'(rounded & ((1 << mb) - 1));
  endfunction

  assign fp32_i.ready = result_o.ready;
  assign result_o.valid = fp32_i.valid;
  assign result_o.strb = '1;
  for (genvar i = 0; i < N; i++) begin : lane
    if (Width == 16) begin : half
      assign result_o.data[i*Width+:Width] = bf16_i ?
        convert(fp32_i.data[i*32+:32], 8, 7) : convert(fp32_i.data[i*32+:32], 5, 10);
    end else begin : byte_float
      assign result_o.data[i*Width+:Width] = Width'(bf16_i ?
        convert(fp32_i.data[i*32+:32], 4, 3) : convert(fp32_i.data[i*32+:32], 5, 2));
    end
  end
endmodule
