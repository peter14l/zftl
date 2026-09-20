// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
`timescale 1ns/1ps

// ============================================================================
// Module: axi_async_fifo
// Description: Parameterized Dual-Clock Asynchronous FIFO for safe Clock Domain
//              Crossing (CDC) between CPU/SoC bus domain (e.g. 1.0 GHz) and 
//              the Physical Memory Controller domain (e.g. 400-800 MHz).
//              Uses Gray code pointers for multi-bit synchronization.
// ============================================================================

module axi_async_fifo #(
    parameter DATA_WIDTH = 512,
    parameter ADDR_WIDTH = 4   // Depth = 2^4 = 16 entries
)(
    // Write Domain (Source / CPU side)
    input  wire                  wclk,
    input  wire                  wrst_n,
    input  wire                  winc,
    input  wire [DATA_WIDTH-1:0] wdata,
    output wire                  wfull,

    // Read Domain (Destination / Memory side)
    input  wire                  rclk,
    input  wire                  rrst_n,
    input  wire                  rinc,
    output wire [DATA_WIDTH-1:0] rdata,
    output wire                  rempty
);

    localparam DEPTH = 1 << ADDR_WIDTH;

    // Dual-port distributed / block RAM
    reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];

    reg  [ADDR_WIDTH:0] wptr_bin;
    reg  [ADDR_WIDTH:0] wptr_gray;
    reg  [ADDR_WIDTH:0] wptr_gray_sync1, wptr_gray_sync2;

    reg  [ADDR_WIDTH:0] rptr_bin;
    reg  [ADDR_WIDTH:0] rptr_gray;
    reg  [ADDR_WIDTH:0] rptr_gray_sync1, rptr_gray_sync2;

    // ------------------------------------------------------------------------
    // Write Domain Logic
    // ------------------------------------------------------------------------
    wire [ADDR_WIDTH:0] wptr_bin_next  = wptr_bin + (winc & ~wfull);
    wire [ADDR_WIDTH:0] wptr_gray_next = (wptr_bin_next >> 1) ^ wptr_bin_next;

    always @(posedge wclk or negedge wrst_n) begin
        if (!wrst_n) begin
            wptr_bin  <= {(ADDR_WIDTH+1){1'b0}};
            wptr_gray <= {(ADDR_WIDTH+1){1'b0}};
        end else begin
            wptr_bin  <= wptr_bin_next;
            wptr_gray <= wptr_gray_next;
        end
    end

    always @(posedge wclk) begin
        if (winc && !wfull) begin
            mem[wptr_bin[ADDR_WIDTH-1:0]] <= wdata;
        end
    end

    // Synchronize read pointer into write clock domain (2-stage synchronizer)
    always @(posedge wclk or negedge wrst_n) begin
        if (!wrst_n) begin
            rptr_gray_sync1 <= {(ADDR_WIDTH+1){1'b0}};
            rptr_gray_sync2 <= {(ADDR_WIDTH+1){1'b0}};
        end else begin
            rptr_gray_sync1 <= rptr_gray;
            rptr_gray_sync2 <= rptr_gray_sync1;
        end
    end

    // Write Full Condition: MSB and 2nd MSB inverted, lower bits identical
    assign wfull = (wptr_gray_next == {~rptr_gray_sync2[ADDR_WIDTH:ADDR_WIDTH-1], 
                                        rptr_gray_sync2[ADDR_WIDTH-2:0]});

    // ------------------------------------------------------------------------
    // Read Domain Logic
    // ------------------------------------------------------------------------
    wire [ADDR_WIDTH:0] rptr_bin_next  = rptr_bin + (rinc & ~rempty);
    wire [ADDR_WIDTH:0] rptr_gray_next = (rptr_bin_next >> 1) ^ rptr_bin_next;

    always @(posedge rclk or negedge rrst_n) begin
        if (!rrst_n) begin
            rptr_bin  <= {(ADDR_WIDTH+1){1'b0}};
            rptr_gray <= {(ADDR_WIDTH+1){1'b0}};
        end else begin
            rptr_bin  <= rptr_bin_next;
            rptr_gray <= rptr_gray_next;
        end
    end

    assign rdata = mem[rptr_bin[ADDR_WIDTH-1:0]];

    // Synchronize write pointer into read clock domain (2-stage synchronizer)
    always @(posedge rclk or negedge rrst_n) begin
        if (!rrst_n) begin
            wptr_gray_sync1 <= {(ADDR_WIDTH+1){1'b0}};
            wptr_gray_sync2 <= {(ADDR_WIDTH+1){1'b0}};
        end else begin
            wptr_gray_sync1 <= wptr_gray;
            wptr_gray_sync2 <= wptr_gray_sync1;
        end
    end

    // Read Empty Condition: Gray pointers match exactly
    assign rempty = (rptr_gray == wptr_gray_sync2);

endmodule
