#pragma once

#include <cstddef>

namespace FFTUtil
{
    /**
     * @brief Return the frequency separation between adjacent DFT bins.
     *
     * @param sampleCount Number of uniformly spaced time-domain samples.
     * @param sampleInterval Time between adjacent samples, in seconds.
     * @return Frequency-bin separation in hertz.
     *
     * @pre sampleCount is positive and sampleInterval is positive.
     */
    [[nodiscard]] constexpr double frequencyIncrement(
        std::size_t sampleCount, double sampleInterval) noexcept
    {
        return 1.0 / (static_cast<double>(sampleCount) * sampleInterval);
    }

    /**
     * @brief Return the frequency represented by a DFT bin.
     */
    [[nodiscard]] constexpr double binFrequency(
        std::size_t bin, std::size_t sampleCount, double sampleInterval) noexcept
    {
        return static_cast<double>(bin) *
               frequencyIncrement(sampleCount, sampleInterval);
    }
}
