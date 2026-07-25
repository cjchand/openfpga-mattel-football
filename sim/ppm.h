#pragma once
#include <cstdio>
#include <cstdint>

// Minimal binary PPM (P6) writer. rgb is w*h*3 bytes, row-major, RGB order.
inline void write_ppm(const char* path, int w, int h, const uint8_t* rgb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, (size_t)w * h * 3, f);
    std::fclose(f);
}
