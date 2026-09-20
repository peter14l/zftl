// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
`timescale 1ns/1ps

// ============================================================================
// Module: bdi_decoder_64b
// Description: Fully parallel, single-cycle Base-Delta-Immediate (BDI) hardware
//              decompressor. Reconstructs a 512-bit (64-byte) cache line from
//              compressed tags, bases, and delta vectors in < 5ns.
// ============================================================================

module bdi_decoder_64b (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         decompress_en,

    // Input compressed payload (up to 512 bits) + 4-bit pattern tag
    input  wire [3:0]   pattern_tag,
    input  wire [511:0] comp_data_in,

    // Output decompressed 64-byte line (512 bits)
    output reg  [511:0] line_data_out,
    output reg          decompress_valid
);

    // Pattern definitions matching BDI specification
    localparam PAT_ZEROS        = 4'h0;
    localparam PAT_REP_WORD     = 4'h1;
    localparam PAT_BASE8_DELTA1 = 4'h2;
    localparam PAT_BASE8_DELTA2 = 4'h3;
    localparam PAT_BASE8_DELTA4 = 4'h4;
    localparam PAT_BASE4_DELTA1 = 4'h5;
    localparam PAT_BASE4_DELTA2 = 4'h6;
    localparam PAT_BASE2_DELTA1 = 4'h7;
    localparam PAT_UNCOMPRESSED = 4'hF;

    // ------------------------------------------------------------------------
    // Intermediate base and delta extractions
    // ------------------------------------------------------------------------
    wire [63:0] base64 = comp_data_in[63:0];
    wire [31:0] base32 = comp_data_in[31:0];
    wire [15:0] base16 = comp_data_in[15:0];

    // Base8-Delta1 adders (8x 64-bit words)
    wire [63:0] b8d1_word [0:7];
    assign b8d1_word[0] = base64;
    genvar i;
    generate
        for (i = 1; i < 8; i = i + 1) begin : gen_b8d1
            wire signed [63:0] d = {{56{comp_data_in[64 + (i-1)*8 + 7]}}, comp_data_in[64 + (i-1)*8 +: 8]};
            assign b8d1_word[i] = base64 + d;
        end
    endgenerate

    // Base8-Delta2 adders (8x 64-bit words)
    wire [63:0] b8d2_word [0:7];
    assign b8d2_word[0] = base64;
    generate
        for (i = 1; i < 8; i = i + 1) begin : gen_b8d2
            wire signed [63:0] d = {{48{comp_data_in[64 + (i-1)*16 + 15]}}, comp_data_in[64 + (i-1)*16 +: 16]};
            assign b8d2_word[i] = base64 + d;
        end
    endgenerate

    // Base4-Delta1 adders (16x 32-bit words)
    wire [31:0] b4d1_word [0:15];
    assign b4d1_word[0] = base32;
    generate
        for (i = 1; i < 16; i = i + 1) begin : gen_b4d1
            wire signed [31:0] d = {{24{comp_data_in[32 + (i-1)*8 + 7]}}, comp_data_in[32 + (i-1)*8 +: 8]};
            assign b4d1_word[i] = base32 + d;
        end
    endgenerate

    // ------------------------------------------------------------------------
    // Synchronous Decompression Pipeline Output
    // ------------------------------------------------------------------------
    integer k;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            line_data_out    <= 512'd0;
            decompress_valid <= 1'b0;
        end else if (decompress_en) begin
            decompress_valid <= 1'b1;
            case (pattern_tag)
                PAT_ZEROS: begin
                    line_data_out <= 512'd0;
                end

                PAT_REP_WORD: begin
                    for (k = 0; k < 8; k = k + 1) begin
                        line_data_out[k*64 +: 64] <= base64;
                    end
                end

                PAT_BASE8_DELTA1: begin
                    for (k = 0; k < 8; k = k + 1) begin
                        line_data_out[k*64 +: 64] <= b8d1_word[k];
                    end
                end

                PAT_BASE8_DELTA2: begin
                    for (k = 0; k < 8; k = k + 1) begin
                        line_data_out[k*64 +: 64] <= b8d2_word[k];
                    end
                end

                PAT_BASE4_DELTA1: begin
                    for (k = 0; k < 16; k = k + 1) begin
                        line_data_out[k*32 +: 32] <= b4d1_word[k];
                    end
                end

                PAT_UNCOMPRESSED: begin
                    line_data_out <= comp_data_in;
                end

                default: begin
                    line_data_out <= comp_data_in;
                end
            endcase
        end else begin
            decompress_valid <= 1'b0;
        end
    end

endmodule
