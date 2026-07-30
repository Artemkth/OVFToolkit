//VField memory management header
#include<algorithm>
#include <memory>
#include<type_traits>
#include<limits>
#include<array>
#include<cassert>
#include<cmath>
#include<utility>
#if defined(_MSC_VER)
#include<stdexcept> //workaround for missing logic_error
#endif
#include"VField.h"
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
            *dest = nullptr;
            return;
        }
        static_assert(std::is_convertible<U,T>::value, "Trying to do the conversion of incompatible types!");
        T* buffer = new T[size];
        std::copy_n(data, size, buffer);

        *dest = buffer;
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
                [&] (const T& v1, const U& v2) { return v1 != 0.0? std::abs(v1 - v2)/std::abs(v1) <= epsilon : std::abs(v2) <= min_val; } );
    }

    struct VField::StorageArray
    {
        //data size
        std::size_t storSize {0};
        //data storage
        std::variant<
          std::monostate,
          std::unique_ptr<float[]>, 
          std::unique_ptr<double[]> > array{};
        //purgin the data
        inline void clear()
        {
          //check that we free when storSize tracks the array, if it isn't it needs investigation
          assert(std::holds_alternative<std::monostate>(array) == (storSize == 0) );
          array.emplace<std::monostate>();
          storSize = 0;
        }
        //is there some data?
        [[nodiscard]]
          bool isEmpty() const noexcept
          {
            assert( std::holds_alternative<std::monostate>(array) == (storSize==0) );
            return std::holds_alternative<std::monostate>(array);
          }
        //c-tors
        constexpr StorageArray() = default; //should work fine with monostate :)
        StorageArray(const StorageArray& ref):StorageArray()
        {
            storSize = ref.storSize;
            std::visit(
                [this,&ref](const auto& token)
                {
                  using TokenType = std::remove_cvref_t<decltype(token)>;
                  if constexpr(!std::is_same_v<TokenType, std::monostate>)
                    this->array.emplace<TokenType> ( ref.makeCopy<typename TokenType::element_type>() );
                }, ref.array);
        }
        StorageArray& operator=(const StorageArray& ref)
        {
            //for smartasses assigning class to itself
            if (&ref == this)
              return *this;

            auto copy { ref };
            *this = std::move(copy);

            return *this;
        }
        //conversion constructors, eat up the pointer
        template<typename T>
        explicit StorageArray(std::unique_ptr<T[]> data, const std::size_t& length): StorageArray()
        {
            static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                          "StorageArray only stores float or double arrays");
            if (data == nullptr)
                return;
            if(length == 0)
                return;
            storSize = length;
            array.emplace<std::unique_ptr<T[]>>(std::move(data));
        }

        StorageArray(StorageArray&& ref) noexcept = default;

        StorageArray& operator=(StorageArray&& ref) noexcept = default;
        //and comparison for data
        bool operator==(const StorageArray& ref) const
        {
            //begin with trivial checks
            if (storSize != ref.storSize )
              return false;
            //if the same data array is stored, no need for expensive check
            //same if both are empty
            if (array == ref.array || storSize == 0)
            {
              assert( isEmpty() && ref.isEmpty() );
              return true;
            }
            
            //else by-value comparison needs to be done
            //TODO: try to template following out
            return std::visit(
                [this, &ref](const auto& arr1, const auto& arr2) -> bool
                {
                  using TArr1 = std::remove_cvref_t<decltype(arr1)>;
                  using TArr2 = std::remove_cvref_t<decltype(arr2)>;
                  if constexpr (std::is_same_v<TArr1,std::monostate> || std::is_same_v<TArr2, std::monostate>)
                  {
                    //only way one ends up here is if storSize is unsynchronized from array state
                    assert(ref.storSize == 0 && this->storSize ==0);
                    return false;
                  }
                  else
                  {
                    return cmpFloatArr(arr1.get(), arr2.get(), this->storSize);
                  }
                } , array, ref.array );
        }

        ~StorageArray() = default;
        //also a convert method to swap between representations
        inline void convert()
        {
          std::visit(
              [this](const auto& token)->void
              {
                using TokenType = std::remove_cvref_t<decltype(token)>;
                if constexpr(!std::is_same_v<TokenType, std::monostate>) 
                {
                  if constexpr(std::is_same_v<typename TokenType::element_type, float>)
                    this->array.emplace<std::unique_ptr<double[]>>( this->makeCopy<double>() );
                  else if constexpr(std::is_same_v<typename TokenType::element_type, double>)
                    this->array.emplace<std::unique_ptr<float[]>>( this->makeCopy<float>() );
                  else
                    std::unreachable();
                }
              }, array );
        }

        //data copy template
        template<typename T>
        T* makeCopy() const
          {
            static_assert(std::is_floating_point<T>::value, "StorageArray::makeCopy is only compatible with floating point type");
            if(isEmpty())
              return nullptr;
            T* buffer = new T[storSize];

            std::visit(
                [buffer, this](const auto& token)
                {
                  using TokenType = std::remove_cvref_t<decltype(token)>;
                  if constexpr( !std::is_same_v<TokenType, std::monostate> )
                    std::copy_n( token.get(), this->storSize, buffer );
                }, array );

            return buffer;
          }

        template<typename T>
        std::unique_ptr<T[]> release()
        {
          static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
          if(isEmpty())
            return {};

          auto released = std::move(std::get<std::unique_ptr<T[]>>(array));
          array.emplace<std::monostate>();
          storSize = 0;
          return released;
        }
    };

    
    //outside conversion
    template<typename T>
      void VField::convert()
      {
        static_assert( std::is_same_v<T, float> || std::is_same_v<T, double> ,
            "instantiation is only supported for float or double!");
        if( data->isEmpty() || std::holds_alternative<std::unique_ptr<T[]>> (data -> array) )
          return;
        data->convert();
      }

    std::size_t VField::curDataInternalSize() const noexcept
    {
      return std::visit(
          [](const auto& token) ->std::size_t {
            using TokenType = std::remove_cvref_t<decltype(token)>;
            if constexpr( std::is_same_v<TokenType, std::monostate> )
              return 0;
            else
              return sizeof(typename TokenType::element_type);
          }, data->array );
    }
    [[nodiscard]]
      VField::ScalarType VField::scalarType() const noexcept
      {
        return std::visit(
            [](const auto& token) -> VField::ScalarType
            {
              using  TokenType = std::remove_cvref_t<decltype(token)>;
              if constexpr ( std::is_same_v<TokenType, std::monostate> )
                return VField::ScalarType::None;
              else if constexpr ( std::is_same_v<TokenType, std::unique_ptr<float[]>> )
                return VField::ScalarType::Float32;
              else
                return VField::ScalarType::Float64;
            },
            data->array );
      }

    template<typename T>
      [[nodiscard]]
      bool VField::stores() const noexcept
      {
        static_assert( std::is_same_v<T, float> || std::is_same_v<T, double> ,
            "instantiation is only supported for float or double!");
        return std::holds_alternative<std::unique_ptr<T[]>>(data->array);
      }

    std::size_t VField::curDataPoints() const noexcept
    { 
      assert( (data ->storSize == 0) == data->isEmpty() );
      return data -> storSize; 
    }

    bool VField::isDataPresent() const noexcept
    { return !( data -> isEmpty() ); }

    //ctors
    VField::VField(): data( std::make_unique<StorageArray>() ) {}
    VField::~VField() = default;

    VField::VField(VField&&) noexcept = default;
    VField& VField::operator=(VField&&) noexcept = default;
    
    void VField::clearData() noexcept
    { data -> clear(); }

    template<typename T>
      std::unique_ptr<T[]> VField::releaseData()
      { return data->release<T>(); }
    
    //data access methods
    template<typename T>
      T* VField::getDataCopy() const
      {
        static_assert( std::is_same_v<T, float> || std::is_same_v<T, double> ,
            "instantiation is only supported for float or double!");
        return data -> makeCopy<T>(); 
      }
    
    //and then getting the internal fields
    template<typename T>
      T* VField::getData()
      {
        static_assert( std::is_same_v<T, float> || std::is_same_v<T, double> ,
            "instantiation is only supported for float or double!");
        if (data->isEmpty())
          return nullptr;
        return std::get<std::unique_ptr<T[]>>(data->array).get();
      }

    template<typename T>
      const T* VField::getData() const
      { 
        static_assert( std::is_same_v<T, float> || std::is_same_v<T, double> ,
            "instantiation is only supported for float or double!");
        if (data->isEmpty())
          return nullptr;
        return std::get<std::unique_ptr<T[]>>(data->array).get();
      }
    
    //setters
    template <typename T>
    void VField::insertData(std::unique_ptr<T[]> arr, std::size_t size) noexcept
    { data = std::make_unique<StorageArray>(std::move(arr), size); }
    template <typename T>
    void VField::setData(const T* arr, std::size_t size)
    {
        auto buffer = std::make_unique<T[]>(size);
        std::copy_n(arr, size, buffer.get());
        insertData(std::move(buffer), size);
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
    template<typename T>
    bool VField::setPoint(std::size_t pos, const T& val)
    {
        if( data -> isEmpty() || pos >= data -> storSize )
            return false;
        
        std::visit( 
            [pos, &val](auto& token) -> void
            {
              if constexpr( !std::is_same_v<std::remove_reference_t<decltype(token)>, std::monostate> )
              {
                static_assert(std::is_convertible_v<T, typename std::remove_reference_t<decltype(token)>::element_type>,
                    "setter must provide values convertible to float");
                token.get()[pos] = val;
              }
            }, data->array );
    }

    //constructors and such again
    VField::VField(const VField& ref): data( std::make_unique<StorageArray>() ), Header(ref.Header)
    { *data = *ref.data; }
    VField& VField::operator= (const VField& ref)
    {
      Header = ref.Header;
      *data = *ref.data;

      return *this;
    }

    //comparison operations
    bool VField::isSameDataAs(const VField& ref) const noexcept
    { return *data == *ref.data; }
    bool VField::operator==(const VField& ref) const noexcept
    { return Header == ref.Header && isSameDataAs(ref); }

    template OVFPARSER_EXPORT float* VField::getData<float>();
    template OVFPARSER_EXPORT double* VField::getData<double>();
    template OVFPARSER_EXPORT const float* VField::getData<float>() const;
    template OVFPARSER_EXPORT const double* VField::getData<double>() const;
    template OVFPARSER_EXPORT std::unique_ptr<float[]> VField::releaseData<float>();
    template OVFPARSER_EXPORT std::unique_ptr<double[]> VField::releaseData<double>();
    template OVFPARSER_EXPORT float* VField::getDataCopy<float>() const;
    template OVFPARSER_EXPORT double* VField::getDataCopy<double>() const;
    template OVFPARSER_EXPORT void VField::convert<float>();
    template OVFPARSER_EXPORT void VField::convert<double>();
    template OVFPARSER_EXPORT void VField::insertData<float>(std::unique_ptr<float[]>, std::size_t) noexcept;
    template OVFPARSER_EXPORT void VField::insertData<double>(std::unique_ptr<double[]>, std::size_t) noexcept;
    template OVFPARSER_EXPORT void VField::setData<float>(const float*, std::size_t);
    template OVFPARSER_EXPORT void VField::setData<double>(const double*, std::size_t);
    template OVFPARSER_EXPORT bool VField::stores<float>() const noexcept;
    template OVFPARSER_EXPORT bool VField::stores<double>() const noexcept;
}
