#include "Util.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <libcamera/formats.h>

namespace util
{

    bool parseBayer(const std::string &s, std::string &norm)
    {
        std::string u;
        u.reserve(s.size());
        for (char c : s)
            u.push_back(std::toupper(static_cast<unsigned char>(c)));
        if (u == "RGGB" || u == "BGGR" || u == "GRBG" || u == "GBRG")
        {
            norm = u;
            return true;
        }
        return false;
    }

    bool ensureDir(const std::string &path)
    {
        struct stat st{};
        if (stat(path.c_str(), &st) == 0)
        {
            return S_ISDIR(st.st_mode);
        }
        // 0755 is fine for output dir
        return mkdir(path.c_str(), 0755) == 0;
    }

    std::string joinPath(const std::string &a, const std::string &b)
    {
        if (a.empty())
            return b;
        if (a.back() == '/')
            return a + b;
        return a + "/" + b;
    }

    std::string pixelFormatToString(const libcamera::PixelFormat &pf)
    {
        std::ostringstream oss;
        auto fourcc = pf.fourcc();
        oss << char(fourcc & 0xFF)
            << char((fourcc >> 8) & 0xFF)
            << char((fourcc >> 16) & 0xFF)
            << char((fourcc >> 24) & 0xFF);
        return oss.str();
    }

    /*
     * RAW10 CSI-2 packed format: 4 pixels (10 bits each) → 5 bytes.
     * libcamera buffers may have per-plane strides. We assume single-plane RAW stream.
     */
    bool unpackRaw10To16(const libcamera::FrameBuffer *fb,
                         uint32_t width, uint32_t height,
                         std::vector<uint16_t> &dst)
    {
        if (!fb)
            return false;
        if (dst.size() != static_cast<size_t>(width) * height)
            return false;
        if (fb->planes().size() != 1)
            return false;

        const auto &p = fb->planes()[0];
        const size_t offset = p.offset;
        const size_t length = p.length;

        // Memory mapping
        // FrameBuffer::Plane::fd is exported; on RPi this is mmap'able via libcamera::MappedBuffer.
        // For simplicity here, we assume contiguous span provided by libcamera in Request complete.
        // In production, use libcamera::MappedBuffer to safely map and unmap.
        void *base = fb->cookie(); // we’ll stash the mapped pointer in cookie from main.cpp
        if (!base)
            return false;

        const uint8_t *src = static_cast<const uint8_t *>(base) + offset;

        const size_t packedStride = (width * 10 + 7) / 8; // bytes per line when packed
        const size_t expected = packedStride * height;
        if (length < expected)
            return false;

        size_t outIdx = 0;
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t *line = src + y * packedStride;
            uint32_t x = 0;
            while (x < width)
            {
                // Group of 4 pixels → 5 bytes
                uint8_t b0 = line[0];
                uint8_t b1 = line[1];
                uint8_t b2 = line[2];
                uint8_t b3 = line[3];
                uint8_t b4 = line[4];

                uint16_t p0 = ((b0) | ((b4 & 0x03) << 8));
                uint16_t p1 = ((b1) | (((b4 >> 2) & 0x03) << 8));
                uint16_t p2 = ((b2) | (((b4 >> 4) & 0x03) << 8));
                uint16_t p3 = ((b3) | (((b4 >> 6) & 0x03) << 8));

                // Store left-aligned in 16-bit (10 LSBs used)
                if (x < width)
                    dst[outIdx++] = p0;
                if (x + 1 < width)
                    dst[outIdx++] = p1;
                if (x + 2 < width)
                    dst[outIdx++] = p2;
                if (x + 3 < width)
                    dst[outIdx++] = p3;

                line += 5;
                x += 4;
            }
        }
        return true;
    }

} // namespace util
