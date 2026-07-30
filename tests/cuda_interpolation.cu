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

std::string printMemSize(std::size_t bytes)
{
    return std::to_string(bytes) + " bytes";
}

namespace {
constexpr std::size_t transformLength = 1024;
constexpr std::size_t batchSize = 128;
constexpr std::size_t complexPoints = transformLength / 2 + 1;
constexpr double pi = 3.14159265358979323846;

float sinusoid(double time, std::size_t transform)
{
    const auto frequency = 1 + transform % 24;
    const auto phase = 2.0 * pi * static_cast<double>(transform) / batchSize;
    return static_cast<float>(std::sin(2.0 * pi * frequency * time /
                                       (transformLength - 1) + phase));
}
}

int main()
{
    int deviceCount{};
    if(cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0)
    {
        std::cout << "No CUDA device available; skipping.\n";
        return 77;
    }

    std::vector<double> jitteredTimes(transformLength);
    for(std::size_t time = 0; time < transformLength; ++time)
    {
        const bool endpoint = time == 0 || time + 1 == transformLength;
        jitteredTimes[time] = static_cast<double>(time) +
            (endpoint ? 0.0 : 0.22 * std::sin(1.61803398875 * time));
    }

    std::vector<float> input(2 * complexPoints * batchSize, 0.0f);
    std::vector<float> uniformInput(transformLength * batchSize);
    for(std::size_t time = 0; time < transformLength; ++time)
        for(std::size_t transform = 0; transform < batchSize; ++transform)
        {
            input[time * batchSize + transform] =
                sinusoid(jitteredTimes[time], transform);
            uniformInput[time * batchSize + transform] =
                sinusoid(static_cast<double>(time), transform);
        }

    std::vector<std::complex<float>> reference(complexPoints * batchSize);
    int length = static_cast<int>(transformLength);
    int inputEmbed = length;
    int outputEmbed = static_cast<int>(complexPoints);
    auto referencePlan = fftwf_plan_many_dft_r2c(
        1, &length, static_cast<int>(batchSize), uniformInput.data(), &inputEmbed,
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
    if(!engine.Init(transformLength, batchSize, memoryBudget) ||
       engine.expectedBatch() != batchSize)
    {
        std::cerr << "Failed to initialize the requested cuFFT batch.\n";
        return 1;
    }
    if(!engine.InitInterp(jitteredTimes.data()))
    {
        std::cerr << "Failed to initialize GPU interpolation.\n";
        return 1;
    }
    if(!engine.RunTransform(input.data()))
    {
        std::cerr << "Interpolated cuFFT execution failed.\n";
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
            std::cerr << "Interpolation produced a non-finite result at " << index << ".\n";
            return 1;
        }
        const auto error = static_cast<double>(std::abs(actual[index] - reference[index]));
        const auto magnitude = static_cast<double>(std::abs(reference[index]));
        maximumError = std::max(maximumError, error);
        squaredError += error * error;
        squaredReference += magnitude * magnitude;
    }

    const auto normalizedRms = std::sqrt(squaredError / squaredReference);
    std::cout << "maximum spectral error: " << maximumError
              << ", normalized RMS error: " << normalizedRms << '\n';
    return normalizedRms <= 2.0e-3 ? 0 : 1;
}
