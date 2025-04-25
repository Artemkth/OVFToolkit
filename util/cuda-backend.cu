#include"cuda-backend.h"
#include<iostream>
#include<limits>
#include<array>
#include<vector>
#include<tuple>
#include<algorithm>
#include<string>
#include<memory>

static_assert(2 * sizeof(float) == sizeof(cufftComplex), "Incompatible float!");

//I can't believe this is not already provided by api :'(
__host__ __device__ inline bool operator==(const dim3& ref1, const dim3& ref2)
{ return ref1.x == ref2.x && ref1.y == ref2.y && ref1.z == ref2.z; }

//CUDA APIs error handling wrapper
//Definition of success result for different sub-APIs
template<typename resType> resType cudaSuccVal = 0;
template<> cufftResult_t cudaSuccVal<cufftResult_t> = CUFFT_SUCCESS;
template<> cudaError_t cudaSuccVal<cudaError_t> = cudaSuccess;

//decode error message to something humanly readable
template<typename resType> std::string_view decodeCuError(resType);
//cuFFT specialization
//https://docs.nvidia.com/cuda/archive/12.0.0/cufft/index.html#return-value-cufftresult
template<> constexpr std::string_view decodeCuError<cufftResult_t>(cufftResult_t res)
{
    switch(res)
    {
        case CUFFT_SUCCESS:
            return "CUFFT_SUCCESS";
        case CUFFT_INVALID_PLAN:
            return "CUFFT_INVALID_PLAN";
        case CUFFT_ALLOC_FAILED:
            return "CUFFT_ALLOC_FAILED";
        case CUFFT_INVALID_TYPE:
            return "CUFFT_INVALID_TYPE";
        case CUFFT_INVALID_VALUE:
            return "CUFFT_INVALID_VALUE";
        case CUFFT_INTERNAL_ERROR:
            return "CUFFT_INTERNAL_ERROR";
        case CUFFT_EXEC_FAILED:
            return "CUFFT_EXEC_FAILED";
        case CUFFT_SETUP_FAILED:
            return "CUFFT_SETUP_FAILED";
        case CUFFT_INVALID_SIZE:
            return "CUFFT_INVALID_SIZE";
        case CUFFT_UNALIGNED_DATA:
            return "CUFFT_UNALIGNED_DATA";
        case CUFFT_INCOMPLETE_PARAMETER_LIST:
            return "CUFFT_INCOMPLETE_PARAMETER_LIST";
        case CUFFT_INVALID_DEVICE:
            return "CUFFT_INVALID_DEVICE";
        case CUFFT_PARSE_ERROR:
            return "CUFFT_PARSE_ERROR";
        case CUFFT_NO_WORKSPACE:
            return "CUFFT_NO_WORKSPACE";
        case CUFFT_NOT_IMPLEMENTED:
            return "CUFFT_NOT_IMPLEMENTED";
        case CUFFT_LICENSE_ERROR:
            return "CUFFT_LICENSE_ERROR";
        case CUFFT_NOT_SUPPORTED:
            return "CUFFT_NOT_SUPPORTED";
        default:
            return "Unimplemented";
    }
}
//cuRand specialization
//https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__ERROR.html
template<> std::string_view decodeCuError<cudaError_t>(cudaError_t res) { return cudaGetErrorName(res); }

//wrapper for cuda/cufft APIs
template<typename resType, typename... argsT>
struct PackedAPI {
    using funcType = resType (*)(argsT...);

    funcType func;
    std::tuple<argsT...> argsTpl;
    std::string_view erMsg;

    constexpr PackedAPI(funcType f, std::string_view msg, argsT... args):
        func(f), argsTpl(std::make_tuple(args...)), erMsg(msg) {}

    inline bool exec() {
        auto res = std::apply(func, argsTpl);
        if (res != cudaSuccVal<resType>)
            std::cerr << "Running " << erMsg << " got unexpected result "
                << decodeCuError(res) << "(" << res << "); Aborting!" << std::endl;
        return res == cudaSuccVal<resType>;
    }
};

//run the packs of APIs
template<typename... packsT>
inline bool run_api_pack(packsT... packs)
{ return ( packs.exec() && ... ); }

//allowed factors for the batch size
//https://docs.nvidia.com/cuda/cufft/index.html#cufftmakeplanmany64
constexpr std::array<std::size_t, 31> AllowedFactors { 
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 
    59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127 };
//Determine if the data set is compatible with extended 64 bit addressing
inline __host__ bool is4GCompatible( std::size_t size )
{
    //the one and only dimension (time) should have even number for R2C and C2R transforms
    if (size % 2 != 0)
        return false;

    //check if the factors in dimension are divisible by allowed primes only
    for (const auto x: AllowedFactors)
    {
        while (size != 1 && size % x == 0)
            size /= x;
    }
    return size == 1;
}

//default thread block, 256 threads on square (recommended value)
//https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#thread-hierarchy
constexpr dim3 DefaultBlockSize{16, 16};
constexpr std::size_t threadPerBlock{ DefaultBlockSize.x * DefaultBlockSize.y * DefaultBlockSize.z };
//limits from API documentation
static_assert( DefaultBlockSize.x <= 1024 && DefaultBlockSize.y <= 1024 && DefaultBlockSize.z <= 64 &&
               DefaultBlockSize.x * DefaultBlockSize.y * DefaultBlockSize.z <= 1024, "Default block size is given bad dimensions!" );

std::vector<std::tuple<std::size_t, dim3, dim3>> knownGridDim {};

//Section for calculating block grid for interpolation kernel

//calculate a cuda reference compliant grid fitting requested ammount of kernel launches
__host__ dim3 to_grid(std::size_t size, dim3 bSize = DefaultBlockSize)
{
    //try to use acceleration
    auto lookupResult = std::find_if( knownGridDim.begin(), knownGridDim.end(), [&size, &bSize] ( const std::tuple<std::size_t, dim3, dim3>& p ) { return std::get<0>(p) == size && std::get<1>(p) == bSize; } );
    if( lookupResult != knownGridDim.end() )
        return std::get<2>(*lookupResult);

    constexpr std::size_t gridDimMax { 65535 };
    const std::size_t tPerBlock { bSize.x * bSize.y * bSize.z };
    //ceil( size/tPerBlock )
    const std::size_t bCount { (size + tPerBlock - 1) / tPerBlock };

    dim3 optimalGrid{};
    if( bCount <= gridDimMax )
        //maximal 64MiB of single values
        optimalGrid = dim3{ static_cast<unsigned int>(bCount) };
    else if( bCount <= gridDimMax * gridDimMax )
    {
        //TODO: implement lookup using primes table sometime
        //with float values, and 1 values per array this will address 4TiB of values
        //both guaranteed to not truncate over the branching condition above
        const unsigned int min { static_cast<unsigned int>((bCount + gridDimMax - 1) / gridDimMax) };
        const unsigned int max { static_cast<unsigned int>(std::ceil( std::sqrt(static_cast<double>( bCount )) )) };
        unsigned int optDim = min; unsigned int optRemainder = max;
        unsigned int dim = min;
        for(; dim <= max; dim++)
        {
            unsigned int Remainder = bCount % dim;
            if( Remainder == 0 )
            {
                optDim = dim;
                optRemainder = Remainder;
                break;
            }

            Remainder = dim - Remainder;
            if( Remainder < optRemainder )
            {
                optDim = dim;
                optRemainder = Remainder;
            }
        }
        dim = (bCount + optDim - 1) / optDim;
        optimalGrid = {std::max(optDim, dim), std::min(optDim, dim)};
    }
    else
        //TODO: implement 3D block grid addressing, up to 0.3 EiB(with 1 points per interpolation)
        return {};

    knownGridDim.push_back( { size, bSize, optimalGrid } );

    return optimalGrid;
}

//wrap cuda apis into single function name for use later
//https://docs.nvidia.com/cuda/cufft/#cufftgetsizemany
template<typename T>
__host__ cufftResult cufftGetSizeManyWrap(cufftHandle, int, T*, T*, T, T, T*, T, T, cufftType, T, std::size_t*);
template<> 
__host__ cufftResult cufftGetSizeManyWrap<int> (cufftHandle plan, int rank, int *n, int *inembed, int istride, int idist, int *onembed, int ostride, int odist, cufftType type, int batch, std::size_t *workSize)
{ return cufftGetSizeMany(plan, rank, n, inembed, istride, idist, onembed, ostride, odist, type, batch, workSize); }
template<> 
__host__ cufftResult cufftGetSizeManyWrap<long long int> (cufftHandle plan, int rank, long long int *n, long long int *inembed, long long int istride, long long int idist, long long int *onembed, long long int ostride, long long int odist, cufftType type, long long int batch, std::size_t *workSize)
{ return cufftGetSizeMany64(plan, rank, n, inembed, istride, idist, onembed, ostride, odist, type, batch, workSize); }

template<typename T>
__host__ std::size_t EstimateBatchSize( cufftHandle plan, T len, std::size_t maxMem, std::size_t maxBatch = std::numeric_limits<std::size_t>::max() )
{
    //number of complex values in result of R2C transform, len/2 rounds down automatically, desired behaviour
    auto cPoints = len/2 + 1;
    //first check if the transformation can fit with the least conservative memory usage 
    //cuda might require at most 8x the original array size for work area
    std::size_t tmp { std::min<std::size_t>( std::min<std::size_t>(maxMem, std::numeric_limits<T>::max()) / (9 * sizeof(cufftComplex) * cPoints), maxBatch ) };
    T batch_size{ tmp > std::numeric_limits<T>::max()? std::numeric_limits<T>::max() : static_cast<T>(tmp) };
    // next two are guaranteed to fit since maxMem is capped to addressable space only
    T arrSize { static_cast<T>(batch_size * len) }; 
    T outArrSize { static_cast<T>(batch_size * cPoints) }; //rounds down
    std::size_t estimate{};
    auto estimationError = cufftGetSizeManyWrap<T>(
                plan, 1, &len, 
                &arrSize, batch_size, 1,
                &outArrSize, batch_size, 1,
                CUFFT_R2C, batch_size, &estimate);
    if( estimationError != CUFFT_SUCCESS )
    {
        std::cerr << "Could not run the cufftGetSizeMany, got an error: " << decodeCuError(estimationError) << "("<< estimationError<<")"<< std::endl;
        return 0;
    }

    //if by chance there is enough memory to do transform in a single batch, return that batch size
    if( batch_size == maxBatch && maxMem <= (estimate + sizeof(cufftComplex) * batch_size * cPoints) )
        return batch_size;

    //otherwise try to find the size just large enough starting from the linear extrapolation
    batch_size = std::min<int>( maxMem / (estimate/batch_size + sizeof(cufftComplex) * cPoints), maxBatch );
    bool isFitting = true; std::size_t cnt {0};
    while(batch_size > 0 && batch_size <= maxBatch)
    {
        arrSize = batch_size * len;
        outArrSize = batch_size * cPoints; //rounds down
        estimationError = cufftGetSizeManyWrap<T>(
                plan, 1, &len, 
                &arrSize, batch_size, 1,
                &outArrSize, batch_size, 1,
                CUFFT_R2C, batch_size, &estimate);

        if( estimationError != CUFFT_SUCCESS && estimationError != CUFFT_ALLOC_FAILED )
            return 0; //expect those two since might go over allowed GPU memory

        const auto wasFitting = isFitting;
        isFitting = maxMem >= (estimate + sizeof(cufftComplex) * batch_size * cPoints);
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

extern std::string printMemSize(std::size_t);

std::string cuFFTEngine::Init( std::size_t t_len, std::size_t maxBatch, std::size_t maxMem )
{
    fftLength = t_len;
    //reset internal data if reinitializing
    if( data != nullptr )
    {
        cufftDestroy(plan);
        cudaFree(data);
        InterpAccel.free();
        fail = false;
    }

    int devCount; cudaGetDeviceCount(&devCount);
    if( devCount == 0 )
    {
        fail = true;
        return "Found no CUDA-enabled devices!" ;
    }
    if( gpuID >= devCount )
    {
        fail = true;
        return (std::string)"The GPU id" + std::to_string(gpuID) + " received is invalid(larger than number of available GPUs).";
    }
    int curGPU; cudaGetDevice(&curGPU);
    if( gpuID != -1 )
    {
        //if gpu is not set already set it to the target
        if(curGPU != gpuID)
        {
            cudaDeviceReset(); data = nullptr;
            fail = cudaSetDevice(gpuID) != cudaSuccess;
        }

        if(fail)
            return (std::string)"Failed to get GPU id#" + std::to_string(gpuID) + ".";
    }
    
    if(fail = cufftCreate(&plan) != CUFFT_SUCCESS) 
        return "Failed to create a plan on requested gpu!\n";

    //get device characteristics
    std::string result{};
    std::size_t freeMem, totalMem;
    cudaDeviceProp props{};
    cudaGetDeviceProperties(&props, curGPU);
    cudaMemGetInfo(&freeMem, &totalMem);
    result += (std::string)"Using GPU #" + std::to_string(curGPU) + " \"" + props.name + "\" "
           +  std::to_string( props.multiProcessorCount ) + "SM" + '@' + std::to_string( props.clockRate / 1000 ) + "MHz, "
           +  printMemSize( totalMem ) + "s of global memory on device (" + printMemSize(freeMem) + " free)." ;
    if(maxMem > freeMem)
    {
        result += "\nWarning: Device doesn't have memory requested: " + printMemSize( maxMem ) + ", defaulting to 95% of total GPU memory.";
        maxMem = 0.95 * freeMem;
    }
    if(maxMem == 0) maxMem = 0.95 * freeMem; //defaulting to 95% of available VRAM

    //conversion to acceptable type for fft
    if( fftLength  > std::numeric_limits<long long int>::max() )
    {
        //only possible to trip because currently std::size_t is ull
        fail = true;
        return result + "\nTime series lenth overflows supported index range!";
    }

    //estimate how much size bundle would take
    //first get initial approximation by taking guarantee from documentation that maximum workspace area is 8 times the data
    //note: need at least 2x and at most 9x the data size for FFT transform(depending on length of the fft set)
    //greedy greedy host wants to have ALL the memory :D
    if (maxBatch == 0) maxBatch = std::numeric_limits<long long int>::max();

    //decide which backend to use, 64bit one allows *insane* sampling depthes
    //larger data size backend has limitations https://docs.nvidia.com/cuda/cufft/index.html#unique_184649339
    //first check if we *absolutely* have to use 64-bit backend
    bool needExtended = fftLength + 1 > std::numeric_limits<int>::max();     //if single batch cannot be addressed with 32bit 'int'
    useExtended = needExtended || maxMem/(sizeof(float) * (fftLength + 2)) > std::numeric_limits<int>::max(); //if we can get any advantage from batching more

    //and then check if backend is actually usable
    if (needExtended)
    {
        //abort if there is not enough VRAM to accomodate even a single batch
        if (maxMem < sizeof(float) * (fftLength + 2) * 2)
        {
            fail = true;
            return result + "\n64bit API fail, doesn't have enough VRAM to accomodate a single spatial point batch!";
        }
        //abort if fftLength is not 
        else if ((fftLength % 2 != 0 || !is4GCompatible(fftLength)) && fftLength + 2 > 4ll * 1024 * 1024 * 1024)
        {
            fail = true;
            return result + "\n64bit API fail, to be able to use more than 2^32 time points, number of points should be even and largest prime divisor should be less than 127!";
        }

        //else good to try
        batchSize = EstimateBatchSize<long long int>(plan, fftLength, maxMem, maxBatch);
    }
    else if (useExtended)
        batchSize = EstimateBatchSize<long long int>(plan, fftLength, maxMem, maxBatch);
    else
        batchSize = EstimateBatchSize<int>(plan, fftLength, maxMem, std::min<std::size_t>(maxBatch, std::numeric_limits<int>::max()));

    if(batchSize == 0)
    {
        fail = true;
        return result + "\nUnhandled error happending during batch size estimation!";
    }

    auto workSize = reallocate(batchSize);
    if( workSize == 0 )
    {
        fail = true;
        return result + "\nFailed to allcate work assets in VRAM, tried to go with " + printMemSize( 2 * batchSize * (fftLength/2 + 1) * sizeof(float)) + " batches.";
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
    cufftDestroy(plan); cufftCreate(&plan);

    std::size_t workSize {};
    cufftResult allocationError{};
    if(useExtended)
    {
        long long int len = fftLength;
        long long int cPoints = fftLength/2 + 1;
        long long int arrSize = newBatch * fftLength;
        long long int outArrSize = newBatch * cPoints ;

        allocationError = cufftMakePlanMany64(
                plan, 1, &len, 
                &arrSize, newBatch, 1,
                &outArrSize, newBatch, 1,
                CUFFT_R2C, newBatch, &workSize);
    }
    else
    {
        int len = fftLength;
        int cPoints = fftLength/2 + 1;
        int arrSize =  newBatch * fftLength;
        int outArrSize =  newBatch * cPoints;

        allocationError = cufftMakePlanMany(
                plan, 1, &len, 
                &arrSize, newBatch, 1,
                &outArrSize, newBatch, 1,
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

//actually excited to do some computation on videocard on my own :D
__global__ void normalize( cufftReal* data, std::size_t nSize, float norm )
{
    //CUDA kernel for normalizing data, should be zupa fast
    std::size_t coord = (blockIdx.y * gridDim.x + blockIdx.x) * blockDim.x * blockDim.y + //size of a block in values 
                        blockDim.x * threadIdx.y + threadIdx.x;                            //and position of thread within block

    if( coord < nSize ) //if thread is within block, proceed normalizing the value
        *(data + coord) *= norm;
}

//init interpolation
__host__ bool cuFFTEngine::InitInterp( const double* ts )
{
    if( fail || fftLength < 2 )
        //2 is acceptable = do nothing, lol
        return false;

    //constants for reinterpreter
    InterpAccel.sCnt = fftLength - 1;//nunmber of intervals between cnt points
    InterpAccel.trueStep = (ts[fftLength - 1] - ts[0]) / (fftLength - 1);
    const auto& sCnt = InterpAccel.sCnt;
    const auto& step = InterpAccel.trueStep;

    const std::size_t staticOverhead {
        //shared interp accelerators
        // 3 (h, mu and l) arrays for spline parameters
        // and fftLength - 2 dts points
        (3 * sCnt + fftLength - 2) * sizeof(float) + 
        // fftLength - 2 spline indices
        (fftLength - 2) * sizeof(std::size_t)
    };

    //evaluate the memory constraints, give up if there is not enough free memory
    std::size_t freeMem, totMem;
    cudaMemGetInfo( &freeMem, &totMem );
    if( freeMem < staticOverhead )
    {
        std::cerr << "Not enough free VRAM for interpolation! Giving up on interpolation!" << std::endl;
        return false;
    }

    //do some host calculations and upload arithmetic accelerators onto the gpu
    std::unique_ptr<float[]> h{new float[3 * sCnt + fftLength - 2]};
    auto mu = h.get() + sCnt;
    auto l  = h.get() + 2 * sCnt;
    auto dts= h.get() + 3 * sCnt;
    std::unique_ptr<std::size_t[]> ind{new std::size_t[fftLength - 2]};

    //fill in const data
    mu[0] = 0.; l[0] = 1.; h[0] = ts[1] - ts[0];
    for( std::size_t i = 1; i < sCnt; i++ )
    {
        h[i] = ts[i+1] - ts[i];
        l[i] = 2 * (ts[i+1] - ts[i-1]) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
    }
    //calculate access indices and time shifts
    //skip initial and final points, those don't need reinterpolation
    for( std::size_t i = 1; i < fftLength - 1; i++ )
    {
        std::size_t j{0};
        for(; j < sCnt; j++)
            if( (ts[0] + i * step) < ts[j + 1] )
                break;

        ind[i - 1] = j;
        dts[i - 1] = ts[0] + i * step - ts[j];
    }

    //upload results to gpu memory
    bool failed = run_api_pack(
        PackedAPI{static_cast<cudaError_t(*)(void**, size_t)>(cudaMalloc), "allocating GPU memory for FP constants",
          (void**)&InterpAccel.h, sizeof(float) * (3 * sCnt + fftLength -2)},
        PackedAPI{cudaMemcpy, "copying FP constants to GPU",
          (void*)&InterpAccel.h, (const void*)h.get(), sizeof(float) * (3*sCnt + fftLength -2), cudaMemcpyHostToDevice},
        PackedAPI{static_cast<cudaError_t(*)(void**, size_t)>(cudaMalloc), "allocating spline indexing array",
          (void**)&InterpAccel.Indices, sizeof(std::size_t) * (fftLength -2)},
        PackedAPI{cudaMemcpy, "copying spline indexes to GPU",
          (void*)&InterpAccel.Indices, (const void*)ind.get(), (fftLength - 2) * sizeof(std::size_t), cudaMemcpyHostToDevice});

    if(!failed)
    {
        InterpAccel.mu = InterpAccel.h + sCnt;
        InterpAccel.l  = InterpAccel.h + 2 * sCnt;
        InterpAccel.dt = InterpAccel.h + 3 * sCnt;
    }
    //TODO: implement retrying BlockBuffer allocation

    //and cleanup if failed along the way 
    if(failed || cudaDeviceSynchronize() != cudaSuccess)
    {
        InterpAccel.free();
        cudaDeviceSynchronize();
    }
    InterpAccel.Ready = !failed;

    return InterpAccel.Ready;
}

cuFFTEngine::~cuFFTEngine() noexcept
{
    run_api_pack( 
            PackedAPI{cufftDestroy, "destroying the plan", plan},
            PackedAPI{cudaFree, "freeing the data buffer", (void*)data}
            );
    InterpAccel.free();
}

__host__ void cuFFTEngine::InterpAccel_t::free()
{
    run_api_pack(
            PackedAPI{cudaFree, "freeing FP accelerators", (void*)h},
            PackedAPI{cudaFree, "freeing indice array", (void*)Indices}
            );
    Ready = false;
}

//run interpolation over data to remove jitter
__global__ void interp_kernel(float * const __restrict__ data,
                                           float * const __restrict__ workBuffer,
                                           std::size_t splLen,
                                           std::size_t batchSize,
                                           std::size_t outLen,
                                           float const * const __restrict__ h,
                                           float const * const __restrict__ mu,
                                           float const * const __restrict__ l,
                                           std::size_t const * const __restrict__ ind,
                                           float const * const __restrict__ dt
                                           )
{
    const std::size_t index { (blockIdx.y*gridDim.x + blockIdx.x)*threadPerBlock + 
                              threadIdx.y * blockDim.x + threadIdx.x };

    //nothing to do for overflow threads
    if( !index < batchSize )
        return;

    //the additional 2*splLen floats dynamic overhead
    //reuse cuFFT buffer
    auto a = (float*)workBuffer + 2 * splLen * index;
    auto z = a + splLen;

    //copy original values into the array 'a'
    for(size_t i = 0; i < splLen; i++)
        a[i] = data[ i * batchSize + index ];

    //forward loop to calculate z_i
    z[0] = 0.;
    for(size_t i = 1; i < splLen; i++)
    {
        auto alpha = 3./h[i]   *   (data[(i + 1) * batchSize + index] - data[i * batchSize + index]) -
                     3./h[i - 1] * (data[i * batchSize + index] - data[(i-1) * batchSize + index]);
        z[i] = (alpha - h[i - 1] * z[i - 1])/l[i];
    }

    //set value of the last point manually, in case splLen < outLen - 1
    //data[(outLen - 1) * batchSize + index] = data[ splLen * batchSize + index];
    double cnext = 0.;
    size_t spl_i = 0;//number of spline from the end
    for(size_t i = 0; i < splLen; i++)
    {
        const size_t j = splLen - i - 1;
        double c = z[j] - mu[j] * cnext;

        //TODO: optimize by making index and dtVal shared
        //range check before result fetch
        for(; spl_i < outLen - 2 && ind[outLen - 3 - spl_i] == j; spl_i++ )
        {
            const auto dtVal = dt[outLen - 3 - spl_i];

            //and assign the interpolated value
            data[(outLen - 2 - spl_i) * batchSize + index] = a[j] +
                ((data[(j + 1) * batchSize + index] - data[j * batchSize + index])/h[j] -h[j] * (cnext + 2 * c) / 3.) * dtVal +
                c * dtVal * dtVal +
                (cnext - c)/(3. * h[j]) * dtVal * dtVal * dtVal;
        }
        if( spl_i == outLen - 2 )
            break;

        //rotate in the end
        cnext = c;
    }
}

bool cuFFTEngine::RunTransform( float* input, float norm, std::size_t padding)
{
    if( fail || input == nullptr || padding >= batchSize )
        return false;

    std::size_t nBatchSize { batchSize - padding };

    if( padding != 0 )
        fail = reallocate(nBatchSize) == 0;
    //move data into array
    fail = fail || cudaMemcpy( (void*)data, (const void*)input, nBatchSize * (fftLength/2 + 1) * sizeof(cufftComplex), cudaMemcpyHostToDevice ) != cudaSuccess;
    //reinterpolate data if interpolation is ready
    if ( InterpAccel.Ready && !fail)
        interp_kernel<<<to_grid(nBatchSize, DefaultBlockSize), DefaultBlockSize>>>(
                (float*)data, (float*)cudaBuffer, InterpAccel.sCnt, nBatchSize, fftLength,
                InterpAccel.h, InterpAccel.mu, InterpAccel.l, 
                InterpAccel.Indices, InterpAccel.dt);

    fail = fail || cufftExecR2C( plan, (cufftReal*)data, data ) != CUFFT_SUCCESS;
    //if there is a norm to use, normalize the data
    if( norm != 1.0f && !fail )
        normalize<<<to_grid(2 * nBatchSize * (fftLength/2 + 1), DefaultBlockSize), DefaultBlockSize>>> ( (cufftReal*)data, 2 * nBatchSize * (fftLength/2 + 1), norm );

    fail = fail || cudaMemcpy( (void*)input, (const void*)data, nBatchSize * (fftLength/2 + 1) * sizeof(cufftComplex), cudaMemcpyDeviceToHost ) != cudaSuccess;
    fail = fail || cudaDeviceSynchronize() != cudaSuccess;

    if( !fail && padding != 0 )
        fail = reallocate(batchSize) == 0;

    return !fail;
}

