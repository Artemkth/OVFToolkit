#include<wstp.h>
#include<filesystem> //using for checking file stuff
#include<string>
#include<type_traits>

//glue code for mathematica's library
//handles the connection to mathematica kernel
int main(int argc, char** argv)
{ return WSMain(argc, argv); }

//templated functions to put out different types
template<typename T>
std::enable_if_t<std::is_integral<T>::value, bool> PutValue(T val)
{
    //if type is unsigned take next largest value type
    //only std::uint64_t will get truncated like this
    //iff val > std::numeric_limits<std::int64_t> 
    switch(std::is_unsigned_v<T>? 2*sizeof(T) : sizeof(T))
    {
        case(8):
            return WSPutInteger8(stdlink, val) != 0;
        case(16):
            return WSPutInteger16(stdlink, val) != 0;
        case(32):
            return WSPutInteger32(stdlink, val) != 0;
        default:
            return WSPutInteger64(stdlink, val) != 0;
    }
}

//template an error throw
template<typename... T>
inline void PostErrorMessage(
        const std::string& symbName,
        const std::string& symbError,
        T... params)
{
}
//template for signiling failure
inline void PostFailure()
{
    WSPutSymbol(stdlink, "$Failed");
    WSEndPacket(stdlink);
    WSFlush(stdlink);//only failure should flush
} 

extern "C" void importWhole(const char* fileName)
{
}

