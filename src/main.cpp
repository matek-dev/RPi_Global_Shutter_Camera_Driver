#include <libcamera/libcamera.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#include <chrono>
#include <filesystem>

#include "Imx296Defaults.hpp"
#include "DngWriter.hpp"
#include "Util.hpp"

using namespace std::chrono_literals;

static volatile std::sig_atomic_t g_stop = 0;
static void onSigInt(int) { g_stop = 1; }

// Simple mapped buffer wrapper: in production use libcamera::MappedBuffer
struct BufferMap
{
    libcamera::FrameBuffer *fb{nullptr};
    void *addr{nullptr};
    size_t len{0};
};

static std::string usageStr()
{
    return R"(gs_cam - Raspberry Pi Global Shutter Camera (IMX296) RAW10 capture

Usage:
  gs_cam [--camera <id|model-substr>] [--frames N]
         [--exposure-us US] [--gain X.Y] [--fps X.Y]
         [--bayer RGGB|BGGR|GRBG|GBRG]
         [--outdir DIR] [--outfmt DNG|RAW]

Defaults:
  frames        : )" +
           std::to_string(Imx296Defaults::defaultFrameCount()) + R"(
  exposure-us   : )" +
           std::to_string(Imx296Defaults::defaultExposureUs()) + R"(
  gain          : )" +
           std::to_string(Imx296Defaults::defaultAnalogueGain()) + R"(
  fps           : )" +
           std::to_string(Imx296Defaults::defaultFps()) + R"(
  bayer         : )" +
           Imx296Defaults::defaultBayer() + R"(
  outdir        : )" +
           Imx296Defaults::defaultOutDir() + R"(
  outfmt        : )" +
           Imx296Defaults::defaultOutFmt() + R"(

Examples:
  gs_cam --frames 300 --exposure-us 6000 --gain 2.0 --fps 60 --outfmt DNG
)";
}

int main(int argc, char **argv)
{
    std::signal(SIGINT, onSigInt);

    // --- CLI parsing (quick and clean) ---
    std::string camMatch;
    unsigned frames = Imx296Defaults::defaultFrameCount();
    int exposureUs = Imx296Defaults::defaultExposureUs();
    float analogueGain = Imx296Defaults::defaultAnalogueGain();
    float fps = Imx296Defaults::defaultFps();
    std::string bayer = Imx296Defaults::defaultBayer();
    std::string outDir = Imx296Defaults::defaultOutDir();
    std::string outFmt = Imx296Defaults::defaultOutFmt();

    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto need = [&](const char *name)
        { if (i+1>=argc) { std::cerr<<"Missing arg after "<<name<<"\n"; std::cout<<usageStr(); return false;} return true; };
        if (a == "-h" || a == "--help")
        {
            std::cout << usageStr();
            return 0;
        }
        else if (a == "--camera")
        {
            if (!need("--camera"))
                return 1;
            camMatch = argv[++i];
        }
        else if (a == "--frames")
        {
            if (!need("--frames"))
                return 1;
            frames = std::stoul(argv[++i]);
        }
        else if (a == "--exposure-us")
        {
            if (!need("--exposure-us"))
                return 1;
            exposureUs = std::stoi(argv[++i]);
        }
        else if (a == "--gain")
        {
            if (!need("--gain"))
                return 1;
            analogueGain = std::stof(argv[++i]);
        }
        else if (a == "--fps")
        {
            if (!need("--fps"))
                return 1;
            fps = std::stof(argv[++i]);
        }
        else if (a == "--bayer")
        {
            if (!need("--bayer"))
                return 1;
            std::string in = argv[++i];
            if (!util::parseBayer(in, bayer))
            {
                std::cerr << "Invalid bayer pattern: " << in << "\n";
                return 1;
            }
        }
        else if (a == "--outdir")
        {
            if (!need("--outdir"))
                return 1;
            outDir = argv[++i];
        }
        else if (a == "--outfmt")
        {
            if (!need("--outfmt"))
                return 1;
            outFmt = argv[++i];
        }
        else
        {
            std::cerr << "Unknown arg: " << a << "\n"
                      << usageStr();
            return 1;
        }
    }

    if (!util::ensureDir(outDir))
    {
        std::cerr << "Failed to create/access outdir: " << outDir << "\n";
        return 1;
    }

    // --- libcamera setup ---
    libcamera::CameraManager cm;
    if (cm.start())
    {
        std::cerr << "CameraManager start failed.\n";
        return 1;
    }

    // Choose camera
    libcamera::Camera *camera = nullptr;
    for (auto &c : cm.cameras())
    {
        const auto id = c->id();
        const auto props = c->properties();
        std::string model = props.get(libcamera::properties::Model) ? *props.get(libcamera::properties::Model) : "";
        if (camMatch.empty() || id.find(camMatch) != std::string::npos || model.find(camMatch) != std::string::npos)
        {
            camera = c.get();
            break;
        }
    }
    if (!camera)
    {
        std::cerr << "No camera found (tip: try --camera imx296).\n";
        cm.stop();
        return 1;
    }

    if (camera->acquire())
    {
        std::cerr << "Failed to acquire camera.\n";
        cm.stop();
        return 1;
    }

    std::unique_ptr<libcamera::CameraConfiguration> config = camera->generateConfiguration({libcamera::StreamRole::Raw});
    if (!config || config->size() != 1)
    {
        std::cerr << "Failed to generate RAW config.\n";
        camera->release();
        cm.stop();
        return 1;
    }

    libcamera::StreamConfiguration &streamCfg = config->at(0);

    // Request RAW10 packed format (CSI-2). If not supported, libcamera will pick nearest.
    streamCfg.pixelFormat = libcamera::formats::SBGGR10_CSI2P; // mosaic overridden later by DNG tag; buffer layout is the same
    // Let pipeline pick a good resolution; but if you want, set explicit sensor native (IMX296 ~ 1440x1080ish depending mode).
    // streamCfg.size = {1440, 1080};

    if (config->validate() == libcamera::CameraConfiguration::Status::Invalid)
    {
        std::cerr << "Camera configuration invalid.\n";
        camera->release();
        cm.stop();
        return 1;
    }
    if (camera->configure(config.get()))
    {
        std::cerr << "Camera configure failed.\n";
        camera->release();
        cm.stop();
        return 1;
    }

    libcamera::FrameBufferAllocator allocator(camera);
    if (allocator.allocate(streamCfg.stream()))
    {
        std::cerr << "Buffer allocation failed.\n";
        camera->release();
        cm.stop();
        return 1;
    }
    auto buffers = allocator.buffers(streamCfg.stream());
    if (buffers.empty())
    {
        std::cerr << "No buffers allocated.\n";
        camera->release();
        cm.stop();
        return 1;
    }

    // Queue all buffers
    std::vector<std::unique_ptr<libcamera::Request>> requests;
    requests.reserve(buffers.size());
    for (auto &buf : buffers)
    {
        auto req = camera->createRequest();
        if (!req)
        {
            std::cerr << "Failed to create request.\n";
            camera->release();
            cm.stop();
            return 1;
        }
        if (req->addBuffer(streamCfg.stream(), buf.get()))
        {
            std::cerr << "Failed to add buffer to request.\n";
            camera->release();
            cm.stop();
            return 1;
        }
        // We'll store the mmap address in fb->cookie for easy access in Util::unpackRaw10To16
        // In production, use MappedBuffer RAII per frame.
        // Map whole plane (single plane expected)
        libcamera::FrameBuffer *fb = buf.get();
        if (fb->planes().size() != 1)
        {
            std::cerr << "Unexpected plane count.\n";
            camera->release();
            cm.stop();
            return 1;
        }
        int fd = fb->planes()[0].fd.get();
        size_t len = fb->planes()[0].length;
        void *addr = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED)
        {
            std::perror("mmap");
            camera->release();
            cm.stop();
            return 1;
        }
        fb->setCookie(addr); // stash pointer
        requests.push_back(std::move(req));
    }

    // Controls: exposure, gain, frame duration (fps)
    libcamera::ControlList controls = camera->properties(); // not used; need Request controls
    // We'll set per-request controls before first start.

    // Calculate frame duration from FPS
    int64_t frameDurationNs = static_cast<int64_t>(1e9 / std::max(1.0f, fps));
    // Minimal sanity
    if (frameDurationNs < 1'000'000)
        frameDurationNs = 1'000'000;

    // Build control list we’ll apply to the first N requests (libcamera lets controls per request)
    auto applyControls = [&](libcamera::Request *req)
    {
        libcamera::ControlList cl(camera->controls());
        if (camera->controls().contains(libcamera::controls::ExposureTime))
        {
            cl.set(libcamera::controls::ExposureTime, exposureUs); // microseconds
        }
        if (camera->controls().contains(libcamera::controls::AnalogueGain))
        {
            // AnalogueGain is commonly an integer in 1/256ths; on RPi it may accept float-ish. We try int*256 if needed.
            // Attempt float first; libcamera will coerce as needed.
            cl.set(libcamera::controls::AnalogueGain, analogueGain);
        }
        if (camera->controls().contains(libcamera::controls::FrameDurationLimits))
        {
            cl.set(libcamera::controls::FrameDurationLimits, libcamera::Span<const int64_t>({frameDurationNs, frameDurationNs}));
        }
        // Disable AE/AGC if exposed
        if (camera->controls().contains(libcamera::controls::AeEnable))
        {
            cl.set(libcamera::controls::AeEnable, false);
        }
        // Global shutter is sensor-defined for IMX296; no rolling->global switch control needed.
        if (req->controls().merge(cl))
            std::cerr << "Warning: failed to merge controls into request.\n";
    };

    // Signal handler for completed frames
    unsigned saved = 0;
    const uint32_t outW = streamCfg.size.width;
    const uint32_t outH = streamCfg.size.height;
    const bool writeDng = (outFmt == "DNG" || outFmt == "dng");
    const bool writeRaw = (outFmt == "RAW" || outFmt == "raw");
    if (!writeDng && !writeRaw)
    {
        std::cerr << "Unknown outfmt: " << outFmt << " (use DNG or RAW)\n";
        goto shutdown;
    }

    auto reqComplete = [&](libcamera::Request *req)
    {
        if (req->status() == libcamera::Request::RequestCancelled)
            return;

        const auto &buffers = req->buffers();
        auto it = buffers.begin();
        if (it == buffers.end())
        {
            std::cerr << "No buffer in request.\n";
            return;
        }
        libcamera::FrameBuffer *fb = it->second;

        // Unpack RAW10
        std::vector<uint16_t> pixels(size_t(outW) * outH);
        if (!util::unpackRaw10To16(fb, outW, outH, pixels))
        {
            std::cerr << "Unpack RAW10 failed.\n";
            return;
        }

        // Save
        std::ostringstream name;
        name << "imx296_" << std::setw(6) << std::setfill('0') << saved;
        std::string fileBase = util::joinPath(outDir, name.str());

        if (writeDng)
        {
            DngMeta meta;
            meta.width = outW;
            meta.height = outH;
            meta.bayer = toBayer(bayer);
            meta.bitsPerSample = 16;
            meta.whiteLevel = 1023;
            meta.blackLevel = 0;
            meta.analogGain = analogueGain;
            meta.exposureSeconds = exposureUs / 1e6f;

            if (!DngWriter::write(fileBase + ".dng", meta, pixels))
            {
                std::cerr << "DNG write failed.\n";
            }
        }
        else
        {
            // Dump as raw16 little-endian (10 bits valid)
            std::ofstream raw(fileBase + ".raw", std::ios::binary);
            raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * 2);
        }

        saved++;
        if (saved >= frames || g_stop)
        {
            camera->stop(); // stop streaming; safe to do from callback
            return;
        }

        // Re-queue buffer for continued streaming
        req->reuse(libcamera::Request::ReuseBuffers);
        applyControls(req);
        if (camera->queueRequest(req))
            std::cerr << "Re-queue request failed.\n";
    };

    camera->requestCompleted.connect(reqComplete);

    // Queue all initial requests
    for (auto &r : requests)
    {
        applyControls(r.get());
        if (camera->queueRequest(r.get()))
        {
            std::cerr << "Queue request failed.\n";
            goto shutdown;
        }
    }

    if (camera->start())
    {
        std::cerr << "Camera start failed.\n";
        goto shutdown;
    }

    // Simple loop while streaming (callbacks do the heavy lifting)
    while (!g_stop && saved < frames)
    {
        std::this_thread::sleep_for(10ms);
    }

shutdown:
    // Unmap buffers
    for (auto &buf : buffers)
    {
        libcamera::FrameBuffer *fb = buf.get();
        if (fb && fb->cookie())
        {
            void *addr = fb->cookie();
            munmap(addr, fb->planes()[0].length);
            fb->setCookie(nullptr);
        }
    }

    allocator.free(streamCfg.stream());

    camera->release();
    cm.stop();

    if (saved)
    {
        std::cout << "Saved " << saved << " frame(s) to " << outDir << "\n";
    }
    return 0;
}
