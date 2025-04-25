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
        void* cudaBuffer{nullptr};   //pointer to shared working area for cuFFT and interpolation
        std::size_t allocBufferSize{0};
        int gpuID {0};
        bool useExtended {false};
        bool cufftReady {false};

        std::size_t reallocate(std::size_t);

        //hold interpolation's global constants
        struct InterpAccel_t {
            bool interpReady {false};
            double trueStep;
            std::size_t sCnt; //spline count

            //GPU buffers with interpolation accelerators
            float* h {nullptr};
            float* mu{nullptr};
            float* l {nullptr};

            //point locators
            std::size_t* Indices{nullptr};
            float* dt{nullptr};

            //free the data
            void free();
        } InterpAccel;

        //initialize GPU and return free memory
        std::size_t InitGPU();

    public:
        //create cuda engine bound to GPU #gpu
        cuFFTEngine(int gpu = -1) noexcept : gpuID(gpu)
        {}
        ~cuFFTEngine() noexcept;

        cuFFTEngine(const cuFFTEngine&) = delete;
        cuFFTEngine& operator=(const cuFFTEngine&) = delete;

        //move operations are allowed tho
        cuFFTEngine(cuFFTEngine&& ref)
        { std::swap(plan, ref.plan); std::swap(data, ref.data); std::swap(InterpAccel, ref.InterpAccel); }
        cuFFTEngine& operator=(cuFFTEngine&& ref)
        { std::swap(plan, ref.plan); std::swap(data, ref.data); std::swap(InterpAccel, ref.InterpAccel); return *this; }

        bool Init( std::size_t, std::size_t maxBatch, std::size_t maxMem = 0 );
        bool RunTransform( float*, float norm = 1.0f, std::size_t padding = 0 );
        bool InitInterp( const double* );

        void free();
};

