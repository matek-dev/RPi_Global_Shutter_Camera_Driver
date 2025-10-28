#pragma once
#include <string>

/*
 * Centralized defaults for the Sony IMX296 Raspberry Pi Global Shutter Camera.
 * These defaults are conservative and good starting points for most lighting.
 * Tweak at runtime via CLI flags.
 */

struct Imx296Defaults
{
    // RAW10 Bayer mosaic: some boards present RGGB; others BGGR. Make configurable.
    // Choose RGGB as a default—override with --bayer=<RGGB|BGGR|GRBG|GBRG>
    static inline std::string defaultBayer() { return "RGGB"; }

    // Exposure in microseconds (global shutter exposes all pixels at once)
    static inline int defaultExposureUs() { return 8000; } // 8 ms
    // Analogue gain in libcamera units (1.0 = unity, 2.0 = 2x). Float allowed but we pass as control int where needed.
    static inline float defaultAnalogueGain() { return 1.0f; }

    // Target FPS; sensor/driver will quantize to valid frame durations.
    static inline float defaultFps() { return 60.0f; }

    // Number of frames to capture
    static inline unsigned defaultFrameCount() { return 100; }

    // Output format: DNG or RAW
    static inline std::string defaultOutFmt() { return "DNG"; }

    // Output directory
    static inline const char *defaultOutDir() { return "./out"; }

    // If you’re streaming to disk, a modest queue helps avoid backpressure.
    static inline unsigned defaultBufferCount() { return 8; }
};
