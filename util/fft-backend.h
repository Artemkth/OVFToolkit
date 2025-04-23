#pragma once
#include<string>

template<typename T>
class FFTEngine
{
    protected:
        std::size_t batchSize {0};  //number of concurrent transforms in a batch
        std::size_t fftLength {0};  //size of single FFT source dataset
        bool fail {false};          //fail flag

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
        //len tells the length of fft sets
        //maxBatch is the size of the original dataset
        //suggests maximum memory to be used 
        virtual std::string Init( std::size_t t_len, std::size_t dataSize, std::size_t maxMem ) = 0;

        //function to run a transform with given data and padding for it,
        //will pad out the data in gpu memory with reinitializing for new array
        //host array data is expected to have batchSize 2 * (fftLength/2 + 1) * ( batchSize - padding ) elements 
        //norm is an fp number which all data will be divided by after transform
        virtual bool RunTransform( T* data, T norm, std::size_t padding ) = 0;

        //initialize interpolation engine to remove jitter from timed data
        //array times has cnt time-stamps for the sampling points
        //cnt is expected to be smaller or same as fftLength for data alignment
        //method sets up accelerators for calculating fftLength points in parallelized fashion
        //using cubic spline interpolation
        virtual bool InitInterp( const double* times, std::size_t cnt ) = 0;
};

