// Rockwell B6100 CPU — mirrors MAME's rw5000 family implementation
// (src/devices/cpu/rw5000/: rw5000base.cpp, b5000.cpp, b5000op.cpp,
//  b6000.cpp, b6100.cpp). One instruction per ce pulse. ROM is external:
// rom_addr is the registered PC, so a synchronous BRAM whose read data is
// valid by the next ce satisfies the interface.
module b6100_cpu (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       ce,

    output wire [9:0] rom_addr,
    input  wire [7:0] rom_data,

    input  wire [3:0] kb,
    input  wire [3:0] din,
    output reg  [8:0] str,
    output reg  [9:0] seg,
    output reg        spk,

    // debug/trace visibility (unconnected in synthesis top)
    output wire [9:0] dbg_pc,
    output wire [3:0] dbg_a,
    output wire [3:0] dbg_bl,
    output wire [1:0] dbg_bu,
    output wire [5:0] dbg_b,
    output wire       dbg_c,
    output wire [9:0] dbg_s,
    output wire       dbg_skip,
    output reg        dbg_illegal
);

    // architectural state
    reg [9:0] pc, s;
    reg [7:0] prev_op;
    reg [3:0] a, bl;
    reg [1:0] bu;
    reg       c, sr, skip_r;
    reg [5:0] ram_addr;
    reg [1:0] tra_step, ret_step;
    reg [3:0] ram [0:63];   // 48 nibbles used (banks x0-xB); rest harmless

    assign rom_addr = pc;
    assign dbg_pc = pc;   assign dbg_a = a;   assign dbg_bl = bl;
    assign dbg_bu = bu;   assign dbg_b = ram_addr;
    assign dbg_c = c;     assign dbg_s = s;   assign dbg_skip = skip_r;

    // ---- helper functions (mirror b5000.cpp / b6100.cpp predicates) ----
    function op_is_tl(input [7:0] o);          // b6100 op_is_tl
        op_is_tl = ((o & 8'hf8) == 8'h30) || ((o & 8'hfc) == 8'h38);
    endfunction
    function op_is_lb(input [7:0] o);          // b6100 op_is_lb
        op_is_lb = ((o & 8'hf0) == 8'h20) || ((o & 8'hfc) == 8'h3c)
                || ((o & 8'hfc) == 8'h1c);
    endfunction
    function op_is_atb(input [7:0] o);         // b5000 op_is_atb
        op_is_atb = (o == 8'h77);
    endfunction
    function [9:0] lfsr_next(input [9:0] p);   // rw5000base increment_pc
        reg feed;
        begin
            feed = ((p[5:0] & 6'h3e) == 6'h00) ^ p[1] ^ p[0];
            lfsr_next = {p[9:6], feed, p[5:1]};
        end
    endfunction
    function [9:0] seg_decode(input [3:0] d);  // b6100 decode_digit
        case (d)
            4'h0: seg_decode = 10'h03f;  4'h1: seg_decode = 10'h006;
            4'h2: seg_decode = 10'h05b;  4'h3: seg_decode = 10'h04f;
            4'h4: seg_decode = 10'h066;  4'h5: seg_decode = 10'h06d;
            4'h6: seg_decode = 10'h07d;  4'h7: seg_decode = 10'h007;
            4'h8: seg_decode = 10'h07f;  4'h9: seg_decode = 10'h06f;
            4'ha: seg_decode = 10'h070;  4'hb: seg_decode = 10'h046;
            4'hc: seg_decode = 10'h000;  4'hd: seg_decode = 10'h080;
            4'he: seg_decode = 10'h100;  4'hf: seg_decode = 10'h200;
        endcase
    endfunction

    // combinational RAM read at the address settled last instruction
    wire [3:0] ram_rdata = ram[ram_addr];

    // per-instruction temporaries (blocking-assigned inside the clocked
    // block, committed at the end; lint_off because this mirrors MAME's
    // strictly ordered execute loop)
    /* verilator lint_off BLKSEQ */
    reg [7:0] op_v;
    reg       skip_taken;
    reg [9:0] pc_v, s_v;
    reg [3:0] a_v, bl_v;
    reg [1:0] bu_v;
    reg       c_v, sr_v, skip_v;
    reg [9:0] seg_v;
    reg [8:0] str_v;
    reg       bl_delay_v, bu_delay_v;
    reg       ram_we;
    reg [3:0] ram_wdata;
    reg [1:0] tra_v, ret_v;
    reg [4:0] sum5;
    reg       illegal_v;
    reg [1:0] tdin_idx;
    reg [5:0] ram_addr_v;
    /* verilator lint_off UNUSEDSIGNAL */
    reg [15:0] str_full;
    /* verilator lint_on UNUSEDSIGNAL */
    integer i;

    always @(posedge clk) begin
        if (!rst_n) begin
            // b6000.cpp device_reset + rw5000base device_reset
            pc <= 10'd0; s <= 10'd0; prev_op <= 8'd0;
            a <= 4'd0; bl <= 4'd0; bu <= 2'd0; c <= 1'b0;
            sr <= 1'b0; skip_r <= 1'b0; ram_addr <= 6'd0;
            tra_step <= 2'd0; ret_step <= 2'd0;
            str <= 9'd0; seg <= 10'd0; spk <= 1'b0;
            dbg_illegal <= 1'b0;
        end else if (ce) begin
            // ---- fetch & skip (rw5000base execute_run) ----
            op_v = rom_data;
            skip_taken = skip_r && !op_is_tl(op_v);

            // defaults
            pc_v = lfsr_next(pc);
            s_v = s; a_v = a; bl_v = bl; bu_v = bu; c_v = c; sr_v = sr;
            seg_v = seg; str_v = str;
            skip_v = 1'b0; bl_delay_v = 1'b0; bu_delay_v = 1'b0;
            ram_we = 1'b0; ram_wdata = 4'd0; illegal_v = 1'b0;
            tra_v = tra_step; ret_v = ret_step;
            sum5 = 5'd0; tdin_idx = 2'd0; str_full = 16'd0; // reserved for later tasks

            if (!skip_taken) begin
                casez (op_v)
                    8'h00: ;                                  // NOP
                    8'h01: skip_v = c;                        // TC
                    8'h4?: a_v = ~op_v[3:0];                  // LAX (op_lax)
                    8'b0111_10??: a_v = a ^ 4'hf;             // COMP (op_comp)
                    // RAM addressing (b5000op.cpp)
                    8'b0010_00??: begin                       // LB 7,y (op_lb)
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = 4'd7;  bu_v = op_v[1:0];
                            bu_delay_v = (op_v[1:0] != 2'd0) != (bu != 2'd0);
                        end
                    end
                    8'b0010_01??: begin                       // LB 10,y
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = 4'd10; bu_v = op_v[1:0];
                            bu_delay_v = (op_v[1:0] != 2'd0) != (bu != 2'd0);
                        end
                    end
                    8'b0010_10??: begin                       // LB 9,y
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = 4'd9;  bu_v = op_v[1:0];
                            bu_delay_v = (op_v[1:0] != 2'd0) != (bu != 2'd0);
                        end
                    end
                    8'b0010_11??: begin                       // LB 8,y
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = 4'd8;  bu_v = op_v[1:0];
                            bu_delay_v = (op_v[1:0] != 2'd0) != (bu != 2'd0);
                        end
                    end
                    8'b0011_11??: begin                       // LB 0,y
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = 4'd0;  bu_v = op_v[1:0];
                            bu_delay_v = (op_v[1:0] != 2'd0) != (bu != 2'd0);
                        end
                    end
                    8'b0001_11??: begin                       // LB 11,y (b6100)
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = 4'd11; bu_v = op_v[1:0];
                            bu_delay_v = (op_v[1:0] != 2'd0) != (bu != 2'd0);
                        end
                    end
                    8'h77: begin                              // ATB (b6000 op_atb)
                        if (!op_is_lb(prev_op) && !op_is_atb(prev_op)) begin
                            bl_v = a; bl_delay_v = 1'b1;
                        end
                    end
                    8'b0101_00??: begin                       // LDA x (op_lda)
                        a_v = ram_rdata;
                        bu_v = op_v[1:0] ^ bu;
                        bu_delay_v = (bu_v != 2'd0) != (bu != 2'd0);
                    end
                    8'b0101_10??: begin                       // EXC x,0 (op_exc0)
                        a_v = ram_rdata; ram_we = 1'b1; ram_wdata = a;
                        bu_v = op_v[1:0] ^ bu;
                        bu_delay_v = (bu_v != 2'd0) != (bu != 2'd0);
                    end
                    8'b0101_01??: begin                       // EXC x,+1 (op_excp)
                        a_v = ram_rdata; ram_we = 1'b1; ram_wdata = a;
                        bu_v = op_v[1:0] ^ bu;
                        bu_delay_v = (bu_v != 2'd0) != (bu != 2'd0);
                        bl_v = bl + 4'd1; skip_v = (bl_v[2:0] == 3'd0);
                        bl_delay_v = 1'b1;
                    end
                    8'b0101_11??: begin                       // EXC x,-1 (op_excm)
                        a_v = ram_rdata; ram_we = 1'b1; ram_wdata = a;
                        bu_v = op_v[1:0] ^ bu;
                        bu_delay_v = (bu_v != 2'd0) != (bu != 2'd0);
                        bl_v = bl - 4'd1; skip_v = (bl_v == 4'hf);
                        bl_delay_v = 1'b1;
                    end
                    8'b0001_00??: begin                       // SM x (op_sm)
                        ram_we = 1'b1;
                        ram_wdata = ram_rdata | (4'd1 << op_v[1:0]);
                    end
                    8'b0001_01??: begin                       // RSM x (op_rsm)
                        ram_we = 1'b1;
                        ram_wdata = ram_rdata & ~(4'd1 << op_v[1:0]);
                    end
                    8'b0000_10??: skip_v = !ram_rdata[op_v[1:0]]; // TM x (op_tm)
                    8'b0111_11??: skip_v = (a == ram_rdata);      // TAM (op_tam)
                    // arithmetic (b5000op.cpp; SC/RSC opcodes moved on b6100)
                    8'h0c: c_v = 1'b1;                        // SC (b6100)
                    8'h0d: c_v = 1'b0;                        // RSC (b6100)
                    8'h6?: begin
                        if (op_v[3:0] != 4'hf) begin          // ADX x (op_adx)
                            sum5 = {1'b0, a} + {1'b0, ~op_v[3:0]};
                            skip_v = !sum5[4];
                            a_v = sum5[3:0];
                        end else begin                        // READ (b6100 op_read)
                            sum5 = {1'b0, a} + {1'b0, kb};
                            skip_v = !sum5[4];
                            a_v = sum5[3:0];
                        end
                    end
                    8'b0111_00??: begin                       // ADD (op_add)
                        sum5 = {1'b0, a} + {1'b0, ram_rdata};
                        if (!op_v[1]) begin
                            sum5 = sum5 + {4'd0, c};
                            c_v = sum5[4];
                        end
                        if (op_v[0]) skip_v = !sum5[4];
                        a_v = sum5[3:0];
                    end
                    // control flow (b5000op.cpp, b6100 opcode positions)
                    8'b0011_0???,
                    8'b0011_10??: begin                       // TL z (op_tl)
                        pc_v = {op_v[3:0], pc_v[5:0]};
                        s_v  = {pc_v[9:6], s_v[5:0]};         // S upper rides PU
                    end
                    8'b0001_10??: ret_v = 2'd1;               // RET (op_ret_step)
                    8'b1???_????: tra_v = 2'd1;               // TRA (op_tra_step)
                    // I/O (b5000op.cpp + b6000/b6100 overrides)
                    8'h02: skip_v = (kb != 4'd0);             // TKB (op_tkb)
                    8'h03: seg_v = seg | seg_decode(ram_rdata); // TKBS (b6100)
                    8'h74: seg_v = 10'd0;                     // KSEG (op_kseg)
                    8'h76: begin                              // ATBZ (b6000)
                        seg_v = 10'd0;
                        str_full = 16'h0001 << a;
                        str_v = str_full[8:0];
                    end
                    8'b0000_01??: begin                       // TDIN x (op_tdin)
                        tdin_idx = op_v[1:0] + 2'd3;          // (op-1)&3
                        skip_v = din[tdin_idx];
                    end
                    default: illegal_v = 1'b1;                // op_illegal -> nop
                endcase
            end

            // ---- multi-step continuations (run even on skipped slots) ----
            // TRA/RET step machines (op_tra_step / op_ret_step): step 1 arms
            // the skip; step 2 runs after the (usually skipped) next opcode.
            if (tra_v == 2'd1 && !skip_taken && op_v[7]) begin
                skip_v = 1'b1; tra_v = 2'd2;
            end else if (tra_v == 2'd2) begin
                if (!sr_v && !prev_op[6]) begin               // call: push return
                    sr_v = 1'b1;
                    s_v  = {s_v[9:6], pc[5:0]};               // slot addr low6
                end
                if (sr_v) pc_v = {4'd15 ^ {3'b000, prev_op[6]}, pc_v[5:0]};
                pc_v = {pc_v[9:6], prev_op[5:0]};             // PL from TRA op
                tra_v = 2'd0;
            end
            if (ret_v == 2'd1 && !skip_taken && (op_v & 8'hfc) == 8'h18) begin
                skip_v = 1'b1; ret_v = 2'd2;
            end else if (ret_v == 2'd2) begin
                pc_v = s_v; sr_v = 1'b0; ret_v = 2'd0;
            end

            // ---- ram_addr update with 1-instruction delays ----
            ram_addr_v = {bu_v, bl_v};
            if (bl_delay_v) ram_addr_v = {ram_addr_v[5:4], bl};
            if (bu_delay_v) ram_addr_v = {bu, ram_addr_v[3:0]};

            // ---- commit ----
            pc <= pc_v; s <= s_v;
            prev_op <= skip_taken ? 8'h00 : op_v;  // skipped -> fake nop
            a <= a_v; bl <= bl_v; bu <= bu_v; c <= c_v; sr <= sr_v;
            skip_r <= skip_v; ram_addr <= ram_addr_v;
            tra_step <= tra_v; ret_step <= ret_v;
            seg <= seg_v; str <= str_v;
            spk <= c_v;   // b6100: SPK follows carry (change-driven in MAME,
                          // equivalent since SPK only updates when C changes)
            if (!skip_taken && ram_we) ram[ram_addr] <= ram_wdata;
            if (illegal_v && !skip_taken) dbg_illegal <= 1'b1;
        end
    end
    /* verilator lint_on BLKSEQ */

    // RAM has no reset on real silicon; initialize for simulation determinism
    initial for (i = 0; i < 64; i = i + 1) ram[i] = 4'd0;

endmodule
