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
#include<format>
#include"OVFParser.h"
#include"OVFDictionary.h"
//boost endian conversion library setup
#include<boost/endian/conversion.hpp>
#include<cstdint>

namespace VField{
    namespace {
        void appendDiagnostic(std::string& report, std::string message)
        {
            if(!report.empty()) report += '\n';
            report += std::move(message);
        }
    }
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
    inline void VFieldFile::logMessage(const std::string& msg)
    {
        if(!data_->log.empty()) data_->log += '\n';
        data_->log += msg;
    }
    const std::filesystem::path& VFieldFile::path() const noexcept
    { return path_; }
    ReadError VFieldFile::error(ReadErrorCode code, std::string message,
                                std::optional<std::size_t> segment) const
    { return {code, std::move(message), path_, segment}; }

    //housekeeping
    VFieldFile::VFieldFile(): data_(std::make_unique<FileData>()) {}
    VFieldFile::~VFieldFile() = default;
    VFieldFile::VFieldFile(const VFieldFile& ref):
        path_(ref.path_), data_(std::make_unique<FileData>(*ref.data_)) {}
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
    std::size_t VFieldFile::segmentCount() const noexcept
    { return data_->fields.size();}
    //check if some data exists
    bool VFieldFile::dataLoaded(std::size_t index) const noexcept
    {
        if(index >= data_->fields.size())
            return false;
        return data_->fields[index].isDataPresent();
    }
    bool VFieldFile::unload(std::size_t index) noexcept
    {
        if(index >= data_->fields.size())
            return false;
        data_->fields[index].clearData();
        return true;
    }
    bool VFieldFile::dataAvailable(std::size_t index) const noexcept
    {
        if(index >= data_->fields.size())
            return false;
        if(data_->fields[index].isDataPresent())
            return true;
        //otherwise return if the data was found during prefetch
        return data_->prefetch[index].first.has_value() &&
               data_->prefetch[index].second != 0;
    }

    ReadResult<std::span<VField>> VFieldFile::fields()
    {
        for(std::size_t index = 0; index < segmentCount(); ++index)
            if(auto result = fetch(index); !result)
                return std::unexpected(std::move(result.error()));
        return std::span<VField>{data_->fields};
    }

    ReadResult<std::vector<VField>> VFieldFile::fieldsCopy() const
    {
        std::vector<VField> result;
        result.reserve(segmentCount());
        for(std::size_t index = 0; index < segmentCount(); ++index)
        {
            auto field = copy(index);
            if(!field)
                return std::unexpected(std::move(field.error()));
            result.push_back(std::move(*field));
        }
        return result;
    }

    //////////////////////////////////////////////////////////
    /// main code for reading stuff based off of regexes /////
    //////////////////////////////////////////////////////////

    //shared flags
    constexpr auto commonFlags = std::regex_constants::icase |          //ignore case while matching
        std::regex_constants::ECMAScript;

    //and then regex generators
    std::regex regexToken(const std::string& token)
    { return std::regex(std::format(R"(^#\s*({})\s*:\s*(.*?)\s*(?:##.*)?$)", token), commonFlags); }
    std::regex regexTokenValue(const std::string& token, const std::string& value)
    { return std::regex(std::format(R"(^#\s*({})\s*:\s*({})\s*(?:##.*)?$)", token, value), commonFlags); }

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
    ReadResult<VFieldFile> VFieldFile::open(const std::filesystem::path& path,
                                            DataLoading loading)
    {
        VFieldFile file;
        if(auto result = file.read(path, loading); !result)
            return std::unexpected(std::move(result.error()));
        return file;
    }

    ReadResult<void> VFieldFile::read(const std::filesystem::path& path,
                                      DataLoading loading)
    {
        //A read always starts at the beginning of a file and owns a fresh set
        //of diagnostics. Lazy-access diagnostics may be appended afterwards.
        data_->log.clear();
        path_ = path;

        constexpr std::array<OVFParameter, 5> TopLevelTags{
            OVFParameter::Open,
            OVFParameter::Close,
            OVFParameter::Segcnt,
            OVFParameter::Empty,
            OVFParameter::Comment
        }; 
        //first try to open the file
        std::ifstream file(path_, std::ios_base::binary);//TODO: check if opening it as binary from the start messes with getline
        if(!file.good())
        {
            logMessage("VFieldFile::read: Unable to open source file");
            return std::unexpected(error(ReadErrorCode::OpenFailed, data_->log));
        }
        //there is hope if we were able to open file, so erase old data
        data_-> fields.clear();
        data_-> prefetch.clear();
        
        //else continue
        std::string version{""};
        std::getline(file, version);
        if(!file.good())
        {
            logMessage("VFieldFile::read: Source file ended while reading its signature");
            return std::unexpected(error(ReadErrorCode::StreamFailure, data_->log));
        }
        if(matchVersionString(version) == OVFVersion::Unknown)
        {
            logMessage(std::format(
                "VFieldFile::read: Invalid OVF signature: \"{}{}\"",
                version.substr(0, 20), version.length() > 21 ? "..." : ""));
            return std::unexpected(error(ReadErrorCode::InvalidFormat, data_->log));
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
                logMessage(std::format(
                    "VFieldFile::read: {}ecoverable stream error at line {}",
                    (file.rdstate() & std::ios_base::badbit) ? "Unr" : "R",
                    line_cnt));
                return std::unexpected(error(ReadErrorCode::StreamFailure, data_->log));
            }

            //otherwise check
            auto matchIt = std::find_if(TopLevelTags.begin(), TopLevelTags.end(),
                        [&](const OVFParameter& param)
                        {return std::regex_match(buffer, TokenMap.at(param));});
            //check if no match was found, i.e. line was invalid for being at a top level
            if( matchIt == TopLevelTags.end() )
            {
                if(++BadLineCnt < BadBlockMax) //truncate output if bad lines come one after another(like misalinged reading frame)
                    logMessage(std::format(
                        "VFieldFile::read: Encountered unexpected line {}: \"{}{}\"",
                        line_cnt, buffer.substr(0, 20), buffer.length() > 21 ? "..." : ""));

                continue;
            }
            if( BadLineCnt != 0)
            {
                if( BadLineCnt >= BadBlockMax )
                {
                    logMessage("VFieldFile::read: Too many invalid lines in a row, suspending further output");
                    logMessage(std::format(
                        "VFieldFile::read: Block of bad lines ended at line {}", line_cnt - 1));
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
                    logMessage(std::format(
                        "VFieldFile::read: Segment count was redefined at line {}; ignoring it",
                        line_cnt));
                    break;
                }
                //else parse the segment count
                else
                {
                    std::regex_match(buffer, res, TokenMap.at(OVFParameter::Segcnt));//guaranteed to succeed
                    auto segCntParse = ParseToken<ParameterType::Unsigned>(res[2].str());
                    if(segCntParse == std::nullopt)
                    {
                        logMessage(std::format(
                            "VFieldFile::read: Could not parse segment count at line {}",
                            line_cnt));
                        logMessage(std::format("\t{}", buffer));
                        break;
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
                        logMessage(std::format(
                            "VFieldFile::read: Duplicate section opening at line {}; ignoring it",
                            line_cnt));
                        break;
                    }
                    SegmentOpened = true;
                }
                else if(std::regex_match(buffer, regexTokenValue("Begin", "Header")) )
                {
                    if(!SegmentOpened)
                        logMessage(std::format(
                            "VFieldFile::read: Found a segment header outside a segment at line {}",
                            line_cnt));
                    if(WaitingForData)
                        logMessage(std::format(
                            "VFieldFile::read: Duplicate segment header at line {}", line_cnt));
                    //in either case read and start waiting for data
                    data_->fields.emplace_back(version);
                    data_->prefetch.emplace_back(std::nullopt, 0u);
                    //rewind back 1 line
                    file.seekg(pos);
                    //and read the header
                    auto log = readHeader(file, line_cnt, data_->fields.back().header());
                    if(log != "")
                        logMessage(std::format(
                            "VFieldFile::read: Errors reading header ending at line {}:\n{}",
                            line_cnt, log));
                    WaitingForData = true;
                }
                else if(std::regex_match(buffer, regexTokenValue("Begin", "Data\\s+.+?")) )
                {
                    if(!SegmentOpened)
                    {
                        logMessage(std::format(
                            "VFieldFile::read: Found segment data outside a segment at line {}; "
                            "opening a segment with an empty header", line_cnt));
                        SegmentOpened = true;
                        //open a new segment with empty header
                        data_->fields.emplace_back(version);
                        data_->prefetch.emplace_back(std::nullopt, 0u);
                    }
                    else if(!WaitingForData) // == !WaitingForData && SegmentOpened, missed header, or a duplicate data segment
                    {
                        logMessage(std::format(
                            "VFieldFile::read: Unexpected segment data at line {}", line_cnt));
                        //now need to distinguish from having read a header and having a duplicated data
                        data_->fields.emplace_back(version);
                        data_->prefetch.emplace_back(std::nullopt, 0u);
                    }
                    //rewind back 1 line
                    file.seekg(pos); 
                    data_->prefetch.back().first = pos;
                    auto log = readData(
                            file,
                            data_->fields.back(),
                            std::nullopt,
                            data_->prefetch.back().second,
                            loading == DataLoading::Lazy
                        );
                    if(log != "")
                        logMessage(std::format(
                            "VFieldFile::read: Errors reading data at line {}:\n{}", line_cnt, log));
                    WaitingForData = false;
                    line_cnt++; //increment line counter for end line after data
                }
                else
                {
                    logMessage(std::format(
                        "VFieldFile::read: Unknown section token at line {}:", line_cnt));
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
                            logMessage(std::format(
                                "VFieldFile::read: Unexpected statement at line {}; continuing",
                                line_cnt));
                        if(WaitingForData)
                        {
                            logMessage(std::format(
                                "VFieldFile::read: Expected a data section but the section ended at line {}",
                                line_cnt));
                            WaitingForData = false;
                        }
                    }
                    else //in case when it is either data or header, which should be handled in respective functions
                    {
                        logMessage(std::format(
                            "VFieldFile::read: Unexpected end of block at line {}", line_cnt));
                        logMessage(std::format("\t{}", buffer));
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
            logMessage(std::format(
                "VFieldFile::read: Block of bad lines ended at line {} (EOF)", line_cnt));
        }
        //bad bit error is handled inside the loop, reaching here necesarily means that EOF occured
        if( SegmentOpened || WaitingForData )
            logMessage("VFieldFile::read: File ended unexpectedly");
        if(data_->fields.size() != seg_cnt)
            logMessage(std::format(
                "VFieldFile::read: Found {} segments instead of {}",
                data_->fields.size(), SegCntDefined ? std::to_string(seg_cnt) : "undefined"));

        if(!data_->log.empty())
            return std::unexpected(error(ReadErrorCode::InvalidFormat, data_->log));
        return {};
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
                appendDiagnostic(log, std::format(
                    "readHeader: {}ecoverable error while reading line {}; aborting",
                    (file.rdstate() & std::ios_base::badbit) ? "Unr" : "R", line_cnt));
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
                    appendDiagnostic(log, std::format(
                        "readHeader: Duplicate '{}' value at line {}; ignoring it",
                        paramName(*it), line_cnt));
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
                            appendDiagnostic(log, std::format(
                                "readHeader: Could not parse unsigned '{}' at line {}:\n{}",
                                paramName(*it), line_cnt, buffer));
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
                            appendDiagnostic(log, std::format(
                                "readHeader: Could not parse floating '{}' at line {}:\n{}",
                                paramName(*it), line_cnt, buffer));
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
                if(++BadLineCnt < BadBlockMax) //truncate output if bad lines come one after another(like misalinged reading frame)
                    appendDiagnostic(log, std::format(
                        "readHeader: Encountered unexpected line {}: \"{}{}\"",
                        line_cnt, buffer.substr(0, 20), buffer.length() > 21 ? "..." : ""));
            }
            
            if( BadLineCnt != 0)
            {
                if( BadLineCnt >= BadBlockMax )
                {
                    appendDiagnostic(log,
                        "readHeader: Too many invalid lines in a row; suspending output");
                    appendDiagnostic(log, std::format(
                        "readHeader: Block of bad lines ended at line {}", line_cnt - 1));
                }
                BadLineCnt = 0;
            }
            //else can again switch on a type of parameter
            switch(*itOpt)
            {
            case(OVFParameter::Open):
                appendDiagnostic(log, std::format(
                    "readHeader: Section opened prematurely at line {}:\n{}", line_cnt, buffer));
                break;
            case(OVFParameter::Close):
                if(std::regex_match(buffer, regexTokenValue("End", "Header")))
                {
                    if( BadLineCnt >= BadBlockMax )
                    {
                        appendDiagnostic(log,
                            "readHeader: Too many invalid lines in a row; suspending output");
                        appendDiagnostic(log, std::format(
                            "readHeader: Block of bad lines ended at line {} (end of header)",
                            line_cnt));
                    }
                    return log; //successfully finished reading the header
                }
                //else it is an error and should be reported
                appendDiagnostic(log, std::format(
                    "readHeader: Section closed prematurely at line {}:\n{}", line_cnt, buffer));
                break;
            case(OVFParameter::Mtype):
                if(head.contains(OVFParameter::Mtype))
                {
                    appendDiagnostic(log, std::format(
                        "readHeader: Mesh type redefined at line {}", line_cnt));
                    break;
                }
                if(std::regex_match(buffer, regexTokenValue("Meshtype", "rectangular")))
                    head.setMeshType(MeshType::Rectangular);
                else if(std::regex_match(buffer, regexTokenValue("Meshtype", "irregular")))
                    head.setMeshType(MeshType::Irregular);
                else
                    appendDiagnostic(log, std::format(
                        "readHeader: Invalid mesh type at line {}: \"{}\"",
                        line_cnt, res[1].str()));
                break;
            default://skip comments and empty lines
                break;
            }
        }
        if( BadLineCnt >= BadBlockMax )
        {
            appendDiagnostic(log,
                "readHeader: Too many invalid lines in a row; suspending output");
            appendDiagnostic(log, std::format(
                "readHeader: Block of bad lines ended at line {} (EOF)", line_cnt));
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
            return std::format("readData: Ill-formed data opening line: \"{}\"", dataHeader);
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
            else if(out.header().contains(OVFParameter::Mtype))
            {
                if(out.header().meshType() == MeshType::Irregular &&
                   out.header().contains(OVFParameter::Pcount))
                    advertisedCnt = out.header().requireAs<std::size_t>(OVFParameter::Pcount);
                else if(out.header().meshType() == MeshType::Rectangular &&
                        out.header().contains(OVFParameter::Xnodes) &&
                        out.header().contains(OVFParameter::Ynodes) &&
                        out.header().contains(OVFParameter::Znodes))
                    advertisedCnt =
                        out.header().requireAs<std::size_t>(OVFParameter::Xnodes) *
                        out.header().requireAs<std::size_t>(OVFParameter::Ynodes) *
                        out.header().requireAs<std::size_t>(OVFParameter::Znodes);
            }
            if(advertisedDim == 0 || advertisedCnt == 0)
                log += "readData: Couldn't read the array dimensions from the header provided!";
        }
        auto endRegex = regexTokenValue("End", isBinary
            ? std::format("data\\s+binary\\s+{}", internalSize)
            : "data\\s+text");
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
            appendDiagnostic(log, std::format(
                "readData: Closing section has the wrong data type: {}", closingString));
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
    ReadResult<std::reference_wrapper<VField>> VFieldFile::fetch(std::size_t index)
    {
        if(index >= data_->fields.size())
            return std::unexpected(error(ReadErrorCode::InvalidSegment,
                std::format("Segment {} is out of range", index), index));

        auto& field = data_->fields[index];
        auto& [pos, size] = data_->prefetch[index];
        if(field.isDataPresent())
            return std::ref(field);
        if(!pos.has_value() || size == 0)
            return std::unexpected(error(ReadErrorCode::DataUnavailable,
                std::format("Segment {} has no source data", index), index));

        std::ifstream file(path_, std::ios_base::binary);
        file.seekg(*pos);
        if(!file.good())
            return std::unexpected(error(ReadErrorCode::OpenFailed,
                std::format("Unable to reopen the source file for segment {}", index), index));

        auto report = readData(file, field, std::nullopt, size, false);
        if(!report.empty())
            return std::unexpected(error(ReadErrorCode::InvalidFormat,
                std::format("Errors while reading segment {} data:\n{}", index, report), index));
        return std::ref(field);
    }

    ReadResult<std::reference_wrapper<VField>> VFieldFile::load(std::size_t index)
    { return fetch(index); }

    ReadResult<VField> VFieldFile::copy(std::size_t index) const
    {
        if(index >= data_->fields.size())
            return std::unexpected(error(ReadErrorCode::InvalidSegment,
                std::format("Segment {} is out of range", index), index));

        auto field = data_->fields[index];
        auto [pos, size] = data_->prefetch[index];
        if(field.isDataPresent())
            return field;
        if(!pos.has_value() || size == 0)
            return std::unexpected(error(ReadErrorCode::DataUnavailable,
                std::format("Segment {} has no source data", index), index));

        std::ifstream file(path_, std::ios_base::binary);
        file.seekg(*pos);
        if(!file.good())
            return std::unexpected(error(ReadErrorCode::OpenFailed,
                std::format("Unable to reopen the source file for segment {}", index), index));
        auto report = readData(file, field, std::nullopt, size, false);
        if(!report.empty())
            return std::unexpected(error(ReadErrorCode::InvalidFormat,
                std::format("Errors while reading segment {} data:\n{}", index, report), index));
        return field;
    }

    ReadResult<std::reference_wrapper<const OVFHeader>>
      VFieldFile::header(std::size_t index) const
    {
        if(index >= data_->fields.size())
            return std::unexpected(error(ReadErrorCode::InvalidSegment,
                std::format("Segment {} is out of range", index), index));
        return std::cref(data_->fields[index].header());
    }

    ReadResult<VField> VFieldFile::readSlice(std::size_t index,
                                             std::size_t firstPoint,
                                             std::size_t pointCount) const
    {
        if(index >= data_->fields.size())
            return std::unexpected(error(ReadErrorCode::InvalidSegment,
                std::format("Segment {} is out of range", index), index));

        auto field = data_->fields[index];
        auto [pos, size] = data_->prefetch[index];
        if(!pos.has_value() || size == 0)
            return std::unexpected(error(ReadErrorCode::DataUnavailable,
                std::format("Segment {} has no source data", index), index));

        field.clearData();
        std::ifstream file(path_, std::ios_base::binary);
        file.seekg(*pos);
        if(!file.good())
            return std::unexpected(error(ReadErrorCode::OpenFailed,
                std::format("Unable to reopen the source file for segment {}", index), index));

        auto report = readData(file, field, PointRange{firstPoint, pointCount}, size, false);
        if(!report.empty())
            return std::unexpected(error(ReadErrorCode::InvalidFormat,
                std::format("Errors while reading segment {} slice:\n{}", index, report), index));
        return field;
    }
}
