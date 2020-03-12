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

        //hold interpolation's global constants
        struct {
            bool Ready {false};
            double trueStep;
            std::size_t sCnt; //spline count

            //GPU buffers with interpolation accelerators
            double* h {nullptr};
            double* mu{nullptr};
            double* l {nullptr};

            //point locators
            std::size_t* Indices{nullptr};
            double* dt{nullptr};

            //per-thread buffer heap
            std::size_t blockBufferCnt {0};
            double* BlockBuffer{nullptr};

            //free the data
            void free()
            {
                cudaFree(h);
                cudaFree(Indices);
                cudaFree(BlockBuffer);

                Ready = false;
            }
        } InterpAccel;

    public:
        //create cuda engine bound to GPU #gpu
        cuFFTEngine(int gpu = -1) noexcept : gpuID(gpu)
        { if(cufftCreate(&plan) != CUFFT_SUCCESS) fail = true; }
        //TODO: move destructor into .cu
        ~cuFFTEngine() noexcept
        {
            cufftDestroy(plan); cudaFree(data);
            InterpAccel.free();
        }

        cuFFTEngine(const cuFFTEngine&) = delete;
        cuFFTEngine& operator=(const cuFFTEngine&) = delete;

        //move operations are allowed tho
        cuFFTEngine(cuFFTEngine&& ref)
        { std::swap(plan, ref.plan); std::swap(data, ref.data); std::swap(InterpAccel, ref.InterpAccel); }
        cuFFTEngine& operator=(cuFFTEngine&& ref)
        { std::swap(plan, ref.plan); std::swap(data, ref.data); std::swap(InterpAccel, ref.InterpAccel); return *this; }

        std::string Init( std::size_t, std::size_t maxBatch = 0, std::size_t maxMem = 0 );
        bool RunTransform( float*, float norm = 1.0f, std::size_t padding = 0 );
        bool InitInterp( const double*, std::size_t );
};

