#pragma once
#include <cstdint>
#include <string>
#include <vector>

/*
 * Minimalistic DNG writer for RAW Bayer 10-bit packed → stored as 16-bit samples.
 * This is not a full-featured DNG engine, but produces compliant, openable files
 * in RawTherapee, Darktable, dcraw, etc.
 *
 * We write a baseline TIFF with DNG tags:
 *  - CFARepeatPatternDim, CFAPattern, CFAPlaneColor
 *  - CFA layout guessed from user-specified mosaic
 *  - BitsPerSample = 16, BlackLevel = 0 (or 64 if your pipeline expects), WhiteLevel = 1023
 *  - ColorMatrix (identity-ish placeholder) – acceptable for RAW workflows
 */

enum class BayerPattern
{
    RGGB,
    BGGR,
    GRBG,
    GBRG
};

struct DngMeta
{
    uint32_t width{0};
    uint32_t height{0};
    BayerPattern bayer{BayerPattern::RGGB};
    uint16_t bitsPerSample{16};    // we store as 16-bit
    uint16_t blackLevel{0};        // adjust if you characterize the sensor (often 64–256)
    uint16_t whiteLevel{1023};     // 10-bit max
    float analogGain{1.0f};        // optional metadata
    float exposureSeconds{0.008f}; // optional metadata (8ms default)
    float cfaIlluminant{21.0f};    // D65-ish placeholder
};

class DngWriter
{
public:
    // Writes 16-bit little-endian Bayer samples line-packed
    static bool write(const std::string &path,
                      const DngMeta &meta,
                      const std::vector<uint16_t> &pixels /* size = w*h */);
};
