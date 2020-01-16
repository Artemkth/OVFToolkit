#include<iostream>
#include<random>
#include<chrono> //seeding with time point
#include<algorithm>
#include<cmath>
#include<array>
#include<VField.h>

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
    commonHeader.set(VField::OVFParameter::Desc, "reall, it is random!");
    //mesh parameters
    commonHeader.set(VField::OVFParameter::Xnodes, Xstep);
    commonHeader.set(VField::OVFParameter::Ynodes, Ystep);
    commonHeader.set(VField::OVFParameter::Znodes, Zstep);

    //don't forget to clean up after ourselves
    delete [] data;
    delete [] irrData;
    return 0;
}
