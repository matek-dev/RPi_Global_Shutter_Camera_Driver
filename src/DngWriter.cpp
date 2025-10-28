#include "DngWriter.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include <cstdint>
#include <fstream>

/*
 * Minimal TIFF/DNG writer. We keep this short and clear.
 * For production, consider libtiff + full DNG tag coverage.
 */

namespace
{
#pragma pack(push, 1)
    struct TiffHeader
    {
        uint16_t endian;    // 0x4949 = little-endian "II"
        uint16_t magic;     // 42
        uint32_t ifdOffset; // offset to first IFD
    };
#pragma pack(pop)

    struct IfdEntry
    {
        uint16_t tag;
        uint16_t type;
        uint32_t count;
        uint32_t value; // if fits; else offset
    };

    // TIFF type enums
    enum
    {
        TYPE_BYTE = 1,
        TYPE_ASCII = 2,
        TYPE_SHORT = 3,
        TYPE_LONG = 4,
        TYPE_RATIONAL = 5
    };

    // Tag IDs (subset)
    enum
    {
        TAG_ImageWidth = 256,
        TAG_ImageLength = 257,
        TAG_BitsPerSample = 258,
        TAG_Compression = 259,
        TAG_Photometric = 262,
        TAG_StripOffsets = 273,
        TAG_SamplesPerPixel = 277,
        TAG_RowsPerStrip = 278,
        TAG_StripByteCounts = 279,
        TAG_PlanarConfig = 284,
        TAG_CFARepeatPattern = 33421,
        TAG_CFAPattern = 33422,
        // DNG specific
        TAG_DNGVersion = 50706,
        TAG_UniqueCameraModel = 50708,
        TAG_CFAPlaneColor = 50710,
        TAG_BlackLevel = 50714,
        TAG_WhiteLevel = 50717,
        TAG_DefaultScale = 50733,
        TAG_CalibrationIlluminant1 = 50778,
        TAG_ColorMatrix1 = 50721,
    };

    uint16_t bayerToPlane(const BayerPattern b)
    {
        // 0=Red, 1=Green, 2=Blue, order of planes in CFA
        // DNG spec expects a 3-byte array for CFAPlaneColor = {0,1,2}
        return 0; // not used per-plane here (we supply array)
    }

    std::array<uint8_t, 4> bayerPattern2x2(const BayerPattern b)
    {
        // 2x2 CFAPattern values: 0=Red,1=Green,2=Blue
        switch (b)
        {
        case BayerPattern::RGGB:
            return {0, 1, 1, 2};
        case BayerPattern::BGGR:
            return {2, 1, 1, 0};
        case BayerPattern::GRBG:
            return {1, 0, 2, 1};
        case BayerPattern::GBRG:
            return {1, 2, 0, 1};
        }
        return {0, 1, 1, 2};
    }

} // namespace

static BayerPattern toBayer(const std::string &s)
{
    if (s == "RGGB")
        return BayerPattern::RGGB;
    if (s == "BGGR")
        return BayerPattern::BGGR;
    if (s == "GRBG")
        return BayerPattern::GRBG;
    if (s == "GBRG")
        return BayerPattern::GBRG;
    return BayerPattern::RGGB;
}

bool DngWriter::write(const std::string &path, const DngMeta &meta,
                      const std::vector<uint16_t> &pixels)
{
    const uint32_t w = meta.width, h = meta.height;
    if (pixels.size() != size_t(w) * h)
        return false;

    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    // Header
    TiffHeader hdr{};
    hdr.endian = 0x4949; // little-endian
    hdr.magic = 42;
    hdr.ifdOffset = sizeof(TiffHeader);
    f.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));

    // We’ll assemble IFD entries and data blocks, tracking offsets.
    std::vector<IfdEntry> ifd;

    auto writeValue = [&](uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
    {
        IfdEntry e{tag, type, count, value};
        ifd.push_back(e);
    };

    auto currentOffset = [&](void) -> uint32_t
    {
        return static_cast<uint32_t>(f.tellp());
    };

    auto align2 = [&]()
    {
        if ((currentOffset() & 1) != 0)
        {
            uint8_t z = 0;
            f.write(reinterpret_cast<const char *>(&z), 1);
        }
    };

    // Reserve space for IFD count + entries + nextIFD
    const uint16_t entryCount = 16 + 6; // approx number of entries (keep slack)
    std::streampos ifdStart = f.tellp();
    uint16_t actualCount = 0;
    f.seekp(ifdStart + std::streamoff(2 + entryCount * sizeof(IfdEntry) + 4)); // skip ahead

    // Data blocks we must write first (BitsPerSample array, CFA arrays, ColorMatrix, etc.)
    auto writeArraySHORT = [&](const std::vector<uint16_t> &arr) -> uint32_t
    {
        align2();
        uint32_t off = currentOffset();
        for (uint16_t v : arr)
            f.write(reinterpret_cast<const char *>(&v), sizeof(v));
        return off;
    };

    auto writeArrayBYTE = [&](const std::vector<uint8_t> &arr) -> uint32_t
    {
        align2();
        uint32_t off = currentOffset();
        f.write(reinterpret_cast<const char *>(arr.data()), arr.size());
        return off;
    };

    auto writeArrayRATIONAL = [&](const std::vector<std::pair<uint32_t, uint32_t>> &arr) -> uint32_t
    {
        align2();
        uint32_t off = currentOffset();
        for (auto &p : arr)
        {
            f.write(reinterpret_cast<const char *>(&p.first), 4);
            f.write(reinterpret_cast<const char *>(&p.second), 4);
        }
        return off;
    };

    auto writeAscii = [&](const std::string &s) -> uint32_t
    {
        std::string z = s;
        z.push_back('\0');
        uint32_t off = currentOffset();
        f.write(z.data(), z.size());
        return off;
    };

    // BitsPerSample = 16 for a single sample per pixel (Bayer)
    uint32_t bpsOff = writeArraySHORT({meta.bitsPerSample});
    // SamplesPerPixel = 1 (Bayer)
    const uint16_t spp = 1;

    // CFARepeatPatternDim = {2,2}
    uint32_t cfaDimOff = writeArraySHORT({2, 2});

    // CFAPattern 2x2
    auto patt = bayerPattern2x2(meta.bayer);
    uint32_t cfaPattOff = writeArrayBYTE({patt[0], patt[1], patt[2], patt[3]});

    // CFAPlaneColor = {0,1,2}
    uint32_t cfaPlaneColorOff = writeArrayBYTE({0, 1, 2});

    // DefaultScale (optional, 1/1,1/1)
    uint32_t defaultScaleOff = writeArrayRATIONAL({{1, 1}, {1, 1}});

    // DNGVersion
    uint32_t dngVerOff = writeArrayBYTE({1, 4, 0, 0}); // DNG 1.4.0.0

    // UniqueCameraModel
    uint32_t ucmOff = writeAscii("Raspberry Pi Global Shutter Camera IMX296");

    // BlackLevel & WhiteLevel
    uint32_t blackOff = writeArraySHORT({meta.blackLevel});
    uint32_t whiteOff = writeArraySHORT({meta.whiteLevel});

    // CalibrationIlluminant1
    uint16_t illuminant = static_cast<uint16_t>(meta.cfaIlluminant);

    // ColorMatrix1 (placeholder identity-ish)
    // 3x3 matrix as rationals. Use identity to avoid lying—RAW editors will still open fine.
    uint32_t cmOff = writeArrayRATIONAL({
        {1, 1},
        {0, 1},
        {0, 1},
        {0, 1},
        {1, 1},
        {0, 1},
        {0, 1},
        {0, 1},
        {1, 1},
    });

    // Image data (strip)
    align2();
    uint32_t stripOffset = currentOffset();
    const size_t bytes = size_t(w) * h * 2; // 16-bit
    f.write(reinterpret_cast<const char *>(pixels.data()), bytes);
    uint32_t stripByteCount = bytes;

    // Now assemble IFD
    auto push = [&](uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
    {
        IfdEntry e{tag, type, count, value};
        ifd.push_back(e);
        actualCount++;
    };

    push(TAG_ImageWidth, TYPE_LONG, 1, w);
    push(TAG_ImageLength, TYPE_LONG, 1, h);
    push(TAG_BitsPerSample, TYPE_SHORT, 1, bpsOff);
    push(TAG_Compression, TYPE_SHORT, 1, 1);     // no compression
    push(TAG_Photometric, TYPE_SHORT, 1, 32803); // CFA
    push(TAG_SamplesPerPixel, TYPE_SHORT, 1, spp);
    push(TAG_PlanarConfig, TYPE_SHORT, 1, 1); // contig
    push(TAG_RowsPerStrip, TYPE_LONG, 1, h);
    push(TAG_StripOffsets, TYPE_LONG, 1, stripOffset);
    push(TAG_StripByteCounts, TYPE_LONG, 1, stripByteCount);
    push(TAG_CFARepeatPattern, TYPE_SHORT, 2, cfaDimOff);
    push(TAG_CFAPattern, TYPE_BYTE, 4, cfaPattOff);
    push(TAG_CFAPlaneColor, TYPE_BYTE, 3, cfaPlaneColorOff);
    push(TAG_DNGVersion, TYPE_BYTE, 4, dngVerOff);
    push(TAG_UniqueCameraModel, TYPE_ASCII, static_cast<uint32_t>(std::strlen("Raspberry Pi Global Shutter Camera IMX296") + 1), ucmOff);
    push(TAG_BlackLevel, TYPE_SHORT, 1, blackOff);
    push(TAG_WhiteLevel, TYPE_SHORT, 1, whiteOff);
    push(TAG_DefaultScale, TYPE_RATIONAL, 2, defaultScaleOff);
    push(TAG_CalibrationIlluminant1, TYPE_SHORT, 1, illuminant);
    push(TAG_ColorMatrix1, TYPE_RATIONAL, 9, cmOff);

    // Write IFD
    std::streampos endPos = f.tellp();
    f.seekp(ifdStart);
    f.write(reinterpret_cast<const char *>(&actualCount), 2);
    for (uint16_t i = 0; i < actualCount; i++)
    {
        f.write(reinterpret_cast<const char *>(&ifd[i]), sizeof(IfdEntry));
    }
    uint32_t next = 0;
    f.write(reinterpret_cast<const char *>(&next), 4);
    f.seekp(endPos);

    return true;
}
