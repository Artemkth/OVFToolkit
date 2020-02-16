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
        virtual std::string Init( std::size_t len, std::size_t maxMem = 0 ) = 0;

        //function to run a transform with given data and padding for it,
        //will pad out the data in gpu memory with reinitializing for new array
        //host array data is expected to have batchSize (fftLength + 2) * batchSize elements 
        virtual bool RunTransform( T* data, std::size_t padding = 0 ) = 0;
};

