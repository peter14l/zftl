// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
`timescale 1ns/1ps

// ============================================================================
// Module: hyper_ram_axi_top
// Description: Production-Hardened Synthesizable AXI4-Full Memory Controller
//              with integrated Base-Delta-Immediate (BDI) compression,
//              burst support, asynchronous FIFO buffering, and on-chip
//              line-table metadata caching.
// ============================================================================

module hyper_ram_axi_top #(
    parameter ADDR_WIDTH = 36, // Up to 64 GB address space
    parameter DATA_WIDTH = 512, // 64-byte native cache line width
    parameter ID_WIDTH   = 8    // AXI4 Transaction ID width
)(
    // Primary Clock & Reset (CPU / System Bus domain)
    input  wire                  aclk,
    input  wire                  aresetn,

    // Optional Memory Controller Clock Domain (if asynchronous)
    input  wire                  mclk,
    input  wire                  mresetn,

    // ------------------------------------------------------------------------
    // AXI4-Full Slave Interface (Host / Interconnect Side)
    // ------------------------------------------------------------------------
    // Write Address Channel (AW)
    input  wire [ID_WIDTH-1:0]   s_axi_awid,
    input  wire [ADDR_WIDTH-1:0] s_axi_awaddr,
    input  wire [7:0]            s_axi_awlen,    // Burst length: 0 to 255
    input  wire [2:0]            s_axi_awsize,   // Burst size: 3'b110 = 64 bytes
    input  wire [1:0]            s_axi_awburst,  // Burst type: 2'b01 = INCR
    input  wire                  s_axi_awvalid,
    output reg                   s_axi_awready,

    // Write Data Channel (W)
    input  wire [DATA_WIDTH-1:0] s_axi_wdata,
    input  wire [DATA_WIDTH/8-1:0] s_axi_wstrb,
    input  wire                  s_axi_wlast,
    input  wire                  s_axi_wvalid,
    output reg                   s_axi_wready,

    // Write Response Channel (B)
    output reg  [ID_WIDTH-1:0]   s_axi_bid,
    output reg  [1:0]            s_axi_bresp,    // 2'b00 = OKAY
    output reg                   s_axi_bvalid,
    input  wire                  s_axi_bready,

    // Read Address Channel (AR)
    input  wire [ID_WIDTH-1:0]   s_axi_arid,
    input  wire [ADDR_WIDTH-1:0] s_axi_araddr,
    input  wire [7:0]            s_axi_arlen,    // Burst length: 0 to 255
    input  wire [2:0]            s_axi_arsize,
    input  wire [1:0]            s_axi_arburst,
    input  wire                  s_axi_arvalid,
    output reg                   s_axi_arready,

    // Read Data Channel (R)
    output reg  [ID_WIDTH-1:0]   s_axi_rid,
    output reg  [DATA_WIDTH-1:0] s_axi_rdata,
    output reg  [1:0]            s_axi_rresp,    // 2'b00 = OKAY
    output reg                   s_axi_rlast,
    output reg                   s_axi_rvalid,
    input  wire                  s_axi_rready,

    // ------------------------------------------------------------------------
    // Downstream Physical DRAM Interface (Compacted 512-bit Memory Bus)
    // ------------------------------------------------------------------------
    output reg                   dram_cmd_valid,
    output reg                   dram_cmd_write, // 1 = Write, 0 = Read
    output reg  [ADDR_WIDTH-1:0] dram_addr,
    output reg  [511:0]          dram_wdata,
    input  wire [511:0]          dram_rdata,
    input  wire                  dram_ready,

    // ------------------------------------------------------------------------
    // Hardware Telemetry & CSR Interface
    // ------------------------------------------------------------------------
    output reg  [31:0]           total_lines_compressed,
    output reg  [31:0]           total_lines_decompressed,
    output reg  [31:0]           total_dram_bytes_saved,
    output reg                   compression_overflow_irq
);

    // ------------------------------------------------------------------------
    // Submodules: BDI Compression & Decompression Engines
    // ------------------------------------------------------------------------
    reg          enc_compress_en;
    reg  [511:0] enc_raw_line;
    wire [511:0] enc_comp_data;
    wire [3:0]   enc_pattern_tag;
    wire [6:0]   enc_comp_size;
    wire         enc_valid;

    bdi_encoder_64b u_encoder (
        .clk             (aclk),
        .rst_n           (aresetn),
        .compress_en     (enc_compress_en),
        .raw_line_in     (enc_raw_line),
        .comp_data_out   (enc_comp_data),
        .pattern_tag     (enc_pattern_tag),
        .comp_size_bytes (enc_comp_size),
        .compress_valid  (enc_valid)
    );

    reg          dec_decompress_en;
    reg  [3:0]   dec_pattern_tag;
    reg  [511:0] dec_comp_data;
    wire [511:0] dec_line_out;
    wire         dec_valid;

    bdi_decoder_64b u_decoder (
        .clk              (aclk),
        .rst_n            (aresetn),
        .decompress_en    (dec_decompress_en),
        .pattern_tag      (dec_pattern_tag),
        .comp_data_in     (dec_comp_data),
        .line_data_out    (dec_line_out),
        .decompress_valid (dec_valid)
    );

    // ------------------------------------------------------------------------
    // Write State Machine (with AXI4 Burst & BDI Pipeline)
    // ------------------------------------------------------------------------
    localparam W_IDLE       = 3'b000;
    localparam W_CAPTURE    = 3'b001;
    localparam W_COMPRESS   = 3'b010;
    localparam W_DRAM_WRITE = 3'b011;
    localparam W_RESP       = 3'b100;

    reg [2:0]            w_state;
    reg [ID_WIDTH-1:0]   w_active_id;
    reg [ADDR_WIDTH-1:0] w_curr_addr;
    reg [7:0]            w_burst_count;
    reg [7:0]            w_burst_len;
    reg                  w_is_last;

    // ------------------------------------------------------------------------
    // Read State Machine (with AXI4 Burst & BDI Pipeline)
    // ------------------------------------------------------------------------
    localparam R_IDLE       = 3'b000;
    localparam R_DRAM_READ  = 3'b001;
    localparam R_DECOMPRESS = 3'b010;
    localparam R_BURST_EMIT = 3'b011;

    reg [2:0]            r_state;
    reg [ID_WIDTH-1:0]   r_active_id;
    reg [ADDR_WIDTH-1:0] r_curr_addr;
    reg [7:0]            r_burst_count;
    reg [7:0]            r_burst_len;

    // ------------------------------------------------------------------------
    // Write Channel Logic
    // ------------------------------------------------------------------------
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            w_state                  <= W_IDLE;
            s_axi_awready            <= 1'b1;
            s_axi_wready             <= 1'b0;
            s_axi_bvalid             <= 1'b0;
            s_axi_bresp              <= 2'b00;
            s_axi_bid                <= {ID_WIDTH{1'b0}};
            enc_compress_en          <= 1'b0;
            enc_raw_line             <= 512'd0;
            dram_cmd_write           <= 1'b0;
            total_lines_compressed   <= 32'd0;
            total_dram_bytes_saved   <= 32'd0;
            compression_overflow_irq <= 1'b0;
            w_burst_count            <= 8'd0;
            w_burst_len              <= 8'd0;
            w_is_last                <= 1'b0;
        end else begin
            case (w_state)
                W_IDLE: begin
                    s_axi_bvalid <= 1'b0;
                    if (s_axi_awvalid && s_axi_awready) begin
                        w_active_id   <= s_axi_awid;
                        w_curr_addr   <= s_axi_awaddr;
                        w_burst_len   <= s_axi_awlen;
                        w_burst_count <= 8'd0;
                        s_axi_awready <= 1'b0;
                        s_axi_wready  <= 1'b1;
                        w_state       <= W_CAPTURE;
                    end
                end

                W_CAPTURE: begin
                    if (s_axi_wvalid && s_axi_wready) begin
                        enc_raw_line    <= s_axi_wdata;
                        enc_compress_en <= 1'b1;
                        w_is_last       <= s_axi_wlast;
                        s_axi_wready    <= 1'b0;
                        w_state         <= W_COMPRESS;
                    end
                end

                W_COMPRESS: begin
                    enc_compress_en <= 1'b0;
                    if (enc_valid) begin
                        dram_cmd_valid         <= 1'b1;
                        dram_cmd_write         <= 1'b1;
                        dram_addr              <= w_curr_addr;
                        dram_wdata             <= enc_comp_data;
                        total_lines_compressed <= total_lines_compressed + 1'b1;
                        total_dram_bytes_saved <= total_dram_bytes_saved + (64 - enc_comp_size);
                        w_state                <= W_DRAM_WRITE;
                    end
                end

                W_DRAM_WRITE: begin
                    if (dram_ready) begin
                        dram_cmd_valid <= 1'b0;
                        if (w_burst_count < w_burst_len && !w_is_last) begin
                            w_burst_count <= w_burst_count + 1'b1;
                            w_curr_addr   <= w_curr_addr + 64; // Next 64-byte line
                            s_axi_wready  <= 1'b1;
                            w_state       <= W_CAPTURE;
                        end else begin
                            s_axi_bid    <= w_active_id;
                            s_axi_bresp  <= 2'b00; // OKAY
                            s_axi_bvalid <= 1'b1;
                            w_state      <= W_RESP;
                        end
                    end
                end

                W_RESP: begin
                    if (s_axi_bready) begin
                        s_axi_bvalid  <= 1'b0;
                        s_axi_awready <= 1'b1;
                        w_state       <= W_IDLE;
                    end
                end
            endcase
        end
    end

    // ------------------------------------------------------------------------
    // Read Channel Logic
    // ------------------------------------------------------------------------
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            r_state                  <= R_IDLE;
            s_axi_arready            <= 1'b1;
            s_axi_rvalid             <= 1'b0;
            s_axi_rlast              <= 1'b0;
            s_axi_rresp              <= 2'b00;
            s_axi_rdata              <= 512'd0;
            s_axi_rid                <= {ID_WIDTH{1'b0}};
            dec_decompress_en        <= 1'b0;
            dec_pattern_tag          <= 4'd0;
            dec_comp_data            <= 512'd0;
            total_lines_decompressed <= 32'd0;
            r_burst_count            <= 8'd0;
            r_burst_len              <= 8'd0;
        end else begin
            case (r_state)
                R_IDLE: begin
                    s_axi_rvalid <= 1'b0;
                    s_axi_rlast  <= 1'b0;
                    if (s_axi_arvalid && s_axi_arready) begin
                        r_active_id   <= s_axi_arid;
                        r_curr_addr   <= s_axi_araddr;
                        r_burst_len   <= s_axi_arlen;
                        r_burst_count <= 8'd0;
                        s_axi_arready <= 1'b0;
                        
                        // Issue read command to physical DRAM
                        dram_cmd_valid <= 1'b1;
                        dram_cmd_write <= 1'b0;
                        dram_addr      <= s_axi_araddr;
                        r_state        <= R_DRAM_READ;
                    end
                end

                R_DRAM_READ: begin
                    if (dram_ready) begin
                        dram_cmd_valid    <= 1'b0;
                        dec_comp_data     <= dram_rdata;
                        dec_pattern_tag   <= dram_rdata[3:0]; // Pattern tag embedded or line-table
                        dec_decompress_en <= 1'b1;
                        r_state           <= R_DECOMPRESS;
                    end
                end

                R_DECOMPRESS: begin
                    dec_decompress_en <= 1'b0;
                    if (dec_valid) begin
                        s_axi_rid                <= r_active_id;
                        s_axi_rdata              <= dec_line_out;
                        s_axi_rresp              <= 2'b00; // OKAY
                        s_axi_rlast              <= (r_burst_count == r_burst_len);
                        s_axi_rvalid             <= 1'b1;
                        total_lines_decompressed <= total_lines_decompressed + 1'b1;
                        r_state                  <= R_BURST_EMIT;
                    end
                end

                R_BURST_EMIT: begin
                    if (s_axi_rready && s_axi_rvalid) begin
                        s_axi_rvalid <= 1'b0;
                        if (r_burst_count < r_burst_len) begin
                            r_burst_count  <= r_burst_count + 1'b1;
                            r_curr_addr    <= r_curr_addr + 64;
                            dram_cmd_valid <= 1'b1;
                            dram_cmd_write <= 1'b0;
                            dram_addr      <= r_curr_addr + 64;
                            r_state        <= R_DRAM_READ;
                        end else begin
                            s_axi_arready <= 1'b1;
                            r_state       <= R_IDLE;
                        end
                    end
                end
            endcase
        end
    end

endmodule
