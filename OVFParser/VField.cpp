#include<algorithm>
#include<type_traits>
#include"VField.h"

namespace VField{
    //asserting floating point type byte sizes to be compatible with those defined in OVF file standard
    static_assert(sizeof(float) == 4, "Incompatible float type");
    static_assert(sizeof(double) == 8, "Incompatible double type");
        
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
            if(farray != nullptr)
            {
                delete[] farray;
                farray = nullptr;
            }
            if(darray != nullptr)
            {
                delete[] darray;
                darray = nullptr;
            }
        }
        //is there some data?
        inline bool isEmpty() const
        {
            return farray == nullptr && darray == nullptr;
        }
        //c-tors
        StorageArray() = default; //fine with initializing everything to nullptr
        StorageArray(const StorageArray& ref):StorageArray()
        {
            storSize = ref.storSize;
            if( storSize != 0)
            {
                //farray and darray are set to nullptr by default constructor
                //weekly exception safe, if needed add unique_ptr code later
                if( ref.farray != nullptr)
                {
                    auto ndata = new float[storSize];
                    std::copy_n(ref.farray, storSize, ndata);
                    
                    farray = ndata;
                }
                if( ref.darray != nullptr)
                {
                    auto ndata = new double[storSize];
                    std::copy_n(ref.darray, storSize, ndata);
                    
                    darray = ndata;
                }
            }
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
            if ( storSize != 0)
            {
                if constexpr(std::is_same<double, T>::value)
                    darray = data;
                else
                {
                    auto buffer = new double[storSize];
                    std::copy_n( data, storSize, buffer);
                    darray = buffer;
                    delete[] data;
                }
            }
        }
        
        StorageArray& operator= (const StorageArray& ref)
        {
            if (storSize != 0)
            {
                if( ref.farray != nullptr)
                {
                    auto ndata = new float[storSize];
                    std::copy_n(ref.farray, storSize, ndata);
                    std::swap(farray, ndata);
                    
                    if( ndata != nullptr)
                        delete[] ndata;
                }
                if( ref.darray != nullptr)
                {
                    auto ndata = new double[storSize];
                    std::copy_n(ref.darray, storSize, ndata);
                    std::swap(darray, ndata);
                    
                    if( ndata != nullptr)
                        delete[] ndata;
                }
            }
            return *this;
        }
        StorageArray(StorageArray&& ref): StorageArray()
        {
            storSize = ref.storSize;
            if(ref.farray != nullptr)
            {
                farray = ref.farray;
                ref.farray = nullptr;
            }
            if(ref.darray != nullptr)
            {
                darray = ref.darray;
                ref.darray = nullptr;
            }
        }
        StorageArray& operator= (StorageArray&& ref)
        {
            clear();
            storSize = ref.storSize;
            if(ref.farray != nullptr)
            {
                farray = ref.farray;
                ref.farray = nullptr;
            }
            if(ref.darray != nullptr)
            {
                darray = ref.darray;
                ref.darray = nullptr;
            }
            return *this;
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
    
    //specialization for floats
    template<>
    VField::StorageArray::StorageArray<float>(float* data, const std::size_t& length): StorageArray()
    {
        storSize = length;
        if (data == nullptr)
            return;
        
        farray = data;
    }
    
    void VField::StorageArray::convert()
    {
        //hurray if empty, nothing to do
        if(isEmpty())
            return;
        //else start doing work
        if(farray != nullptr)
        {
            auto buffer = new double[storSize];
            std::copy_n(farray, storSize, buffer);
            
            *this = std::move(StorageArray(buffer, storSize));    
        }
        if(darray != nullptr)
        {
            auto buffer = new float[storSize];
            std::copy_n(darray, storSize, buffer);
            
            *this = std::move(StorageArray(buffer, storSize));
        }
        //CAUTION: both being non-null would result in memory leak
        //TODO: insert a c assert here
    }
    
    std::size_t VField::curDataInternalSize() const
    {
        if( data -> farray != nullptr)
            return sizeof(float);
        if( data -> darray != nullptr)
            return sizeof(double);
        throw OVFHeader::read_unitialized("VField::curDataInternalSize: unitialized");
    }
    std::size_t VField::curDataPoints() const
    {
        return data -> storSize;
    }
    
    bool VField::isDataPresent() const
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
    
    void VField::clearData()
    {
        data -> clear();
    }
    
    //data copy template
    template<typename T>
    T* VField::StorageArray::makeCopy() const
    {
        static_assert(std::is_floating_point<T>::value, "StorageArray::makeCopy is only compatible with floating point type");
        if(isEmpty())
            throw OVFHeader::read_unitialized("VField::getDataCopy: Trying to read non-initialized data");
        T* buffer = new T[storSize];
        if(farray != nullptr)
            std::copy_n( farray, storSize, buffer);
        if(darray != nullptr)
            std::copy_n( farray, storSize, buffer);
        
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
        if(data -> isEmpty())
            throw OVFHeader::read_unitialized("VField::getData: Trying to read non-initialized data");
        if(data -> farray == nullptr)
            throw OVFHeader::read_unitialized("VField::getData<float>: trying to read the data in wrong type");
        return data -> farray;
    }
    template<>
    const double* VField::getData<double>() const
    {
        if(data -> isEmpty())
            throw OVFHeader::read_unitialized("VField::getData: Trying to read non-initialized data");
        if(data -> farray == nullptr)
            throw OVFHeader::read_unitialized("VField::getData<double>: trying to read the data in wrong type");
        return data -> darray;
    }
    
    //setters
    void VField::setData(float* arr, const std::size_t& size)
    {
        *data = std::move(StorageArray(arr, size));
    }
    void VField::setData(double* arr, const std::size_t& size)
    {
        *data = std::move(StorageArray(arr, size));
    }
    void VField::setData(const float* arr, const std::size_t& size)
    {
        auto buffer = new float[size];
        std::copy_n( arr, size, buffer);
        *data = std::move(StorageArray(buffer, size));
    }
    void VField::setData(const double* arr, const std::size_t& size)
    {
        auto buffer = new double[size];
        std::copy_n( arr, size, buffer);
        *data = std::move(StorageArray(buffer, size));
    }
    
    //point set methods
    template<typename T, typename U>
    constexpr void conv_assign(T* arr, const std::size_t& pos, const U& val)
    {
        static_assert(std::is_floating_point_v<T> && std::is_floating_point_v<U>, "conv_assign called with a non floating point type");
        arr[pos] = static_cast<T>(val);
    }
    //here we go
    //look into templating this stuff
    void VField::setPoint(const std::size_t& pos, const float& val)
    {
        if( data -> isEmpty() )
            throw OVFHeader::read_unitialized("VField::setPoint: trying to change a point of non-initialized vector field");
        if( pos >= data -> storSize )
            throw std::logic_error("VField::setPoint: accessing field out of array bounds");
        
        if( data -> farray != nullptr)
            conv_assign(data -> farray, pos, val);
        if( data -> darray != nullptr)
            conv_assign(data -> darray, pos, val);
    }
    void VField::setPoint(const std::size_t& pos, const double& val)
    {
        if( data -> isEmpty() )
            throw OVFHeader::read_unitialized("VField::setPoint: trying to change a point of non-initialized vector field");
        if( pos >= data -> storSize )
            throw std::logic_error("VField::setPoint: accessing field out of array bounds");
        
        if( data -> farray != nullptr)
            conv_assign(data -> farray, pos, val);
        if( data -> darray != nullptr)
            conv_assign(data -> darray, pos, val);
    }
    //constructors and such again
    VField::VField(const VField& ref): Header(ref.Header)
    {
        auto buffer = new StorageArray(*ref.data);
        std::swap(data, buffer);
        
        if(buffer != nullptr)
            delete buffer;
    }
    VField& VField::operator= (const VField& ref)
    {
        auto buffer = new StorageArray(*ref.data);
        std::swap(data, buffer);
        
        if(buffer != nullptr)
            delete buffer;
        
        return *this;
    }
    VField& VField::operator= (VField&& ref)
    {
        std::swap(data, ref.data);
        //ref.data will be cleaned up by ref's destructor
        Header = std::move(ref.Header);
        
        return *this;
    }
}
