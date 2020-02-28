#pragma once
#include<string>

template<typename T>
class FFTEngine
{
    protected:
        std::size_t batchSize {0};
        std::size_t fftLength {0};
        bool fail {false};

    public:
        //should always have a destructor to free the resources
        virtual ~FFTEngine() = default;

        //return expected batch sizes after initialization was done
        std::size_t expectedBatch() const noexcept
        { return batchSize; }
        std::size_t expectedLength() const noexcept
        { return fftLength; }
        bool isReady() const noexcept
        { return !fail && batchSize != 0; }

        //interfaces
        //initialize the engine and return some information about how it went
        //len tells the length of fft sets, and maxBatch tells maximum memory which can be allocated
        virtual std::string Init( std::size_t t_len, std::size_t maxBatch, std::size_t maxMem ) = 0;

        //function to run a transform with given data and padding for it,
        //will pad out the data in gpu memory with reinitializing for new array
        //host array data is expected to have batchSize 2 * (fftLength/2 + 1) * ( batchSize - padding ) elements 
        //norm is an fp number which all data will be divided by after transform
        virtual bool RunTransform( T* data, T norm, std::size_t padding ) = 0;

        //initialize interpolation engine to remove jitter from timed data
        //cnt time points is received through times array,
        //which is later interpolated into fftLength of interpolated values on target device
        //cnt is expected to be smaller or same as fftLength for data alignment
        virtual bool InitInterp( const double* times, std::size_t cnt ) = 0;
};

