#pragma once
//testing utilities used across parser and writer
#include<limits>
#include"OVFVersion.h"

namespace VField
{
    ///Break compilation if the float or double are not standard,
    //very sorry, the file is in binary :p
    static_assert(std::numeric_limits<double>::is_iec559, "The systems double is not IEC559 compatible");
    static_assert(std::numeric_limits<float>::is_iec559, "The systems float is not IEC559 compatible");
    static_assert(sizeof(float) == 4, "Systems 'float' has wrong number of bytes!");
    static_assert(sizeof(double) == 8, "Systems 'double' has wrong number of bytes!");
    //check if numerics are double by default
    static_assert(sizeof(1.0) == sizeof(double), "Double literals function unexpectedly");
    //and just for kicks
    static_assert(sizeof(1.0f) == sizeof(float), "Fload literals function unexpectedly");

    //test constants
    template<typename T>
    constexpr T TestVal{};

    template<> constexpr float TestVal<float> = 1234567.0f;
    template<> constexpr double TestVal<double> = 123456789012345.0;
}

