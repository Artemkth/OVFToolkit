#include<wstp.h>
#include<stdexcept>
#include<filesystem> //using for checking file stuff
#include<algorithm>
#include<string>
#include<type_traits>
#include<vector>
#include<optional>
#include<map>
//small convinience on *nix systems
//TODO: replace with cmake detection later, CheckIncludeFile
#if defined(__unix__) || defined(__LINUX__) || defined(__APPLE__)
#define EXPANDPATH
#endif

#ifdef EXPANDPATH
#include<wordexp.h>
#endif

//and interfaces to ovfparser
#include<OVFParser.h>
#include<OVFDictionary.h>


//glue code for mathematica's library
//handles the connection to mathematica kernel
int main(int argc, char** argv)
{ return WSMain(argc, argv); }

#ifdef EXPANDPATH
auto ExpandPath(const std::string& fPath)
{
    wordexp_t expansions;
    std::vector<std::string> results {};
    if(wordexp(fPath.c_str(), &expansions, 0) != 0)
        return results; //error occured
    for(std::size_t i = 0; i < expansions.we_wordc; i++)
        results.push_back(expansions.we_wordv[i]);
    wordfree(&expansions);
    return results;
}
#endif

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
template< typename T>
inline std::enable_if_t<std::is_integral_v<typename T::value_type> || std::is_floating_point_v<typename T::value_type>, bool> PutValue(const T& val)
{
    //TODO: futureproof by spliting the load by INT_MAX
    //(hack with putting Sequence functions with INT_MAX size)
    int size { val.size() }; //has to be int for the array input function
    auto ptr { val.data() };
    if constexpr(std::is_integral_v<typename T::value_type>)
    {
        //first need to check if type is signed or not
        if constexpr(std::is_signed_v<typename T::value_type>)
        {
            if constexpr(std::is_same_v<typename T::value_type, std::int8_t>)
                return WSPutInteger8Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<typename T::value_type, std::int16_t>)
                return WSPutInteger16Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<typename T::value_type, std::int32_t>)
                return WSPutInteger32Array(stdlink, ptr, &size, nullptr, 1) != 0;
            else if constexpr(std::is_same_v<typename T::value_type, std::int64_t>)
                return WSPutInteger64Array(stdlink, ptr, &size, nullptr, 1) != 0;
            static_assert(true, "Unsupported type!");
        }
        else
        {
            using U = typename ToSigned<typename T::value_type>::type;
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
        if constexpr(std::is_same_v<typename T::value_type, float>)
            return WSPutReal32Array(stdlink, ptr, &size, nullptr, 1) != 0;
        else if constexpr(std::is_same_v<typename T::value_type, double>)
            return WSPutReal64Array(stdlink, ptr, &size, nullptr, 1) != 0;
        else if constexpr(std::is_same_v<typename T::value_type, long double> && sizeof(long double) == 16)
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

//check the file path
std::optional<std::filesystem::path> checkFileName(const char* fileName)
{
    const auto any_read { 
            std::filesystem::perms::owner_read |
            std::filesystem::perms::group_read |
            std::filesystem::perms::others_read
    };
    std::filesystem::path fPath {};
    if(!std::filesystem::exists(fileName))
#ifndef EXPANDPATH
    {
        WSPutFunction(stdlink, "CompoundExpression", 2);
        PostErrorMessage("General", "noopen", fileName);
        PostFailure();
        return std::nullopt;
    }
    fPath = fileName;
#else //defined(EXPANDPATH)
    {
        //try expansion, that's a good trick
        auto expanded = ExpandPath(fileName);
        if( expanded.size() != 1 || !std::filesystem::exists(expanded[0]) )
        {   
            WSPutFunction(stdlink, "CompoundExpression", 2);
            PostErrorMessage("General", "noopen", expanded.size()==1? expanded[0] : fileName);
            PostFailure();
            return std::nullopt;
        }
        fPath = expanded[0];
        WSPutFunction(stdlink, "CompoundExpression", 2);
        PostErrorMessage( "OVFToolkit", "fsub", fileName, expanded[0] );
    }
    else
        fPath = fileName;
#endif
    //next check if fPath is good for reading(i.e. have appropriate permissions and it is not a dir)
    auto status = std::filesystem::status( fPath );
    if( !std::filesystem::is_regular_file(status) || 
        (status.permissions() & any_read) == std::filesystem::perms::none )
    {
        WSPutFunction(stdlink, "CompoundExpression", 2);
        PostErrorMessage("OVFToolkit", "notperm", "read", fPath.c_str());
        PostFailure();
        return std::nullopt;
    }
    return fPath;
}

bool OutputData(const VField::VField& field) //output data to mathematica
{
    //output data methods
    const bool isRect { field.Header.getMeshType() == VField::OVFHeader::MeshType::rectangular };
    const int depth {isRect? 4 : 2};
    std::vector<int> dims;
    //static casts, explicitly converting to smaller type :'(
    //TODO: add an error throw for when some argument is larger than INT_MAX
    if(isRect)
        dims = { 
            static_cast<int>(field.Header.getUint(VField::OVFParameter::Znodes)),
            static_cast<int>(field.Header.getUint(VField::OVFParameter::Ynodes)),
            static_cast<int>(field.Header.getUint(VField::OVFParameter::Xnodes)),
            static_cast<int>(field.pntDimension())
        };
    else
        dims = {
            static_cast<int>(field.pntCount()),
            static_cast<int>(field.pntDimension())
        };

    bool result;
    if(field.curDataInternalSize() == 4)
        result = WSPutReal32Array(stdlink, field.getData<float>(), dims.data(), nullptr, depth);
    else
        result = WSPutReal64Array(stdlink, field.getData<double>(), dims.data(), nullptr, depth);

    return result;
}

//outputing header
const std::map<
    VField::OVFParameter,
    std::string> ParamKeys
{
    {VField::OVFParameter::VersionString, "VersionString"},
    {VField::OVFParameter::Title, "Title"},
    {VField::OVFParameter::Desc, "Description"},
    {VField::OVFParameter::Munit, "MeshUnits"},
    {VField::OVFParameter::Vunit, "ValueUnits"},
    {VField::OVFParameter::Vmult, "ValueMultiplier"},
    {VField::OVFParameter::Vlabels, "ValueLabels"},
    {VField::OVFParameter::Bound, "BoundingPolygon"},
    {VField::OVFParameter::Mtype, "MeshType"},
    {VField::OVFParameter::Xbase, "X0"},
    {VField::OVFParameter::Ybase, "Y0"},
    {VField::OVFParameter::Zbase, "Z0"},
    {VField::OVFParameter::Xstep, "dX"},
    {VField::OVFParameter::Ystep, "dY"},
    {VField::OVFParameter::Zstep, "dZ"},
    {VField::OVFParameter::Vmin, "MinVal"},
    {VField::OVFParameter::Vmax, "MaxVal"},
    {VField::OVFParameter::Xmin, "MinX"},
    {VField::OVFParameter::Xmin, "MinY"},
    {VField::OVFParameter::Xmin, "MinZ"},
    {VField::OVFParameter::Xmin, "MaxX"},
    {VField::OVFParameter::Xmin, "MaxY"},
    {VField::OVFParameter::Xmin, "MaxZ"}
};

//putting a value from header
template<VField::OVFParameter p>
constexpr bool putVal(const VField::OVFHeader& head)
{
    //putting values out as rules
    bool result{WSPutFunction(stdlink, "Rule", 2) != 0};
    result = result && PutValue(ParamKeys.at(p));

    if constexpr(paramIndex(p) == VField::pType::Uint)
        return result && PutValue(head.getUint(p));
    else if constexpr(paramIndex(p) == VField::pType::Float)
        return result && PutValue(head.getFloat(p));
    else if constexpr(paramIndex(p) == VField::pType::String)
        return result && PutValue(head.getString(p));
    //if everything else fails
    static_assert(true, "Wrong, unhandled type of parameter!");
}

//a lot of duck-type magic
template<typename T, T v>
constexpr bool isOutputted(const VField::OVFHeader& head)
{
    if constexpr(std::is_same<T, VField::OVFParameter>::value)
        return head.isSet(v);
    else
        //assuming first element is a predicate taking a header
        return v(head, false);
}
template<typename T, T v>
constexpr bool Output(const VField::OVFHeader& head)
{
    if constexpr(std::is_same<T, VField::OVFParameter>::value)
        return !head.isSet(v) || putVal<v>(head);
    else if(v(head, false))
        return v(head, true);
    return false;
}

template<typename... T>
struct Wrapper{
    Wrapper(T...) {}

    template<T... args>
    struct Wrapping{
        static constexpr bool OutputAll(const VField::OVFHeader& head)
        {
            bool result {
                WSPutFunction(stdlink, "List", 
                        (0 + ... + (isOutputted<T, args>(head)? 1 : 0)) ) != 0 //count outputted parameters
            };
            return (result && ... && Output<T, args>(head));
        }
    };
};

#define OUTPUT_HEADER(head, args...) decltype(Wrapper{args})::Wrapping<args>::OutputAll(head)

inline bool OutputMeshType(const VField::OVFHeader& head, bool write = false)
{
    if(!write)
        return true;

    bool res {WSPutFunction(stdlink, "Rule", 2) != 0};
    res = res && PutValue(ParamKeys.at(VField::OVFParameter::Mtype));
    if(!head.isSet(VField::OVFParameter::Mtype))
        res = res && WSPutSymbol(stdlink, "Undefined") != 0;
    return res && PutValue(head.getMeshType() == VField::OVFHeader::MeshType::rectangular? "Rectangular" : "Irregular");
}

inline bool OutputCoordIncrement(const VField::OVFHeader& head, bool write = false)
{
    if(!write)
        return true;
    constexpr std::array<VField::OVFParameter, 3> args{
        VField::OVFParameter::Xstep,
        VField::OVFParameter::Ystep,
        VField::OVFParameter::Zstep
    };
    bool res {WSPutFunction(stdlink, "Rule", 2) != 0};
    res = res && PutValue("CellSize");
    if(std::all_of(args.begin(), args.end(), [&](const VField::OVFParameter& p){return head.isSet(p);}))
    {
        std::array<VField::associatedType_t<VField::pType::Float>,3> val {
            head.getFloat(VField::OVFParameter::Xstep),
            head.getFloat(VField::OVFParameter::Ystep),
            head.getFloat(VField::OVFParameter::Zstep)
        };
        res = res && PutValue(val);
    }
    //otherwise do it all manually :(
    else
    {
        res = res && WSPutFunction(stdlink, "List", 3) != 0;
        for(const auto& p: args)
            res = res && (head.isSet(p) ? PutValue(head.getFloat(p)) : WSPutSymbol(stdlink, "Undefined") != 0);
    }

    return res;
}
inline bool OutputCoordOrigin(const VField::OVFHeader& head, bool write = false)
{
    if(!write)
        return true;
    constexpr std::array<VField::OVFParameter, 3> args{
        VField::OVFParameter::Xbase,
        VField::OVFParameter::Ybase,
        VField::OVFParameter::Zbase
    };
    bool res {WSPutFunction(stdlink, "Rule", 2) != 0};
    res = res && PutValue("Origin");
    if(std::all_of(args.begin(), args.end(), [&](const VField::OVFParameter& p){return head.isSet(p);}))
    {
        std::array<VField::associatedType_t<VField::pType::Float>,3> val {
            head.getFloat(VField::OVFParameter::Xbase),
            head.getFloat(VField::OVFParameter::Ybase),
            head.getFloat(VField::OVFParameter::Zbase)
        };
        res = res && PutValue(val);
    }
    //otherwise do it all manually :(
    else
    {
        res = res && WSPutFunction(stdlink, "List", 3) != 0;
        for(const auto& p: args)
            res = res && (head.isSet(p) ? PutValue(head.getFloat(p)) : WSPutSymbol(stdlink, "Undefined") != 0);
    }

    return res;
}
inline bool OutputBBox(const VField::OVFHeader& head, bool write = false)
{
    constexpr std::array<VField::OVFParameter, 6> args{
        VField::OVFParameter::Xmin,
        VField::OVFParameter::Xmax,
        VField::OVFParameter::Ymin,
        VField::OVFParameter::Ymax,
        VField::OVFParameter::Zmin,
        VField::OVFParameter::Zmax,
    };
    const bool any_present{std::any_of(args.begin(), args.end(), [&](const VField::OVFParameter& p){return head.isSet(p);})};
    if(!write)
        return any_present;

    if(!any_present)
        return true; //nothing to output

    bool res {WSPutFunction(stdlink, "Rule", 2) != 0};
    res = res && PutValue("BoundingBox");
    res = res && WSPutFunction(stdlink, "List", 3) != 0;
    int i = 0;
    for(const auto& p: args)
    {
        if(i++%2 == 0)
            res = res && WSPutFunction(stdlink, "List", 2);
        res = res && (head.isSet(p) ? PutValue(head.getFloat(p)) : WSPutSymbol(stdlink, "Undefined") != 0);
    }

    return res;
}

//function for outputting the header
bool OutputHeader(const VField::OVFHeader& head)
{
    return OUTPUT_HEADER(head, 
            VField::OVFParameter::VersionString,
            VField::OVFParameter::Title,
            VField::OVFParameter::Desc,
            VField::OVFParameter::Vlabels,
            VField::OVFParameter::Vunit,
            VField::OVFParameter::Munit,
            VField::OVFParameter::Vmult,
            OutputMeshType,
            OutputCoordOrigin,
            OutputCoordIncrement,
            VField::OVFParameter::Bound,
            OutputBBox
            );//currently fails because type is deduced to be rvalue :'(
}

extern "C" void importWhole(const char* fileName)
{
    const auto fPath { checkFileName(fileName) };
    if(!fPath.has_value()) return; //all output is done by checkFileName when it cannot recover
    //next open the file finally
    const VField::VFieldFile fileHandle(fPath.value().c_str());
    if(!fileHandle.WorkLog().empty())
    {
        WSPutFunction(stdlink, "CompoundExpression", 2);
        PostErrorMessage("ImportOVF", "prserr", fileHandle.WorkLog());
    }
    //and start outputting data
    WSPutFunction(stdlink, "List", fileHandle.cntSegments() );
    std::size_t seg_cnt {0};
    for(const auto& vfield: fileHandle)
    {
        WSPutFunction(stdlink, "List", 2);
        //Output Header
        OutputHeader(vfield.Header);

        //Output data
        if(!vfield.isAddressable())
        {
            WSPutFunction(stdlink, "CompoundExpression", 2);
            PostErrorMessage("ImportOVF", "naddr", seg_cnt, fileHandle.getCurrentPath());
            WSPutFunction(stdlink, "List", 0); //and that's all the data you get when field is not addressable :p
        }
        else
            OutputData(vfield);
    }

    WSEndPacket(stdlink);
    WSFlush(stdlink);
}

