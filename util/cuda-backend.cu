#include"cuda-backend.h"
#include<iostream>
#include<limits>
#include<array>
#include<vector>
#include<tuple>
#include<algorithm>
#include<string>
#include<memory>
#include<utility>

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
            std::cerr << erMsg << " got unexpected result: "
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

//cast an integer and clamp it to maximum of a new type, assuming value is positive
template<typename U, typename T>
constexpr U clamp_cast(T val)
{
  const auto uMax = std::numeric_limits<U>::max();
  if constexpr (std::numeric_limits<T>::max() <= uMax)
    return static_cast<U>(val);

  //else clamp
  if (val > uMax)
    return uMax;
  else return static_cast<U>(val);
}

//wrap cuda apis into single function name for use later
//https://docs.nvidia.com/cuda/cufft/#cufftgetsizemany
template<typename T>
using cufftPlanFnc_t = cufftResult (*) (cufftHandle, int, T*, T*, T, T, T*, T, T, cufftType, T, size_t);
template<typename T>
inline constexpr cufftPlanFnc_t<T> cuSizeEstFunc {nullptr} ;
template<> inline constexpr auto cuSizeEstFunc<int> = cufftGetSizeMany;
template<> inline constexpr auto cuSizeEstFunc<long long int> = cufftGetSizeMany64;

template<typename T>
__host__ cufftResult cufftGetSizeManyWrap(cufftHandle plan, T* fftLen, T batchSize, std::size_t *estimate)
{
    T cPoints { *fftLen/2 + 1 };

    return cuSizeEstFunc<T>(plan, 1, fftLen, fftLen, batchSize, 1, &cPoints, batchSize, 1, CUFFT_R2C, batchSize, estimate);
}

//ofc cudaMalloc is a macro, fml
cudaError_t(*cudaMallocFunc)(void**, size_t) = cudaMalloc;

template<typename T>
__host__ std::size_t EstimateBatchSize( cufftHandle plan, T len, std::size_t maxMem, std::size_t maxBatch )
{
    //clamp batch size to underlying type
    constexpr T tMax = std::numeric_limits<T>::max();
    if (maxBatch > tMax) maxBatch = tMax;
    //number of complex values in result of R2C transform, len/2 rounds down automatically, desired behaviour
    auto cPoints = len/2 + 1;
    //first check if the transformation can fit with the least conservative memory usage 
    //cuda might require at most 8x the original array size for work area
    //https://docs.nvidia.com/cuda/cufft/#fourier-transform-setup
    T batch_size{ std::min<T>(clamp_cast<T>(maxMem) / (9 * sizeof(cufftComplex) * cPoints), clamp_cast<T>(maxBatch)) };
    std::size_t estimate{};

    if ( !PackedAPI{cufftGetSizeManyWrap<T>, "estimating needed work area", plan, &len, batch_size, &estimate}.exec() )
        return 0;

    //if by chance there is enough memory to do transform in a single batch, return that batch size
    if( batch_size == maxBatch && maxMem <= (estimate + sizeof(cufftComplex) * batch_size * cPoints) )
        return batch_size;

    //otherwise try to find the size just large enough starting from the linear extrapolation
    //first guess is extrapolation, and I guess it is also the one before last :)
    batch_size = std::min<T>( maxMem / (estimate/batch_size + sizeof(cufftComplex) * cPoints), maxBatch );
    while(batch_size > 0 && batch_size <= maxBatch)
    {
        if ( !PackedAPI{cufftGetSizeManyWrap<T>, "estimating needed work area", plan, &len, batch_size, &estimate}.exec() )
            return 0;

        const auto perBatch { sizeof(cufftComplex) * cPoints + estimate/batch_size };
        //using signed type could be buggy if we get gpu memory bigger than 2^32 bytes lol
        const long long int excessMem { static_cast<long long int>(maxMem - (estimate + sizeof(cufftComplex)*batch_size*cPoints)) };
        const bool isFitting { excessMem >= 0 };
        //stop if you cannot fit another spatial point in a batch 
        if ( isFitting && (batch_size == maxBatch || excessMem < perBatch) )
            return batch_size;

        if ( isFitting )
        {
            if ( batch_size == maxBatch || excessMem < perBatch )
                return batch_size;

            batch_size += std::min<std::size_t>(excessMem/perBatch, 1);
        }
        else batch_size--;
    }
    return 0;
}

extern std::string printMemSize(std::size_t);

std::size_t cuFFTEngine::InitGPU()
{
    //reset internal data if reinitializing
    free();

    int devCount, curGPU; cudaGetDeviceCount(&devCount);
    fail = !run_api_pack( PackedAPI{ cudaGetDeviceCount, "getting device count", &devCount },
                          PackedAPI{ cudaGetDevice, "getting current GPU", &curGPU } );

    //basic checks
    if( devCount == 0 )
    {
        fail = true;
        std::cerr << "Found no CUDA-enabled devices!\n" ;
        return 0;
    }
    if( gpuID >= devCount || (gpuID < 0 && gpuID != -1) )
    {
        fail = true;
        std::cerr << "The requested GPU id" << std::to_string(gpuID) << " is invalid(larger than number of available GPUs).\n";
    }
    
    //if a specific GPU has been requested 
    if( gpuID != -1 )
    {
        //if gpu is not set already set it to the target
        if(curGPU != gpuID && !fail)
            fail = !run_api_pack( PackedAPI{ cudaDeviceReset, "freeing GPU allocation" }, //most likely excessive, since this process have not allocated anything at this point
                                  PackedAPI{ cudaSetDevice, "setting a GPU to run transform on", gpuID } );

        if(fail)
        {
            std::cerr << "Failed to get GPU id#" << std::to_string(gpuID) << ".\n";
            return 0;
        }
    }
    else fail = !PackedAPI{ cudaGetDevice, "getting a gpuID", &gpuID }.exec();
    //WARNING curGPU is not updated because it is supposed to not be used past this point :p
    
    std::string result{};
    std::size_t freeMem, totalMem;
    cudaDeviceProp props{};
    if( !fail )
        fail = !run_api_pack(
            PackedAPI{ cudaGetDeviceProperties, "getting current GPU properties", &props, gpuID },
            PackedAPI{ cudaMemGetInfo, "getting GPU VRAM info", &freeMem, &totalMem },
            PackedAPI{ cufftCreate, "creating cuFFT plan", &plan }) &&
            //need to run it separately since values are passed by copy
            !PackedAPI{ cufftSetAutoAllocation, "setting auto allocation OFF", plan, 0 }.exec(); //sets work area to be manually managed
    std::cout << props.name << std::endl;

    //hard cutoff point 1 if one cannot initialize here, before memory allocation
    if (fail)
    {
        std::cerr << "Failed to create a plan on requested gpu!\n";
        return 0;
    }
    //describe the device characteristics
    std::cout << "Using GPU #" << std::to_string(curGPU) << " \"" << props.name << "\" "
           <<  std::to_string( props.multiProcessorCount ) << "SM" << '@' << std::to_string( props.clockRate / 1000 ) << "MHz, "
           <<  printMemSize( totalMem ) << "s of global memory on device (" << printMemSize(freeMem) << " free).\n" ;
    return freeMem;
}

bool cuFFTEngine::Init( std::size_t t_len, std::size_t maxBatch, std::size_t maxMem )
{
    //start by resetting fail flag
    fail = false;

    fftLength = t_len;

    auto freeMem = InitGPU();
    if( fail ) 
    {
        std::cerr << "Could not initialize a GPU, quitting!\n";
        return !fail;
    }
    if(maxMem > freeMem)
    {
        std::cout << "Warning: Device doesn't have memory requested: " << printMemSize( maxMem ) << ", defaulting to 95% of total GPU memory.\n";
        maxMem = 0.95 * freeMem;
    }
    if(maxMem == 0) maxMem = 0.95 * freeMem; //defaulting to 95% of available VRAM

    //conversion to acceptable type for fft
    if( fftLength  > std::numeric_limits<long long int>::max() )
    {
        //only possible to trip because currently std::size_t is ull
        //will not happen for a while
        //the limit specified is 32 EiB lul
        std::cerr <<  "Time series lenth overflows supported index range (lli max)!\n";

        fail = true;
        return !fail;
    }

    //estimate how much size bundle would take
    //first get initial approximation by taking guarantee from documentation that maximum workspace area is 8 times the data
    //note: need at least 2x and at most 9x the data size for FFT transform(depending on length of the fft set)
    //greedy greedy host wants to have ALL the memory :D
    //if (maxBatch == 0) maxBatch = std::numeric_limits<long long int>::max();
    //abort if there is not enough VRAM to accomodate even a single batch

    //decide which backend to use, 64bit one allows *insane* sampling depthes
    //larger data size backend has limitations https://docs.nvidia.com/cuda/cufft/index.html#unique_184649339
    //first check if we *absolutely* have to use 64-bit backend
    bool needExtended = fftLength > std::numeric_limits<int>::max();     //if single batch cannot be addressed with 32bit 'int'
    bool fitIn4GEl { fftLength * maxBatch <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1 }; //do we even have more than 4G elements?!
    bool wantExtended = !fitIn4GEl && maxMem >= 2 * sizeof(float) * static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())  && //but we also must have more than enough memory for 4G elements
                            is4GCompatible(fftLength);                                                                              //at least 48 gigs of free memory!

    //and then check if backend is actually usable
    if (needExtended || wantExtended)
    {
        //else good to try
        useExtended = true;
        //if cannot use 4G+ elements, limit maxBatch
        if( !fitIn4GEl && !is4GCompatible(fftLength) )
           maxBatch = (static_cast<std::uint64_t>(std::numeric_limits<uint32_t>::max()) + 1)/fftLength;
        batchSize = EstimateBatchSize<long long int>(plan, fftLength, maxMem, maxBatch);
    }
    else
    {
        useExtended = false;
        batchSize = EstimateBatchSize<int>(plan, fftLength, maxMem, std::min<std::size_t>(maxBatch, std::numeric_limits<int>::max()));
    }

    if(batchSize == 0)
    {
        fail = true;
        std::cerr << "An error happened during batch size estimation! Please check the previous log messages.\n";
        return !fail;
    }

    const auto workSize = reallocate(batchSize);
    if( workSize == 0 )
    {
        fail = true;
        std::cerr << "Failed to allocate work assets in VRAM, tried to go with " << printMemSize( 2 * batchSize * (fftLength/2 + 1) * sizeof(float)) << " batches.\n";
        return !fail;
    }

    std::cout << "Chosen to do transforms in " << batchSize << " point batches (" << printMemSize(2 * batchSize * (fftLength/2 + 1) * sizeof(float)) <<
        " each, " << printMemSize(2 * batchSize * (fftLength/2 + 1) * sizeof(float) + workSize) << " together with work area in the gpu).\n";
    return !fail;
}

template<typename T>
inline constexpr cufftPlanFnc_t<T> cufftMakePlanFnc {nullptr};
template<> inline constexpr auto cufftMakePlanFnc<int> = cufftMakePlanMany;
template<> inline constexpr auto cufftMakePlanFnc<long long int> = cufftMakePlanMany64;

template<typename T>
cufftResult cufftMakePlanWrap(cufftHandle plan, T fftLength, T batch_size, std::size_t* workAreaSize)
{
    T cPoints { fftLength/2 + 1 };

    return cufftMakePlanFnc<T>(plan, 1, &fftLength,
            &fftLength, batch_size, 1,
            &cPoints, batch_size, 1,
            CUFFT_R2C, batch_size, workAreaSize);
}

template<typename T>
using cufftMakePlanFunc_t = cufftResult (*) (cufftHandle, T, T, std::size_t*);

std::size_t cuFFTEngine::reallocate(std::size_t batch_size, bool lazy)
{
    //fall through if the state is fucked already
    if (fail) return 0;

    std::size_t cPoints { fftLength/2 + 1 };

    //clear the old work areas if needed
    if (!lazy && allocDataSize != 0 || allocDataSize < batch_size)
    {
        fail = !run_api_pack( 
                PackedAPI{ cudaFree, "freeing data buffer", (void*)data },
                PackedAPI{ cudaFree, "freeing work buffer", cudaBuffer } );
        allocDataSize = 0;
        allocBufferSize = 0;
    }

    //recreate a plan
    fail = fail || !run_api_pack( 
            PackedAPI{ cufftDestroy, "destroying old plan", plan },
            PackedAPI{ cufftCreate, "creating a new plan", &plan } ) ||
        !PackedAPI{ cufftSetAutoAllocation, "setting auto allocation off", plan, 0 }.exec();

    std::size_t workSize {};
    fail = fail || !(useExtended? 
            PackedAPI{ static_cast<cufftMakePlanFunc_t<long long int>>(cufftMakePlanWrap<long long int>), "making a 64bit API cuFFT plan", plan, static_cast<long long int>(fftLength), static_cast<long long int>(batch_size), &workSize }.exec() :
            PackedAPI{ static_cast<cufftMakePlanFunc_t<int>>(cufftMakePlanWrap<int>), "making a cuFFT plan", plan, static_cast<int>(fftLength), static_cast<int>(batch_size), &workSize }.exec() );

    if( !fail && allocDataSize == 0 )
    {
        fail = !run_api_pack(
                PackedAPI{ cudaMallocFunc, "allocating data buffer", (void**)&data, sizeof(cufftComplex)*batch_size*cPoints },
                PackedAPI{ cudaMallocFunc, "allocating work buffer", (void**)&cudaBuffer, workSize } );
        if( !fail )
        {
            allocDataSize = batch_size;
            allocBufferSize = workSize;

        }
    }

    if( !fail )
        fail = !PackedAPI{cufftSetWorkArea, "setting work buffer location", plan, cudaBuffer}.exec();
    return fail? 0 : workSize;
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
        std::cerr << "Not enough free VRAM for interpolation! Giving up on interpolation! Consider limiting vram usage by the main subroutine with --max-vram" << std::endl;
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
    bool failed = !run_api_pack(
        PackedAPI{cudaMallocFunc, "allocating GPU memory for FP constants",
          (void**)&InterpAccel.h, sizeof(float) * (3 * sCnt + fftLength -2)},
        PackedAPI{cudaMallocFunc, "allocating spline indexing array",
          (void**)&InterpAccel.Indices, sizeof(std::size_t) * (fftLength -2)}) &&
        !run_api_pack(
        PackedAPI{cudaMemcpy, "copying FP constants to GPU",
          (void*)&InterpAccel.h, (const void*)h.get(), sizeof(float) * (3*sCnt + fftLength -2), cudaMemcpyHostToDevice},
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
    InterpAccel.interpReady = !failed;

    return InterpAccel.interpReady;
}

void cuFFTEngine::free()
{
    if(cufftReady)
    {
        run_api_pack( PackedAPI{cudaFree, "freeing data buffer", (void*)data},
                      PackedAPI{cudaFree, "freeing work area buffer", (void*)cudaBuffer} );
        allocDataSize = 0;
        allocBufferSize = 0;
        cufftReady = false;
    }
    InterpAccel.free();
}


cuFFTEngine::~cuFFTEngine() noexcept
{
    free();
    PackedAPI{cufftDestroy, "destroying the plan", plan}.exec();
    InterpAccel.free();
}

void cuFFTEngine::InterpAccel_t::free()
{
    if (interpReady)
    {
        run_api_pack( PackedAPI{cudaFree, "freeing interp FP accelerators", (void*)h},
                      PackedAPI{cudaFree, "freeing interp indice array", (void*)Indices} );
        interpReady = false;
    }
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

#include<boost/crc.hpp>
bool cuFFTEngine::RunTransform( float* input, float norm, std::size_t padding)
{
    if( fail || input == nullptr || padding > batchSize )
        return false;

    std::size_t realSize = batchSize - padding;
    //spit out crcs for diagnostics
    boost::crc_32_type result;
    result.process_bytes(input, sizeof(float)*realSize * fftLength);
    std::cout << "Data CRC32 checksum is: " << std::hex << result.checksum() << std::endl;

    if( padding != 0 )
        fail = reallocate(realSize) == 0;
    //move data into array
    fail = fail || cudaMemcpy( (void*)data, (const void*)input, realSize * fftLength * sizeof(cufftReal), cudaMemcpyHostToDevice ) != cudaSuccess;
    //reinterpolate data if interpolation is ready
    if ( InterpAccel.interpReady && !fail )
        interp_kernel<<<to_grid(realSize, DefaultBlockSize), DefaultBlockSize>>>(
                (float*)data, (float*)cudaBuffer, InterpAccel.sCnt, realSize, fftLength,
                InterpAccel.h, InterpAccel.mu, InterpAccel.l, 
                InterpAccel.Indices, InterpAccel.dt);

    fail = fail || cufftExecR2C( plan, (cufftReal*)data, data ) != CUFFT_SUCCESS;
    //if there is a norm to use, normalize the data
    if( norm != 1.0f && !fail )
        normalize<<<to_grid(2 * realSize * (fftLength/2 + 1), DefaultBlockSize), DefaultBlockSize>>> ( (cufftReal*)data, 2 * realSize * (fftLength/2 + 1), norm );

    fail = fail || cudaMemcpy( (void*)input, (const void*)data, realSize * (fftLength/2 + 1) * sizeof(cufftComplex), cudaMemcpyDeviceToHost ) != cudaSuccess;
    fail = fail || cudaDeviceSynchronize() != cudaSuccess;

    //and then print out result crc
    result.reset();
    result.process_bytes(input, sizeof(cufftComplex) * realSize * (fftLength/2 + 1) );
    std::cout << "Result CRC32 is: " << std::hex << result.checksum() << std::endl;

    if( !fail && padding != 0 )
        fail = reallocate(batchSize) == 0;

    return !fail;
}

