#pragma once
#include <string>
#include <libcamera/framebuffer.h>
#include <libcamera/pixel_format.h>
#include <vector>
#include <cstdint>

namespace util
{

    // Map string → Bayer code fourcc used in DNG tags (we store string; DNG code mapping is in DngWriter)
    bool parseBayer(const std::string &s, std::string &norm);

    // Ensure directory exists (mkdir -p equivalent)
    bool ensureDir(const std::string &path);

    // Unpack RAW10 CSI-2 packed buffer to 16-bit little-endian samples (aligned to 10 LSBs).
    // dst must have width*height elements; returns false on size mismatch.
    bool unpackRaw10To16(const libcamera::FrameBuffer *fb,
                         uint32_t width, uint32_t height,
                         std::vector<uint16_t> &dst);

    // Quick & simple filename helper
    std::string joinPath(const std::string &a, const std::string &b);

    // Tiny helper to convert libcamera PixelFormat to string (debug)
    std::string pixelFormatToString(const libcamera::PixelFormat &pf);

} // namespace util
