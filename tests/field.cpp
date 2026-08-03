#include<iostream>
#include<random>
#include<chrono> //seeding with time point
#include<algorithm>
#include<cmath>
#include<array>
#include<vector>
#include<VField.h>
#include<limits>

template<typename T>
concept InitializableFieldScalar = requires(VField::VField& field)
{
    field.initData<T>(1);
};

static_assert(InitializableFieldScalar<float>);
static_assert(InitializableFieldScalar<double>);
static_assert(!InitializableFieldScalar<int>);

int main()
{
    struct CountingDeleter
    {
        std::size_t* calls;

        explicit CountingDeleter(std::size_t& count) noexcept: calls(&count) {}
        CountingDeleter(const CountingDeleter&) = delete;
        CountingDeleter& operator=(const CountingDeleter&) = delete;
        CountingDeleter(CountingDeleter&&) noexcept = default;
        CountingDeleter& operator=(CountingDeleter&&) noexcept = default;

        void operator()(double* pointer) noexcept
        {
            ++*calls;
            delete[] pointer;
        }
    };

    {
        VField::VField owned;
        auto source = std::make_unique<double[]>(3);
        source[0] = 1.0;
        source[1] = 2.0;
        source[2] = 3.0;
        auto* original = source.get();
        owned.adoptData(std::move(source), 3);

        auto released = owned.releaseData<double>();
        if(released.get() != original || owned.isDataPresent() || owned.scalarCount() != 0 ||
           released[0] != 1.0 || released[2] != 3.0)
        {
            std::cerr << "Releasing field data did not transfer ownership correctly!\n";
            return 17;
        }

        owned.adoptData(std::move(released), 3);
        owned.clearData();
        if(owned.isDataPresent() || owned.scalarCount() != 0)
        {
            std::cerr << "Clearing field data did not reset its storage!\n";
            return 18;
        }
    }

    {
        std::size_t deleteCalls{};
        VField::VField adopted;
        std::unique_ptr<double[], CountingDeleter> source{
            new double[2]{4.0, 5.0}, CountingDeleter{deleteCalls}};
        adopted.adoptData(std::move(source), 2);

        auto released = adopted.releaseData<double>();
        if(deleteCalls != 0 || released[0] != 4.0 || released[1] != 5.0)
        {
            std::cerr << "Releasing adopted data lost its custom deleter or values!\n";
            return 19;
        }
        released.reset();
        if(deleteCalls != 1)
        {
            std::cerr << "Adopted data was not returned to its original allocator!\n";
            return 20;
        }
    }

    {
        VField::VField copied;
        const std::array<float, 3> floatValues{1.0F, 2.0F, 3.0F};
        copied.setData(floatValues);
        if(!copied.stores<float>() || copied.scalarCount() != floatValues.size() ||
           copied.scalarSizeBytes() != sizeof(float) ||
           copied.dataSizeBytes() != sizeof(float) * floatValues.size() ||
           copied.data<float>()[1] != 2.0F)
        {
            std::cerr << "Copying a float container did not preserve its scalar type!\n";
            return 21;
        }

        const std::vector<int> integerValues{4, 5, 6, 7};
        copied.setData(integerValues);
        if(!copied.stores<double>() || copied.scalarCount() != integerValues.size() ||
           copied.data<double>()[0] != 4.0 || copied.data<double>()[3] != 7.0)
        {
            std::cerr << "Copying a convertible container did not produce double data!\n";
            return 22;
        }

        std::vector<double> consumedValues{8.0, 9.0, 10.0};
        auto* originalStorage = consumedValues.data();
        copied.setData(std::move(consumedValues));
        if(!copied.stores<double>() || copied.data<double>() != originalStorage ||
           copied.scalarCount() != 3 || copied.data<double>()[2] != 10.0)
        {
            std::cerr << "Consuming a double container copied or corrupted its storage!\n";
            return 23;
        }

        const std::array<float, 2> spanValues{11.0F, 12.0F};
        auto view = std::span{spanValues};
        copied.setData(std::move(view));
        if(!copied.stores<float>() || copied.data<float>() == spanValues.data() ||
           copied.scalarCount() != spanValues.size() || copied.data<float>()[1] != 12.0F)
        {
            std::cerr << "An rvalue span was consumed instead of copied!\n";
            return 24;
        }
    }

    //begin by setting and populating a common VField file
    VField::VField testData;
    //populate with data using random
    std::default_random_engine generator( std::chrono::duration_cast<std::chrono::milliseconds>( 
                std::chrono::system_clock::now().time_since_epoch()
            ).count() );//seeded with milliseconds from unix epoch start
    //after which pump out an array with test data
    const std::size_t Xstep{ 256 }, Ystep { 256 }, Zstep { 8 }, Pdim { 3 }; 
    const auto pCount { Xstep * Ystep * Zstep };
    auto data = new double[pCount * Pdim];
    auto irrData = new double[pCount * (Pdim +3)];
    //random distribution for the field and coord
    std::normal_distribution<double> fieldDist(0.0, 1.0);  //0.0 mean and 1.0 st. deviation
    std::uniform_real_distribution<double> coordDist (0, 100);
    //initially fill in stuff with random before sorting it out
    std::generate( data, data + pCount*Pdim, [&](){return fieldDist(generator);} );
    //and a bit more complex for irregular data
    for(std::size_t i = 0; i < pCount * (Pdim + 3); i++)
        *(irrData + i) = (i%6 < 3) ? coordDist(generator) : fieldDist(generator);
    
    auto norm = [](double *ref) //normalize tripplets of coordinated into unitary vectors
    {
        double coef {std::sqrt(ref[0] * ref[0] + ref[1] * ref[1] + ref[2] * ref[2])};
        for(int i = 0; i < 3; i++)
            ref[i] /= coef;
    };
    const double maxSteps {std::max(std::max(Xstep, Ystep), Zstep)};
    const std::array<double, 3> cCoefs {Xstep/maxSteps, Ystep/maxSteps, Zstep/maxSteps};

    //actually fill in the data
    for(std::size_t i = 0; i < pCount; i++)
    {
        norm(data + i * Pdim);
        norm(irrData + i * (Pdim + 3) + 3);
        auto normCoef = cCoefs.begin();
        for(std::size_t j = 0; j < 3; j++)
            *(irrData + i * (Pdim + 3) + j) *= *normCoef++;
    }

    //Define common header
    VField::OVFHeader commonHeader;
    commonHeader.set(VField::OVFParameter::Title, "Random VField");
    commonHeader.set(VField::OVFParameter::Desc, "really, it is random!");
    //mesh parameters
    commonHeader.set(VField::OVFParameter::Xnodes, Xstep);
    commonHeader.set(VField::OVFParameter::Ynodes, Ystep);
    commonHeader.set(VField::OVFParameter::Znodes, Zstep);
    commonHeader.set(VField::OVFParameter::Vdim, Pdim);
    //To be expanded later for features needing full header
    
    //test features requiring Weak Addressibility (ability to traverse points of internal array)
    {
        //make copies and keep originals of fields for later
        VField::VField tmpRegular(commonHeader, pCount * Pdim, data);
        VField::VField tmpIrregular(commonHeader, pCount * (Pdim + 3), irrData);
        tmpRegular.header().setMeshType(VField::MeshType::Rectangular);
        tmpIrregular.header().setMeshType(VField::MeshType::Irregular);
        //this should be enough to make both weakly addressable!
        if(!tmpRegular.isWeaklyAddressable() || !tmpIrregular.isWeaklyAddressable())
        {
            std::cerr << "Arrays unexpectedly not weakly addressable!\n";
            return 1;
        }
        if(!tmpRegular.isGridAddressable() || tmpIrregular.isGridAddressable())
        {
            std::cerr << "Grid addressability was detected incorrectly!\n";
            return 27;
        }
        auto inconsistentGrid = tmpRegular;
        inconsistentGrid.header().set(VField::OVFParameter::Xnodes, Xstep + 1);
        if(inconsistentGrid.isGridAddressable())
        {
            std::cerr << "A grid with inconsistent node counts was addressable!\n";
            return 28;
        }
        try
        {
            static_cast<void>(inconsistentGrid.gridView<double>());
            std::cerr << "gridView accepted a field rejected by isGridAddressable!\n";
            return 29;
        }
        catch(const std::logic_error&)
        {}
        //now check how well can point count be determined
        if(tmpRegular.pointDimension() != Pdim || tmpIrregular.pointDimension() != (Pdim + 3))
        {
            std::cerr << "Got unexpected point dimensions!\n";
            return 2;
        }
        if(tmpRegular.pointCount() != pCount || tmpIrregular.pointCount() != pCount)
        {
            std::cerr << "Got incorrent number of points!\n";
            return 3;
        }
        const auto constPoints = std::as_const(tmpRegular).pointView<double>();
        auto mutablePoints = tmpRegular.pointView<double>();
        if( constPoints.extent(0) != pCount || mutablePoints.extent(0) != pCount ||
            constPoints.extent(1) != Pdim || mutablePoints.extent(1) != Pdim )
        {
            std::cerr << "Iterator goes over invalid ammount of points!\n";
            return 4;
        }
        //now try to convert and see if it is still RICHTIG
        auto tmpCopy {tmpRegular};
        tmpCopy.convert<float>();
        //check if after conversion numbers are still within rounding error!
        const auto convertedPoints = std::as_const(tmpCopy).pointView<float>();
        bool conversionMatches = true;
        for (std::size_t point = 0; point < pCount && conversionMatches; ++point)
            for (std::size_t component = 0; component < Pdim; ++component)
            {
                const auto original = constPoints[point, component];
                const auto converted = convertedPoints[point, component];
                conversionMatches = std::abs(original - converted) / std::abs(original)
                    <= std::numeric_limits<float>::epsilon();
            }
        if( !conversionMatches )
        {
            std::cerr << "Conversion and/or copy failed!\n";
            return 5;
        }
        auto convertedOnAccess = tmpRegular;
        const auto convertedRaw = convertedOnAccess.rawViewAs<float>();
        if(!convertedOnAccess.stores<float>() || convertedRaw.size() != pCount * Pdim ||
           convertedRaw[1] != static_cast<float>(tmpRegular.data<double>()[1]))
        {
            std::cerr << "Conversion-on-access view failed!\n";
            return 25;
        }
        const auto copiedVector = tmpCopy.dataCopy<double>();
        if(copiedVector.size() != tmpCopy.scalarCount() ||
           copiedVector.front() != static_cast<double>(tmpCopy.data<float>()[0]))
        {
            std::cerr << "Dressed converted data copy failed!\n";
            return 26;
        }
        //now to check the exported comparison operations
        if( !tmpCopy.isSameDataAs(tmpRegular) )
        {
            std::cerr << "Built-in comparison failed!\n";
            return 6;
        }
        //check if it also detects inconsistencies
        tmpCopy.pointView<float>()[0, 0] = 42.0f;
        if(tmpCopy == tmpRegular)
        {
            std::cerr << "Failure to set a point/or failure in comparison!\n";
            return 7;
        }

        auto raw = tmpRegular.rawView<double>();
        raw[0] = 24.0;
        if (tmpRegular.data<double>()[0] != 24.0)
        {
            std::cerr << "Mutable raw data access failed!\n";
            return 8;
        }

        const auto grid = std::as_const(tmpRegular).gridView<double>();
        if (grid.extent(0) != Zstep || grid.extent(1) != Ystep ||
            grid.extent(2) != Xstep || grid.extent(3) != Pdim ||
            grid[0, 0, 0, 0] != 24.0)
        {
            std::cerr << "Grid view has unexpected extents or mapping!\n";
            return 9;
        }
    }

    //don't forget to clean up after ourselves
    delete [] data;
    delete [] irrData;
    return 0;
}
