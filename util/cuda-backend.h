#pragma once
#include<fft-backend.h>
#include<cuda_runtime.h>
#include<cufft.h>

class cuFFTEngine: public FFTEngine<float>
{
    private:
        //initialized later
        cufftHandle plan{};          //handle for plan
        cufftComplex* data{nullptr}; //and pointer to data allocated on GPU
        int gpuID {0};
        bool useExtended {false};

        std::size_t reallocate(std::size_t);

    public:
        //create cuda engine bound to GPU #gpu
        cuFFTEngine(int gpu = -1) noexcept : gpuID(gpu)
        { if(cufftCreate(&plan) != CUFFT_SUCCESS) fail = true; }
        ~cuFFTEngine() noexcept
        { cufftDestroy(plan); cudaFree(data); }

        cuFFTEngine(const cuFFTEngine&) = delete;
        cuFFTEngine& operator=(const cuFFTEngine&) = delete;

        //move operations are allowed tho
        cuFFTEngine(cuFFTEngine&& ref)
        { std::swap(plan, ref.plan); std::swap(data, ref.data); }
        cuFFTEngine& operator=(cuFFTEngine&& ref)
        { std::swap(plan, ref.plan); std::swap(data, ref.data); return *this; }

        std::string Init( std::size_t, std::size_t maxBatch = 0, std::size_t maxMem = 0 );
        bool RunTransform( float*, std::size_t padding = 0 );
};

