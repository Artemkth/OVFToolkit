//file for implementing interfaces of 'OVFHeader.h'
#include"OVFDictionary.h"
#include<map>

namespace VField{
    //first for the field of class
    pType paramType (const OVFParameter& param)
    {
        return paramIndex(param);
    }
    
    //form a initialization list with given array of parameters and for given parameter type
    template<pType p, std::size_t size>
    constexpr auto formInsertionList(const std::array<OVFParameter, size>& list)
    {
        static_assert( p != pType::Other, "Cannot make an initialization list for pType::Other!");
        std::map<OVFParameter, HeaderField<associatedType_t<p>>> accum{};
        for (const auto& x: list)
            accum[x] = HeaderField<associatedType_t<p>> ();
        return accum;
    }
    
    //reset all fields in a map
    template<typename T>
    void resetAll(std::map<OVFParameter, HeaderField<T>>& map)
    {
        for(auto& x: map)
            x.second.reset();
    }
    
    struct OVFHeader::HeaderData
    {
        //internal storage of variables
        //map of string storing field headers
        std::map<OVFParameter, HeaderField<associatedType_t<pType::String>>> stringFields{
            formInsertionList<pType::String>(StringParamList)
        };
        //uint storing fields 
        std::map<OVFParameter, HeaderField<associatedType_t<pType::Uint>>> UINTFields{
            formInsertionList<pType::Uint>(UINTParamList)
        };
        //float storing fields
        std::map<OVFParameter, HeaderField<associatedType_t<pType::Float>>> FPFields{
            formInsertionList<pType::Float>(FPParamList)
        };
        
        //does nothing, but just to insure that there is c-tor
        HeaderData() = default;
        //method to reset all fields
        void reset()
        {
            resetAll(stringFields);
            resetAll(UINTFields);
            resetAll(FPFields);
        }
    };
    
    //Header storage default c-tor
    OVFHeader::OVFHeader()
    {
        data = new HeaderData();
    }
    //d-tor
    OVFHeader::~OVFHeader()
    {
        if(data != nullptr)
            delete data;
    }
}
