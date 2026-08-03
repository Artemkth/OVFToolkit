#include "cuda-backend.h"
#include "fftw-backend.h"

#include <cuda_runtime.h>

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

namespace
{
constexpr std::size_t transformLength = 231;
constexpr std::size_t batchSize = 257;
constexpr std::size_t complexPoints = transformLength / 2 + 1;
constexpr double pi = 3.14159265358979323846;

float signal(std::size_t time, std::size_t transform)
{
    const auto phase = 2.0 * pi * time / transformLength;
    return static_cast<float>(
        0.55 * std::sin((3 + transform % 13) * phase + 0.007 * transform) +
        0.31 * std::cos((17 + transform % 11) * phase) +
        0.05 * std::sin(53 * phase));
}
}

int main()
{
    int deviceCount{};
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0)
    {
        std::cout << "No CUDA device available; skipping.\n";
        return 77;
    }

    std::vector<float> cudaData(2 * complexPoints * batchSize, 0.0f);
    std::vector<float> fftwData(cudaData.size(), 0.0f);
    for (std::size_t time = 0; time < transformLength; ++time)
        for (std::size_t transform = 0; transform < batchSize; ++transform)
            cudaData[time * batchSize + transform] =
                fftwData[time * batchSize + transform] = signal(time, transform);

    constexpr std::size_t memoryBudget = 64ULL * 1024 * 1024;
    cuFFTEngine cudaEngine;
    FFTWEngine fftwEngine;
    if (!cudaEngine.Init(transformLength, batchSize, memoryBudget) ||
        !fftwEngine.Init(transformLength, batchSize, memoryBudget) ||
        cudaEngine.expectedBatch() != batchSize ||
        fftwEngine.expectedBatch() != batchSize)
    {
        std::cerr << "Could not initialize matching cross-engine batches.\n";
        return 1;
    }
    if (!cudaEngine.RunTransform(cudaData.data()) ||
        !fftwEngine.RunTransform(fftwData.data()))
    {
        std::cerr << "A cross-engine transform failed.\n";
        return 1;
    }

    const auto* cudaOutput =
        reinterpret_cast<const std::complex<float>*>(cudaData.data());
    const auto* fftwOutput =
        reinterpret_cast<const std::complex<float>*>(fftwData.data());
    const auto outputCount = complexPoints * batchSize;
    double squaredError{};
    double squaredReference{};
    double maximumError{};
    for (std::size_t i = 0; i < outputCount; ++i)
    {
        if (!std::isfinite(cudaOutput[i].real()) ||
            !std::isfinite(cudaOutput[i].imag()) ||
            !std::isfinite(fftwOutput[i].real()) ||
            !std::isfinite(fftwOutput[i].imag()))
        {
            std::cerr << "An engine produced a non-finite value at " << i << ".\n";
            return 1;
        }
        const auto error = static_cast<double>(
            std::abs(cudaOutput[i] - fftwOutput[i]));
        maximumError = std::max(maximumError, error);
        squaredError += error * error;
        squaredReference += std::norm(fftwOutput[i]);
    }

    const auto normalizedRms = std::sqrt(squaredError / squaredReference);
    std::cout << "cuFFT/FFTW maximum error: " << maximumError
              << ", normalized RMS error: " << normalizedRms << '\n';
    return normalizedRms <= 1.0e-5 ? 0 : 1;
}
