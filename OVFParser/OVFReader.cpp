//implemetation of reading parts of interfaces defined in OVFParser
//alongside with internal data-structure, and associated fuckery
#include<fstream>
#include<utility>
#include<map>
#include<vector>
#include<array>
#include<regex>
#include<algorithm>
#include<execution>
#include<optional>
#include"OVFParser.h"
#include"OVFVersion.h"
#include"OVFDictionary.h"
//boost endian conversion library setup
#include<boost/endian/conversion.hpp>
#include<cstdint>

namespace VField{
    //first internal data of OVFReader
    struct VFieldFile::FileData
    {
        //segment storage
        //                      field             data begin pos                data size
        std::vector<std::tuple<VField, std::optional<std::ifstream::pos_type>, std::size_t>>
            segments{};
        //and the log storage
        std::string log{""};

        FileData() = default;
        FileData(const FileData&) = default;
    };

    //logger stuff
    inline void VFieldFile::logMessage(const std::string& msg) const
    {
        if(!data -> log.empty()) data->log += '\n';
        data->log += msg;
    }
    void VFieldFile::clearLog() const
    { data -> log = ""; }
    const std::string& VFieldFile::WorkLog() const
    { return data -> log; }

    //housekeeping
    VFieldFile::VFieldFile() noexcept
    {data = new FileData();}
    VFieldFile::~VFieldFile() noexcept
    {delete data;}
    VFieldFile::VFieldFile(const VFieldFile& ref) noexcept
    {
        try{
            data = new FileData(*ref.data);
        } catch (const std::exception& e){
            logMessage((std::string)"Error occured while copying data: " + e.what());
            return;
        }
        fPath = ref.fPath;
    }
    VFieldFile& VFieldFile::operator= (const VFieldFile& ref) noexcept
    {
        try{
            auto buf = new FileData(*ref.data);
            std::swap(data, buf);
            delete buf;
        } catch (const std::exception& e){
            logMessage((std::string)"Error occured while copying data: " + e.what());
            return *this;
        }
        fPath = ref.fPath;
        return *this;
    }

    //access stuff
    std::size_t VFieldFile::cntSegments() const noexcept
    { return data->segments.size();}
    //check if some data exists
    bool VFieldFile::isFetched(const std::size_t& index) const noexcept
    {
        if(index >= data->segments.size())
        {
            logMessage((std::string)"VFieldFile::isFetched: Index '" + std::to_string(index) +
                    "' is out of range [0, " + std::to_string(data->segments.size()) + ')');
            return false;
        }
        return std::get<0>(data->segments[index]).isDataPresent();
    }
    //iterator helpers 
    std::size_t VFieldFile::insert(VFieldFile::FieldIterator it, VField&& ref)
    {
        try{
            //checking for exception since VField can be pretty large
            data->segments.insert(data->segments.begin() + it.pos,
                    {ref, std::nullopt, ref.pntCount()});
        }catch (const std::exception& e) {
            logMessage("VFieldFile::insert: trouble inserting at position: "+ std::to_string(it.pos)+
                    ", exception was: " + e.what());
            return 0;
        }
        return 1;
    }
    std::size_t VFieldFile::remove(VFieldFile::FieldIterator it1, VFieldFile::FieldIterator it2)
    {
        data->segments.erase(
                data->segments.begin() + it1.pos,
                data->segments.end() + it2.pos);
        return 1;
    }

    //translate slice into range specifier
    inline std::array<std::size_t, 3> translateSlice(const VField& ref, const VFieldFile::slice_type& Slice)
    {
        auto end = ref.pntCount();
        return
        {
            !Slice.begin.isSpecial()? Slice.begin.getPos() : (Slice.begin == slice_pnt::begin ? 0 : end),
                !Slice.end.isSpecial()? Slice.end.getPos() : (Slice.end == slice_pnt::begin ? 0 : end),
                Slice.stride
        };
    }

    //////////////////////////////////////////////////////////
    /// main code for reading stuff based off of regexes /////
    //////////////////////////////////////////////////////////
    ///Break compilation if the float or double are not standard,
    //very sorry, the file is in binary :p
    static_assert(std::numeric_limits<double>::is_iec559, "The systems double is not IEC559 compatible");
    static_assert(std::numeric_limits<float>::is_iec559, "The systems float is not IEC559 compatible");
    //check if numerics are double by default
    static_assert(sizeof(1.0) == sizeof(double), "Double literals function unexpectedly");
    //and just for kicks
    static_assert(sizeof(1.0f) == sizeof(float), "Fload literals function unexpectedly");

    //shared flags
    constexpr auto commonFlags = std::regex_constants::icase |          //ignore case while matching
        std::regex_constants::ECMAScript;

    //and then regex generators
    std::regex regexToken(const std::string& token)
    { return std::regex("^#\\s*(" + token + ")\\s*:\\s*(.*?)\\s*(?:##.*)?$", commonFlags); }
    std::regex regexTokenValue(const std::string& token, const std::string& value)
    { return std::regex("^#\\s*(" + token + ")\\s*:\\s*(" + value + ")\\s*(?:##.*)?$", commonFlags); }

    //collection of patterns for all the parameters
    const std::map<OVFParameter, std::regex> TokenMap
    {
        { OVFParameter::Open,    regexToken("Begin")            },
        { OVFParameter::Close,   regexToken("End")              },
        { OVFParameter::Comment, std::regex("^#{2,}(.*)$")      },
        { OVFParameter::Desc,    regexToken("Desc")             },
        { OVFParameter::Munit,   regexToken("meshunit")         },
        { OVFParameter::Segcnt,  regexToken("Segment\\s+count") },
        { OVFParameter::Munit,   regexToken("meshunit")         },
        { OVFParameter::Vunit,   regexToken("valueunits?")      },//last s is optional for OVF 2
        { OVFParameter::Vmult,   regexToken("valuemultiplier")  },
        { OVFParameter::Vdim,    regexToken("valuedim")         },
        { OVFParameter::Xmin,    regexToken("xmin")             },
        { OVFParameter::Ymin,    regexToken("ymin")             },
        { OVFParameter::Zmin,    regexToken("zmin")             },
        { OVFParameter::Xmax,    regexToken("xmax")             },
        { OVFParameter::Ymax,    regexToken("ymax")             },
        { OVFParameter::Zmax,    regexToken("zmax")             },
        { OVFParameter::Bound,   regexToken("boundary")         },
        { OVFParameter::Vmax,    regexToken("ValueRangeMaxMag") },
        { OVFParameter::Vmin,    regexToken("ValueRangeMinMax") },
        { OVFParameter::Mtype,   regexToken("meshtype")         },
        { OVFParameter::Pcount,  regexToken("pointcount")       },
        { OVFParameter::Xbase,   regexToken("xbase")            },
        { OVFParameter::Ybase,   regexToken("ybase")            },
        { OVFParameter::Zbase,   regexToken("zbase")            },
        { OVFParameter::Xstep,   regexToken("xstepsize")        },
        { OVFParameter::Ystep,   regexToken("ystepsize")        },
        { OVFParameter::Zstep,   regexToken("zstepsize")        },
        { OVFParameter::Xnodes,  regexToken("xnodes")           },
        { OVFParameter::Ynodes,  regexToken("ynodes")           },
        { OVFParameter::Znodes,  regexToken("znodes")           },
        { OVFParameter::Title,   regexToken("title")            },
        { OVFParameter::Vlabels, regexToken("valuelabels")      },
        { OVFParameter::Empty,   std::regex("^#\\s*(##.*)?$")   } //includes the case of comment on empty line
    };

    //forward declarations of main functions
    //function to read the header, stops after reaching '# End: Header', stream is kept at just after end header line
    //returns the header and a log file, second argument is a line counter to be incremented
    std::string readHeader(std::istream&, std::size_t&, OVFHeader&);
    //read the data beginning with data header and ending all the way at '# End: Data', bool variable to tell if it is just a prefetch 
    std::string readData(std::istream&, VField&, const VFieldFile::slice_type&, std::size_t&, bool);

    //declaration of templated parse method
    template<pType p>
    inline std::optional<associatedType_t<p>> ParseToken(const std::string&);
    //specialisations for reading
    template<> inline std::optional<associatedType_t<pType::Uint>> ParseToken<pType::Uint>(const std::string& str)
    {
        try{ return stoul(str); }
        catch(const std::logic_error& e)
        { return std::nullopt; }
    }
    template<> inline std::optional<associatedType_t<pType::Float>> ParseToken<pType::Float>(const std::string& str)
    {
        try{ return stod(str); }
        catch(const std::logic_error& e)
        { return std::nullopt; }
    }
    template<> inline std::optional<associatedType_t<pType::String>> ParseToken<pType::String>(const std::string& str)
    { return str; }

    constexpr std::size_t BadBlockMax {5};
    //reading from file
    bool VFieldFile::read( const pathType& path, bool prefetch) noexcept
    {
        //reset the log
        clearLog();

        constexpr std::array<OVFParameter, 5> TopLevelTags{
            OVFParameter::Open,
            OVFParameter::Close,
            OVFParameter::Segcnt,
            OVFParameter::Empty,
            OVFParameter::Comment
        }; 
        //first try to open the file
        std::ifstream file(path, std::ios_base::binary);//TODO: check if opening it as binary from the start messes with getline
        if(!file.good())
        {
            logMessage((std::string)"VFieldFile::read: Error opening a file: " + path);
            return false;
        }
        //there is hope if we were able to open file, so erase old data
        data -> segments.clear();
        
        //else continue
        associatedType_t<pType::String> version{""};
        std::getline(file, version);
        if(!file.good())
        {
            logMessage((std::string)"VFieldFile::read: File ended abruptly while reading the header! path:" + path);
            return false;
        }

        //counters
        bool SegCntDefined{false}; std::size_t line_cnt{1};//file indexed from 1, line 1 being header
        std::size_t BadLineCnt{0};
        std::size_t seg_cnt{};
        bool WaitingForData{false}, SegmentOpened{false};
        //afterwards start parsing in a main loop
        while(file.good())
        {
            std::string buffer{};
            const auto pos = file.tellg(); //store the initial position in case one needs to seek back

            std::getline(file, buffer); line_cnt++;
            if(!file) //any error bit set but EOF
            {
                logMessage((std::string)"VFieldFile::read: " + ((file.rdstate()&std::ios_base::badbit)? "Unr":"R") +
                        "ecoverable error ocured while reading '" + path + "', line #" + std::to_string(line_cnt) + " aborting!");
                return false;
            }

            //otherwise check
            auto matchIt = std::find_if(TopLevelTags.begin(), TopLevelTags.end(),
                        [&](const OVFParameter& param)
                        {return std::regex_match(buffer, TokenMap.at(param));});
            //check if no match was found, i.e. line was invalid for being at a top level
            if( matchIt == TopLevelTags.end() )
            {
                logMessage((std::string)"VFieldFile::read: Encountered unexpected line # " + 
                        std::to_string(line_cnt) + ": ");
                if(++BadLineCnt < BadBlockMax) //truncate output if bad lines come one after another(like misalinged reading frame)
                    logMessage((std::string)"\t" + buffer.substr(0, 20) + "...");

                continue;
            }
            if( BadLineCnt != 0)
            {
                if( BadLineCnt >= BadBlockMax )
                {
                    logMessage("VFieldFile::read: Too many invalid lines in a row, suspending further output");
                    logMessage((std::string)"VFieldFile::read: Block of bad lines ended at line #" +
                            std::to_string(line_cnt - 1));
                }
                BadLineCnt = 0;
            }
            
            std::smatch res;
            switch(*matchIt)
            {
            //breaks here go out of switch only (I hope!)
            case(OVFParameter::Segcnt):
                if(SegCntDefined)
                {
                    logMessage((std::string)"VFieldFile::read: Segment count was redefined at the line #" + 
                            std::to_string(line_cnt) + " ! it is being ignored!");
                    break;
                }
                //else parse the segment count
                else
                {
                    std::regex_match(buffer, res, TokenMap.at(OVFParameter::Segcnt));//guaranteed to succeed
                    auto segCntParse = ParseToken<pType::Uint>(res[2].str());
                    if(segCntParse == std::nullopt)
                    {
                        logMessage((std::string)"VFieldFile::read: Could not parse the count of segments! line #" + 
                                std::to_string(line_cnt));
                        logMessage((std::string)"\t" + buffer);
                    }
                    seg_cnt = segCntParse.value();
                    SegCntDefined = true;
                }
                break;
            case(OVFParameter::Open):
                //much harder here, have to first decide what to call 
                //have to switch between opening of segment, data or header
                if(std::regex_match(buffer, regexTokenValue("Begin", "Segment")) )
                {
                    if(SegmentOpened)
                    {
                        logMessage((std::string)"VFieldFile::read: Duplicated opening of section encountered at line #" + 
                                std::to_string(line_cnt) + ", ignoring!");
                        break;
                    }
                    SegmentOpened = true;
                }
                else if(std::regex_match(buffer, regexTokenValue("Begin", "Header")) )
                {
                    if(!SegmentOpened)
                        logMessage((std::string)"VFieldFile::read: Found a segment header outside a segment on line #" +
                                std::to_string(line_cnt));
                    if(WaitingForData)
                        logMessage((std::string)"VFieldFile::read: Duplicate segment header on line #" + 
                                std::to_string(line_cnt));
                    //in either case read and start waiting for data
                    data -> 
                        segments.push_back({ VField(version),
                                             std::nullopt,
                                             0u });
                    //rewind back 1 line
                    file.seekg(pos);
                    //and read the header
                    auto log = readHeader(file, line_cnt, std::get<0>(data -> segments.back()).Header);
                    if(log != "")
                        logMessage("VFieldFile::read: Errors encountered while reading a Header ending at line #" +
                                std::to_string(line_cnt) + ":\n" + log);
                    WaitingForData = true;
                }
                else if(std::regex_match(buffer, regexTokenValue("Begin", "Data")) )
                {
                    if(!SegmentOpened)
                    {
                        logMessage((std::string)"VFieldFile::read: Found a segment data outside a segment on line #" +
                                std::to_string(line_cnt) + ", opening a new segment with empty header.");
                        SegmentOpened = true;
                        //open a new segment with empty header
                        data -> 
                            segments.push_back({ VField(version),
                                                 std::nullopt,
                                                 0u });
                    }
                    else if(!WaitingForData) // == !WaitingForData && SegmentOpened, missed header, or a duplicate data segment
                    {
                        logMessage((std::string)"VFieldFile::read: Unexpected segment data on line #" + 
                                std::to_string(line_cnt));
                        //now need to distinguish from having read a header and having a duplicated data
                        data -> 
                            segments.push_back({ VField(version),
                                                 std::nullopt,
                                                 0u });
                    }
                    //rewind back 1 line
                    file.seekg(pos); 
                    std::get<1>(data -> segments.back()) = pos;
                    auto log = readData(
                            file,
                            std::get<0>(data -> segments.back()),
                            VFieldFile::slice_type(),
                            std::get<2>(data -> segments.back()),
                            prefetch
                        );
                    if(log != "")
                        logMessage("VFieldFile::read: Errors encountered while reading Data at line #" +
                                std::to_string(line_cnt) + ":\n" + log);
                    line_cnt++; //increment line counter for end line after data
                }
                else
                {
                    logMessage((std::string)"VFieldFile::read: Encountered unknown section token on line #" + 
                            std::to_string(line_cnt) + " :");
                    logMessage(buffer);
                }
                break;
            case(OVFParameter::Close):
                {
                    if(std::regex_match(buffer, regexTokenValue("Begin","segment")))
                    {
                        if(SegmentOpened)
                            SegmentOpened = false;
                        else
                            logMessage((std::string)"VFieldFile::read: Unexpected statement at a line #" +
                                    std::to_string(line_cnt) + "continuing");
                        if(WaitingForData)
                        {
                            logMessage(
                               (std::string)"VFieldFile::read: Was expecting data section, got abrupt section ending at line #"+
                               std::to_string(line_cnt) + "instead!");
                            WaitingForData = false;
                        }
                    }
                    else //in case when it is either data or header, which should be handled in respective functions
                    {
                        logMessage((std::string)"VFieldFile::read: Unexpected end of block at line# " + std::to_string(line_cnt));
                        logMessage((std::string)"\t" + buffer);
                    }
                }
                break;
            default: //ignoring both comment and empty lines
                break;
            }
        }
        //in case bad lines were present until the end of file!
        if( BadLineCnt >= BadBlockMax )
        {
            logMessage("VFieldFile::read: Too many invalid lines in a row, suspending further output");
            logMessage((std::string)"VFieldFile::read: Block of bad lines ended at line #" +
                    std::to_string(line_cnt) + " (EOF)");
        }
        //bad bit error is handled inside the loop, reaching here necesarily means that EOF occured
        if( SegmentOpened || WaitingForData )
            logMessage("VFieldFile::read: File ended unexpectedly");
        if( data -> segments.size() != seg_cnt)
            logMessage((std::string)"VFieldFile::read: Got an unexpected number of segments from file: " +
                    std::to_string(data -> segments.size()) + " instead of expected: " +
                    (SegCntDefined? std::to_string(seg_cnt) : "undefined"));

        return data -> log == "";
    }

    //reading 
    std::string readHeader(std::istream& file, std::size_t& line_cnt, OVFHeader& head)
    {
        constexpr auto ValidParams = DictionaryHelpers::makeUnion(UINTParamList, FPParamList, StringParamList);
        constexpr auto AllowedOtherParams = DictionaryHelpers::make_array(
                    OVFParameter::Open,
                    OVFParameter::Close,
                    OVFParameter::Mtype,
                    OVFParameter::Empty,
                    OVFParameter::Comment
                );

        std::string log{""};
        std::string buffer{""};
        std::getline(file, buffer);
        if(!file.good() || !std::regex_match(buffer, regexTokenValue("Begin", "Header")) )
            return "readHeader: failed to find beginning of the header!";

        //otherwise start the main loop
        std::size_t BadLineCnt{0};
        while(file.good())
        {
            std::getline(file, buffer); line_cnt++;
            if(!file)
            {
                if(log != "") log += "\n";
                log += (std::string)"readHeader: " + ((file.rdstate()&std::ios_base::badbit)? "Unr" : "R") +
                       "ecoverable error occured while reading line #" + std::to_string(line_cnt) + "aborting!";
                return log; 
            }

            //first check if value is a normal value type
            std::smatch res;
            auto it = std::find_if(std::execution::seq, ValidParams.begin(), ValidParams.end(), 
                                   [&](const OVFParameter& p){return std::regex_match(buffer, res, TokenMap.at(p));});
            if(it != ValidParams.end())
            {
                //handling:
                //first check if parameter is already set
                if(head.isSet(*it) && *it != OVFParameter::Desc)
                {
                    if(log != "") log+= "\n";
                    log += (std::string)"readHeader: found a duplicate value of type: " + ParameterName(*it) +
                        "at line #" + std::to_string(line_cnt) + ", ignoring!";
                    continue;
                }
                //else set the value
                switch(paramIndex(*it))
                {
                case(pType::Uint):
                    {
                        auto pval = ParseToken<pType::Uint>(res[1].str());
                        if(pval == std::nullopt)
                        {
                            if(log != "") log+= "\n";
                            log+= (std::string)"readHeader: Error occured while parsing the unsigned integer token: \"" +
                                ParameterName(*it) + "\" at line #" + std::to_string(line_cnt)+ ", line content:\n" + buffer;
                            break;
                        }
                        head.set(*it, pval.value());
                    }
                    break;
                case(pType::Float):
                    {
                        auto pval = ParseToken<pType::Float>(res[1].str());
                        if(pval == std::nullopt)
                        {
                            if(log != "") log+= "\n";
                            log+= (std::string)"readHeader: Error occured while parsing the floating point token: \"" +
                                ParameterName(*it) + "\" at line #" + std::to_string(line_cnt)+ ", line content:\n" + buffer;
                            break;
                        }
                        head.set(*it, pval.value());
                    }
                    break;
                case(pType::String):
                    if(*it == OVFParameter::Desc)
                    {
                        head.at<pType::String>(OVFParameter::Desc) += "\n";
                        head.at<pType::String>(OVFParameter::Desc) += res[1].str();
                        break;
                    }
                    head.set(*it, res[1].str());
                default:
                    break;
                }
                continue;
            }

            //else try to match for one of the allowed other parameters
            it = std::find_if(std::execution::seq, AllowedOtherParams.begin(), AllowedOtherParams.end(),
                    [&](const OVFParameter& p){return std::regex_match(buffer, res, TokenMap.at(p));});

            if(it == AllowedOtherParams.end())
            {
                if(log != "") log += "\n";
                log+=(std::string)"readHeader: Encountered unexpected line # " + 
                        std::to_string(line_cnt) + ": ";
                if(++BadLineCnt < BadBlockMax) //truncate output if bad lines come one after another(like misalinged reading frame)
                {
                   log+= "\n"; 
                   log+=(std::string)"\t" + buffer.substr(0, 20) + "...";
                }
            }
            
            if( BadLineCnt != 0)
            {
                if( BadLineCnt >= BadBlockMax )
                {
                    log+= "\nreadHeader: Too many invalid lines in a row, suspending further output";
                    log+= "\nreadHeader: Block of bad lines ended at line #" +
                            std::to_string(line_cnt - 1);
                }
                BadLineCnt = 0;
            }
            //else can again switch on a type of parameter
            switch(*it)
            {
            case(OVFParameter::Open):
                if(log != "") log += "\n";
                log+= (std::string)"readHeader: opening a section prematurely at a line #" + std::to_string(line_cnt) +
                    ":\n" + buffer;
                break;
            case(OVFParameter::Close):
                if(std::regex_match(buffer, regexTokenValue("End", "Header")))
                {
                    if( BadLineCnt >= BadBlockMax )
                    {
                        log+="VFieldFile::read: Too many invalid lines in a row, suspending further output";
                        log+=(std::string)"VFieldFile::read: Block of bad lines ended at line #" +
                            std::to_string(line_cnt) + " (end of header)";
                    }
                    return log; //successfully finished reading the header
                }
                //else it is an error and should be reported
                if(log != "") log += "\n";
                log+= (std::string)"readHeader: found premature close of a section at line #" + std::to_string(line_cnt) +
                    ":\n" + buffer;
            case(OVFParameter::Mtype):
                if(head.isSet(OVFParameter::Mtype))
                {
                    if(log != "") log += "\n";
                    log+= (std::string)"readHeader: Trying to redefine mesh type at line #" + std::to_string(line_cnt);
                    break;
                }
                if(std::regex_match(buffer, regexTokenValue("Meshtype", "rectangular")))
                    head.setMesh(OVFHeader::MeshType::rectangular);
                else if(std::regex_match(buffer, regexTokenValue("Meshtype", "irregular")))
                    head.setMesh(OVFHeader::MeshType::irregular);
                else
                { if (log != "") log+= "\n"; log += (std::string)"readHeader: Invalid mesh type token was passed at line #" +
                    std::to_string(line_cnt) + ": \"" + res[1].str() + "\"";}
                break;
            default://skip comments and empty lines
                break;
            }
        }
        if( BadLineCnt >= BadBlockMax )
        {
            log+="VFieldFile::read: Too many invalid lines in a row, suspending further output";
            log+=(std::string)"VFieldFile::read: Block of bad lines ended at line #" +
                    std::to_string(line_cnt) + " (EOF)";
        }
        return log;
    }

    //and then for the cream of the crop, header reader!
    //test constants
    template<typename T>
    constexpr T TestVal;
    
    template<> constexpr float TestVal<float> = 1234567.0f;
    template<> constexpr double TestVal<double> = 123456789012345.0;
    
    //main method
    //TODO: implement OVF0 reading at some point
    std::string readData(std::istream& file, VField& out, const VFieldFile::slice_type& slice, std::size_t& cnt, bool prefetch)
    {
        auto version = (out.Header.isSet(OVFParameter::VersionString))? 
            matchVersionString(out.Header.getString(OVFParameter::VersionString)) : OVFVersion::Unknown; 
        std::string dataHeader {""};
        std::getline(file, dataHeader);
        if(!file)
            return "readData: unexpected error occured while reading file";
        // next check if data header is valid
        std::smatch match;
        if(!std::regex_match(dataHeader, match, regexTokenValue("Begin","Data*\\s+(binary\\s+(4|8)|text)")))
            return (std::string)"readData: Ill formed data begin line: \"" + dataHeader + "\"";
        bool isBinary = !std::regex_match(dataHeader, regexTokenValue("Begin", "Data\\s+text"));
        std::size_t internalSize = isBinary? ParseToken<pType::Uint>(match[4].str()).value() : 8; // guaranteed to have value from previous lines
        const auto DataBeginPos {file.tellg()};
        //next peek if data is ending at expected position
        const std::size_t advertisedDim {out.pntDimension()};
        const std::size_t advertisedCnt {(cnt != 0 && advertisedDim != 0)? cnt/advertisedDim : out.pntCount()};
        auto endRegex = regexTokenValue("Begin", (std::string)"" + (isBinary? 
                    ((std::string)"binary\\s+" + std::to_string(internalSize)) : "text"));
        //seeking to expected end
        if((advertisedDim * advertisedCnt != 0) || cnt !=0 ) 
        {
            std::size_t pnts = cnt;
            if(pnts == 0) pnts = advertisedCnt * advertisedDim;
            if(isBinary)
                file.ignore( (pnts + 1)  * internalSize / sizeof(std::istream::char_type) );
            else
                for(std::size_t i = 0; i < advertisedCnt && file.good(); i++)
                    file.ignore( std::numeric_limits<std::streamsize>::max(), '\n');
        }
        if(!file.good())
            return "readData: reached the end of file searching for the end of data section!";
        auto DataEndPos {file.tellg()};
        std::string closingString{""};
        std::getline(file, closingString);
        std::string log {""};
        if(std::regex_match(closingString, regexTokenValue("End","Data")))
            cnt = advertisedDim * advertisedCnt;
        else
        {
            //if didn't got a correct line have to reseek manually
            file.seekg(DataBeginPos);
            while(file.good())
            {
                file.ignore( std::numeric_limits<std::streamsize>::max(), '#'); //seek until next line
                if(!file.good())
                    return "readData: reached the end of file searching for end of data manually :'(";
                file.unget(); //push # back into stream
                DataEndPos = file.tellg();
                std::getline(file, closingString);
                if(std::regex_match(closingString, regexTokenValue("End", "Data")))
                    break;
                if(!file.good())
                    return "readData: reached the end of file searching for end of data manually ";
            }
            log = "Found the end of data manually";
            if( isBinary)
                cnt = (DataEndPos - DataBeginPos)/internalSize - 1; //last constant for test value
            else//is text
            {
                cnt = 0;
                file.seekg(DataBeginPos);
                while(file.tellg() < DataEndPos && file.good())
                {
                    file.ignore( std::numeric_limits<std::streamsize>::max(), '\n');
                    cnt++;
                }
                cnt *= advertisedDim;
                file.ignore( std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }
        if(!std::regex_match(closingString, endRegex)) //stricter check
        {
            if(log != "") log += "\n";
            log = (std::string)"readData: failed strict check of data type in closing section, got: " + closingString;
        }
        if(slice == VFieldFile::slice_type() && advertisedDim == 0)
        {
            if(log !="") log+= "\n";
            log += "readData: Cannot read a slice without properly-defined dimension";
            return log;
        }
        const auto AfterDataEnd {file.tellg()};

        if(!prefetch)
        {
            file.seekg(DataBeginPos);
            //actual reading of data
            auto [begin, end, stride] = translateSlice(out, slice); //hurray for structural binding
            if(advertisedDim * advertisedCnt != cnt) //if had to adjust  
            {
                if(slice.begin == slice_pnt::end)
                    begin = std::min(cnt/advertisedDim, begin);
                if(slice.end == slice_pnt::end)
                    end = std::min(cnt/advertisedDim, end);
            }
            if(end < begin) std::swap(begin, end);
            if(end == begin) 
            {
                file.seekg(AfterDataEnd);
                return log; //nothing to import, EZ
            }
            const bool importWhole {slice == VFieldFile::slice_type()};
            if(isBinary)
            {
                if(internalSize == 4)
                {
                    float test{};
                    file>>test;
                    if( (version == OVFVersion::OVF1 && boost::endian::order::native == boost::endian::order::little) ||
                        (version == OVFVersion::OVF2 && boost::endian::order::native == boost::endian::order::big) )
                        boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint32_t*>(&test));
                        //WARNING: causes warning in gcc :'(
                    if(test != TestVal<float>)
                    {
                        if(log!="") log += "\n";
                        log+= "readData: binary data (4-byte) has a wrong test magic number!";
                        return log;
                    }
                    //then seek the first value
                    file.ignore(begin * sizeof(float) / sizeof(std::istream::char_type));
                    const std::size_t importDepth {importWhole? cnt : ((end - begin - 1) * advertisedDim)};
                    auto buffer = new float[importDepth];
                    file.read(reinterpret_cast<std::istream::char_type*>(buffer), 
                              importDepth * sizeof(float)/sizeof(std::istream::char_type));
                    if(!file.good())
                    {
                        if(log != "") log += "";
                        log+= "readData: Unexpected end of data";
                        return log;
                    }
                    if( (version == OVFVersion::OVF1 && boost::endian::order::native == boost::endian::order::little) ||
                        (version == OVFVersion::OVF2 && boost::endian::order::native == boost::endian::order::big) )
                        for(std::size_t i =0; i < importDepth; i++)
                            boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint32_t*>(buffer + i));
                    if(stride != 1)
                    {
                        auto res = new float[importDepth / stride];
                        for(std::size_t i = 0; i < importDepth / stride / advertisedDim; i++)
                            for(std::size_t j = 0; j < advertisedDim; j++)
                                res[i * advertisedDim + j] = buffer[i * stride * advertisedDim + j];
                        std::swap(res, buffer);
                        delete[] res;
                        out.setData(buffer, importDepth / stride);
                        return log;
                    }
                    out.setData(buffer, importDepth);
                    return log; 
                }
                if(internalSize == 8)
                {
                    double test{};
                    file>>test;
                    if( (version == OVFVersion::OVF1 && boost::endian::order::native == boost::endian::order::little) ||
                        (version == OVFVersion::OVF2 && boost::endian::order::native == boost::endian::order::big) )
                        boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint64_t*>(&test));
                    if(test != TestVal<double>)
                    {
                        if(log!="") log += "\n";
                        log+= "readData: binary data (4-byte) has a wrong test magic number!";
                        return log;
                    }
                    //then seek the first value
                    file.ignore(begin * sizeof(double) / sizeof(std::istream::char_type));
                    const std::size_t importDepth {importWhole? cnt : ((end - begin - 1) * advertisedDim)};
                    auto buffer = new double[importDepth];
                    file.read(reinterpret_cast<std::istream::char_type*>(buffer), 
                              importDepth * sizeof(double)/sizeof(std::istream::char_type));
                    if(!file.good())
                    {
                        if(log != "") log += "";
                        log+= "readData: Unexpected end of data";
                        return log;
                    }
                    if( (version == OVFVersion::OVF1 && boost::endian::order::native == boost::endian::order::little) ||
                        (version == OVFVersion::OVF2 && boost::endian::order::native == boost::endian::order::big) )
                        for(std::size_t i =0; i < importDepth; i++)
                            boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint64_t*>(buffer + i));
                    if(stride != 1)
                    {
                        auto res = new double[importDepth / stride];
                        for(std::size_t i = 0; i < importDepth / stride / advertisedDim; i++)
                            for(std::size_t j = 0; j < advertisedDim; j++)
                                res[i * advertisedDim + j] = buffer[i * stride * advertisedDim + j];
                        std::swap(res, buffer);
                        delete[] res;
                        out.setData(buffer, importDepth / stride);
                        return log;
                    }
                    out.setData(buffer, importDepth);
                    return log; 
                }
            }
            else //data is in text format
            {

                if(advertisedDim == 0)
                {
                    if(log != "") log+= "\n";
                    log += "readData: Text data import is impossible without known dimension, stopping!";
                    return log;
                }

                std::string line{""};
                const std::size_t importDepth {importWhole? cnt : ((end - begin - 1) * advertisedDim)};
                auto buffer = new double[importDepth];
                //main loop implementation here
                std::size_t line_cnt{0};
                const std::regex tokenizer ("^\\s*([^\\s]+)(?:\\s+|$)", std::regex_constants::ECMAScript |
                                                                        std::regex_constants::optimize);
                //TODO: check if comments are allowed
                while(file.good())
                {
                    //first skip forward to beginning of data
                    std::size_t skip_cnt {begin};
                    while(skip_cnt != 0 && file.good())
                    { file.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); skip_cnt--;}
                    std::getline(file, line);
                    if(!file)
                    {
                        if(log != "") log += "\n";
                        log += "readData: Unexpected file read error!";
                        delete[] buffer;
                        return log;
                    }
                    std::size_t count {0};
                    std::smatch sm;
                    while(std::regex_search(line, sm, tokenizer))
                    {
                        auto val = ParseToken<pType::Float>(sm[1].str());
                        if(val == std::nullopt)
                            break;
                        buffer[line_cnt * advertisedDim + count++] = val.value();
                        line = sm.suffix();
                    }
                    if(count != advertisedDim - 1)
                    {
                        if( log != "" ) log+= "\n";
                        log += (std::string)"readData: Unexpected number of values on line #"
                               +std::to_string(line_cnt);
                        delete[] buffer;
                        return log;
                    }
                    if(line_cnt == importDepth/advertisedDim) //TODO: check for error by 1 later
                        break;
                }
                if(stride != 1)
                {
                    auto res = new double[importDepth / stride];
                    for(std::size_t i = 0; i < importDepth / stride / advertisedDim; i++)
                        for(std::size_t j = 0; j < advertisedDim; j++)
                            res[i * advertisedDim + j] = buffer[i * stride * advertisedDim + j];
                    std::swap(res, buffer);
                    delete[] res;
                    out.setData(buffer, importDepth / stride);
                    return log;
                }
                out.setData(buffer, importDepth);
                return log; 
            }
            file.seekg(AfterDataEnd);
        }

        return log;
    }

    //and now more high-level interfaces
    VField& VFieldFile::operator[] (const std::size_t& index) 
    {
        auto& [field, pos, size] = data->segments.at(index);
        if(pos == std::nullopt && size == 0)
        {
            logMessage("VFieldFile::operator[]:  during prefetch phase no data was found!");
            return field;
        }
        if(field.isDataPresent())
            return field;

        //else read the data and return that
        std::ifstream file(fPath, std::ios_base::binary);
        file.seekg(pos.value());
        if(!file.good())
        {
            logMessage("VFieldFile::operator[]: error opening file!");
            return field;
        }
        auto log = readData(file, field, VFieldFile::slice_type(), size, false); 
        if(log != "")
        {
            logMessage("VFieldFile::operator[]: errors occured while reading data:\n");
            logMessage(log);
        }
        return field;
    }
    VField VFieldFile::operator[] (const std::size_t& index) const noexcept
    {
        //first, check if index is OOB
        if(index >= data->segments.size())
        {
            logMessage("VFieldFile::operator[]: index out of range!");
            return {};
        }
        //then check the element
        auto [field, pos, size] = data->segments[index];
        if(pos == std::nullopt && size == 0)
        {
            logMessage("VFieldFile::operator[]:  during prefetch phase no data was found!");
            return std::move(field);
        }
        if(field.isDataPresent())
            return std::move(field);

        //else read the data and return that
        std::ifstream file(fPath, std::ios_base::binary);
        file.seekg(pos.value());
        if(!file.good())
        {
            logMessage("VFieldFile::operator[]: error opening file!");
            return std::move(field);
        }
        auto log = readData(file, field, VFieldFile::slice_type(), size, false); 
        if(log != "")
        {
            logMessage("VFieldFile::operator[]: errors occured while reading data:\n");
            logMessage(log);
        }
        return std::move(field);
    }
    //templates for slicing vfields after import
    //TODO: check if doing in-place version instead is better
    template<typename T>
    VField SliceVField(VField val, const VFieldFile::slice_type& slice)
    {
        //beginning iterator
        auto beginIt = val.cbegin<T>();
        //and teh dimension
        const auto dim{val.pntDimension()};
        //and translate slice into a specification for bounds
        const auto [begin, end, stride] = translateSlice(val, slice);
        const std::size_t newSize {val.pntCount() / stride * dim };
        //and start this bad boy up
        auto buffer = new T[newSize];
        for(auto cnt = begin;begin <= end; cnt += stride)
            for(std::size_t i = 0; i < dim; i++)
                buffer[dim * cnt + i] = (beginIt + cnt)[i];
        val.setData(buffer, newSize);
        return val;
    }
    //and slice for rectangular grid
    template<typename T>
    VField SliceVField(VField val, const VFieldFile::slice_type& xslice,
                                   const VFieldFile::slice_type& yslice,
                                   const VFieldFile::slice_type& zslice)
    {
        if(!val.isAddressable() || val.Header.getMeshType() != OVFHeader::MeshType::rectangular)
            return {};
        //beginning iterator
        auto beginIt = val.cbegin<T>();
        const auto XNodeCnt = val.Header.getUint(OVFParameter::Xnodes);
        const auto YNodeCnt = val.Header.getUint(OVFParameter::Ynodes);
        const auto ZNodeCnt = val.Header.getUint(OVFParameter::Znodes);
        std::size_t slices[9];
        const std::size_t dim { val.pntDimension() };
        std::size_t vCount{1u};
        //convert slices into real coordinates
        for(const auto& x: {std::make_tuple(XNodeCnt, xslice, slices),
                            std::make_tuple(YNodeCnt, yslice, slices+3),
                            std::make_tuple(ZNodeCnt, zslice, slices+6)})
        {
            auto& begin = *std::get<2>(x);//get the points from slices array
            auto& end   = *(std::get<2>(x) + 1);
            auto& stride= *(std::get<2>(x) + 2);
            const auto& maxVal = std::get<0>(x);
            const auto& slice = std::get<1>(x);
            stride = slice.stride; //easy
            begin = !slice.begin.isSpecial()? slice.begin.getPos() : ((slice.begin == slice_pnt::begin)? 0 : maxVal);
            end = !slice.end.isSpecial()? slice.end.getPos() : ((slice.end == slice_pnt::begin)? 0 : maxVal);
            if(begin > end)
                std::swap(begin, end);
            vCount *= 1 + (end - begin + 1) / stride;
            if( begin > maxVal || end > maxVal) // no need to check for <0, unsigned types
                return {};
        }
        //then recalculate how large of an array is needed
        vCount *= dim;
        auto buffer = new T[vCount];
        //whole load of magic values INC
        for(auto k = slices[6]; k <= slices[7]; k+= slices[8]) //z cycle
        {
            for(auto j = slices[3]; j <= slices[4]; j+= slices[5]) //y cycle
            {
                for(auto i = slices[0]; i <= slices[1]; i+= slices[2]) //x cycle
                    for(std::size_t p = 0; p < dim; p++)//point values
                        buffer[(k * XNodeCnt * YNodeCnt + j * XNodeCnt + i) * dim + p] =
                            (beginIt + k * XNodeCnt * YNodeCnt + j * XNodeCnt + i)[p];
            }
        }
        if(val.Header.validate())//only do the coordinate resize if header was valid to begin with
        {
            //first set the correct node counts
            val.Header.at<pType::Uint>(OVFParameter::Xnodes) = 1 + (slices[1]-slices[0])/slices[2];
            val.Header.at<pType::Uint>(OVFParameter::Xnodes) = 1 + (slices[4]-slices[3])/slices[5];
            val.Header.at<pType::Uint>(OVFParameter::Xnodes) = 1 + (slices[7]-slices[6])/slices[8];
            //then set correct initial position
            val.Header.at<pType::Float>(OVFParameter::Xbase) += val.Header.at<pType::Float>(OVFParameter::Xstep) * slices[0];
            val.Header.at<pType::Float>(OVFParameter::Ybase) += val.Header.at<pType::Float>(OVFParameter::Ystep) * slices[3];
            val.Header.at<pType::Float>(OVFParameter::Zbase) += val.Header.at<pType::Float>(OVFParameter::Zstep) * slices[6];
            //and then, finaly, set correct steps
            val.Header.at<pType::Float>(OVFParameter::Xstep) *= slices[2];
            val.Header.at<pType::Float>(OVFParameter::Ystep) *= slices[5];
            val.Header.at<pType::Float>(OVFParameter::Zstep) *= slices[8];
        }

        val.setData(buffer, vCount);
        return val;
    }

    //and then slice reads interfaces
    VField VFieldFile::readSlice(const std::size_t& index, const slice_type& slice) const noexcept
    {
        //first, check if index is OOB
        if(index >= data->segments.size())
        {
            logMessage("VFieldFile::readSlice: index out of range!");
            return {};
        }
        //then check the element
        auto [field, pos, size] = data->segments[index];
        if(pos == std::nullopt && size == 0)
        {
            logMessage("VFieldFile::readSlice:  during prefetch phase no data was found!");
            return std::move(field);
        }
        if(!field.isWeaklyAddressable())
        {
            logMessage("VFieldFile:readSlice: VField is not weakly addressable, abborting!");
            return {};
        }
        //if field is not here it is time to import it
        if(!field.isDataPresent())
        {
            //read the data and return that
            std::ifstream file(fPath, std::ios_base::binary);
            file.seekg(pos.value());
            if(!file.good())
            {
                logMessage("VFieldFile::operator[]: error opening file!");
                return std::move(field);
            }
            auto log = readData(file, field, slice, size, false); 
            if(log != "")
            {
                logMessage("VFieldFile::operator[]: errors occured while reading data:\n");
                logMessage(log);
            }
            return std::move(field);
        }
        //else slice vfield
        if(field.curDataInternalSize() == 4)
            return SliceVField<float>(field, slice);
        else if(field.curDataInternalSize() == 8)
            return SliceVField<double>(field, slice);
        else
            //unreachable branch, hope compiler prunes it
            return {};
    }
    VField VFieldFile::readSlice(const std::size_t& index,
                                 const VFieldFile::slice_type& xslice,
                                 const VFieldFile::slice_type& yslice,
                                 const VFieldFile::slice_type& zslice) const noexcept
    {
        //first, check if index is OOB
        if(index >= data->segments.size())
        {
            logMessage("VFieldFile::readSlice: index out of range!");
            return {};
        }
        //then check the element
        auto [field, pos, size] = data->segments[index];
        if(pos == std::nullopt && size == 0)
        {
            logMessage("VFieldFile::readSlice:  during prefetch phase no data was found!");
            return std::move(field);
        }
        if(!field.isAddressable())
        {
            logMessage("VFieldFile:readSlice: VField is not addressable, abborting!");
            return {};
        }
        //if field is not here it is time to import it
        if(!field.isDataPresent())
        {
            //read the data and return that
            std::ifstream file(fPath, std::ios_base::binary);
            file.seekg(pos.value());
            if(!file.good())
            {
                logMessage("VFieldFile::operator[]: error opening file!");
                return std::move(field);
            }
            //TODO: look into optimizing by translating three splices into one beforehand
            auto log = readData(file, field, {}, size, false); 
            if(log != "" || !field.isDataPresent())
            {
                logMessage("VFieldFile::operator[]: errors occured while reading data:\n");
                logMessage(log);
            }
        }
        //else slice vfield
        if(field.curDataInternalSize() == 4)
            return SliceVField<float>(field, xslice, yslice, zslice);
        else if(field.curDataInternalSize() == 8)
            return SliceVField<double>(field, xslice, yslice, zslice);
        else
            //unreachable branch, hope compiler prunes it
            return {};
    }
}

