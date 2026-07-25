#pragma once
#include <cstdio>
#include <cstdint>

// Minimal binary PPM (P6) writer. rgb is w*h*3 bytes, row-major, RGB order.
// Returns false (without throwing) if the file could not be opened, the
// header/pixel data could not be written, or the close failed -- callers
// must check this so a bad outdir can't silently produce an empty capture.
inline bool write_ppm(const char* path, int w, int h, const uint8_t* rgb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return false; }
    bool ok = true;
    size_t n = (size_t)w * h * 3;
    if (std::fprintf(f, "P6\n%d %d\n255\n", w, h) < 0) ok = false;
    if (std::fwrite(rgb, 1, n, f) != n) ok = false;
    if (std::fclose(f) != 0) ok = false;
    return ok;
}
