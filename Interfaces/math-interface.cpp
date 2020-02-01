#include<wstp.h>
#include<stdexcept>
#include<filesystem> //using for checking file stuff
#include<algorithm>
#include<string>
#include<type_traits>
#include<vector>
#include<stack>
#include<optional>
#include<variant>
#include<map>
//for mapping types
#include<memory>
#include<stdexcept>
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

//global counter for expected return packets
std::size_t skip_cnt{0};

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
    bool result { WSPutFunction(stdlink, "EvaluatePacket", 1) != 0 };
    result = result && WSPutFunction(stdlink, "Message", 1 + sizeof...(T)) != 0; 
    result = result && WSPutFunction(stdlink, "MessageName", 2) != 0;
    result = result && WSPutSymbol(stdlink, symbName.c_str()) != 0;
    result = result && PutValue(symbError);
    //now real cock magic, I love fold expressions
    result = (result && ... && PutValue(params)); //by default will be true if parameter pack is empty
    if(!result) throw std::runtime_error("Failed to ouput an error message, PANIC!");

    //increment ignored expression count
    skip_cnt++;
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
            PostErrorMessage("General", "noopen", expanded.size()==1? expanded[0] : fileName);
            PostFailure();
            return std::nullopt;
        }
        fPath = expanded[0];
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
        return res && WSPutSymbol(stdlink, "Undefined") != 0;
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
            );
}

//wrappers to interfaces for importing data from mathematica
//                                               string for symbol types
using math_atom = std::variant<long, double, std::string>;
//ohboi
class Expression;
class Expression : public std::vector<std::variant<math_atom, std::unique_ptr<Expression>>> {
    public:
        //constructors
        Expression() = default;
        Expression(std::initializer_list<math_atom> init){
            for(auto& val: init)
                emplace_back(std::move(val));
        }

        //method to tell if expression is symbol
        bool isSymbol() const noexcept
        { return size() == 1; }
        // check if value is numeric
        bool isNumeric(const_iterator it) const
        { 
            if( it->index() != 0 )
                return false; //nested expression or symbol
            const auto& val = std::get<math_atom> (*it);
            return val.index() == 0 || val.index() == 1;
        }
        // get header
        const std::string& getHeader() const noexcept
        { return std::get<std::string>(std::get<math_atom>(front())); }

        //find iterator to a first 'Rule' expression in the current expression with a specified pattern
        auto getRule(const std::string& rule) const
        {
            const std::string ruleHead {"Rule"}; 
            //target expression root to match
            auto begin = ++cbegin(); //skip the header of root expression
            auto end = cend();
            auto srch_res = std::find_if(begin, end, 
                    [&](const std::variant<math_atom, std::unique_ptr<Expression>>& var)
                    {
                        if(var.index() != 1 || std::get<std::unique_ptr<Expression>>(var)->size() != 3) return false;
                        const auto& sub_exp = std::get<std::unique_ptr<Expression>>(var).get();
                        const auto& rulePatt = sub_exp -> at(1);
                        return sub_exp -> getHeader() == ruleHead &&
                            rulePatt.index()==0 && std::get<math_atom>(rulePatt).index() == 2 &&
                            std::get<std::string>(std::get<math_atom>(rulePatt)) == rule;
                    });
            return srch_res;
        }

        bool testHeader(const std::string& head) const
        {
            //first element is assumed to be a string by construction
            return std::get<std::string>(std::get<math_atom>(front())) == head;
        }
};

std::optional<bool> ParseFlag(const Expression& expr, const std::string& flagName)
{
    const auto srch_res = expr.getRule(flagName);
    if(srch_res == expr.end())
        return std::nullopt;

    //guaranteed by how getRule was built
    const auto& replace_rule = std::get<std::unique_ptr<Expression>>(*srch_res) -> at(2);
    if( replace_rule.index() != 1 || !std::get<std::unique_ptr<Expression>>(replace_rule) -> isSymbol() )
        //return nothing if replacement_rule is not a symbol!
        return std::nullopt;
    const auto& replace_symbol = std::get<std::unique_ptr<Expression>>(replace_rule);

    //else compare to two symbol definitions
    if(replace_symbol -> getHeader() == "True")
        return true;
    else if(replace_symbol -> getHeader() == "False")
        return false;
    return std::nullopt;
}

//parse input from Mathematica
Expression ParseWSTPExpression(int optc = 0)
{ 
    //nothing to do if nothing is expected on output
    if(optc == 0)
        return {};

    Expression result{"ROOT"};
    //set root header to ROOT
    std::stack<std::pair<Expression*, std::size_t>> workStack {{{&result, optc}}};
    //next parse flags

    //need to flush all writes before calling WSReady!
    WSFlush(stdlink);
    while(WSReady(stdlink) && !workStack.empty())
    {
        //decrement top by 1 each time we get a new value
        workStack.top().second--;
        const char * str{nullptr};
        auto type = WSGetNext(stdlink);
        switch(type)
        {
            case WSTKERR:
                break;

            case WSTKINT:
                workStack.top().first -> emplace_back(long{});
                WSGetInteger64(stdlink, &std::get<long>(std::get<math_atom>(workStack.top().first -> back())));
                break;

            case WSTKREAL:
                workStack.top().first -> emplace_back(double{});
                WSGetReal64(stdlink, &std::get<double>(std::get<math_atom>(workStack.top().first -> back())));
                break;

            case WSTKSTR:
                if( WSGetString(stdlink, &str) != 0)
                {
                    workStack.top().first -> emplace_back(std::string{ str });
                    WSReleaseString(stdlink, str); 
                }
                break;

            default:
                int dim {};
                if( type == WSTKSYM && WSGetSymbol(stdlink, &str) != 0 || 
                    type == WSTKFUNC && WSGetFunction(stdlink, &str, &dim) != 0 )
                {
                    workStack.top().first -> emplace_back( std::make_unique<Expression> (std::initializer_list<math_atom>({str})) );
                    if( type == WSTKFUNC )
                        workStack.emplace(
                                std::make_pair(std::get<std::unique_ptr<Expression>>( workStack.top().first -> back() ).get() ,dim)
                            );
                }
        }

        //if at the end of a block pop last reference off of the stack top
        while(!workStack.empty() && workStack.top().second == 0)
            workStack.pop();
    }

    if(!workStack.empty())
    {
        WSPutFunction(stdlink, "CompoundExpression", 2);
        PostErrorMessage("OVFToolkit", "prserr");
    }

    return std::move(result);
}

//cleans up stdlink after 
void deinit()
{
    if(WSError(stdlink) != WSEOK)
        throw std::runtime_error("Unhandled error occured on link!");

    WSFlush(stdlink);
    if( skip_cnt != 0 || WSReady(stdlink) )
    {
        if(WSNewPacket(stdlink) == 0)
            throw std::runtime_error("Unhandled error occured on link!");
        WSFlush(stdlink);
        //following will block if there is a return packet to wait for
        while( skip_cnt != 0 || WSReady(stdlink) )
        {
            auto mark = WSCreateMark(stdlink);
            switch(WSNextPacket(stdlink))
            {
                case RETURNPKT:
                    if(skip_cnt == 0)
                        throw std::runtime_error("Got an unexpected ReturnPacket!");

                    --skip_cnt;
                    if(WSNewPacket(stdlink) == 0)
                        throw std::runtime_error("Unhandled error occured on link!");
                    break;

                case CALLPKT:
                    //SEARCHING...., SEEK AND DESTROY
                    WSSeekMark(stdlink, mark, 0); 
                    WSDestroyMark(stdlink, mark); 
                    return;

                default: //including INVALIDPKT
                    //throw if it any other packet
                    throw std::runtime_error("Got an unhandled packet type!");
            }
            WSDestroyMark(stdlink, mark);
        }
    }

    //if there were some packets left
    if(skip_cnt != 0)
        throw std::runtime_error("Didn't find all of the return packets!");
}

extern "C" void import(const char* fileName, int optc)
{
    const auto fPath { checkFileName(fileName) };
    if(!fPath.has_value()) return; //all output is done by checkFileName when it cannot recover
    //next open the file finally
    const VField::VFieldFile fileHandle(fPath.value().c_str());
    if(!fileHandle.WorkLog().empty())
        PostErrorMessage("ImportOVF", "prserr", fileHandle.WorkLog());
    
    //parse other parameters
    auto OtherParams{ParseWSTPExpression(optc)};
    if( WSReady(stdlink) )
    {/*TODO implement error throw */}

    const auto sendHeader { ParseFlag(OtherParams, "GetHeader") };
    const auto sendData { ParseFlag(OtherParams, "GetData") };
    const int segment_dim {  (sendHeader.value_or(true) ? 1 : 0) +
                             (sendData.value_or(true)   ? 1 : 0)   };

    //and start outputting data
    WSPutFunction(stdlink, "List", fileHandle.cntSegments() );
    auto begin = fileHandle.begin();
    auto end   = fileHandle.end();
    std::size_t seg_cnt{0};
    for(; begin != end; ++begin)
    {
        if(segment_dim!=1) WSPutFunction(stdlink, "List", segment_dim);
        //Output Header
        if (sendHeader.value_or(true)) OutputHeader(begin.getHeader());

        //Output Data
        if (sendData.value_or(true)) 
        {
            const auto field = *begin;

            //Output data
            if(!field.isAddressable())
            {
                PostErrorMessage("ImportOVF", "naddr", seg_cnt, fileHandle.getCurrentPath());
                WSPutFunction(stdlink, "List", 0); //and that's all the data you get when field is not addressable :p
            }
            else
                OutputData(field);
        }
        seg_cnt++;
    }

    //clean up after ourselfs
    deinit();
}

//now for parsing different types of sub-expressions
//small helper to prevent wrong type from next template
template<typename T, typename Variant>
struct isVariantMember{static_assert(true, "isVariant is applicable to only variant");};
template<typename T, typename... types>
struct isVariantMember<T, std::variant<types...>> : public std::disjunction<std::is_same<T, types>...> {
    private:
        template <typename> struct tag {};
    public:
        static constexpr std::size_t index { std::variant<tag<types>...>(tag<T>()).index() };
};

template<typename T>
std::enable_if_t<isVariantMember<T, math_atom>::value, std::optional<T>>
                                ParseValue(const Expression& expr, const std::string& pattern)
{
    auto srch_res = expr.getRule(pattern);

    //get some constants for later in compiletime
    constexpr auto T_index { isVariantMember<T, math_atom>::index };
    constexpr auto Int_index { isVariantMember<long, math_atom>::index };

    //if no value was found throw empty value instead
    if(srch_res == expr.end())
        return std::nullopt;

    if(std::get<std::unique_ptr<Expression>>(*srch_res) -> at(2).index() != 0)//symbol or expression
        return std::nullopt;
    const auto& val = std::get<math_atom>(std::get<std::unique_ptr<Expression>>(*srch_res) -> at(2));
    
    if(T_index == val.index())
        return std::get<T>(val);
    //else only in one case can we succeed
    if constexpr (std::is_floating_point_v<T>)
        if(val.index() == math_atom{long{}}.index() )
            return std::get< Int_index > (val);

    //else return nothing
    return std::nullopt;
}

//read data from WSTP
VField::VField ParseWSTPData(std::size_t ByteSize)
{
    //fields for storing information from link
    int* dims {nullptr};
    int depth {0};
    char** headers {nullptr};

    //and create a VField header for future dumping, empty for now
    VField::VField field{}; //and import the data into it
    switch(ByteSize)
    {
        //switch is for later if I decide to add long double
        case 8:
            {
                double* data {nullptr};
                
                if(WSGetReal64Array(stdlink, &data, &dims, &headers, &depth) == 0 || (depth != 2 && depth != 4))
                    throw std::runtime_error("ParseWSTPData: Unexpected data array specifications on data link!");
                std::size_t dataPts {1};
                for(std::size_t i = 0; i < depth; i++)
                    dataPts *= dims[i];

                //put data into VField
                field.insertData(data, dataPts);
                break;
            }
        default:
            float* data {nullptr};
            if(WSGetReal32Array(stdlink, &data, &dims, &headers, &depth) == 0 || (depth != 2 && depth != 4))
                throw std::runtime_error("ParseWSTPData: Unexpected data array specifications on data link!");
            std::size_t dataPts {1};
            for(std::size_t i = 0; i < depth; i++)
                dataPts *= dims[i];

            field.insertData(data, dataPts);
    }
    VField::OVFHeader& head {field.Header};
    //first deduce mesh type
    if(depth == 2)
    {
        head.setMesh(VField::OVFHeader::MeshType::irregular);
        head.at<VField::pType::Uint>(VField::OVFParameter::Pcount) = dims[0];
        if(dims[1] > 3)
            head.at<VField::pType::Uint>(VField::OVFParameter::Vdim) = dims[1] - 3;
        //else skip
    }
    else
    {
        head.setMesh(VField::OVFHeader::MeshType::rectangular);

        head.at<VField::pType::Uint>(VField::OVFParameter::Znodes) = dims[0];
        head.at<VField::pType::Uint>(VField::OVFParameter::Ynodes) = dims[1];
        head.at<VField::pType::Uint>(VField::OVFParameter::Xnodes) = dims[2];
        head.at<VField::pType::Uint>(VField::OVFParameter::Vdim)   = dims[3];
    }

    WSReleaseReal64Array(stdlink, nullptr, dims, headers, depth);
    return field;
}

//set a field p with a math_atom
void SetField(VField::OVFHeader& head, VField::OVFParameter p, const math_atom& atom)
{
    switch(paramIndex(p))
    {
        case VField::pType::String:
            if(atom.index() != 2)
            {
                PostErrorMessage("ExportOVF", "badexp", ParamKeys.at(p), "a string");
                return;
            }
            head.set(p, std::get<std::string>(atom));
            break;
        case VField::pType::Float:
            if(atom.index() == 2)
            {
                PostErrorMessage("ExportOVF", "badexp", ParamKeys.at(p), "a numeric value");
                return;
            }
            if(atom.index() == 0)
                head.set(p, static_cast<VField::associatedType_t<VField::pType::Float>>(std::get<0>(atom)));
            else if(atom.index() == 1)
                head.set(p, static_cast<VField::associatedType_t<VField::pType::Float>>(std::get<1>(atom)));
            break;
        case VField::pType::Uint:
            if(atom.index() != 0)
            {
                PostErrorMessage("ExportOVF", "badexp", ParamKeys.at(p), "a integer");
                return;
            }
            head.set(p, static_cast<VField::associatedType_t<VField::pType::Uint>>(std::get<0>(atom)));
            break;

        default:
            throw std::runtime_error("SetField called with non-numeric target type!");
    }
}

//function to set fields from vector of pairs
using set_list = std::vector<std::pair<VField::OVFParameter, const math_atom*>>;
inline void SetBunch(VField::VField& field, const set_list& vals)
{
    for(const auto& x: vals)
    {
        if(x.second == nullptr)
            field.DeduceField(x.first, true);
        else
            SetField(field.Header, x.first, *x.second);
    }
}

//mapping from mathematica string tokens to OVFParameter tokens
const std::map<std::string, std::variant<VField::OVFParameter, set_list (*)(const Expression*)>> TokenMap{
    {   "VersionString",        VField::OVFParameter::VersionString     },
    {   "Title",                VField::OVFParameter::Title             },
    {   "Description",          VField::OVFParameter::Munit             },
    {   "MeshUnits",            VField::OVFParameter::Vunit             },
    {   "ValueUnits",           VField::OVFParameter::Vmult             },
    {   "ValueLabels",          VField::OVFParameter::Vlabels           },
    {   "BoundingPolygon",      VField::OVFParameter::Bound             },
    {   "X0",                   VField::OVFParameter::Xbase             },
    {   "Y0",                   VField::OVFParameter::Ybase             },
    {   "Z0",                   VField::OVFParameter::Zbase             },
    {   "dX",                   VField::OVFParameter::Xstep             },
    {   "dY",                   VField::OVFParameter::Ystep             },
    {   "dZ",                   VField::OVFParameter::Zstep             },
    {   "MinVal",               VField::OVFParameter::Vmin              },
    {   "MaxVal",               VField::OVFParameter::Vmax              },
    {   "MinX",                 VField::OVFParameter::Xmin              },
    {   "MinY",                 VField::OVFParameter::Ymin              },
    {   "MinZ",                 VField::OVFParameter::Zmin              },
    {   "MaxX",                 VField::OVFParameter::Xmax              },
    {   "MaxY",                 VField::OVFParameter::Ymax              },
    {   "MaxZ",                 VField::OVFParameter::Zmax              },
    //less trivial rules
    {   "Origin", [](const Expression* expr) -> set_list
        {
            //a small dandy list of values we try to replace
            constexpr std::array<VField::OVFParameter, 3> targetParams{
                VField::OVFParameter::Xbase,
                VField::OVFParameter::Ybase,
                VField::OVFParameter::Zbase
            };

            set_list res {};

            //main logic
            if( expr->isSymbol() && expr->testHeader("Default") )
            {
                for (const auto& x: targetParams)
                    res.push_back({x, nullptr}); //try to deduce the field with default value
                return res;
            }
            if( expr -> testHeader("List") && expr -> size() == targetParams.size() + 1 )
            {
                //if it is a correct shaped list, set parameters from it
                auto it = ++ expr->begin();
                for(const auto& x: targetParams)
                {
                    if( it -> index() == 1 )
                    {
                        const auto& subexp = *std::get<std::unique_ptr<Expression>>(*it);
                        if(subexp.isSymbol() && subexp.testHeader("Default"))
                            res.push_back({x, nullptr});
                        else
                            PostErrorMessage("ExportOVF", "badexp", ParamKeys.at(x), "either a numeric value or Default in the list");
                    }
                    else
                        res.push_back({x, &std::get<math_atom>(*it)});
                }
            }
            //else do nothing LULW
            PostErrorMessage("ExportOVF", "badexp", "Origin", "either a length 3 list or Default");
            return {};
        }
    },
    {   "CellSize", [](const Expression* expr) -> set_list
        {
            //a small dandy list of values we try to replace
            constexpr std::array<VField::OVFParameter, 3> targetParams{
                VField::OVFParameter::Xstep,
                VField::OVFParameter::Ystep,
                VField::OVFParameter::Zstep
            };

            set_list res {};

            //main logic
            if( expr->isSymbol() && expr->testHeader("Default") )
            {
                for (const auto& x: targetParams)
                    res.push_back({x, nullptr}); //try to deduce the field with default value
                return res;
            }
            if( expr -> testHeader("List") && expr -> size() == targetParams.size() + 1 )
            {
                //if it is a correct shaped list, set parameters from it
                auto it = ++ expr->begin();
                for(const auto& x: targetParams)
                {
                    if( it -> index() == 1 )
                    {
                        const auto& subexp = *std::get<std::unique_ptr<Expression>>(*it);
                        if(subexp.isSymbol() && subexp.testHeader("Default"))
                            res.push_back({x, nullptr});
                        else
                            PostErrorMessage("ExportOVF", "badexp", ParamKeys.at(x), "either a numeric value or Default in the list");
                    }
                    else
                        res.push_back({x, &std::get<math_atom>(*it)});
                }
            }
            //else do nothing LULW
            PostErrorMessage("ExportOVF", "badexp", "CellSize", "either a length 3 list or Default");
            return {};
        }
    }
};

//exporting section
extern "C" void exportOVF(const char* fName, int optc)
{
#ifdef EXPANDPATH
    auto expansions = ExpandPath(fName);
    if(expansions.size() != 1)
    {
        PostErrorMessage("ExportOVF", "ambig", fName, expansions.size());
        deinit();
        WSPutSymbol(stdlink, "$Failed");
        return;
    }
    const std::filesystem::path output {expansions.front()};
#else
    const std::filesystem::path output {fName};
#endif //EXPANDPATH

    //parse all other inputs
    //first get the options
    const auto Options { ParseWSTPExpression(optc) };
    //4 byte floats by default, but allow for double precision fields too 
    const std::size_t ByteSize { static_cast<std::size_t>(ParseValue<long>(Options, "BinarySize").value_or(4)) };
    if(ByteSize != 4 && ByteSize != 8)
        PostErrorMessage("ExportOVF", "badsize", ByteSize);

    auto field {ParseWSTPData(ByteSize)};
    const auto HeaderRules {ParseWSTPExpression(1)};

    //on success end by returning a 'Null'
    deinit();
    WSPutSymbol(stdlink, "Null");
}

