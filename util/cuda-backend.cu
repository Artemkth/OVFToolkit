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
                &arrSize, batch_size, 1,
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
                &arrSize, batch_size, 1,
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
                &arrSize, batch_size, 1,
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
                &arrSize, batch_size, 1,
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
    //greedy greedy host wants to have ALL the memory :D
    if (maxBatch == 0) maxBatch = std::numeric_limits<long long int>::max();
    
    if( fftLength + 2 > std::numeric_limits<int>::max() || //either index for internal array is OOB, or
        (maxMem > 2l * std::numeric_limits<int>::max() * sizeof(float) && //only when available gpu space is higher than 16GB, otherwise there is not enough space to fit both data and fft workspace
        std::min<std::size_t>( maxBatch * (fftLength + 2) * sizeof(float), maxMem ) > sizeof(float) * std::numeric_limits<int>::max() &&
        fftLength % 2 != 0 && is4GCompatible(fftLength)) ) //max memory usage is > 8GB
    {
        useExtended = true;//to indicate if *64 methods are called later, in a case where GPU supports craploads of data
        
        if( fftLength %2 != 0 )
        {
            result += "\nWarning: Limiting the batch size to 2G of data becuase number of time steps is odd!";
            maxBatch = 2l * 1024 * 1024 * 1024 / sizeof(float);
        }
        if( !is4GCompatible(fftLength) )
        {
            result += "\nWarning: Limiting max batch size to 1G";
            maxBatch = 1l * 1024 * 1024 * 1024 / sizeof(float);
        }

        batchSize = EstimateBatch64(plan, fftLength, maxMem, maxBatch);
    }
    else
    {
        useExtended = false;
        if( maxBatch > std::numeric_limits<int>::max() && maxMem > 2l * std::numeric_limits<int>::max() * sizeof(float) )
            result += "\nWarning: Batch size is being limited by transform lenth being odd, or having prime factors higher than 127!";
        batchSize = EstimateBatch(plan, fftLength, maxMem, std::min<std::size_t>(maxBatch, std::numeric_limits<int>::max()));
    }

    if(batchSize == 0)
    {
        fail = true;
        return result + "\nUnhandled error happending during batch size estimation!";
    }

    auto workSize = reallocate(batchSize);
    if( workSize == 0 )
    {
        fail = true;
        return result + "\nFailed to allcate work assets in VRAM, tried to go with " + printMemSize( 2 * batchSize * (fftLength/2 + 1) * sizeof(float)) + "batches.";
    }

    return result + "\nChosen to do transforms in " + std::to_string(batchSize) + " point batches (" + 
            printMemSize(2 * batchSize * (fftLength/2 + 1) * sizeof(float)) + " each, " + printMemSize(2 * batchSize * (fftLength/2 + 1) * sizeof(float) + workSize) +
            " together with work area in the gpu).";
}

std::size_t cuFFTEngine::reallocate(std::size_t newBatch)
{
    //do not bother with bad inputs
    if(newBatch > batchSize || fftLength == 0 || fail)
        return 0;

    cudaFree( data ); data = nullptr;

    std::size_t workSize {};
    cufftResult allocationError{};
    if(useExtended)
    {
        long long int len = fftLength;
        long long int cPoints = fftLength/2 + 1;
        long long int arrSize = newBatch * cPoints * 2;
        long long int outArrSize = newBatch * cPoints ;

        allocationError = cufftMakePlanMany64(
                plan, 1, &len, 
                &arrSize, newBatch, 1,
                &outArrSize, 1, cPoints,
                CUFFT_R2C, newBatch, &workSize);
    }
    else
    {
        int len = fftLength;
        int cPoints = fftLength/2 + 1;
        int arrSize = newBatch * cPoints * 2;
        int outArrSize = newBatch * cPoints;

        allocationError = cufftMakePlanMany(
                plan, 1, &len, 
                &arrSize, newBatch, 1,
                &outArrSize, 1, cPoints,
                CUFFT_R2C, newBatch, &workSize);
    }
    if(allocationError != CUFFT_SUCCESS)
        return 0;

    //and allocate work data area
    if(cudaMalloc((void**)&data, sizeof(cufftComplex) * (fftLength/2 + 1) * batchSize) != cudaSuccess)
    {
        cufftDestroy(plan); cufftCreate(&plan);
        return 0;
    } 

    return workSize;
}

__global__ void normalize( cufftReal* data, std::size_t nSize, float norm )
{
    //CUDA kernel for normalizing data, should be zupa fast
    std::size_t coord = (blockIdx.y * blockDim.x + blockIdx.x) * blockDim.x * blockDim.y + //size of a block in values 
                        blockDim.x * threadIdx.y + threadIdx.x;                            //and position of thread within block

    if( coord < nSize ) //if thread is within block, proceed normalizing the value
        *(data + coord) *= norm;
}

bool cuFFTEngine::RunTransform( float* input, float norm, std::size_t padding)
{
    if( fail || input == nullptr || padding >= batchSize )
        return false;

    std::size_t nBatchSize { batchSize - padding };
    bool result {true};

    if( padding != 0 )
        result = reallocate(nBatchSize);
    //move data into array
    result = result && (cudaMemcpy( (void*)data, (void*)input, nBatchSize * (fftLength/2 + 1) * sizeof(cufftComplex), cudaMemcpyHostToDevice ) == cudaSuccess);
    result = result && (cufftExecR2C( plan, (cufftReal*)data, data ) == CUFFT_SUCCESS);
    if( norm != 1.0f && result )
    {
        constexpr const std::size_t maxPerBDim { 65535 };
        dim3 threadPerBlock( 16, 16 );
        const std::size_t bCount { (nBatchSize + 255) / 256 };//block count is rounded up
        if( bCount <= maxPerBDim ) // max ~64MB
        {
            int bLineSize = bCount;
            normalize<<<bLineSize, threadPerBlock>>>((cufftReal*)data, nBatchSize, norm);
        }
        else if( bCount <= maxPerBDim * maxPerBDim ) // max ~4TB
        {
            int dim = std::ceil( std::sqrt (static_cast<double>(maxPerBDim)) );
            int optDim = dim; int optRemainder = dim;
            while( (bCount + dim - 1 ) / dim < maxPerBDim && dim > 0 )
            {
                int Remainder = dim - bCount % dim;
                if( Remainder == 0 )
                {
                    optDim = dim;
                    optRemainder = Remainder;
                    break;
                }
                else if( Remainder < optRemainder )
                {
                    optDim = dim;
                    optRemainder = Remainder;
                }

                dim--;
            }
            dim3 numBlocks( (bCount+optDim-1) / optDim, optDim );
            normalize<<<numBlocks, threadPerBlock>>> ((cufftReal*)data, nBatchSize, norm);
        }
        //TODO: think how to handle case if somehow there is more than 4TB of data
    }
    result = result && (cudaMemcpy( (void*)input, (void*)data, nBatchSize * (fftLength/2 + 1) * sizeof(cufftComplex), cudaMemcpyDeviceToHost ) == cudaSuccess);
    result = result && (cudaDeviceSynchronize() == cudaSuccess);

    if( padding != 0 )
        result = result && reallocate(batchSize) != 0;

    if(!result)
        fail = true;
    return result;
}

