// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// Synthesizable Pipelined LZ4 Hardware Decompressor for 4KB Flash Pages.
// Open-Hardware Architecture Initiative for Storage Silicon.
`timescale 1ns/1ps

module lz4_decompressor_4k #(
    parameter MAX_BLOCK_SIZE = 4096
)(
    input  wire        clk,
    input  wire        rst_n,

    // Control interface
    input  wire        start,
    output reg         busy,
    output reg         done,
    output reg         error_flag,
    output reg  [12:0] decompressed_bytes,

    // AXI4-Stream Slave: Compressed Input Byte Stream
    input  wire [7:0]  s_axis_tdata,
    input  wire        s_axis_tvalid,
    output reg         s_axis_tready,
    input  wire        s_axis_tlast,

    // AXI4-Stream Master: Decompressed Output Byte Stream
    output reg  [7:0]  m_axis_tdata,
    output reg         m_axis_tvalid,
    input  wire        m_axis_tready,
    output reg         m_axis_tlast
);

    // State Machine Definitions
    localparam ST_IDLE       = 4'd0;
    localparam ST_TOKEN      = 4'd1;
    localparam ST_EXT_LIT    = 4'd2;
    localparam ST_LITERAL    = 4'd3;
    localparam ST_OFFSET_L   = 4'd4;
    localparam ST_OFFSET_H   = 4'd5;
    localparam ST_EXT_MATCH  = 4'd6;
    localparam ST_MATCH_COPY = 4'd7;
    localparam ST_DONE       = 4'd8;

    reg [3:0]  state;
    reg [12:0] write_ptr;
    reg [15:0] lit_len;
    reg [15:0] match_len;
    reg [15:0] match_offset;
    reg [3:0]  saved_match_token;
    reg [7:0]  offset_low;

    // Dual-Port History Buffer (Synthesizes to 4KB Block RAM)
    reg [7:0] history_ram [0:MAX_BLOCK_SIZE-1];

    wire [11:0] match_read_addr = (write_ptr >= match_offset[12:0]) ?
                                  (write_ptr[11:0] - match_offset[11:0]) : 12'd0;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state              <= ST_IDLE;
            busy               <= 1'b0;
            done               <= 1'b0;
            error_flag         <= 1'b0;
            decompressed_bytes <= 13'd0;
            write_ptr          <= 13'd0;
            lit_len            <= 16'd0;
            match_len          <= 16'd0;
            match_offset       <= 16'd0;
            saved_match_token  <= 4'd0;
            offset_low         <= 8'd0;
            s_axis_tready      <= 1'b0;
            m_axis_tvalid      <= 1'b0;
            m_axis_tdata       <= 8'd0;
            m_axis_tlast       <= 1'b0;
        end else begin
            case (state)
                ST_IDLE: begin
                    done          <= 1'b0;
                    error_flag    <= 1'b0;
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;
                    if (start) begin
                        busy               <= 1'b1;
                        write_ptr          <= 13'd0;
                        decompressed_bytes <= 13'd0;
                        s_axis_tready      <= 1'b1;
                        state              <= ST_TOKEN;
                    end else begin
                        busy          <= 1'b0;
                        s_axis_tready <= 1'b0;
                    end
                end

                ST_TOKEN: begin
                    m_axis_tvalid <= 1'b0;
                    if (s_axis_tvalid && s_axis_tready) begin
                        saved_match_token <= s_axis_tdata[3:0];
                        if (s_axis_tdata[7:4] == 4'hF) begin
                            lit_len       <= 16'd15;
                            s_axis_tready <= 1'b1;
                            state         <= ST_EXT_LIT;
                        end else if (s_axis_tdata[7:4] > 4'd0) begin
                            lit_len       <= {12'd0, s_axis_tdata[7:4]};
                            s_axis_tready <= 1'b1;
                            state         <= ST_LITERAL;
                        end else begin
                            // 0 literals -> proceed straight to offset
                            lit_len       <= 16'd0;
                            s_axis_tready <= 1'b1;
                            state         <= ST_OFFSET_L;
                        end
                    end
                end

                ST_EXT_LIT: begin
                    if (s_axis_tvalid && s_axis_tready) begin
                        lit_len <= lit_len + s_axis_tdata;
                        if (s_axis_tdata != 8'hFF) begin
                            s_axis_tready <= 1'b1;
                            state         <= ST_LITERAL;
                        end
                    end
                end

                ST_LITERAL: begin
                    if (s_axis_tvalid) begin
                        m_axis_tdata  <= s_axis_tdata;
                        m_axis_tvalid <= 1'b1;
                        m_axis_tlast  <= (write_ptr == MAX_BLOCK_SIZE - 1);

                        // If downstream consumed the byte or is ready
                        if (m_axis_tready) begin
                            history_ram[write_ptr] <= s_axis_tdata;
                            write_ptr              <= write_ptr + 1'b1;
                            decompressed_bytes     <= decompressed_bytes + 1'b1;

                            if (lit_len == 16'd1) begin
                                if (write_ptr + 1'b1 == MAX_BLOCK_SIZE || s_axis_tlast) begin
                                    s_axis_tready <= 1'b0;
                                    state         <= ST_DONE;
                                end else begin
                                    s_axis_tready <= 1'b1;
                                    state         <= ST_OFFSET_L;
                                end
                            end else begin
                                lit_len       <= lit_len - 1'b1;
                                s_axis_tready <= 1'b1;
                            end
                        end else begin
                            s_axis_tready <= 1'b0; // Wait for downstream consumer
                        end
                    end
                end

                ST_OFFSET_L: begin
                    m_axis_tvalid <= 1'b0;
                    if (s_axis_tvalid && s_axis_tready) begin
                        offset_low    <= s_axis_tdata;
                        s_axis_tready <= 1'b1;
                        state         <= ST_OFFSET_H;
                    end
                end

                ST_OFFSET_H: begin
                    if (s_axis_tvalid && s_axis_tready) begin
                        match_offset <= {s_axis_tdata, offset_low};
                        if (saved_match_token == 4'hF) begin
                            match_len     <= 16'd19; // 15 + 4
                            s_axis_tready <= 1'b1;
                            state         <= ST_EXT_MATCH;
                        end else begin
                            match_len     <= {12'd0, saved_match_token} + 16'd4;
                            s_axis_tready <= 1'b0; // Pause input stream during match copy
                            state         <= ST_MATCH_COPY;
                        end
                    end
                end

                ST_EXT_MATCH: begin
                    if (s_axis_tvalid && s_axis_tready) begin
                        match_len <= match_len + s_axis_tdata;
                        if (s_axis_tdata != 8'hFF) begin
                            s_axis_tready <= 1'b0;
                            state         <= ST_MATCH_COPY;
                        end
                    end
                end

                ST_MATCH_COPY: begin
                    s_axis_tready <= 1'b0;
                    // Read match from history buffer
                    m_axis_tdata  <= history_ram[match_read_addr];
                    m_axis_tvalid <= 1'b1;
                    m_axis_tlast  <= (write_ptr == MAX_BLOCK_SIZE - 1);

                    if (m_axis_tready) begin
                        history_ram[write_ptr] <= history_ram[match_read_addr];
                        write_ptr              <= write_ptr + 1'b1;
                        decompressed_bytes     <= decompressed_bytes + 1'b1;

                        if (match_len == 16'd1) begin
                            if (write_ptr + 1'b1 == MAX_BLOCK_SIZE) begin
                                state <= ST_DONE;
                            end else begin
                                s_axis_tready <= 1'b1;
                                state         <= ST_TOKEN;
                            end
                        end else begin
                            match_len <= match_len - 1'b1;
                        end
                    end
                end

                ST_DONE: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;
                    s_axis_tready <= 1'b0;
                    done          <= 1'b1;
                    busy          <= 1'b0;
                    state         <= ST_IDLE;
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
