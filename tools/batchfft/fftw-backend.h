#pragma once

#include "fft-backend.h"

#include <fftw3.h>

#include <cstddef>
#include <vector>

class FFTWEngine final : public FFTEngine<float>
{
    fftwf_plan plan{nullptr};
    std::vector<float> inputBuffer;
    std::vector<float> outputBuffer;

    struct InterpolationData
    {
        std::vector<float> h;
        std::vector<float> mu;
        std::vector<float> l;
        std::vector<float> dt;
        std::vector<std::size_t> indices;
        bool ready{false};
    } interpolation;

    bool makePlan(std::size_t batch);
    void interpolate(float* data, std::size_t batch) const;

public:
    FFTWEngine() noexcept;
    ~FFTWEngine() noexcept override;

    FFTWEngine(const FFTWEngine&) = delete;
    FFTWEngine& operator=(const FFTWEngine&) = delete;

    bool Init(std::size_t length, std::size_t maxBatch,
              std::size_t maxMemory = 0,
              std::size_t batchMultiple = 1) override;
    bool RunTransform(float* data, float norm = 1.0f,
                      std::size_t padding = 0) override;
    bool InitInterp(const double* times) override;
};
