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
#include"OVFDictionary.h"
//boost endian conversion library setup
#include<boost/endian/conversion.hpp>
#include<cstdint>

namespace VField{
    //first internal data of OVFReader
    struct VFieldFile::FileData
    {
        using PrefetchData = std::pair<std::optional<std::ifstream::pos_type>, std::size_t>;

        //Fields are kept separately so the public interface can expose a contiguous span.
        std::vector<VField> fields{};
        std::vector<PrefetchData> prefetch{};
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
    const std::string& VFieldFile::WorkLog() const
    { return data -> log; }

    //housekeeping
    VFieldFile::VFieldFile(): data(std::make_unique<FileData>()) {}
    VFieldFile::~VFieldFile() = default;
    VFieldFile::VFieldFile(const VFieldFile& ref):
        fPath(ref.fPath), data(std::make_unique<FileData>(*ref.data)) {}
    VFieldFile& VFieldFile::operator= (const VFieldFile& ref)
    {
        if (this == &ref)
            return *this;

        auto copy = ref;
        *this = std::move(copy);
        return *this;
    }
    VFieldFile::VFieldFile(VFieldFile&&) noexcept = default;
    VFieldFile& VFieldFile::operator= (VFieldFile&&) noexcept = default;

    //access stuff
    std::size_t VFieldFile::cntSegments() const noexcept
    { return data->fields.size();}
    //check if some data exists
    bool VFieldFile::isFetched(std::size_t index) const noexcept
    {
        if(index >= data->fields.size())
            return false;
        return data->fields[index].isDataPresent();
    }
    bool VFieldFile::unfetch(std::size_t index) noexcept
    {
        if(index >= data->fields.size())
            return false;
        data->fields[index].clearData();
        return true;
    }
    bool VFieldFile::hasData(std::size_t index) const noexcept
    {
        if(index >= data->fields.size())
            return false;
        if( data->fields[index].isDataPresent() )
            return true;
        //otherwise return if the data was found during prefetch
        return data->prefetch[index].first.has_value() &&
               data->prefetch[index].second != 0;
    }

    std::span<VField> VFieldFile::fieldView()
    {
        for (std::size_t index = 0; index < cntSegments(); ++index)
            fetch(index);
        return data->fields;
    }

    std::span<const VField> VFieldFile::fieldView() const
    {
        for (std::size_t index = 0; index < cntSegments(); ++index)
            fetch(index);
        return data->fields;
    }

    //////////////////////////////////////////////////////////
    /// main code for reading stuff based off of regexes /////
    //////////////////////////////////////////////////////////

    //shared flags
    constexpr auto commonFlags = std::regex_constants::icase |          //ignore case while matching
        std::regex_constants::ECMAScript;

    //and then regex generators
    std::regex regexToken(const std::string& token)
    { return std::regex("^#\\s*(" + token + ")\\s*:\\s*(.*?)\\s*(?:##.*)?$", commonFlags); }
    std::regex regexTokenValue(const std::string& token, const std::string& value)
    { return std::regex("^#\\s*(" + token + ")\\s*:\\s*(" + value + ")\\s*(?:##.*)?$", commonFlags); }

    //Reader patterns are derived from the canonical dictionary tokens. The
    //exceptions accept syntax shared by multiple OVF versions or structural
    //lines which do not have a conventional token/value pair.
    const std::map<OVFParameter, std::regex> TokenMap = []
    {
        std::map<OVFParameter, std::regex> result{
            {OVFParameter::Comment, std::regex("^#{2,}(.*)$")},
            {OVFParameter::Empty, std::regex("^#\\s*(##.*)?$")}
        };

        for(const auto& descriptor: ParamTable)
        {
            const auto parameter = descriptor.parameter;
            if(parameter == OVFParameter::Comment || parameter == OVFParameter::Empty)
                continue;

            if(parameter == OVFParameter::Vunit)
            {
                result.emplace(parameter, regexToken("valueunits?"));
                continue;
            }
            if(parameter == OVFParameter::Segcnt)
            {
                result.emplace(parameter, regexToken("Segment\\s+count"));
                continue;
            }

            const auto token = paramToken(parameter, OVFVersion::OVF2);
            if(token.has_value())
                result.emplace(parameter, regexToken(std::string(*token)));
        }
        return result;
    }();

    //forward declarations of main functions
    //function to read the header, stops after reaching '# End: Header', stream is kept at just after end header line
    //returns the header and a log file, second argument is a line counter to be incremented
    std::string readHeader(std::istream&, std::size_t&, OVFHeader&);
    //read the data beginning with data header and ending all the way at '# End: Data', bool variable to tell if it is just a prefetch 
    using PointRange = std::pair<std::size_t, std::size_t>; //first point, point count
    std::string readData(std::istream&, VField&, std::optional<PointRange>, std::size_t&, bool);

    //declaration of templated parse method
    template<ParameterType p>
    inline std::optional<parameter_cpp_type_t<p>> ParseToken(const std::string&);
    //specialisations for reading
    template<> inline std::optional<parameter_cpp_type_t<ParameterType::Unsigned>> ParseToken<ParameterType::Unsigned>(const std::string& str)
    {
        try{ return stoul(str); }
        catch(const std::logic_error&)
        { return std::nullopt; }
    }
    template<> inline std::optional<parameter_cpp_type_t<ParameterType::Floating>> ParseToken<ParameterType::Floating>(const std::string& str)
    {
        try{ return stod(str); }
        catch(const std::logic_error&)
        { return std::nullopt; }
    }
    template<> inline std::optional<std::string> ParseToken<ParameterType::String>(const std::string& str)
    { return str; }

    constexpr std::size_t BadBlockMax {5};
    //reading from file
    bool VFieldFile::read( const pathType& path, bool prefetch) noexcept
    {
        //A read always starts at the beginning of a file and owns a fresh set
        //of diagnostics. Lazy-access diagnostics may be appended afterwards.
        data->log.clear();
        fPath = path;

        constexpr std::array<OVFParameter, 5> TopLevelTags{
            OVFParameter::Open,
            OVFParameter::Close,
            OVFParameter::Segcnt,
            OVFParameter::Empty,
            OVFParameter::Comment
        }; 
        //first try to open the file
        std::ifstream file(fPath, std::ios_base::binary);//TODO: check if opening it as binary from the start messes with getline
        if(!file.good())
        {
            logMessage((std::string)"VFieldFile::read: Error opening a file: " + path);
            return false;
        }
        //there is hope if we were able to open file, so erase old data
        data -> fields.clear();
        data -> prefetch.clear();
        
        //else continue
        std::string version{""};
        std::getline(file, version);
        if(!file.good())
        {
            logMessage((std::string)"VFieldFile::read: File ended abruptly while reading the header! path:" + path);
            return false;
        }
        if(matchVersionString(version) == OVFVersion::Unknown)
        {
            logMessage((std::string)"VFieldFile::read: File \"" + path + 
                    "\" is not a valid OVF file, invalid Header: " + version.substr(0, 20) + 
                    ((version.length() > 21)?"...":"") + "\"");
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
            //TODO: evaluate this since it is non-standard conforming, but both mumax and oommf do it
            if(buffer == "" && file.eof())
                break;

            if(!file.good() && !file.eof()) //any error bit set but EOF
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
                if(++BadLineCnt < BadBlockMax) //truncate output if bad lines come one after another(like misalinged reading frame)
                    logMessage((std::string)"VFieldFile::read: Encountered unexpected line # " + 
                            std::to_string(line_cnt) + ": \"" + buffer.substr(0, 20) + ((buffer.length() > 21)?"...":"") + "\"");;

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
                    auto segCntParse = ParseToken<ParameterType::Unsigned>(res[2].str());
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
                    data->fields.emplace_back(version);
                    data->prefetch.emplace_back(std::nullopt, 0u);
                    //rewind back 1 line
                    file.seekg(pos);
                    //and read the header
                    auto log = readHeader(file, line_cnt, data->fields.back().header());
                    if(log != "")
                        logMessage("VFieldFile::read: Errors encountered while reading a Header ending at line #" +
                                std::to_string(line_cnt) + ":\n" + log);
                    WaitingForData = true;
                }
                else if(std::regex_match(buffer, regexTokenValue("Begin", "Data\\s+.+?")) )
                {
                    if(!SegmentOpened)
                    {
                        logMessage((std::string)"VFieldFile::read: Found a segment data outside a segment on line #" +
                                std::to_string(line_cnt) + ", opening a new segment with empty header.");
                        SegmentOpened = true;
                        //open a new segment with empty header
                        data->fields.emplace_back(version);
                        data->prefetch.emplace_back(std::nullopt, 0u);
                    }
                    else if(!WaitingForData) // == !WaitingForData && SegmentOpened, missed header, or a duplicate data segment
                    {
                        logMessage((std::string)"VFieldFile::read: Unexpected segment data on line #" + 
                                std::to_string(line_cnt));
                        //now need to distinguish from having read a header and having a duplicated data
                        data->fields.emplace_back(version);
                        data->prefetch.emplace_back(std::nullopt, 0u);
                    }
                    //rewind back 1 line
                    file.seekg(pos); 
                    data->prefetch.back().first = pos;
                    auto log = readData(
                            file,
                            data->fields.back(),
                            std::nullopt,
                            data->prefetch.back().second,
                            prefetch
                        );
                    if(log != "")
                        logMessage("VFieldFile::read: Errors encountered while reading Data at line #" +
                                std::to_string(line_cnt) + ":\n" + log);
                    WaitingForData = false;
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
                    if(std::regex_match(buffer, regexTokenValue("End","segment")))
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
        if( data -> fields.size() != seg_cnt )
            logMessage((std::string)"VFieldFile::read: Got an unexpected number of segments from file: " +
                    std::to_string(data -> fields.size()) + " instead of expected: " +
                    (SegCntDefined? std::to_string(seg_cnt) : "undefined"));

        return data -> log == "";
    }

    //reading 
    std::string readHeader(std::istream& file, std::size_t& line_cnt, OVFHeader& head)
    {
        constexpr auto ValidParams = DictionaryHelpers::removeValue( DictionaryHelpers::makeUnion(UINTParamList, FPParamList, StringParamList), OVFParameter::VersionString );
        constexpr auto AllowedOtherParams = std::array{
                    OVFParameter::Open,
                    OVFParameter::Close,
                    OVFParameter::Mtype,
                    OVFParameter::Empty,
                    OVFParameter::Comment
        };

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
            auto it = std::find_if(ValidParams.begin(), ValidParams.end(), 
                                   [&](const OVFParameter& p){return std::regex_match(buffer, res, TokenMap.at(p));});
            if(it != ValidParams.end())
            {
                //handling:
                //first check if parameter is already set
                if(head.contains(*it) && *it != OVFParameter::Desc)
                {
                    if(log != "") log+= "\n";
                    log += (std::string)"readHeader: found a duplicate value of type: " + std::string(paramName(*it)) +
                        "at line #" + std::to_string(line_cnt) + ", ignoring!";
                    continue;
                }
                //else set the value
                switch(paramType(*it))
                {
                case(ParameterType::Unsigned):
                    {
                        auto pval = ParseToken<ParameterType::Unsigned>(res[2].str());
                        if(pval == std::nullopt)
                        {
                            if(log != "") log+= "\n";
                            log+= (std::string)"readHeader: Error occured while parsing the unsigned integer token: \"" +
                                std::string(paramName(*it)) + "\" at line #" + std::to_string(line_cnt)+ ", line content:\n" + buffer;
                            break;
                        }
                        head.set(*it, pval.value());
                    }
                    break;
                case(ParameterType::Floating):
                    {
                        auto pval = ParseToken<ParameterType::Floating>(res[2].str());
                        if(pval == std::nullopt)
                        {
                            if(log != "") log+= "\n";
                            log+= (std::string)"readHeader: Error occured while parsing the floating point token: \"" +
                                std::string(paramName(*it)) + "\" at line #" + std::to_string(line_cnt)+ ", line content:\n" + buffer;
                            break;
                        }
                        head.set(*it, pval.value());
                    }
                    break;
                case(ParameterType::String):
                    if(*it == OVFParameter::Desc)
                    {
                        std::string description;
                        if(head.contains(OVFParameter::Desc))
                            description = head.requireAs<std::string>(OVFParameter::Desc) + "\n";
                        description += res[2].str();
                        head.set(OVFParameter::Desc, std::move(description));
                        break;
                    }
                    head.set(*it, res[2].str());
                default:
                    break;
                }
                continue;
            }

            //else try to match for one of the allowed other parameters
            auto itOpt = std::find_if(AllowedOtherParams.cbegin(), AllowedOtherParams.cend(),
                [&](const OVFParameter& p) {return std::regex_match(buffer, res, TokenMap.at(p)); });

            if(itOpt == AllowedOtherParams.end())
            {
                if(log != "") log += "\n";
                if(++BadLineCnt < BadBlockMax) //truncate output if bad lines come one after another(like misalinged reading frame)
                {
                   log+=(std::string)"readHeader: Encountered unexpected line # " + 
                        std::to_string(line_cnt) + ": ";
                   log+=(std::string)"\"" + buffer.substr(0, 20) + ((buffer.length() > 21)?"...":"") + "\"";
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
            switch(*itOpt)
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
                break;
            case(OVFParameter::Mtype):
                if(head.contains(OVFParameter::Mtype))
                {
                    if(log != "") log += "\n";
                    log+= (std::string)"readHeader: Trying to redefine mesh type at line #" + std::to_string(line_cnt);
                    break;
                }
                if(std::regex_match(buffer, regexTokenValue("Meshtype", "rectangular")))
                    head.setMeshType(MeshType::Rectangular);
                else if(std::regex_match(buffer, regexTokenValue("Meshtype", "irregular")))
                    head.setMeshType(MeshType::Irregular);
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
    //main method
    //TODO: implement OVF0 reading at some point
    std::string readData(std::istream& file, VField& out, std::optional<PointRange> range,
                         std::size_t& cnt, bool prefetch)
    {
        const auto version = out.header().version();
        std::string dataHeader {""};
        std::getline(file, dataHeader);
        if(!file)
            return "readData: unexpected error occured while reading file";
        // next check if data header is valid
        std::smatch match;
        if(!std::regex_match(dataHeader, match, regexTokenValue("Begin","Data*\\s+(binary\\s+(4|8)|text)")))
            return (std::string)"readData: Ill formed data begin line: \"" + dataHeader + "\"";
        bool isBinary = !std::regex_match(dataHeader, regexTokenValue("Begin", "Data\\s+text"));
        std::size_t internalSize = isBinary? ParseToken<ParameterType::Unsigned>(match[4].str()).value() : 8; // guaranteed to have value from previous lines
        const auto DataBeginPos {file.tellg()};
        //next peek if data is ending at expected position
        std::string log {""};
        std::size_t advertisedDim {0};
        std::size_t advertisedCnt {0};
        {
            //try setting previous 2 parameters
            if(version != OVFVersion::Unknown && out.header().contains(OVFParameter::Mtype) && (version != OVFVersion::OVF2 || out.header().contains(OVFParameter::Vdim)))
                advertisedDim = (out.header().meshType() == MeshType::Rectangular? 0:3)+(version == OVFVersion::OVF2? out.header().requireAs<std::size_t>(OVFParameter::Vdim) : 3);
            if(cnt!=0 && advertisedDim!=0)
                advertisedCnt = cnt / advertisedDim;
            else if( out.header().contains(OVFParameter::Mtype) && (out.header().meshType() != MeshType::Irregular || out.header().contains(OVFParameter::Pcount)) &&
                     out.header().contains(OVFParameter::Xnodes) && out.header().contains(OVFParameter::Ynodes) && out.header().contains(OVFParameter::Znodes) )
                advertisedCnt = out.header().meshType() == MeshType::Irregular ? out.header().requireAs<std::size_t>(OVFParameter::Pcount) :
                                    out.header().requireAs<std::size_t>(OVFParameter::Xnodes) * out.header().requireAs<std::size_t>(OVFParameter::Ynodes) * out.header().requireAs<std::size_t>(OVFParameter::Znodes);
            if(advertisedDim == 0 || advertisedCnt == 0)
                log += "readData: Couldn't read the array dimensions from the header provided!";
        }
        auto endRegex = regexTokenValue("End", (std::string)"data\\s+" + (isBinary? 
                    ((std::string)"binary\\s+" + std::to_string(internalSize)) : "text"));
        //seeking to expected end
        if((advertisedDim * advertisedCnt != 0) || cnt !=0 ) 
        {
            std::size_t pnts = cnt;
            if(pnts == 0) pnts = advertisedCnt * advertisedDim;
            if(isBinary)
                file.seekg(  (pnts + 1)  * internalSize / sizeof(std::istream::char_type) , std::ios_base::cur);
            else
                for(std::size_t i = 0; i < advertisedCnt && file.good(); i++)
                    file.ignore( std::numeric_limits<std::streamsize>::max(), '\n');
        }
        auto DataEndPos {file.tellg()};
        std::string closingString{""};
        std::getline(file, closingString);
        //TODO: check if this is needed, since oommf is specified to not have this issue
        if(closingString == "")
            std::getline(file, closingString);
        if( file.good() && std::regex_match(closingString, regexTokenValue("End","Data\\s+.+?")))
        {
            //set count if it was not known for sure previously
            if(cnt == 0)
                cnt = advertisedDim * advertisedCnt;
        }
        else
        {
            //if didn't got a correct line have to reseek manually
            log+= "readData: reached the end of file searching for the end of data section!";
            file.seekg(DataBeginPos);
            while(file.good())
            {
                file.ignore( std::numeric_limits<std::streamsize>::max(), '#'); //seek until next line
                if(!file.good())
                    return "readData: reached the end of file searching for end of data manually :'(";
                file.unget(); //push # back into stream
                DataEndPos = file.tellg();
                std::getline(file, closingString);
                if(std::regex_match(closingString, regexTokenValue("End", "Data\\s+.+?")))
                    break;
                if(!file.good())
                    return "readData: reached the end of file searching for end of data manually ";
            }
            log = "Found the end of data manually";
            if( isBinary )
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

        if(range.has_value() && advertisedDim == 0)
        {
            if(log !="") log+= "\n";
            log += "readData: Cannot read a point range without properly-defined dimension";
            return log;
        }
        if(!prefetch)
        {
            file.seekg(DataBeginPos);
            const auto firstPoint = range.has_value() ? range->first : 0u;
            const auto pointCount = range.has_value() ? range->second :
                (advertisedDim == 0 ? 0u : cnt / advertisedDim);
            if(range.has_value() &&
               (firstPoint > advertisedCnt || pointCount > advertisedCnt - firstPoint))
            {
                if(log != "") log += "\n";
                log += "readData: Requested point range is outside the stored data";
                return log;
            }

            const std::size_t importDepth = range.has_value() ? pointCount * advertisedDim : cnt;
            if(importDepth == 0)
                return log;

            if(isBinary)
            {
                if(internalSize == 4)
                {
                    float test{};
                    file.read( reinterpret_cast<std::istream::char_type *>(&test), 
                               sizeof(float)/sizeof(std::istream::char_type) );
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
                    file.seekg(firstPoint * advertisedDim * sizeof(float) / sizeof(std::istream::char_type), std::ios_base::cur);
                    auto buffer = std::make_unique<float[]>(importDepth);
                    file.read(reinterpret_cast<std::istream::char_type*>(buffer.get()),
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
                            boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint32_t*>(buffer.get() + i));
                    out.adoptData(std::move(buffer), importDepth);
                    return log; 
                }
                if(internalSize == 8)
                {
                    double test{};
                    file.read( reinterpret_cast<std::istream::char_type *>(&test), 
                               sizeof(double)/sizeof(std::istream::char_type) );
                    if( (version == OVFVersion::OVF1 && boost::endian::order::native == boost::endian::order::little) ||
                        (version == OVFVersion::OVF2 && boost::endian::order::native == boost::endian::order::big) )
                        boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint64_t*>(&test));
                    if(test != TestVal<double>)
                    {
                        if(log!="") log += "\n";
                        log+= "readData: binary data (4-byte) has a wrong test magic number!";
                        return log;
                    }
                    file.seekg(firstPoint * advertisedDim * sizeof(double) / sizeof(std::istream::char_type), std::ios_base::cur);
                    auto buffer = std::make_unique<double[]>(importDepth);
                    file.read(reinterpret_cast<std::istream::char_type*>(buffer.get()),
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
                            boost::endian::endian_reverse_inplace(*reinterpret_cast<std::uint64_t*>(buffer.get() + i));
                    out.adoptData(std::move(buffer), importDepth);
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

                for(std::size_t skipped = 0; skipped < firstPoint && file.good(); ++skipped)
                    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                std::string line{};
                auto buffer = std::make_unique<double[]>(importDepth);
                const std::regex tokenizer ("^\\s*([^\\s]+)(?:\\s+|$)", std::regex_constants::ECMAScript |
                                                                        std::regex_constants::optimize);
                for(std::size_t point = 0; point < pointCount; ++point)
                {
                    std::getline(file, line);
                    if(!file)
                    {
                        if(log != "") log += "\n";
                        log += "readData: Unexpected file read error!";
                        return log;
                    }
                    std::size_t count {0};
                    std::smatch sm;
                    while(std::regex_search(line, sm, tokenizer))
                    {
                        auto val = ParseToken<ParameterType::Floating>(sm[1].str());
                        if(val == std::nullopt)
                            break;
                        buffer[point * advertisedDim + count++] = val.value();
                        line = sm.suffix();
                    }
                    if(count != advertisedDim)
                    {
                        if( log != "" ) log+= "\n";
                        log += (std::string)"readData: Unexpected number of values on line #"
                               +std::to_string(firstPoint + point);
                        return log;
                    }
                }
                out.adoptData(std::move(buffer), importDepth);
                return log; 
            }
        }

        return log;
    }

    //and now more high-level interfaces
    VField& VFieldFile::fetch(std::size_t index) const
    {
        auto& field = data->fields.at(index);
        auto& [pos, size] = data->prefetch.at(index);
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
        auto log = readData(file, field, std::nullopt, size, false);
        if(log != "")
        {
            logMessage("VFieldFile::operator[]: errors occured while reading data:\n");
            logMessage(log);
        }
        return field;
    }
    VField& VFieldFile::operator[] (std::size_t index) &
    { return fetch(index); }
    VField VFieldFile::operator[] (std::size_t index) const & noexcept
    {
        //first, check if index is OOB
        if(index >= data->fields.size())
        {
            logMessage("VFieldFile::operator[]: index out of range!");
            return {};
        }
        //then check the element
        auto field = data->fields[index];
        auto [pos, size] = data->prefetch[index];
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
        auto log = readData(file, field, std::nullopt, size, false);
        if(log != "")
        {
            logMessage("VFieldFile::operator[]: errors occured while reading data:\n");
            logMessage(log);
        }
        return field;
    }
    const OVFHeader& VFieldFile::getSegmentHeader(std::size_t index) const & 
    {
        //first, check if index is OOB
        if(index >= data->fields.size())
            throw std::out_of_range("Segment access out of range!");
        //else go and fetch the damm data
        return data->fields[index].header();
    }
    VField VFieldFile::readSlice(std::size_t index, std::size_t firstPoint,
                                 std::size_t pointCount) const noexcept
    {
        if(index >= data->fields.size())
        {
            logMessage("VFieldFile::readSlice: index out of range!");
            return {};
        }

        auto field = data->fields[index];
        auto [pos, size] = data->prefetch[index];
        if(!pos.has_value() || size == 0)
        {
            logMessage("VFieldFile::readSlice: no source data was found during prefetch!");
            return field;
        }

        field.clearData();
        std::ifstream file(fPath, std::ios_base::binary);
        file.seekg(*pos);
        if(!file.good())
        {
            logMessage("VFieldFile::readSlice: error opening file!");
            return field;
        }

        auto log = readData(file, field, PointRange{firstPoint, pointCount}, size, false);
        if(log != "")
        {
            logMessage("VFieldFile::readSlice: errors occurred while reading data:\n");
            logMessage(log);
        }
        return field;
    }
}
