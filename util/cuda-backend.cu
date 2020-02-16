#include"cuda-backend.h"
#include<limits>
#include<array>
#include<algorithm>

using namespace std::string_literals;
constexpr std::array<std::size_t, 31> AllowedFactors { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127 };
inline bool is4GCompatible( std::size_t size )
{
    while(size != 1)
    {
        auto old = size;
        for (const auto& x: AllowedFactors)
            if( size % x == 0 )
            {
                size /= x;
                break;
            }

        if(old == size)
            return false;
    }
    return true;
}

int EstimateBatch( cufftHandle plan, int len, std::size_t maxMem, std::size_t maxBatch = std::numeric_limits<std::size_t>::max() )
{
    int cPoints = len/2 + 1;
    int batch_size = std::min<int>( maxMem / (9 * sizeof(float) * 2 * cPoints), maxBatch );
    int arrSize { batch_size * cPoints * 2 };
    int outArrSize { batch_size * cPoints }; //rounds down
    std::size_t estimate{};
    auto estimationError = cufftGetSizeMany(
                plan, 1, &len, 
                &arrSize, 2 * cPoints - 2, 1,
                &outArrSize, 1, cPoints,
                CUFFT_R2C, batch_size, &estimate);
    if( estimationError != CUFFT_SUCCESS )
        return 0;

    if( batch_size == maxBatch && maxMem <= (estimate + sizeof(float) * batch_size * (len + 2)) )
        return batch_size;

    //otherwise try to find the size just large enough starting from the linear extrapolation
    batch_size = std::min<int>( maxMem / (estimate/batch_size + sizeof(float) * (len + 2)), maxBatch );
    bool isFitting = true; std::size_t cnt {0};
    while(batch_size > 0 && batch_size <= maxBatch)
    {
        arrSize = batch_size * cPoints * 2;
        outArrSize = batch_size * cPoints; //rounds down
        estimationError = cufftGetSizeMany(
                plan, 1, &len, 
                &arrSize, 2 * cPoints - 2, 1,
                &outArrSize, 1, cPoints,
                CUFFT_R2C, batch_size, &estimate);

        if( estimationError != CUFFT_SUCCESS && estimationError != CUFFT_ALLOC_FAILED )
            return 0; //expect those two since might go over allowed GPU memory

        const auto wasFitting = isFitting;
        isFitting = maxMem >= (estimate + sizeof(float) * batch_size * (len + 2));
        if(isFitting != wasFitting && cnt != 0)
            return isFitting? batch_size : batch_size - 1;

        cnt++;
        if(isFitting)
            batch_size++;
        else
            batch_size--;
    }
    return batch_size==0? batch_size : maxBatch;
}

int EstimateBatch64( cufftHandle plan, long long int len, std::size_t maxMem, std::size_t maxBatch = std::numeric_limits<std::size_t>::max() )
{
    long long int cPoints = len/2 + 1;
    long long int batch_size = std::min<int>( maxMem / (9 * sizeof(float) * 2 * cPoints), maxBatch );
    long long int arrSize { batch_size * cPoints * 2 };
    long long int outArrSize { batch_size * cPoints }; //rounds down
    std::size_t estimate{};
    auto estimationError = cufftGetSizeMany64(
                plan, 1, &len, 
                &arrSize, 2 * cPoints - 2, 1,
                &outArrSize, 1, cPoints,
                CUFFT_R2C, batch_size, &estimate);
    if( estimationError != CUFFT_SUCCESS )
        return 0;

    if( batch_size == maxBatch && maxMem <= (estimate + sizeof(float) * batch_size * (len + 2)) )
        return batch_size;

    //otherwise try to find the size just large enough starting from the linear extrapolation
    batch_size = std::min<long long int>( maxMem / (estimate/batch_size + sizeof(float) * (len + 2)), maxBatch );
    bool isFitting = true; std::size_t cnt {0};
    while(batch_size > 0 && batch_size <= maxBatch)
    {
        arrSize = batch_size * cPoints * 2;
        outArrSize = batch_size * cPoints; //rounds down
        estimationError = cufftGetSizeMany64(
                plan, 1, &len, 
                &arrSize, 2 * cPoints - 2, 1,
                &outArrSize, 1, cPoints,
                CUFFT_R2C, batch_size, &estimate);

        if( estimationError != CUFFT_SUCCESS || estimationError != CUFFT_ALLOC_FAILED )
            return 0; //expect those two since might go over allowed GPU memory

        const auto wasFitting = isFitting;
        isFitting = maxMem >= (estimate + sizeof(float) * batch_size * (len + 2));
        if(isFitting != wasFitting && cnt!=0)
            return isFitting? batch_size : batch_size - 1;

        cnt++;
        if(isFitting)
            batch_size++;
        batch_size--;
    }
    return batch_size==0? batch_size : maxBatch;
}

extern std::string printMemSize(std::size_t);

std::string cuFFTEngine::Init( std::size_t t_len, std::size_t maxBatch, std::size_t maxMem )
{
    fftLength = t_len;
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
        result += "\nWarning: Device doesn't have memory requested: " + printMemSize( maxMem ) + ", defaulting to total GPU memory.";
        maxMem = props.totalGlobalMem;
    }
    if(maxMem == 0) maxMem = 0.95 * props.totalGlobalMem; //defaulting to 95% of available VRAM

    //conversion to acceptable type for fft
    if( fftLength  > std::numeric_limits<long long int>::max() )
    {
        fail = true;
        return result + "\nTime series lenth or memory size values received overflow supported range!";
    }

    //estimate how much size bundle would take
    //first get initial approximation by taking guarantee from documentation that maximum workspace area is 8 times the data
    //note: need at least 2x and at most 9x the data size for FFT transform(depending on length of the fft set)
    if (maxBatch == 0) maxBatch = std::numeric_limits<long long int>::max();
    
    if(fftLength + 2 > std::numeric_limits<int>::max() || //either index for internal array is OOB, or
       std::min<std::size_t>( maxBatch * (fftLength + 2) * sizeof(float), maxMem ) > sizeof(float) * std::numeric_limits<int>::max() ) //max memory usage is > 8GB
    {
        if(fftLength % 2 != 0)
        {
            result += "\nLimiting maximum batch size to 2G of data because of input array length being not even!";
            maxBatch = 2 * 1024 * 1024 / (fftLength * sizeof(float));
        }
        else if(!is4GCompatible(fftLength))
        {
            result += "\nLimiting maximum batch size to 4G of data because times series length is divisible by primes larger than 127!";
            maxBatch = 4 * 1024 * 1024 / (fftLength * sizeof(float));
        }

        batchSize = EstimateBatch64(plan, fftLength, maxMem, maxBatch);
    }
    else
        batchSize = EstimateBatch(plan, fftLength, maxMem, maxBatch);

    if(batchSize == 0)
    {
        fail = true;
        return result + "\nUnhandled error happending during batch size estimation!";
    }

    return result + "\nChosen to do transforms in " + std::to_string(batchSize) + " point batches (" + printMemSize(batchSize * (fftLength + 2) * sizeof(float)) + " each).";
}

bool cuFFTEngine::RunTransform( float* input, std::size_t padding)
{
    return false;
}

