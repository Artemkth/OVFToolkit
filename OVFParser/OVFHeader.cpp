//file for implementing interfaces of 'OVFHeader.h'
#include"OVFDictionary.h"
#include<map>
#include<algorithm>

namespace VField{
    //Class for a field, needs to scream when it is set multiple times, or being accessed unitialized
    //just an intermediate container to keep things clean
    //has to have everything defined in header since it is template class
    //has a lot of unused members now, but whatever
    template<typename T>
    struct HeaderField{
    private:
        //internal telling if the value was set
        bool Set{false};
        //value storage, initialized using default constructor when aplicable
        T value{};
        
    public:
        //can be assigned a value
        HeaderField& operator=(const T& ref)
        {
            if(Set)
                throw OVFHeader::overwrite_initialized("HeaderField::operator=: Trying to reinitialise the field without resetting");
            
            value = ref;
            Set = true;
            
            return *this;
        }
        //or constructed with one
        constexpr explicit HeaderField(const T& ref):value(ref), Set(true) {}
        //otherwise start not set
        constexpr HeaderField() = default;
        
        //set status getter
        constexpr bool IsSet() const
        {
            return Set;
        }
        //conversion back to T, like when getting the value back
        constexpr operator T () const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::operator T: Reading unitialized field");
            
            return value;
        }
        
        constexpr T getValue() const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::getValue; Reading unitialized field");
            
            return value;
        }
        
        constexpr bool operator==(const T& ref) const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::operator==: Trying to compare with unitialized field");
            
            return value == ref;
        }
        
        constexpr bool operator==(const HeaderField& ref) const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::operator==: Trying to compare with unitialized field");
            
            //will do the throw automatically from the function above
            return ref == this->value;
        }
        
        constexpr void reset()
        {
            Set = false;
        }
    };
    
    //specialization for pointer types
    //CAUTION: only for handling null terminated strings!
    template<typename T>
    struct HeaderField<T*>{
    private:
        //internal telling if the value was set
        bool Set{false};
        //value storage, initialized using default constructor when aplicable
        T* value{nullptr};
        static T* copy(const T* ref)
        {
            std::size_t size {0};
            //count number of elements
            //will trip segfaul before overflow if non-nullterminated string was passed
            for( auto it = ref; *it != '\0'; ++it)
                ++size;
            //size is size without a termination character
            //create a buffer, new throws exceptions automatically
            auto buffer = new T[size + 1];
    
            auto it = buffer; //pointer to first element
            while ( (*it++ = *ref++) != '\0');
            
            return buffer;
        }
        
    public:
        //can be assigned a value, also allows for implicit conversions
        HeaderField& operator=(const T* ref)
        {
            if(Set)
                throw OVFHeader::overwrite_initialized("HeaderField::operator=: Trying to reinitialise the field without resetting");
            //doing it exception safe
            auto new_value = copy(ref);
            std::swap(value, new_value);
            Set = true;
            
            if(new_value != nullptr)
                delete[] new_value;
            
            return *this;
        }
        //or constructed with one
        explicit HeaderField(const T* ref): Set(true) { value = copy(ref); }
        //otherwise start not set
        constexpr HeaderField() = default;
        //d-tor
        ~HeaderField() {if(value != nullptr) delete[] value;}
        //copy c-tor
        HeaderField(const HeaderField& ref):Set(ref.Set) {value = copy(ref.value);}
        HeaderField& operator= (const HeaderField& ref)
        {
            //done in self-assignment safe fashion
            //in case of self assignment will just waste time doing another copy and deleting
            T* new_val {nullptr };
            if(ref.Set)
                new_val = copy(ref.value);
            std::swap(value, new_val);
            Set = ref.Set;
            if(new_val != nullptr)
                delete[] new_val;
            
            return *this;
        }
        //and move c-tor
        HeaderField(HeaderField&& ref):Set(ref.Set), value(ref.value)
        { ref.value = nullptr; }
        HeaderField& operator= (HeaderField&& ref)
        {
            Set = ref.Set;
            value = ref.value;
            ref.value = nullptr;
            return *this;
        }
        
        //set status getter
        constexpr bool IsSet() const
        {
            return Set;
        }
        //conversion back to T, like when getting the value back
        constexpr operator T* () const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::operator T: Reading unitialized field");
            
            return value;
        }
        
        constexpr T* getValue() const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::getValue; Reading unitialized field");
            
            return value;
        }
        
        constexpr bool operator==(const T* ref) const
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::operator==: Trying to compare with unitialized field");
            
            auto tmp = value;
            while(*tmp != '\0' && *ref != '\0')
                if(*tmp++ != *ref++)
                    return false;
                        
            return *tmp == '\0' && *ref == '\0';
        }
        
        constexpr bool operator==(const HeaderField& ref)
        {
            if(!Set)
                throw OVFHeader::read_unitialized("HeaderField::operator==: Trying to compare with unitialized field");
            
            return ref == value;
        }
        
        constexpr void reset()
        {
            Set = false;
        }
    };
    
    
    
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
        std::map<OVFParameter, HeaderField<associatedType_t<pType::String>>> StringFields{
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
        //mesh type
        HeaderField<MeshType> meshType{};
        
        //does nothing, but just to insure that there is c-tor
        HeaderData() = default;
        HeaderData(const HeaderData&) = default;
        //method to reset all fields
        void reset()
        {
            resetAll(StringFields);
            resetAll(UINTFields);
            resetAll(FPFields);
            meshType.reset();
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
    //copy c-tor
    OVFHeader::OVFHeader(const OVFHeader& ref)
    {
        data = new HeaderData();
        *data = *(ref.data);
    }
    OVFHeader& OVFHeader::operator= (const OVFHeader& ref)
    {
        auto data_copy = new HeaderData(*ref.data);
        std::swap(data, data_copy);
        //impossible with a program flow, but doing it just in case
        if(data_copy != nullptr)
            delete data_copy;
        
        return *this;
    }
    //setters
    void OVFHeader::set(const OVFParameter& param, const associatedType_t<pType::String>& val)
    {
        if(paramType(param) != pType::String)
            throw OVFHeader::wrong_type_request("OVFHeader::set called string setter with for a non-string parameter");
        data->StringFields[param] = val;
    }
    void OVFHeader::set(const OVFParameter& param, const associatedType_t<pType::Uint>& val)
    {
        if(paramType(param) != pType::Uint)
            throw OVFHeader::wrong_type_request("OVFHeader::set called string setter with for a non-UINT parameter");
        data->UINTFields[param] = val;
    }
    void OVFHeader::set(const OVFParameter& param, const associatedType_t<pType::Float>& val)
    {
        if(paramType(param) != pType::Float)
            throw OVFHeader::wrong_type_request("OVFHeader::set called string setter with for a non-floating point parameter");
        data->FPFields[param] = val;
    }
    //check if a given field is set
    bool OVFHeader::isSet(const OVFParameter& refP) const
    {
        switch(paramIndex(refP))
        {
            case(pType::String):
                return data->StringFields[refP].IsSet();
                break;
            case(pType::Uint):
                return data->UINTFields[refP].IsSet();
                break;
            case(pType::Float):
                return data->FPFields[refP].IsSet();
                break;
            case(pType::Other):
                if(refP == OVFParameter::Mtype)
                {
                    return data->meshType.IsSet();
                }
            default:
                throw OVFHeader::wrong_type_request("OVFHeader::isSet: requested a 'Other' type param status.a");
        }
        //just to silence the compiler
        //lambs are silent now
        return false;
    }
    
    //mesh type functions
    OVFHeader::MeshType OVFHeader::getMeshType() const
    {
        return data->meshType;
    }
    void OVFHeader::setMesh(const MeshType& ref)
    {
        data->meshType = ref;
    }
    //reset function
    void OVFHeader::reset()
    {data->reset();};
}

