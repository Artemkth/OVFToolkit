#include "fftw-backend.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
constexpr std::size_t transformLength = 63;
constexpr std::size_t batchSize = 17;
constexpr std::size_t complexPoints = transformLength / 2 + 1;

float signal(double time, std::size_t transform)
{
    const auto phase = 2.0 * std::numbers::pi * time / transformLength;
    return static_cast<float>(
        0.7 * std::sin((3 + transform % 5) * phase + 0.03 * transform) +
        0.2 * std::cos((11 + transform % 7) * phase));
}

std::vector<std::complex<float>> directTransform(const std::vector<float>& input,
                                                 std::size_t batch)
{
    std::vector<std::complex<float>> result(complexPoints * batch);
    for (std::size_t frequency = 0; frequency < complexPoints; ++frequency)
        for (std::size_t transform = 0; transform < batch; ++transform)
            for (std::size_t time = 0; time < transformLength; ++time)
            {
                const auto angle = -2.0 * std::numbers::pi * frequency * time /
                                   transformLength;
                result[frequency * batch + transform] +=
                    input[time * batch + transform] *
                    std::complex<float>(std::cos(angle), std::sin(angle));
            }
    return result;
}

bool closeToReference(const float* actualData,
                      const std::vector<std::complex<float>>& reference,
                      double tolerance = 2.0e-5)
{
    const auto* actual = reinterpret_cast<const std::complex<float>*>(actualData);
    double squaredError{};
    double squaredReference{};
    for (std::size_t i = 0; i < reference.size(); ++i)
    {
        if (!std::isfinite(actual[i].real()) || !std::isfinite(actual[i].imag()))
            return false;
        squaredError += std::norm(actual[i] - reference[i]);
        squaredReference += std::norm(reference[i]);
    }
    return std::sqrt(squaredError / squaredReference) < tolerance;
}
}

int main()
{
    {
        FFTWEngine quantized;
        constexpr std::size_t pointDimension = 3;
        if(!quantized.Init(transformLength, batchSize,
                           16ULL * 1024 * 1024, pointDimension) ||
           quantized.expectedBatch() != batchSize - batchSize % pointDimension)
        {
            std::cerr << "FFTW did not quantize its batch to complete points.\n";
            return 1;
        }
    }

    FFTWEngine engine;
    if (!engine.Init(transformLength, batchSize, 16ULL * 1024 * 1024) ||
        engine.expectedBatch() != batchSize)
    {
        std::cerr << "Failed to initialize the expected FFTW batch.\n";
        return 1;
    }

    std::vector<float> input(2 * complexPoints * batchSize, 0.0f);
    std::vector<float> referenceInput(transformLength * batchSize);
    for (std::size_t time = 0; time < transformLength; ++time)
        for (std::size_t transform = 0; transform < batchSize; ++transform)
            input[time * batchSize + transform] =
                referenceInput[time * batchSize + transform] = signal(time, transform);

    const auto reference = directTransform(referenceInput, batchSize);
    if (!engine.RunTransform(input.data()) || !closeToReference(input.data(), reference))
    {
        std::cerr << "FFTW backend disagrees with the direct transform.\n";
        return 1;
    }

    constexpr std::size_t padding = 3;
    constexpr std::size_t shortBatch = batchSize - padding;
    std::vector<float> shortInput(2 * complexPoints * batchSize, 0.0f);
    std::vector<float> shortReferenceInput(transformLength * shortBatch);
    for (std::size_t time = 0; time < transformLength; ++time)
        for (std::size_t transform = 0; transform < shortBatch; ++transform)
            shortInput[time * shortBatch + transform] =
                shortReferenceInput[time * shortBatch + transform] = signal(time, transform);
    const auto shortReference = directTransform(shortReferenceInput, shortBatch);
    if (!engine.RunTransform(shortInput.data(), 1.0f, padding) ||
        !closeToReference(shortInput.data(), shortReference) || !engine.isReady())
    {
        std::cerr << "FFTW backend failed its partial-batch layout test.\n";
        return 1;
    }

    std::vector<double> times(transformLength);
    for (std::size_t time = 0; time < transformLength; ++time)
        times[time] = static_cast<double>(time) +
            ((time == 0 || time + 1 == transformLength)
                 ? 0.0 : 0.08 * std::sin(1.7 * time));
    std::fill(input.begin(), input.end(), 0.0f);
    for (std::size_t time = 0; time < transformLength; ++time)
        for (std::size_t transform = 0; transform < batchSize; ++transform)
            input[time * batchSize + transform] = signal(times[time], transform);

    if (!engine.InitInterp(times.data()) || !engine.RunTransform(input.data()) ||
        !closeToReference(input.data(), reference, 2.0e-3))
    {
        std::cerr << "FFTW interpolation did not recover the uniform signal.\n";
        return 1;
    }
    return 0;
}
