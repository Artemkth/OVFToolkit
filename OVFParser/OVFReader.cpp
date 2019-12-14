//implemetation of reading parts of interfaces defined in OVFParser
//alongside with internal data-structure, and associated fuckery
#include<fstream>
#include<utility>
#include<map>
#include<vector>
#include<array>
#include<regex>
#include<algorithm>
#include<optional>
#include"OVFParser.h"
#include"OVFVersion.h"
#include"OVFDictionary.h"

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
    std::string readData(std::istream&, VField&, const VFieldFile::slice_type&, bool&);

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

    //reading from file
    bool VFieldFile::read( const pathType& path, bool prefetch) noexcept
    {
        //reset the log
        clearLog();

        constexpr std::size_t BadBlockMax {5};
        constexpr std::array<OVFParameter, 5> TopLevelTags{
            OVFParameter::Open,
            OVFParameter::Close,
            OVFParameter::Segcnt,
            OVFParameter::Empty,
            OVFParameter::Comment
        }; 
        //first try to open the file
        std::ifstream file(path);
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
                    std::to_string(line_cnt));
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

    //helper method to set a field
}

