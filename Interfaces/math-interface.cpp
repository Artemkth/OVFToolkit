#include<wstp.h>
#include<stdexcept>
#include<filesystem> //using for checking file stuff
#include<algorithm>
#include<string>
#include<type_traits>
#include<vector>

//glue code for mathematica's library
//handles the connection to mathematica kernel
int main(int argc, char** argv)
{ return WSMain(argc, argv); }

template<typename T>
struct ToSigned{static_assert(!std::is_unsigned_v<T>, "Only usable with unsigned types");};
template<> struct ToSigned<std::uint8_t>
{using type = std::int16_t;};
template<> struct ToSigned<std::uint16_t>
{using type = std::int32_t;};
template<> struct ToSigned<std::uint32_t>
{using type = std::int64_t;};
template<> struct ToSigned<std::uint64_t>
{using type = std::int64_t;}; //*the* only truncating conversion

//templated functions to put out different types
template<typename T>
inline std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, bool> PutValue(T val)
{
    //if type is unsigned take next largest value type
    //only std::uint64_t will get truncated like this
    //iff val > std::numeric_limits<std::int64_t> 
    switch(8 * (std::is_unsigned_v<T>? 2*sizeof(T) : sizeof(T)) )
    {
        case(8):
            return WSPutInteger8(stdlink, val) != 0;
        case(16):
            return WSPutInteger16(stdlink, val) != 0;
        case(32):
            return WSPutInteger32(stdlink, val) != 0;
        default:
            return WSPutInteger64(stdlink, static_cast<wsint64>(val)) != 0;
    }
}
template<typename T>
inline std::enable_if_t<std::is_floating_point_v<T>, bool> PutValue(T val)
{
    //for floating point
    switch( 8 * sizeof(T) )
    {
        case(32):
            return WSPutReal32(stdlink, val) != 0;
        case(64):
            return WSPutReal64(stdlink, val) != 0;
        default:
            return WSPutReal128(stdlink, val) != 0;
    }
}
inline bool PutValue(bool val)
{
    if(val)
        return WSPutSymbol(stdlink, "True") != 0;
    return WSPutSymbol(stdlink, "False") != 0;
}
inline bool PutValue(const std::string& str)
{ return WSPutUTF8String(stdlink, reinterpret_cast<const unsigned char*>( str.c_str() ), str.length() ) != 0; }
inline bool PutValue(const char* str)
{ return PutValue(std::string(str)); }
//and very strong magic with lists, only real/integer
//assumed to have 'data' method to access data, and 'size' to count elements
template<typename T, template<typename> typename container>
inline std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>, bool> PutValue(const container<T>& val)
{
    //TODO: futureproof by spliting the load by INT_MAX
    int size { val.size() }; //has to be int for the array input function
    auto ptr { val.data() };
    if constexpr(std::is_integral_v<T>)
    {
        //first need to check if type is signed or not
        if constexpr(std::is_signed_v<T>)
        {
            if constexpr(std::is_same_v<T, std::int8_t>)
                return WSPutInteger8Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<T, std::int16_t>)
                return WSPutInteger16Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<T, std::int32_t>)
                return WSPutInteger32Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<T, std::int64_t>)
                return WSPutInteger64Array(stdlink, ptr, &size, nullptr, 1) != 0;
            static_assert(true, "Unsupported type!");
        }
        else
        {
            using U = typename ToSigned<T>::type;
            U* copy {new U[size]};
            std::copy_n(val.data(), size, copy);
            bool result {};
            if constexpr(std::is_same_v<U, std::int16_t>)
                result = WSPutInteger16Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<U, std::int32_t>)
                result = WSPutInteger32Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<U, std::int64_t>)
                result = WSPutInteger64Array(stdlink, ptr, &size, nullptr, 1) != 0;
            delete[] copy;
            return result;
        }
    }
    else //is_floating_point_v
    {
        if constexpr(std::is_same_v<T, float>)
            return WSPutReal32Array(stdlink, ptr, &size, nullptr, 1) != 0;
        else if constexpr(std::is_same_v<T, double>)
            return WSPutReal64Array(stdlink, ptr, &size, nullptr, 1) != 0;
        else if constexpr(std::is_same_v<T, long double> && sizeof(long double) == 16)
            return WSPutReal128Array(stdlink, ptr, &size, nullptr, 1) != 0;
        static_assert(true, "Unsupported type!");
    }
    return true;
}

//template an error throw
template<typename... T>
inline void PostErrorMessage(
        const std::string& symbName,
        const std::string& symbError,
        T... params)
{
    bool result { WSPutFunction(stdlink, "Message", 1 + sizeof...(T)) != 0 };
    result = result && WSPutFunction(stdlink, "MessageName", 2) != 0;
    result = result && WSPutSymbol(stdlink, symbName.c_str()) != 0;
    result = result && PutValue(symbError);
    //now real cock magic, I love fold expressions
    result = (result && ... && PutValue(params)); //by default will be true if parameter pack is empty
    if(!result) throw std::runtime_error("Failed to ouput an error message, PANIC!");
}
//template for signiling failure
inline bool PostFailure()
{
    bool result{ WSPutSymbol(stdlink, "$Failed") != 0 };
    result = result && WSEndPacket(stdlink) != 0;
    return result && WSFlush(stdlink) != 0;                   //only failure should flush
}

extern "C" void importWhole(const char* fileName)
{
    if(!std::filesystem::exists(fileName))
    {
        WSPutFunction(stdlink, "CompoundExpression", 2);
        PostErrorMessage("General", "noopen", fileName);
        PostFailure();
        return;
    }

    //else return empty list for now, LULw
    WSPutFunction(stdlink, "List", 1);
    PutValue(42);
    WSEndPacket(stdlink);
}

