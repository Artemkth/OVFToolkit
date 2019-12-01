//File with 'constexpr' utilities for implementing compile-time parts of interfaces
//a self-checking dictionary of sorts for names of fields inside a file
//requires recent version of compilers for good implementation of function signature macros
//also requires c++17 support for lambdas being useful for constexpr initialization and such
#pragma once
#include<array>
//file with definitions for the interfaces
#include"OVFHeader.h"
//headers for the hack
#include<type_traits>
#include<limits>

//namespace with utilities for pre-compile computation
namespace DictionaryHelpers{
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
        static constexpr bool value()
        {
            //what is a good hack without a bit of preprocessor mess
            //checked to work with GCC versions of 6+ with godbolt
#if defined(__clang__) || defined(__GNUC__)
            return isValid(__PRETTY_FUNCTION__);
#elif defined(_MSC_VER)
            return isValid(__FUNCSIG__);
#else
            static_assert(false, "Trying to go round with an unsupported compiler do you?");
            return false;
#endif
        }
    };
    
    //helper template to incorporate the value
    template<VField::OVFParameter p>
    constexpr bool IsDefined_v {IsDefined<p>::value()};
    
    //a define for laters
    using intType = typename std::underlying_type<VField::OVFParameter>::type;
    
    static_assert(IsDefined<VField::OVFParameter::Invalid>::value, "The OVFParameter::Invalid was not defined!");
    //helper fold counter classm, by the power of (GRAYSKULL) C++17!
    template<
        template<intType> typename pred,//predicate which is checked when folding
        template<intType> typename fold,//fold operation
        intType n                       //counter
    > 
    struct fold_counter
    {
        static constexpr intType depth 
        { [](){
            if constexpr(pred<fold<n>::value()>::value())
                return fold_counter<pred, fold, fold<n>::value()>::depth ;
            else
                return n;
        }()
        };
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
    //predicate to check for duplicates
    template<std::size_t n>
    constexpr bool hasDuplicates(const std::array<VField::OVFParameter, n>& arr)
    {
        for(auto it = arr.begin(); it != arr.end(); it++)
        {
            auto it2 = it + 1;
            while(it2 != arr.end())
                if(*it2++ == *it)
                    return true;
        }
        return false;
    }
}

namespace VField{
    //main info structure
    using ParamInfo = 
        struct DictionaryHelpers::Helper<static_cast<DictionaryHelpers::intType>(OVFParameter::Invalid)>;
    
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
    constexpr auto FPParamList = DictionaryHelpers::make_array (
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
    constexpr auto UINTParamList = DictionaryHelpers::make_array (
            OVFParameter::Pcount,
            OVFParameter::Xnodes,
            OVFParameter::Ynodes,
            OVFParameter::Znodes
            );
    //string fields
    constexpr auto StringParamList = DictionaryHelpers::make_array (
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
    constexpr auto OtherParamList = DictionaryHelpers::make_array (
            OVFParameter::Open,
            OVFParameter::Close,
            OVFParameter::Segcnt,
            OVFParameter::Mtype,
            OVFParameter::Empty,
            OVFParameter::Unknown,
            OVFParameter::Invalid
            );

    //validate the syntaxis provided in this file
    //check if any of the arrays have duplicates
    static_assert( !DictionaryHelpers::hasDuplicates(FPParamList) &&
                   !DictionaryHelpers::hasDuplicates(UINTParamList) &&
                   !DictionaryHelpers::hasDuplicates(StringParamList) &&
                   !DictionaryHelpers::hasDuplicates(OtherParamList),
                   "One of the arrays has a duplicate!"
    );
    //check if any of the arrays intersect, in square with diagonals fashion, since intersection is non-transmissive
    //total number is C(2,4) = 6, ohboi :'(
    static_assert( !DictionaryHelpers::isIntersecting(FPParamList, UINTParamList) && 
                   !DictionaryHelpers::isIntersecting(FPParamList, StringParamList) &&
                   !DictionaryHelpers::isIntersecting(FPParamList, OtherParamList) && 
                   !DictionaryHelpers::isIntersecting(UINTParamList, StringParamList) &&
                   !DictionaryHelpers::isIntersecting(UINTParamList, OtherParamList) &&
                   !DictionaryHelpers::isIntersecting(StringParamList, OtherParamList),
                 "Some of the OVFParameter were found in more than one list!");
    //and then check if the number of arguments is correct
    static_assert( FPParamList.size() + UINTParamList.size() +
                   StringParamList.size() + OtherParamList.size() <= ParamUniverse.size(), 
                   "If you didn't define elements by normal casts, you probably have error with giving enum elements some value");
    static_assert( FPParamList.size() + UINTParamList.size() +
                   StringParamList.size() + OtherParamList.size() == ParamUniverse.size(),
                 "You have some new enum value not categorized yet" );
    //end of validation!
    
    //Definition of interfaces
    //can skip last check since there is no elements outside of the four arrays, guaranteed by assert above
    constexpr pType paramIndex(const OVFParameter& pname)
    {
        if(DictionaryHelpers::isElem(pname, FPParamList))
            return pType::Float;
        else if(DictionaryHelpers::isElem(pname, UINTParamList))
            return pType::Uint;
        else if(DictionaryHelpers::isElem(pname, StringParamList))
            return pType::String;
        return pType::Other;
    }
};
