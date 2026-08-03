#include "cuda-backend.h"

#include <cuda_runtime.h>
#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

// cuda-backend.cu normally obtains this formatter from fft-util.cpp.
std::string printMemSize(std::size_t bytes)
{
    return std::to_string(bytes) + " bytes";
}

namespace {
constexpr std::size_t transformLength = 125;
constexpr std::size_t batchSize = 4096;
constexpr std::size_t complexPoints = transformLength / 2 + 1;

float signal(std::size_t time, std::size_t transform)
{
    constexpr double pi = 3.14159265358979323846;
    const auto phase = 2.0 * pi * static_cast<double>(time) / transformLength;
    switch(transform % 4)
    {
        case 0: return time == 0 ? 1.0f : 0.0f;
        case 1: return 0.25f;
        case 2: return static_cast<float>(std::sin(7.0 * phase + 0.013 * transform));
        default: return static_cast<float>(0.7 * std::sin(3.0 * phase) +
                                           0.2 * std::cos(31.0 * phase + 0.01 * transform));
    }
}
}

int main()
{
    int deviceCount{};
    const auto deviceResult = cudaGetDeviceCount(&deviceCount);
    if(deviceResult != cudaSuccess || deviceCount == 0)
    {
        std::cout << "No CUDA device available; skipping.\n";
        return 77;
    }

    std::vector<float> input(2 * complexPoints * batchSize, 0.0f);
    std::vector<float> referenceInput(transformLength * batchSize);
    for(std::size_t time = 0; time < transformLength; ++time)
        for(std::size_t transform = 0; transform < batchSize; ++transform)
        {
            const auto value = signal(time, transform);
            input[time * batchSize + transform] = value;
            referenceInput[time * batchSize + transform] = value;
        }

    std::vector<std::complex<float>> reference(complexPoints * batchSize);
    int length = static_cast<int>(transformLength);
    int inputEmbed = length;
    int outputEmbed = static_cast<int>(complexPoints);
    auto referencePlan = fftwf_plan_many_dft_r2c(
        1, &length, static_cast<int>(batchSize), referenceInput.data(), &inputEmbed,
        static_cast<int>(batchSize), 1,
        reinterpret_cast<fftwf_complex*>(reference.data()), &outputEmbed,
        static_cast<int>(batchSize), 1, FFTW_ESTIMATE);
    if(referencePlan == nullptr)
    {
        std::cerr << "FFTW failed to create the reference plan.\n";
        return 1;
    }
    fftwf_execute(referencePlan);
    fftwf_destroy_plan(referencePlan);

    cuFFTEngine engine;
    constexpr std::size_t memoryBudget = 64ULL * 1024 * 1024;
    if(!engine.Init(transformLength, batchSize, memoryBudget))
    {
        std::cerr << "Failed to initialize cuFFT engine.\n";
        return 1;
    }
    if(engine.expectedBatch() != batchSize)
    {
        std::cerr << "Expected batch " << batchSize << ", got "
                  << engine.expectedBatch() << ".\n";
        return 1;
    }
    if(!engine.RunTransform(input.data()))
    {
        std::cerr << "In-place cuFFT execution failed.\n";
        return 1;
    }

    const auto* actual = reinterpret_cast<const std::complex<float>*>(input.data());
    double squaredError{};
    double squaredReference{};
    double maximumError{};
    for(std::size_t index = 0; index < reference.size(); ++index)
    {
        if(!std::isfinite(actual[index].real()) || !std::isfinite(actual[index].imag()))
        {
            std::cerr << "cuFFT produced a non-finite result at " << index << ".\n";
            return 1;
        }
        const auto error = static_cast<double>(std::abs(actual[index] - reference[index]));
        const auto magnitude = static_cast<double>(std::abs(reference[index]));
        maximumError = std::max(maximumError, error);
        squaredError += error * error;
        squaredReference += magnitude * magnitude;
    }

    const auto normalizedRms = std::sqrt(squaredError / squaredReference);
    std::cout << "maximum error: " << maximumError
              << ", normalized RMS error: " << normalizedRms << '\n';
    return normalizedRms <= 1.0e-5 ? 0 : 1;
}
