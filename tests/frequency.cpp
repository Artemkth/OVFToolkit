#include "fft-frequency.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>

namespace
{
bool close(double lhs, double rhs)
{
    return std::abs(lhs - rhs) <= 1.0e-14 * std::max(std::abs(lhs), std::abs(rhs));
}
}

int main()
{
    constexpr std::size_t evenSamples = 1000;
    constexpr double interval = 2.5e-12;
    constexpr double increment = FFTUtil::frequencyIncrement(evenSamples, interval);

    if(!close(increment, 1.0 / (evenSamples * interval)))
    {
        std::cerr << "Incorrect DFT frequency increment.\n";
        return 1;
    }

    const auto lastEvenBin = FFTUtil::binFrequency(
        evenSamples / 2, evenSamples, interval);
    if(!close(lastEvenBin, 1.0 / (2.0 * interval)))
    {
        std::cerr << "The final even-length R2C bin is not the Nyquist frequency.\n";
        return 1;
    }

    constexpr std::size_t oddSamples = 999;
    const auto lastOddBin = FFTUtil::binFrequency(
        oddSamples / 2, oddSamples, interval);
    if(!(lastOddBin < 1.0 / (2.0 * interval)))
    {
        std::cerr << "The final odd-length R2C bin must be below Nyquist.\n";
        return 1;
    }

    constexpr double elapsed = (evenSamples - 1) * interval;
    if(!close(increment,
              static_cast<double>(evenSamples - 1) / (evenSamples * elapsed)))
    {
        std::cerr << "Elapsed-time conversion introduced an off-by-one error.\n";
        return 1;
    }

    return 0;
}
