#include"cuda-backend.h"

using namespace std::string_literals;

extern std::string printMemSize(std::size_t);

std::string cuFFTEngine::Init( std::size_t len, std::size_t maxMem )
{
    fail = false; //reset just in case

    int devCount; cudaGetDeviceCount(&devCount);
    if( devCount == 0 )
    {
        fail = true;
        return "Found no CUDA-enabled devices!"s ;
    }
    if( gpuID >= devCount )
    {
        fail = true;
        return "The GPU id"s + std::to_string(gpuID) + " received is invalid(larger than number of available GPUs).";
    }
    int curGPU; cudaGetDevice(&curGPU);
    if( gpuID != -1 )
    {
        //if gpu is not set already set it to the target
        if(curGPU != gpuID)
        {
            cudaDeviceReset(); data = nullptr;
            fail = cudaSetDevice(gpuID) != cudaSuccess;
            fail = cufftCreate(&plan) != CUFFT_SUCCESS;
        }

        if(fail)
            return "Failed to get GPU id#"s + std::to_string(gpuID) + ".";
    }

    //get device characteristics
    std::string result{};
    cudaDeviceProp props{};
    cudaGetDeviceProperties(&props, curGPU);
    result += "Using GPU #"s + std::to_string(curGPU) + " \"" + props.name + "\" "
           +  std::to_string( props.multiProcessorCount ) + "SM" + '@' + std::to_string( props.clockRate / 1000 ) + "MHz, "
           +  printMemSize( props.totalGlobalMem ) + "s of global memory on device." ;
    if(maxMem > props.totalGlobalMem)
    {
        result += "\n Warning: Device doesn't have memory requested: " + printMemSize( maxMem ) + ", defaulting to total GPU memory.";
        maxMem = props.totalGlobalMem;
    }
    if(maxMem == 0) maxMem = 0.95 * props.totalGlobalMem; //defaulting to 95% of available VRAM

    //estimate how much size bundle would take
    std::size_t estimate {};

    return result;
}

bool cuFFTEngine::RunTransform( float* input, std::size_t padding)
{
    return false;
}

