// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
`timescale 1ns/1ps

module tb_hyper_ram_top;

    // Parameters
    localparam ADDR_WIDTH = 36;
    localparam DATA_WIDTH = 512;
    localparam ID_WIDTH   = 8;
    localparam CLK_PERIOD = 10; // 100 MHz clock for simulation

    // Clock and Reset
    reg aclk;
    reg aresetn;

    // AXI4 Write Channels
    reg  [ID_WIDTH-1:0]   s_axi_awid;
    reg  [ADDR_WIDTH-1:0] s_axi_awaddr;
    reg  [7:0]            s_axi_awlen;
    reg  [2:0]            s_axi_awsize;
    reg  [1:0]            s_axi_awburst;
    reg                   s_axi_awvalid;
    wire                  s_axi_awready;

    reg  [DATA_WIDTH-1:0] s_axi_wdata;
    reg  [DATA_WIDTH/8-1:0] s_axi_wstrb;
    reg                   s_axi_wlast;
    reg                   s_axi_wvalid;
    wire                  s_axi_wready;

    wire [ID_WIDTH-1:0]   s_axi_bid;
    wire [1:0]            s_axi_bresp;
    wire                  s_axi_bvalid;
    reg                   s_axi_bready;

    // AXI4 Read Channels
    reg  [ID_WIDTH-1:0]   s_axi_arid;
    reg  [ADDR_WIDTH-1:0] s_axi_araddr;
    reg  [7:0]            s_axi_arlen;
    reg  [2:0]            s_axi_arsize;
    reg  [1:0]            s_axi_arburst;
    reg                   s_axi_arvalid;
    wire                  s_axi_arready;

    wire [ID_WIDTH-1:0]   s_axi_rid;
    wire [DATA_WIDTH-1:0] s_axi_rdata;
    wire [1:0]            s_axi_rresp;
    wire                  s_axi_rlast;
    wire                  s_axi_rvalid;
    reg                   s_axi_rready;

    // Downstream Physical DRAM Emulation Signals
    wire                  dram_cmd_valid;
    wire                  dram_cmd_write;
    wire [ADDR_WIDTH-1:0] dram_addr;
    wire [511:0]          dram_wdata;
    reg  [511:0]          dram_rdata;
    reg                   dram_ready;

    // Telemetry
    wire [31:0]           total_lines_compressed;
    wire [31:0]           total_lines_decompressed;
    wire [31:0]           total_dram_bytes_saved;
    wire                  compression_overflow_irq;

    // ------------------------------------------------------------------------
    // Simulated DRAM Memory Array
    // ------------------------------------------------------------------------
    reg [511:0] dram_storage [0:1023];

    always @(posedge aclk) begin
        if (!aresetn) begin
            dram_ready <= 1'b1;
            dram_rdata <= 512'd0;
        end else begin
            dram_ready <= 1'b1;
            if (dram_cmd_valid) begin
                if (dram_cmd_write) begin
                    dram_storage[dram_addr[15:6]] <= dram_wdata;
                end else begin
                    dram_rdata <= dram_storage[dram_addr[15:6]];
                end
            end
        end
    end

    // ------------------------------------------------------------------------
    // DUT Instantiation
    // ------------------------------------------------------------------------
    hyper_ram_axi_top #(
        .ADDR_WIDTH (ADDR_WIDTH),
        .DATA_WIDTH (DATA_WIDTH),
        .ID_WIDTH   (ID_WIDTH)
    ) dut (
        .aclk                     (aclk),
        .aresetn                  (aresetn),
        .mclk                     (aclk),
        .mresetn                  (aresetn),
        .s_axi_awid               (s_axi_awid),
        .s_axi_awaddr             (s_axi_awaddr),
        .s_axi_awlen              (s_axi_awlen),
        .s_axi_awsize             (s_axi_awsize),
        .s_axi_awburst            (s_axi_awburst),
        .s_axi_awvalid            (s_axi_awvalid),
        .s_axi_awready            (s_axi_awready),
        .s_axi_wdata              (s_axi_wdata),
        .s_axi_wstrb              (s_axi_wstrb),
        .s_axi_wlast              (s_axi_wlast),
        .s_axi_wvalid             (s_axi_wvalid),
        .s_axi_wready             (s_axi_wready),
        .s_axi_bid                (s_axi_bid),
        .s_axi_bresp              (s_axi_bresp),
        .s_axi_bvalid             (s_axi_bvalid),
        .s_axi_bready             (s_axi_bready),
        .s_axi_arid               (s_axi_arid),
        .s_axi_araddr             (s_axi_araddr),
        .s_axi_arlen              (s_axi_arlen),
        .s_axi_arsize             (s_axi_arsize),
        .s_axi_arburst            (s_axi_arburst),
        .s_axi_arvalid            (s_axi_arvalid),
        .s_axi_arready            (s_axi_arready),
        .s_axi_rid                (s_axi_rid),
        .s_axi_rdata              (s_axi_rdata),
        .s_axi_rresp              (s_axi_rresp),
        .s_axi_rlast              (s_axi_rlast),
        .s_axi_rvalid             (s_axi_rvalid),
        .s_axi_rready             (s_axi_rready),
        .dram_cmd_valid           (dram_cmd_valid),
        .dram_cmd_write           (dram_cmd_write),
        .dram_addr                (dram_addr),
        .dram_wdata               (dram_wdata),
        .dram_rdata               (dram_rdata),
        .dram_ready               (dram_ready),
        .total_lines_compressed   (total_lines_compressed),
        .total_lines_decompressed (total_lines_decompressed),
        .total_dram_bytes_saved   (total_dram_bytes_saved),
        .compression_overflow_irq (compression_overflow_irq)
    );

    // Clock Generator
    always #(CLK_PERIOD/2) aclk = ~aclk;

    // ------------------------------------------------------------------------
    // Tasks: AXI4 Write and Read Operations
    // ------------------------------------------------------------------------
    task axi_write_line(input [ADDR_WIDTH-1:0] addr, input [DATA_WIDTH-1:0] data);
        begin
            @(posedge aclk);
            s_axi_awid    <= 8'hA1;
            s_axi_awaddr  <= addr;
            s_axi_awlen   <= 8'd0; // 1 beat
            s_axi_awsize  <= 3'b110;
            s_axi_awburst <= 2'b01;
            s_axi_awvalid <= 1'b1;

            wait (s_axi_awready);
            @(posedge aclk);
            s_axi_awvalid <= 1'b0;

            s_axi_wdata   <= data;
            s_axi_wlast   <= 1'b1;
            s_axi_wvalid  <= 1'b1;
            s_axi_bready  <= 1'b1;

            wait (s_axi_wready);
            @(posedge aclk);
            s_axi_wvalid <= 1'b0;

            wait (s_axi_bvalid);
            @(posedge aclk);
            s_axi_bready <= 1'b0;
        end
    endtask

    task axi_read_and_verify(input [ADDR_WIDTH-1:0] addr, input [DATA_WIDTH-1:0] expected_data);
        begin
            @(posedge aclk);
            s_axi_arid    <= 8'hB2;
            s_axi_araddr  <= addr;
            s_axi_arlen   <= 8'd0;
            s_axi_arsize  <= 3'b110;
            s_axi_arburst <= 2'b01;
            s_axi_arvalid <= 1'b1;
            s_axi_rready  <= 1'b1;

            wait (s_axi_arready);
            @(posedge aclk);
            s_axi_arvalid <= 1'b0;

            wait (s_axi_rvalid);
            @(posedge aclk);
            if (s_axi_rdata !== expected_data) begin
                $display("[-] ERROR: Data mismatch at addr 0x%h! Got: 0x%h, Expected: 0x%h", addr, s_axi_rdata, expected_data);
                $stop;
            end else begin
                $display("[+] SUCCESS: Verified line readback at addr 0x%h", addr);
            end
            s_axi_rready <= 1'b0;
        end
    endtask

    // ------------------------------------------------------------------------
    // Main Verification Stimulus
    // ------------------------------------------------------------------------
    reg [511:0] test_zeros;
    reg [511:0] test_repeated_word;
    reg [511:0] test_base8_delta1;

    initial begin
        $display("================================================================");
        $display("  HyperRAM AXI4-Full Hardware IP Verification Testbench");
        $display("================================================================");

        aclk          = 0;
        aresetn       = 0;
        s_axi_awvalid = 0;
        s_axi_wvalid  = 0;
        s_axi_bready  = 0;
        s_axi_arvalid = 0;
        s_axi_rready  = 0;
        s_axi_wstrb   = {(DATA_WIDTH/8){1'b1}};

        test_zeros = 512'd0;
        test_repeated_word = {8{64'hCAFEBABE_DEADBEEF}};
        test_base8_delta1 = {
            64'h7FFF_0000_1000 + 64'd70,
            64'h7FFF_0000_1000 + 64'd60,
            64'h7FFF_0000_1000 + 64'd50,
            64'h7FFF_0000_1000 + 64'd40,
            64'h7FFF_0000_1000 + 64'd30,
            64'h7FFF_0000_1000 + 64'd20,
            64'h7FFF_0000_1000 + 64'd10,
            64'h7FFF_0000_1000
        };

        #(CLK_PERIOD * 5);
        aresetn = 1;
        #(CLK_PERIOD * 2);

        // Test 1: Zeros Line
        $display("[*] Test 1: Writing and reading all-zeros line...");
        axi_write_line(36'h1000, test_zeros);
        axi_read_and_verify(36'h1000, test_zeros);

        // Test 2: Repeated Word Line
        $display("[*] Test 2: Writing and reading repeated-word line...");
        axi_write_line(36'h1040, test_repeated_word);
        axi_read_and_verify(36'h1040, test_repeated_word);

        // Test 3: Base8-Delta1 Pointer Line
        $display("[*] Test 3: Writing and reading Base8-Delta1 pointer array...");
        axi_write_line(36'h1080, test_base8_delta1);
        axi_read_and_verify(36'h1080, test_base8_delta1);

        $display("----------------------------------------------------------------");
        $display("[+] ALL HARDWARE RTL IP TESTS PASSED!");
        $display("    - Total Lines Compressed:   %0d", total_lines_compressed);
        $display("    - Total Lines Decompressed: %0d", total_lines_decompressed);
        $display("    - Total Physical RAM Saved: %0d bytes", total_dram_bytes_saved);
        $display("================================================================");
        $finish;
    end

endmodule
