//file for implementing interfaces of 'OVFHeader.h'
#include"OVFDictionary.h"
#include<map>
#include<exception>

namespace VField{
    
    //Class for a field, needs to scream when it is set multiple times, or being accessed unitialized
    //just an intermediate container to keep things clean
    //has to have everything defined in header since it is template class
    template<typename T>
    struct HeaderField{
    private:
        //internal telling if the value was set
        bool Set{false};
        //value storage, initialized using default constructor when aplicable
        T value{};
        
    public:
        //can be assigned a value
        HeaderField operator=(const T& ref)
        {
            if(Set)
                throw std::logic_error("HeaderField: Trying to reinitialise the field without resetting");
            
            value = ref;
            Set = true;
            
            return *this;
        }
        //or constructed with one
        constexpr HeaderField(const T& ref):value(ref), Set(true) {}
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
                throw std::logic_error("HeaderField: Reading unitialized field");
            
            return value;
        }
        
        constexpr T getValue() const
        {
            if(!Set)
                throw std::logic_error("HeaderField: Reading unitialized field");
            
            return value;
        }
        
        constexpr bool operator==(const T& ref) const
        {
            if(!Set)
                throw std::logic_error("HeaderField: Trying to compare with unitialized field");
            
            return value == ref;
        }
        
        constexpr bool operator==(const HeaderField& ref) const
        {
            if(!Set)
                throw std::logic_error("HeaderField: Trying to compare with unitialized field");
            
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
        //can be assigned a value
        HeaderField operator=(const T* ref)
        {
            if(Set)
                throw std::logic_error("HeaderField: Trying to reinitialise the field without resetting");
            
            if(value != nullptr)
                delete value;
            value = copy(ref);
            Set = true;
            
            return *this;
        }
        //or constructed with one
        HeaderField(const T* ref): Set(true) { value = copy(ref); }
        //otherwise start not set
        constexpr HeaderField() = default;
        //d-tor
        ~HeaderField() {if(value != nullptr) delete value;}
        //copy c-tor
        HeaderField(const HeaderField& ref):Set(ref.Set) {value = copy(ref.value);}
        HeaderField operator= (const HeaderField& ref)
        {
            Set = ref.Set;
            if(value != nullptr)
                delete value;
            value = copy(ref.value);
            return *this;
        }
        //and move c-tor
        HeaderField(HeaderField&& ref):Set(ref.Set), value(ref.value)
        { ref.value = nullptr; }
        HeaderField operator= (HeaderField&& ref)
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
                throw std::logic_error("HeaderField: Reading unitialized field");
            
            return value;
        }
        
        constexpr T* getValue() const
        {
            if(!Set)
                throw std::logic_error("HeaderField: Reading unitialized field");
            
            return value;
        }
        
        constexpr bool operator==(const T* ref) const
        {
            if(!Set)
                throw std::logic_error("HeaderField: Trying to compare with unitialized field");
            
            auto tmp = value;
            while(*tmp != '\0' && *ref != '\0')
                if(*tmp++ != *ref++)
                    return false;
                        
            return *tmp == '\0' && *ref == '\0';
        }
        
        constexpr bool operator==(const HeaderField& ref)
        {
            if(!Set)
                throw std::logic_error("HeaderField: Trying to compare with unitialized field");
            
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
