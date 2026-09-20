// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
`timescale 1ns/1ps

// ============================================================================
// Module: bdi_encoder_64b
// Description: Pipelined 64-byte Base-Delta-Immediate (BDI) hardware encoder.
//              Detects low dynamic-range patterns in parallel and outputs
//              compressed representation with optimal slot allocation.
// ============================================================================

module bdi_encoder_64b (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         compress_en,
    input  wire [511:0] raw_line_in,

    output reg  [511:0] comp_data_out,
    output reg  [3:0]   pattern_tag,
    output reg  [6:0]   comp_size_bytes,
    output reg          compress_valid
);

    localparam PAT_ZEROS        = 4'h0;
    localparam PAT_REP_WORD     = 4'h1;
    localparam PAT_BASE8_DELTA1 = 4'h2;
    localparam PAT_BASE8_DELTA2 = 4'h3;
    localparam PAT_BASE4_DELTA1 = 4'h5;
    localparam PAT_UNCOMPRESSED = 4'hF;

    // Word extractions
    wire [63:0] w64 [0:7];
    genvar i;
    generate
        for (i = 0; i < 8; i = i + 1) begin : gen_w64
            assign w64[i] = raw_line_in[i*64 +: 64];
        end
    endgenerate

    wire [31:0] w32 [0:15];
    generate
        for (i = 0; i < 16; i = i + 1) begin : gen_w32
            assign w32[i] = raw_line_in[i*32 +: 32];
        end
    endgenerate

    // 1. Zeros detection
    wire is_zeros = (raw_line_in == 512'd0);

    // 2. Repeated word detection
    wire is_rep_word = (w64[0] == w64[1]) && (w64[1] == w64[2]) &&
                       (w64[2] == w64[3]) && (w64[3] == w64[4]) &&
                       (w64[4] == w64[5]) && (w64[5] == w64[6]) &&
                       (w64[6] == w64[7]);

    // 3. Base8-Delta1 detection: check if (w64[i] - w64[0]) fits in signed 8-bit [-128, 127]
    wire [7:0] b8d1_fits;
    assign b8d1_fits[0] = 1'b1;
    generate
        for (i = 1; i < 8; i = i + 1) begin : gen_b8d1_chk
            wire signed [63:0] diff = w64[i] - w64[0];
            assign b8d1_fits[i] = (diff >= -64'sd128) && (diff <= 64'sd127);
        end
    endgenerate
    wire is_b8d1 = &b8d1_fits;

    // 4. Base4-Delta1 detection: check if (w32[i] - w32[0]) fits in signed 8-bit
    wire [15:0] b4d1_fits;
    assign b4d1_fits[0] = 1'b1;
    generate
        for (i = 1; i < 16; i = i + 1) begin : gen_b4d1_chk
            wire signed [31:0] diff32 = w32[i] - w32[0];
            assign b4d1_fits[i] = (diff32 >= -32'sd128) && (diff32 <= 32'sd127);
        end
    endgenerate
    wire is_b4d1 = &b4d1_fits;

    // ------------------------------------------------------------------------
    // Pipelined Output Stage
    // ------------------------------------------------------------------------
    integer j;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            comp_data_out   <= 512'd0;
            pattern_tag     <= PAT_UNCOMPRESSED;
            comp_size_bytes <= 7'd64;
            compress_valid  <= 1'b0;
        end else if (compress_en) begin
            compress_valid <= 1'b1;

            if (is_zeros) begin
                pattern_tag     <= PAT_ZEROS;
                comp_size_bytes <= 7'd0; // Zero physical storage
                comp_data_out   <= 512'd0;
            end else if (is_rep_word) begin
                pattern_tag     <= PAT_REP_WORD;
                comp_size_bytes <= 7'd16; // Quantized to 16B slot
                comp_data_out   <= {448'd0, w64[0]};
            end else if (is_b8d1) begin
                pattern_tag     <= PAT_BASE8_DELTA1;
                comp_size_bytes <= 7'd16; // 8B base + 7x 1B delta = 15B -> 16B slot
                comp_data_out[63:0] <= w64[0];
                for (j = 1; j < 8; j = j + 1) begin
                    comp_data_out[64 + (j-1)*8 +: 8] <= w64[j][7:0] - w64[0][7:0];
                end
                comp_data_out[511:120] <= 392'd0;
            end else if (is_b4d1) begin
                pattern_tag     <= PAT_BASE4_DELTA1;
                comp_size_bytes <= 7'd32; // 4B base + 15x 1B delta = 19B -> 32B slot
                comp_data_out[31:0] <= w32[0];
                for (j = 1; j < 16; j = j + 1) begin
                    comp_data_out[32 + (j-1)*8 +: 8] <= w32[j][7:0] - w32[0][7:0];
                end
                comp_data_out[511:152] <= 360'd0;
            end else begin
                // Incompressible fallback
                pattern_tag     <= PAT_UNCOMPRESSED;
                comp_size_bytes <= 7'd64;
                comp_data_out   <= raw_line_in;
            end
        end else begin
            compress_valid <= 1'b0;
        end
    end

endmodule
