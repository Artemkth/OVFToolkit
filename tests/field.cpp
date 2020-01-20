#include<iostream>
#include<random>
#include<chrono> //seeding with time point
#include<algorithm>
#include<cmath>
#include<array>
#include<VField.h>
#include<limits>

int main()
{
    //begin by setting and populating a common VField file
    VField::VField testData("# OOMMF OVF 2.0");
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
    std::generate( data, data + (pCount + 1)*Pdim, [&](){return fieldDist(generator);} );
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
    VField::OVFHeader commonHeader("# OOMMF OVF 2.0");
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
        VField::VField tmpRegular(commonHeader, pCount * Pdim, const_cast<const double*>(data));
        VField::VField tmpIrregular(commonHeader, pCount * (Pdim + 3), const_cast<const double*>(irrData));
        tmpRegular.Header.setMesh(VField::OVFHeader::MeshType::rectangular);
        tmpIrregular.Header.setMesh(VField::OVFHeader::MeshType::irregular);
        //this should be enough to make both weakly addressable!
        if(!tmpRegular.isWeaklyAddressable() || !tmpIrregular.isWeaklyAddressable())
        {
            std::cerr << "Arrays unexpectedly not weakly addressable!\n";
            return 1;
        }
        //now check how well can point count be determined
        if(tmpRegular.pntDimension() != Pdim || tmpIrregular.pntDimension() != (Pdim + 3))
        {
            std::cerr << "Got unexpected point dimensions!\n";
            return 2;
        }
        if(tmpRegular.pntCount() != pCount || tmpIrregular.pntCount() != pCount)
        {
            std::cerr << "Got incorrent number of points!\n";
            return 3;
        }
        if( std::distance(tmpRegular.cbegin<double>(), tmpRegular.cend<double>()) != pCount ||
            std::distance(tmpRegular.begin<double>(), tmpRegular.end<double>()) != pCount )
        {
            std::cerr << "Iterator goes over invalid ammount of points!\n";
            return 4;
        }
        //now try to convert and see if it is still RICHTIG
        auto tmpCopy {tmpRegular};
        tmpCopy.convert<float>();
        //check if after conversion numbers are still within rounding error!
        if( !std::equal( tmpCopy.cbegin<float>(), tmpCopy.cend<float>(), tmpRegular.cbegin<double>(),
            [&tmpCopy] (const float* arr1, const double* arr2){
                return std::equal(arr1, arr1 + tmpCopy.pntDimension(), arr2,
                     [](const float& a, const double& b){return std::abs(b-a)/std::abs(b) <= std::numeric_limits<float>::epsilon(); });
            }) )
        {
            std::cerr << "Conversion and/or copy failed!\n";
            return 5;
        }
        //now to check the exported comparison operations
        if( !tmpCopy.isSameDataAs(tmpRegular) )
        {
            std::cerr << "Built-in comparison failed!\n";
            return 6;
        }
        //check if it also detects inconsistencies
        tmpCopy.setPoint(pCount, 42.0f);
        if(tmpCopy == tmpRegular)
        {
            std::cerr << "Failure to set a point/or failure in comparison!\n";
            return 7;
        }
    }

    //don't forget to clean up after ourselves
    delete [] data;
    delete [] irrData;
    return 0;
}
