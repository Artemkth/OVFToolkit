#include "fftw-backend.h"

#include <algorithm>
#include <cstdint>
#include <execution>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
constexpr std::uint64_t fourGiB = 4ULL * 1024 * 1024 * 1024;

std::uint64_t availablePhysicalMemory()
{
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0)
        return status.ullAvailPhys;
#elif defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    std::string label;
    std::uint64_t kibibytes{};
    while (meminfo >> label >> kibibytes)
    {
        if (label == "MemAvailable:")
            return kibibytes <= std::numeric_limits<std::uint64_t>::max() / 1024
                ? kibibytes * 1024 : std::numeric_limits<std::uint64_t>::max();
        meminfo.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
#endif
    return fourGiB;
}

std::size_t defaultWorkspaceLimit()
{
    const auto available = availablePhysicalMemory();
    const auto ninetyFivePercent = available - available / 20;
    return static_cast<std::size_t>(std::min<std::uint64_t>(
        {fourGiB, ninetyFivePercent,
         std::numeric_limits<std::size_t>::max()}));
}
}

FFTWEngine::FFTWEngine() noexcept
{
#ifdef OVFTOOLKIT_HAS_FFTW_THREADS
    if (fftwf_init_threads() != 0)
        fftwf_plan_with_nthreads(
            static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
#endif
}

FFTWEngine::~FFTWEngine() noexcept
{
    if (plan != nullptr)
        fftwf_destroy_plan(plan);
#ifdef OVFTOOLKIT_HAS_FFTW_THREADS
    fftwf_cleanup_threads();
#endif
}

bool FFTWEngine::makePlan(std::size_t batch)
{
    if (plan != nullptr)
    {
        fftwf_destroy_plan(plan);
        plan = nullptr;
    }
    if (fftLength > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        batch > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;

    const auto complexPoints = fftLength / 2 + 1;
    inputBuffer.resize(fftLength * batch);
    outputBuffer.resize(2 * complexPoints * batch);
    const int length = static_cast<int>(fftLength);
    plan = fftwf_plan_many_dft_r2c(
        1, &length, static_cast<int>(batch), inputBuffer.data(), nullptr,
        static_cast<int>(batch), 1,
        reinterpret_cast<fftwf_complex*>(outputBuffer.data()), nullptr,
        static_cast<int>(batch), 1, FFTW_ESTIMATE);
    return plan != nullptr;
}

bool FFTWEngine::Init(std::size_t length, std::size_t maxBatch,
                      std::size_t maxMemory, std::size_t batchMultiple)
{
    fail = false;
    fftLength = length;
    interpolation = {};
    if (length < 2 || maxBatch == 0 || batchMultiple == 0)
        return !(fail = true);

    const auto complexPoints = length / 2 + 1;
    constexpr auto sizeMax = std::numeric_limits<std::size_t>::max();
    // Input, complex output, and the worst-case cubic-spline scratch area.
    if (length > sizeMax / 3 || complexPoints > sizeMax / 2 ||
        3 * length > sizeMax - 2 * complexPoints)
        return !(fail = true);
    const auto floatsPerTransform = 3 * length + 2 * complexPoints;
    if (floatsPerTransform > sizeMax / sizeof(float))
        return !(fail = true);
    const auto bytesPerTransform = floatsPerTransform * sizeof(float);
    const auto memoryLimit = maxMemory == 0 ? defaultWorkspaceLimit() : maxMemory;
    batchSize = std::min(maxBatch,
        std::max<std::size_t>(1, memoryLimit / bytesPerTransform));
    batchSize -= batchSize % batchMultiple;
    if(batchSize == 0)
    {
        std::cerr << "FFTW memory limit cannot hold one " << batchMultiple
                  << "-transform logical point.\n";
        return !(fail = true);
    }

    fail = !makePlan(batchSize);
    if (fail)
    {
        std::cerr << "FFTW failed to create an FFTW_ESTIMATE plan.\n";
        batchSize = 0;
        return false;
    }
    std::cout << "Chosen to do CPU transforms in " << batchSize
              << " scalar-transform batches (" << batchSize / batchMultiple
              << " logical points) using FFTW " << fftwf_version << ".\n";
    return true;
}

bool FFTWEngine::InitInterp(const double* times)
{
    interpolation = {};
    if (fail || times == nullptr || fftLength < 2)
        return false;

    const auto intervals = fftLength - 1;
    interpolation.h.resize(intervals);
    interpolation.mu.resize(intervals);
    interpolation.l.resize(intervals);
    interpolation.dt.resize(fftLength - 2);
    interpolation.indices.resize(fftLength - 2);

    auto& h = interpolation.h;
    auto& mu = interpolation.mu;
    auto& l = interpolation.l;
    mu[0] = 0.0f;
    l[0] = 1.0f;
    h[0] = static_cast<float>(times[1] - times[0]);
    for (std::size_t i = 1; i < intervals; ++i)
    {
        h[i] = static_cast<float>(times[i + 1] - times[i]);
        l[i] = static_cast<float>(2 * (times[i + 1] - times[i - 1])) -
               h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
    }

    const double step = (times[fftLength - 1] - times[0]) / intervals;
    for (std::size_t i = 1; i < fftLength - 1; ++i)
    {
        const double target = times[0] + i * step;
        const auto upper = std::upper_bound(times + 1, times + fftLength, target);
        const auto interval = static_cast<std::size_t>(upper - times - 1);
        interpolation.indices[i - 1] = std::min(interval, intervals - 1);
        interpolation.dt[i - 1] =
            static_cast<float>(target - times[interpolation.indices[i - 1]]);
    }
    interpolation.ready = true;
    return true;
}

void FFTWEngine::interpolate(float* data, std::size_t batch) const
{
    std::vector<std::size_t> transforms(batch);
    std::iota(transforms.begin(), transforms.end(), 0);
    std::vector<float> scratch(2 * fftLength * batch);
    std::for_each(std::execution::par, transforms.begin(), transforms.end(),
        [&, data](std::size_t transform)
        {
            const auto intervals = fftLength - 1;
            auto* const a = scratch.data() + 2 * fftLength * transform;
            auto* const coefficients = a + fftLength;
            for (std::size_t i = 0; i < fftLength; ++i)
                a[i] = data[i * batch + transform];

            coefficients[0] = 0.0f;
            for (std::size_t i = 1; i < intervals; ++i)
            {
                const auto alpha =
                    3.0f / interpolation.h[i] *
                        (a[i + 1] - a[i]) -
                    3.0f / interpolation.h[i - 1] *
                        (a[i] - a[i - 1]);
                coefficients[i] =
                    (alpha - interpolation.h[i - 1] * coefficients[i - 1]) /
                    interpolation.l[i];
            }

            float next = 0.0f;
            for (std::size_t reverse = 0; reverse < intervals; ++reverse)
            {
                const auto j = intervals - reverse - 1;
                const float current = coefficients[j] - interpolation.mu[j] * next;
                coefficients[j] = current;
                next = current;
            }

            for (std::size_t output = 1; output < fftLength - 1; ++output)
            {
                const auto j = interpolation.indices[output - 1];
                const float dt = interpolation.dt[output - 1];
                const float current = coefficients[j];
                const float following = j + 1 < intervals
                    ? coefficients[j + 1] : 0.0f;
                data[output * batch + transform] = a[j] +
                    ((a[j + 1] - a[j]) / interpolation.h[j] -
                     interpolation.h[j] * (following + 2 * current) / 3.0f) * dt +
                    current * dt * dt +
                    (following - current) / (3.0f * interpolation.h[j]) *
                        dt * dt * dt;
            }
        });
}

bool FFTWEngine::RunTransform(float* data, float norm, std::size_t padding)
{
    if (fail || data == nullptr || padding > batchSize)
        return false;
    const auto activeBatch = batchSize - padding;
    if (activeBatch == 0)
        return false;
    const auto originalBatch = batchSize;
    if (activeBatch != batchSize && !makePlan(activeBatch))
        return !(fail = true);

    std::copy_n(data, fftLength * activeBatch, inputBuffer.data());
    if (interpolation.ready)
        interpolate(inputBuffer.data(), activeBatch);
    fftwf_execute(plan);

    const auto outputCount = (fftLength / 2 + 1) * activeBatch;
    const auto* output = outputBuffer.data();
    if (norm == 1.0f)
        std::copy_n(output, 2 * outputCount, data);
    else
        std::transform(std::execution::par_unseq, output, output + 2 * outputCount,
                       data, [norm](float value) { return value * norm; });

    if (activeBatch != originalBatch && !makePlan(originalBatch))
        return !(fail = true);
    return true;
}
