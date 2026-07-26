// Runs the real ROM through the whole system and dumps one PPM per display
// window plus the raw speaker bitstream as a WAV. Automated assertions cover
// the brightness-class invariants; the frames themselves are for human eyes.
#include "Vfootball_system.h"
#include "verilated.h"
#include "ppm.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <sys/stat.h>

// Returns false if any fwrite/fprintf call failed or the close failed, so a
// bad outdir can't silently produce an empty capture that still PASSes.
static bool write_wav(const char* path, const std::vector<uint8_t>& samples, uint32_t rate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return false; }
    bool ok = true;
    uint32_t n = samples.size(), data = n, riff = 36 + data;
    uint16_t ch = 1, bits = 8, align = 1;
    uint32_t brate = rate;
    uint32_t fmtlen = 16; uint16_t pcm = 1;
    auto w = [&](const void* p, size_t sz, size_t cnt) {
        if (std::fwrite(p, sz, cnt, f) != cnt) ok = false;
    };
    w("RIFF", 1, 4); w(&riff, 4, 1);
    w("WAVEfmt ", 1, 8);
    w(&fmtlen, 4, 1); w(&pcm, 2, 1);
    w(&ch, 2, 1); w(&rate, 4, 1);
    w(&brate, 4, 1); w(&align, 2, 1);
    w(&bits, 2, 1);
    w("data", 1, 4); w(&data, 4, 1);
    w(samples.data(), 1, n);
    if (std::fclose(f) != 0) ok = false;
    return ok;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    if (argc != 8) {
        std::fprintf(stderr,
            "usage: football_frames <rom.bin> <n_windows> <kb_hex> <din_hex> <settle> <outdir> "
            "<mode: game|status>\n");
        return 2;
    }
    uint8_t rom[1024] = {0};
    {
        FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
        if (std::fread(rom, 1, 0x300, f) != 0x300 ||
            std::fread(rom + 0x380, 1, 0x80, f) != 0x80) {
            std::fprintf(stderr, "ROM must be 896 bytes\n"); return 2;
        }
        std::fclose(f);
    }
    int n_windows = std::atoi(argv[2]);
    unsigned kb = std::strtoul(argv[3], nullptr, 16) & 0xf;
    unsigned din = std::strtoul(argv[4], nullptr, 16) & 0xf;
    long settle = std::atol(argv[5]);
    const char* outdir = argv[6];
    const char* mode = argv[7];
    bool status_mode;
    if (std::strcmp(mode, "game") == 0) status_mode = false;
    else if (std::strcmp(mode, "status") == 0) status_mode = true;
    else { std::fprintf(stderr, "mode must be 'game' or 'status'\n"); return 2; }
    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "failed to create outdir '%s': %s\n", outdir, std::strerror(errno));
        return 2;
    }

    Vfootball_system d;
    d.ce = 1; d.kb = 0; d.din = din & 0x1; d.score_btn = 0;
    d.px_x = 0; d.px_y = 0;
    d.bezel_enable = 1;
    d.rst_n = 0; d.clk = 0; d.eval();
    d.clk = 1; d.eval(); d.clk = 0; d.eval();
    d.rst_n = 1;

    std::vector<uint8_t> spk_samples;
    long tick = 0;
    int frames = 0;
    bool saw_bright_dash = false, saw_dim_dash = false, saw_digit = false;
    // Column of the bright dash for each post-settle frame (game-mode
    // movement proof). -1 means no bright dash was found that frame.
    std::vector<int> dash_cols;

    while (frames < n_windows) {
        if (tick == settle) { d.kb = kb; d.din = din; }
        d.rom_data = rom[d.rom_addr & 0x3ff];
        d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        spk_samples.push_back(d.spk ? 220 : 35);
        tick++;

        if (d.window_tick) {
            std::vector<uint8_t> buf(400 * 360 * 3);
            for (int y = 0; y < 360; y++)
                for (int x = 0; x < 400; x++) {
                    d.px_x = x; d.px_y = y; d.eval();
                    uint32_t p = d.px_rgb;
                    size_t i = ((size_t)y * 400 + x) * 3;
                    buf[i] = p >> 16; buf[i+1] = p >> 8; buf[i+2] = p;
                }
            char path[512];
            std::snprintf(path, sizeof path, "%s/frame_%03d.ppm", outdir, frames);
            if (!write_ppm(path, 400, 360, buf.data())) {
                std::fprintf(stderr, "failed to write frame '%s'\n", path);
                return 2;
            }
            // classify content by scanning dash and digit pixel centers.
            // Only latch the saw_* invariants (and the movement trace) for
            // frames captured after the input settle point -- the pre-settle
            // boot display already lights both brightness classes and would
            // otherwise make these checks pass regardless of input.
            // Geometry below mirrors src/video_renderer.v's dash_x()/
            // DASH_Y0..2/digit_x()/DIGIT_Y (bezel-overlay layout, 400-wide
            // letterboxed canvas -- see docs/verification.md for the
            // bring-up history behind these numbers).
            bool post_settle = tick >= settle;
            int frame_bright_col = -1;
            for (int col = 0; col < 9; col++)
                for (int row = 0; row < 3; row++) {
                    int cy = (row == 0) ? 159 : (row == 1) ? 195 : 231;
                    d.px_x = 41 + 38 * col + 8; d.px_y = cy + 3; d.eval();
                    if (post_settle && d.px_rgb == 0xFF2020) {
                        saw_bright_dash = true;
                        if (frame_bright_col < 0) frame_bright_col = col;
                    }
                    if (post_settle && d.px_rgb == 0x801414) saw_dim_dash = true;
                }
            if (post_settle) dash_cols.push_back(frame_bright_col);
            static const int dx[7] = {58, 99, 152, 187, 222, 277, 318};
            for (int dg = 0; dg < 7; dg++) {
                d.px_x = dx[dg] + 12; d.px_y = 73; d.eval();  // segment a
                if (post_settle && d.px_rgb != 0x1A0505 && d.px_rgb != 0x000000) saw_digit = true;
            }
            frames++;
        }
    }

    std::string wav = std::string(outdir) + "/spk.wav";
    if (!write_wav(wav.c_str(), spk_samples, 70000)) {
        std::fprintf(stderr, "failed to write '%s'\n", wav.c_str());
        return 2;
    }

    long spk_toggles = 0;
    for (size_t i = 1; i < spk_samples.size(); i++)
        if (spk_samples[i] != spk_samples[i-1]) spk_toggles++;

    // Movement proof: the bright-dash column must change across at least
    // two post-settle frames when input is actually driving the game. This
    // is what catches a harness that would otherwise "pass" with kb=0 (no
    // button held) just because dashes happened to be lit at all.
    bool dash_moved = false;
    for (size_t i = 1; i < dash_cols.size(); i++) {
        if (dash_cols[i] != dash_cols[0]) { dash_moved = true; break; }
    }

    std::printf("mode=%s frames=%d spk_toggles=%ld bright_dash=%d dim_dash=%d digit=%d dash_moved=%d\n",
                mode, frames, spk_toggles, saw_bright_dash, saw_dim_dash, saw_digit, dash_moved);

    // The dash field (ball-carrier/defenders) and the digit readout (down,
    // field position, yards to go) are mutually-exclusive display modes on
    // real hardware (confirmed against MAME): holding Status blanks the
    // dash field for as long as it's held, and the digits never light
    // without it. So each mode asserts only the invariant it can actually
    // produce; the other is still tracked and printed informationally.
    // dash_moved is only asserted in game mode -- status mode blanks the
    // dash field by design, so the column trace there is informational.
    bool ok = status_mode ? saw_digit : (saw_bright_dash && saw_dim_dash && dash_moved);
    if (!ok) {
        std::printf("FAIL: mode=%s did not produce its expected display invariant\n", mode);
        return 1;
    }
    std::printf("PASS: football_frames (mode=%s)\n", mode);
    return 0;
}
