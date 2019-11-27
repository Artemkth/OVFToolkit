#include<array>
//file with definitions for the interfaces
#include"OVFHeader.h"
//headers for the hack
#include<type_traits>
#include<limits>
//some hacky comparison, searches for first occurance
//would be easier with regex or C strstr, but alas there is no constexpr way around it
constexpr bool isValid(const char* const c_str)
{
    const char* it = c_str;
    const char* init = "OVFParameter::";
    auto ref = init;
    const char* fhit = nullptr;
    while(*it != '\0' && *ref != '\0')
    {
        if(*it++ == *ref)
        {
            ref++;
            if(fhit == nullptr) fhit = it;      
        }
        else if(fhit != nullptr)    //otherwise reset search
        {
            ref = init;
            it = fhit;
            fhit = nullptr;
        }
    }
    return *ref == '\0' && *it != '\0';
}
//checks if a parameter p is inside
template<VField::OVFParameter p>
struct IsDefined
{
    static constexpr bool value(){return isValid(__PRETTY_FUNCTION__);}
};
//helper template to incorporate the value
template<VField::OVFParameter p>
constexpr bool IsDefined_v {IsDefined<p>::value()};

//a define for laters
using intType = typename std::underlying_type<VField::OVFParameter>::type;

static_assert(IsDefined<VField::OVFParameter::Invalid>::value, "The OVFParameter::Invalid was not defined!");
//helper fold counter class
//can be redone cleaner with if constexpr (C++17 required)
template
<
    template<intType> typename pred,//predicate which is checked when folding
    template<intType> typename fold,//fold operation
    intType n,                      //counter
    bool = true                     //recursion stopper
> 
struct fold_counter
{
    static constexpr intType depth 
    { (pred<fold<n>::value()>::value())? fold_counter<pred, fold, fold<n>::value(), pred<fold<n>::value()>::value()>::depth : n};
};

//recursion stopping specialization
template
<
    template<intType> typename pred,
    template<intType> typename fold,
    intType n
>
struct fold_counter<pred, fold, n, false>
{
    static constexpr intType depth = 0;
};

template <intType n>
struct SearchPred
{
    static constexpr bool value()
    {
        if(n == std::numeric_limits<intType>::min() || n == std::numeric_limits<intType>::max())
            return false;
        return IsDefined<static_cast<VField::OVFParameter>(n)>::value();
    }
};

template <intType n>
struct Increment
{
    static constexpr intType value () { return std::numeric_limits<intType>::max() != n? n+1 : n; }
};

template <intType n>
struct Decrement
{
    static constexpr intType value () { return std::numeric_limits<intType>::min() != n? n-1 : n; }
};

//main darkvoodoo structure
template< intType n>
struct Helper
{
    static_assert(IsDefined_v<static_cast<VField::OVFParameter>(n)> , "The intial value was for search is invalid!!");
    //meat of teh dish
    static constexpr intType minVal { fold_counter<SearchPred, Decrement, n>::depth };
    static constexpr intType maxVal { fold_counter<SearchPred, Increment, n>::depth };
    //size of the enum
    static constexpr auto count { static_cast<std::size_t>(maxVal - minVal + 1) };
    //casts of parameters
    static constexpr auto firstParam { static_cast<VField::OVFParameter>(minVal)};
    static constexpr auto lastParam { static_cast<VField::OVFParameter>(maxVal)};
};

//test instantiation
template struct Helper<static_cast<intType>(VField::OVFParameter::Invalid)>; 
using ParamInfo = struct Helper<static_cast<intType>(VField::OVFParameter::Invalid)>;
//大成功！

// helper make_array template function
// -> is no longer required in recent standard
// implementation inspired by gist.github.com/klmr/2775736#file-make_array-hpp
// somebody actually wrote paper on it, expect it to be there by default, proposal N3824
template <typename... T>
constexpr auto make_array(T&& ...vals)
{
    return std::array<
        typename std::decay<typename std::common_type<T...>::type>::type, sizeof...(T)> {std::forward<T>(vals)...};
} 

//predicate to check if object is element in some container
//searches if element 'value' is in the 'array'
template<std::size_t n>
constexpr bool isElem(const VField::OVFParameter& value, const std::array<VField::OVFParameter, n>& array)
{
    for(auto it = array.begin(); it != array.end(); ++it)
        if(*it == value)
            return true;
    //return false if true hasn't been tripped
    return false;
}
//predicate to check if two arrays intersect
template<std::size_t n, std::size_t m>
constexpr bool isIntersecting( const std::array<VField::OVFParameter, n>& arr1,
                               const std::array<VField::OVFParameter, m>& arr2 )
{
    //properly it would have been better to chose smaller one first, to decrease number of function calls
    //but constexpr should be inlined anyway resulting in same number of comparisons
    for(auto it = arr1.begin(); it != arr1.end(); ++it)
        if(isElem(*it, arr2))
            return true;
    //default return 'false'
    return false;
}

namespace VField{
    //actual work can be done here finally!
    // define the parameter 'universe'
    constexpr std::array<OVFParameter, ParamInfo::count> ParamUniverse{
        //lambda to fill the array out
        //WARNING: only works with c++17!
        //can be made to work with c++14 with definign a function to fill manually 
        []() -> auto {
            std::array<OVFParameter, ParamInfo::count> accumulator {};
            for(auto it = accumulator.begin(); it != accumulator.end(); ++it)
                *it = static_cast<OVFParameter>( ParamInfo::minVal + it - accumulator.begin() );
            return accumulator;
        } ()
    };
    
    //Warning: user defined syntaxis lists:
    //first floating point ones
    constexpr auto FPParamList = make_array (
            OVFParameter::Vmult,
            OVFParameter::Vmin,
            OVFParameter::Vmax,
            OVFParameter::Xmin,
            OVFParameter::Xmax,
            OVFParameter::Ymin,
            OVFParameter::Ymax,
            OVFParameter::Zmin,
            OVFParameter::Zmax,
            OVFParameter::Xbase,
            OVFParameter::Ybase,
            OVFParameter::Zbase,
            OVFParameter::Xstep,
            OVFParameter::Ystep,
            OVFParameter::Zstep
            );
    //unsigned integral fields
    constexpr auto UINTParamList = make_array (
            OVFParameter::Pcount,
            OVFParameter::Xnodes,
            OVFParameter::Ynodes,
            OVFParameter::Znodes
            );
    //string fields
    constexpr auto StringParamList = make_array (
            OVFParameter::VersionString,
            OVFParameter::Comment,
            OVFParameter::Title,
            OVFParameter::Desc,
            OVFParameter::Munit,
            OVFParameter::Vunit,
            OVFParameter::Vdim,
            OVFParameter::Vlabels,
            OVFParameter::Bound
            );
    //other fields
    constexpr auto OtherParamList = make_array (
            OVFParameter::Open,
            OVFParameter::Close,
            OVFParameter::Segcnt,
            OVFParameter::Mtype,
            OVFParameter::Empty,
            OVFParameter::Unknown,
            OVFParameter::Invalid
            );

    //validate the syntaxis provided in this file
    //first, check if any of the arrays intersect, in square with diagonals fashion, since intersection is non-transmissive
    //total number is C(2,4) = 6, ohboi :'(
    static_assert( !isIntersecting(FPParamList, UINTParamList) && !isIntersecting(FPParamList, StringParamList) &&
            /*&&*/ !isIntersecting(FPParamList, OtherParamList) && !isIntersecting(UINTParamList, StringParamList) &&
            /*&&*/ !isIntersecting(UINTParamList, OtherParamList) && !isIntersecting(StringParamList, OtherParamList),
                 "Some of the OVFParameter were found in more than one list!");
    //and then check if the number of arguments is correct
    static_assert( FPParamList.size() + UINTParamList.size() +
                   StringParamList.size() + OtherParamList.size() <= ParamUniverse.size(),
                 "If you didn't define elements by normal casts, you probably have error with giving enum elements some value");
    static_assert( FPParamList.size() + UINTParamList.size() +
                   StringParamList.size() + OtherParamList.size() == ParamUniverse.size(),
                 "You have some new enum value not categorized yet" );
    //end of validation!
}

