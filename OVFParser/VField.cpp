#include<algorithm>
#include<type_traits>
#include<limits>
#include<cmath>
#if defined(_MSC_VER)
#include<stdexcept> //workaround for missing logic_error
#endif
#include"VField.h"
#include<optional>
#include<variant>

namespace VField{
    //asserting floating point type byte sizes to be compatible with those defined in OVF file standard
    static_assert(sizeof(float) == 4, "Incompatible float type");
    static_assert(sizeof(double) == 8, "Incompatible double type");
    
    template<typename T, typename U>
    inline void emplace_copy(T** dest, const U* data, const std::size_t size)
    {
        //if copy size is 0 cleanup destination and set pointer to nullptr
        if(size == 0)
        {
            delete[] *dest;
            *dest = nullptr;
            return;
        }
        static_assert(std::is_convertible<U,T>::value, "Trying to do the conversion of incompatible types!");
        T* buffer = new T[size];
        std::copy_n(data, size, buffer);

        std::swap(*dest, buffer);
        //delete old data now stored in buffer
        delete[] buffer;
    }

    template<typename T, typename U>
    inline bool cmpFloatArr(const T* arr1, const U* arr2, std::size_t size)
    {
        static_assert( std::is_floating_point<T>::value && std::is_floating_point<U>::value,
                "Comparing floats is only allowed for floating point arithmetic types!" );
        //if types are the same just compare by value
        if constexpr( std::is_same<T, U>::value )
            return std::equal(arr1, arr1 + size, arr2);
        //else need to find least and most accurate types
        using precision_type = typename std::conditional< (sizeof(T) > sizeof(U)), T, U >::type;
        constexpr precision_type epsilon { std::numeric_limits<typename std::conditional<sizeof(T) < sizeof(U), T, U>::type>::epsilon() };
        constexpr precision_type min_val { 10 * std::numeric_limits<typename std::conditional<sizeof(T) < sizeof(U), T, U>::type>::min() };
        //and compare giving allowance for maximum of epsilon discrepancy
        //TODO: check later if you need to cast both v1 and v2 to precision_type
        return std::equal( arr1, arr1 + size, arr2,
                [] (const T& v1, const U& v2) { return v1 != 0.0? std::abs(v1 - v2)/std::abs(v1) <= epsilon : std::abs(v2) <= min_val; } );
    }
        
    struct VField::StorageArray
    {
        //data size
        std::size_t storSize {0};
        //data storage
        float* farray {nullptr};
        double* darray {nullptr};
        //purgin the data
        inline void clear()
        {
            delete[] farray;
            farray = nullptr;
            delete[] darray;
            darray = nullptr;
        }
        //is there some data?
        inline bool isEmpty() const
        {
            return farray == nullptr && darray == nullptr;
        }
        //c-tors
        constexpr StorageArray() = default; //fine with initializing everything to nullptr
        StorageArray(const StorageArray& ref):StorageArray()
        {
            storSize = ref.storSize;
            //farray and darray are set to nullptr by default constructor
            //weekly exception safe, if needed add unique_ptr code later
            if( ref.farray != nullptr)
                emplace_copy(&farray, ref.farray, storSize);
            if( ref.darray != nullptr)
                emplace_copy(&darray, ref.darray, storSize);
        }
        //conversion constructors, eat up the pointer
        template<typename T>
        explicit StorageArray(T* data, const std::size_t& length): StorageArray()
        {
            storSize = length;
            static_assert(std::is_floating_point<T>::value, "StorageArray: constructed from a non-floating point argument!");
            if (data == nullptr)
                return;
            
            //default assume it is 'double' value, casting to double
            if ( storSize != 0 )
            {
                if constexpr (std::is_same<double, T>::value)
                    darray = data;
                else if constexpr (std::is_same<float, T>::value)
                    farray = data;
                else
                {
                    //else convert data to 'double' and nuke it!
                    emplace_copy(&darray, data, length);
                    delete[] data;
                }
            }
        }
        
        StorageArray& operator= (const StorageArray& ref)
        {
            if( ref.farray != nullptr)
                emplace_copy(&farray, ref.farray, ref.storSize);
            if( ref.darray != nullptr)
                emplace_copy(&darray, ref.darray, ref.storSize);
            //only copies new size if emplacing a copy was succesfull
            storSize = ref.storSize;
            return *this;
        }
        StorageArray(StorageArray&& ref)
        {
            std::swap(storSize, ref.storSize);
            std::swap(farray, ref.farray);
            std::swap(darray, ref.darray);
        }
        StorageArray& operator= (StorageArray&& ref)
        {
            std::swap(storSize, ref.storSize);
            std::swap(farray, ref.farray);
            std::swap(darray, ref.darray);
            return *this;
        }
        //and comparison for data
        bool operator==(const StorageArray& ref)
        {
            //first check if either of containers are empty, 
            //return true if both are empty
            if(isEmpty() || ref.isEmpty())
                return isEmpty() && ref.isEmpty();
            if(storSize != ref.storSize)
                return false;
            //else by-value comparison needs to be done
            //TODO: try to template following out
            if(farray != nullptr)
            {
                if(ref.farray != nullptr)
                    return cmpFloatArr(farray, ref.farray, storSize);
                else
                    return cmpFloatArr(farray, ref.darray, storSize);
            }
            else
            {
                if(ref.farray != nullptr)
                    return cmpFloatArr(darray, ref.farray, storSize);
                else
                    return cmpFloatArr(darray, ref.darray, storSize);
            }
        }

        ~StorageArray()
        {
            clear();
        }
        //also a convert method to swap between representations
        inline void convert();

        template<typename T>
        inline T* makeCopy() const;
    };

    void VField::StorageArray::convert()
    {
        //hurray if empty, nothing to do
        if(isEmpty())
            return;
        //else start doing work
        if(farray != nullptr)
        {
            auto buffer = makeCopy<double>();

            *this = std::move(StorageArray(buffer, storSize));    
        }
        if(darray != nullptr)
        {
            auto buffer = makeCopy<float>();

            *this = std::move(StorageArray(buffer, storSize));
        }
    }
    
    //outside conversion
    template<> void VField::convert<float>()
    {
        if(!isDataPresent() || data->farray != nullptr)
            return;
        data->convert();
    }
    template<> void VField::convert<double>()
    {
        if(!isDataPresent() || data->darray != nullptr)
            return;
        data->convert();
    }

    std::size_t VField::curDataInternalSize() const noexcept
    {
        if( data -> farray != nullptr)
            return sizeof(float);
        if( data -> darray != nullptr)
            return sizeof(double);
        return 0;
    }
    std::size_t VField::curDataPoints() const noexcept
    {
        return data -> storSize;
    }

    bool VField::isDataPresent() const noexcept
    {
        return !( data -> isEmpty() );
    }

    //ctors
    VField::VField()
    {
        data = new StorageArray();
    }
    VField::~VField()
    {
        delete data;
    }
    
    void VField::clearData() noexcept
    {
        data -> clear();
    }
    
    //data copy template
    template<typename T>
    T* VField::StorageArray::makeCopy() const
    {
        static_assert(std::is_floating_point<T>::value, "StorageArray::makeCopy is only compatible with floating point type");
        if(isEmpty())
            return nullptr;
        T* buffer = new T[storSize];
        if(farray != nullptr)
            std::copy_n( farray, storSize, buffer);
        else if(darray != nullptr)
            std::copy_n( darray, storSize, buffer);
        
        return buffer;
    }
    
    //data access methods
    template<>
    float* VField::getDataCopy<float>() const
    {
        return data -> makeCopy<float>();
    }
    template<>
    double* VField::getDataCopy<double>() const
    {
        return data -> makeCopy<double>();
    }
    
    //and then getting the internal fields
    template<>
    const float* VField::getData<float>() const
    {
        if(data -> farray == nullptr && data -> darray != nullptr)
            throw std::logic_error("VField::getData<float>: trying to read the data in wrong type");
        return data -> farray;
    }
    template<>
    const double* VField::getData<double>() const
    {
        if(data -> farray == nullptr && data -> darray !=nullptr)
            throw std::logic_error("VField::getData<double>: trying to read the data in wrong type");
        return data -> darray;
    }
    
    //setters
    void VField::setData(float* arr, std::size_t size) noexcept
    {
        *data = std::move(StorageArray(arr, size));
    }
    void VField::setData(double* arr, std::size_t size) noexcept
    {
        *data = std::move(StorageArray(arr, size));
    }
    void VField::setData(const float* arr, std::size_t size)
    {
        auto buffer = new float[size];
        std::copy_n( arr, size, buffer);
        *data = std::move(StorageArray(buffer, size));
    }
    void VField::setData(const double* arr, std::size_t size)
    {
        auto buffer = new double[size];
        std::copy_n( arr, size, buffer);
        *data = std::move(StorageArray(buffer, size));
    }
    
    //point set methods
    template<typename T, typename U>
    constexpr void conv_assign(T* arr, const std::size_t& pos, const U& val)
    {
        static_assert(std::is_convertible<U, T>::value, "conv_assign called with a non-convertible argument");
        arr[pos] = static_cast<T>(val);
    }
    //here we go
    //look into templating this stuff
    bool VField::setPoint(std::size_t pos, const float& val)
    {
        if( data -> isEmpty() || pos >= data -> storSize )
            return false;
        
        if( data -> farray != nullptr)
            conv_assign(data -> farray, pos, val);
        else if( data -> darray != nullptr)
            conv_assign(data -> darray, pos, val);
        return true;
    }
    bool VField::setPoint(std::size_t pos, const double& val)
    {
        if( data -> isEmpty() || pos >= data -> storSize )
            return false;
        
        if( data -> farray != nullptr)
            conv_assign(data -> farray, pos, val);
        else if( data -> darray != nullptr)
            conv_assign(data -> darray, pos, val);
        return true;
    }
    //constructors and such again
    VField::VField(const VField& ref): Header(ref.Header)
    {
        auto buffer = new StorageArray(*ref.data);
        std::swap(data, buffer);
        
        delete buffer;
    }
    VField& VField::operator= (const VField& ref)
    {
        auto buffer = new StorageArray(*ref.data);
        std::swap(data, buffer);
        
        delete buffer;
        Header = ref.Header;
        return *this;
    }

    //comparison operations
    bool VField::isSameDataAs(const VField& ref) const noexcept
    { return *data == *ref.data; }
    bool VField::operator==(const VField& ref) const noexcept
    { return Header == ref.Header && isSameDataAs(ref); }

    //implementation of iterator creators
    //TODO: investigate why templating here fails to export!
    template<> VField::VFieldIterator<float> VField::begin<float> ()
    {
        if(!isWeaklyAddressable())
            throw std::logic_error("VField::begin<T>(): Cannot make iterator for non-addressable data");
        if( data -> farray == nullptr )
            throw std::logic_error("VField::begin<T>(): Trying to access wrong type");

        //initialize a base class, and use private access to it to form a result
        CommonVFieldIterator<float> res;
        res.parent = this;
        res.pntDimension = pntDimension();
        res.it_data = data -> farray;
        return res;
    }
    template<> VField::VFieldIterator<double> VField::begin<double> ()
    {
        if(!isWeaklyAddressable())
            throw std::logic_error("VField::begin<T>(): Cannot make iterator for non-addressable data");
        if( data -> darray == nullptr )
            throw std::logic_error("VField::begin<T>(): Trying to access wrong type");

        //initialize a base class, and use private access to it to form a result
        CommonVFieldIterator<double> res;
        res.parent = this;
        res.pntDimension = pntDimension();
        res.it_data = data -> darray;
        return res;
    }
    template<> VField::VFieldIterator<float> VField::end<float> ()
    {
        if(!isWeaklyAddressable())
            throw std::logic_error("VField::begin<T>(): Cannot make iterator for non-addressable data");
        if( data -> farray == nullptr )
            throw std::logic_error("VField::begin<T>(): Trying to access wrong type");

        //initialize a base class, and use private access to it to form a result
        CommonVFieldIterator<float> res;
        res.parent = this;
        res.pntDimension = pntDimension();
        res.it_data = data->farray + data->storSize;
        return res;
    }
    template<> VField::VFieldIterator<double> VField::end<double> ()
    {
        if(!isWeaklyAddressable())
            throw std::logic_error("VField::begin<T>(): Cannot make iterator for non-addressable data");
        if( data -> darray == nullptr )
            throw std::logic_error("VField::begin<T>(): Trying to access wrong type");

        //initialize a base class, and use private access to it to form a result
        CommonVFieldIterator<double> res;
        res.parent = this;
        res.pntDimension = pntDimension();
        res.it_data = data->darray + data->storSize;
        return res;
    }
}

