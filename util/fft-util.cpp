#include<iostream>
#include<sstream>
#include<filesystem>
#include<stdexcept>
#include<string>
#include<regex>
#include<algorithm>
#include<optional>
#include<cmath>
#include<type_traits>
#include<memory>
#include<chrono>
#include<format>

//headers for parallelization, hurray for atomic future :D
#include<atomic>
#include<future>
#include<thread>
#include<mutex>
//for C++17 built-in algorithm parallelization
#include<execution>

//parsing program options
#include<boost/program_options.hpp>

//file i/o handling
#include<OVFParser.h>
#include<OVFWriter.h>

//miscaleneous options from cmake
#include"OVFToolkitConfig.h"

//fft engines
#include"cuda-backend.h"
#include"fft-frequency.h"

//console backend
#include"console.h"

//assert macro
#include<cassert>

using fname_type = std::string;
using namespace std::string_literals;

//vector of time/handle pairs
using metaPair = std::pair<std::optional<double>, VField::VFieldFile>;
//policy for importing and parsing files
constexpr auto ioPolicy = std::execution::seq;

const ConsoleInfo cInfo;

class CMDMonitor
{
    private:
        std::ostream& out;
        std::size_t cCount {};//count of max possible characters in current line
        std::string lineData {};
        bool ready {false};

        //static magic
        static constexpr const char* cOff = "\x1b[?25l";
        static constexpr const char* cOn = "\x1b[?25h";

        void pad_n(std::size_t n)
        {out << std::string(n, ' ');}
        void erase_n(std::size_t n)
        {out << std::string(n, '\b');}
        //TODO: screw emoji(combininb several utf-8 codepoints into single character)
        std::size_t cntUnicodePts(const std::string& ref)
        { return std::count_if( ref.begin(), ref.end(), [](char c){ return (0xC0&c) != 0x80; }); };
        //crop a sting based on codepoint count
        std::string cropString(const std::string& ref, std::size_t point_offset, std::size_t point_size)
        {
            std::size_t offset{0}, size{0};

            auto str_begin = ref.cbegin();
            auto str_end   = ref.cend();

            //increment point offset to count up to next character after requested one
            point_offset++;
            for(; str_begin != str_end; str_begin++)
            {
                // skip all of the non-leading characters
                if( (*str_begin & 0xC0) == 0x80 )
                {
                    if( point_offset != 0 )
                        offset++;
                    else
                        size++;
                    continue;
                }

                if( point_offset != 0 )
                {
                    point_offset--;
                    if( point_offset != 0 ) offset++;
                    else { size++; point_size--; }
                }
                else if ( point_size != 0 )
                {
                    size++;
                    point_size--;
                }
                else break;
            }

            return ref.substr(offset, size);
        }

        //save the previous overfull string offset so that it can be scrolled there and back in the window
        std::size_t scrollOffset {0};
        
        static constexpr bool cursorOff { false };

    public:
        CMDMonitor(std::ostream& out_): out(out_), ready(true), cCount(0) { if(cursorOff) out << cOff;}
        ~CMDMonitor()
        {
            assert(("Somehow missed CMDMonitor initialization!", ready));

            //clear line and output it full if were outputting fragment before
            const auto nCount = cntUnicodePts(lineData);
            const auto conWidth = cInfo.GetConsoleWidth();
            out << '\r' << lineData;
            //pad the output only if no new line was triggered
            if( cCount < conWidth && nCount < cCount )
                pad_n( cCount - nCount );
            if(cursorOff) out << cOn;
            out << '\n' << std::flush;
        }
        CMDMonitor(const CMDMonitor&) = delete; //DAS IST VERBOTTEN
        CMDMonitor& operator= (const CMDMonitor&) = delete;//DIESER AUCH

        //update
        void update(const std::string& str)
        {
            assert(("Tried updating without initializing!", ready));

            const auto nCount = cntUnicodePts(str);
            const auto conWidth = cInfo.GetConsoleWidth();

            if(conWidth > nCount)
                out << '\r' << str;
            else
            {
                //subtract one character from con width because a cursor triggers new line
                if( scrollOffset > nCount - conWidth - 1 ) scrollOffset = 0;
                out << '\r' << cropString(str, scrollOffset++, conWidth - 1);
            }

            if( cCount > nCount && nCount < conWidth )
            {
                const std::size_t padLen { std::min<std::size_t>(cCount, conWidth - 1) - nCount};

                pad_n( padLen );
                out << std::flush;
                erase_n( padLen );
            }
            out << std::flush;

            //if there is more stuff left to clean at the of the line, keep remembering last time stuff was 
            //written past current console width
            if( cCount < conWidth )
                cCount = nCount;
            lineData = str;//store a copy just in case
        }

        void prependLine(const std::string& str)
        {
            assert(("Tried updating without initializing!", ready));
            out << '\r' << str;
            auto nCount = cntUnicodePts(str);
            const auto conWidth = cInfo.GetConsoleWidth();

            if( nCount < conWidth && nCount < cCount )
                pad_n( cCount - nCount );
            out << '\n';

            out << lineData << std::flush;
        }
};

//getting *fancy* with ASCII :D
class Spinner 
{
    static constexpr const std::array<char, 4> charCycle {'-', '\\', '|', '/'};
    std::array<char, 4>::const_iterator currentState = charCycle.begin();

public:
    Spinner& operator++()
    { ++currentState; if(currentState == charCycle.end()) currentState = charCycle.begin(); return *this; }

    friend std::ostream& operator << (std::ostream& out, const Spinner& spin)
    { out << *spin.currentState; return out; }

    char CurState() const 
    { return *currentState; };
};

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, std::string> roundFloat(T val, int decPlaces = 2)
{
    std::ostringstream strStream;
    
    strStream << std::fixed << std::setprecision(decPlaces);
    strStream << val;

    return strStream.str();
}

std::string printMemSize(std::size_t size)
{
    constexpr const char prefixes[] = {'\0', 'K', 'M', 'G', 'T', 'E'};
    double dSize = size;

    for(const auto& x: prefixes)
    {
        if(dSize < 1024)
            return roundFloat(dSize) + x + 'B';
        dSize /= 1024;
    }

    return roundFloat( (double)size / 1152921504606846976 ) + "EB";
}

template<typename T>
auto Average(const T& array)
{
    //TODO: handle overflow/underflows
    using value_type = typename T::value_type;
    static_assert(std::is_arithmetic_v<value_type>, "Cannot calculate average of non-arithmetic type!");
    constexpr value_type epsilon = std::numeric_limits<value_type>::epsilon();

    std::vector< std::pair<std::size_t, value_type> > accum{{0u, static_cast<value_type>(0)}};
    for(const auto& x: array)
    {
        //if current bucket is getting to a point where new value will be a rounding error, make a new one
        if( epsilon != 0 && accum.back().second != 0 && std::abs(x / accum.back().second) < 100 * epsilon )
            accum.push_back( {0u, static_cast<value_type>(0) } );

        accum.back().first++;
        accum.back().second += x;
    }

    while( accum.size() > 1 )
    {
        accum.front().first += accum.back().first;
        accum.front().second += accum.back().second;

        accum.pop_back();
    }

    return accum.front().second / accum.front().first;
}

//processing data
enum class BufferState { WAIT, IMPORT, POST, PROCESS, EXPORT, STOP, FAIL };
struct GPUBuffer {
    std::unique_ptr<float[]> data{};
    std::size_t nSize{0};
    std::size_t realPoints{0};

    BufferState state{ BufferState::WAIT };
};

template<typename Rep, typename Period>
inline std::string printTimeStamp(std::chrono::duration<Rep, Period> dur)
{
   auto minCnt = std::chrono::floor<std::chrono::minutes>(dur).count();
   auto secCnt = std::chrono::floor<std::chrono::seconds>(dur).count() % 60;
   std::string ret{};
   ret += std::to_string(minCnt) + "m";
   if ( secCnt < 10 ) ret += "0";
   ret += std::to_string(secCnt) + "s";

   return ret;
}

//template to copy data into array
template<typename T>
inline void loadData( const VField::VField& field, float *arr, std::size_t offset, std::size_t cnt, std::size_t skip )
{
    if(skip == 0)
        std::copy_n( field.data<T>() + offset, cnt, arr );
    else
    {
        const auto dim = field.header().pointDimension().value();
        auto begin = field.data<T>();
        const auto end = begin + field.scalarCount();

        std::copy_n(begin + skip + offset, dim - skip - offset, arr);
        begin += dim;

        for(; begin != end; begin += dim)
        {
            std::copy_n(begin + skip, cnt > (dim - skip)? dim - skip : cnt % (dim - skip), arr);
            arr += dim - skip; cnt -= dim - skip;
        }
    }
}

//group import data
void readData( const std::vector<std::pair<std::size_t, const VField::VFieldFile>>& handles,
               float* data,
               std::size_t offset,
               std::atomic<std::size_t>& progMax,
               std::atomic<std::size_t>& progress,
               std::size_t& impLen )
{
    progress = 0;
    
    const auto& head = handles.front().second.getSegmentHeader(0);
    const auto mType = head.meshType().value();
    const auto dim   = head.pointDimension().value();
    const auto pts   = head.pointCount().value();
    const auto len   = handles.size();
    const auto vdim  = dim - (mType == VField::MeshType::Rectangular? 0 : 3);
    impLen = std::min( impLen, vdim * pts - offset );
    progMax = impLen * len;

    //for irregular meshes offset skips over coordinate tripplets
    const auto adjBegin  = (mType == VField::MeshType::Rectangular? offset : dim * (offset/vdim) + offset % vdim)/dim;
    const auto adjEnd    = ((mType == VField::MeshType::Rectangular? offset + impLen : offset + dim * (impLen/vdim) + impLen % vdim ) + dim - 1)/dim;

    auto importer = [&](const std::pair<std::size_t, VField::VFieldFile>& handle)
    {
        auto slice = handle.second.readSlice(0, adjBegin, adjEnd - adjBegin);

        if (slice.scalarSizeBytes() == 4)
            loadData<float>(slice, data + impLen * handle.first, offset % vdim, impLen, mType == VField::MeshType::Rectangular ? 0 : 3);
        else
            loadData<double>(slice, data + impLen * handle.first, offset % vdim, impLen, mType == VField::MeshType::Rectangular ? 0 : 3);

        progress += impLen;
    };

    std::for_each(ioPolicy, handles.cbegin(), handles.cend(), importer);
}

//export into one yuge ovf with multiple segments, defaults to OVF version 2 trying to convert the headers
//descriptor has list of ranges of points produced during fft transform, hostBuffer has first bufferCnt ranges in it,
//the rest were written into file 'fileBuffer', freqInc gives the increment for frequencies in list of length cnt
bool exportSpectrum( const std::filesystem::path& outputFile,
                     const std::vector<std::array<std::size_t, 2>>& descriptor,
                     const std::filesystem::path& fileBuffer,
                     float const* n2last_buffer, float const* last_buffer,
                     const VField::OVFHeader& commonHeader,
                     std::size_t cnt,
                     double freqInc,
                     std::atomic<std::size_t>& progVar,
                     float const* hostBuffer = nullptr,
                     std::size_t  ramBufferCnt  = 0,
                     float const* irregCoords= nullptr
                   )
{
    progVar = 0;

    std::ifstream fsBuffer(fileBuffer, std::ios_base::in | std::ios_base::binary);
    if(!fsBuffer.good())
    {
        std::cerr << "Unable to open the buffer file: " << fileBuffer << "!\n";
        return false;
    }
    const auto mType = commonHeader.meshType().value();
    const std::size_t vdim    = commonHeader.requireAs<std::size_t>( VField::OVFParameter::Vdim );
    const std::size_t pntCnt  = commonHeader.pointCount().value() * vdim;
    const std::size_t bufTotal= descriptor.size();
    assert(("Incompatible array dimensions!\n", vdim % 2 == 0 && pntCnt == descriptor.back()[1] * 2));

    //open output file
    std::ofstream output(outputFile, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    if(!output.good())
    {
        std::cerr << "Unable to open the output file: " << outputFile << "!\n";
        return false;
    }

    //data, later to be put into VField container, disposed by VField's destructor
    auto data = std::make_unique<float[]>(
        commonHeader.pointCount().value() * commonHeader.pointDimension().value());
    //copy the coordinates if buffer is irregular
    if(mType == VField::MeshType::Irregular)
    {
        assert(("Expected a coordinate field for transform", irregCoords != nullptr)); 
        for(std::size_t i = 0; i < pntCnt; i++)
            std::copy_n(irregCoords + i * 3, 3, data.get() + i * (vdim + 3));
    }

    VField::VField field ( commonHeader );
    field.adoptData(std::move(data),
        commonHeader.pointCount().value() * commonHeader.pointDimension().value());
    output << commonHeader.requireAs<std::string>( VField::OVFParameter::VersionString ) << "\n" << "# Segment count: " << cnt;

    std::string desc{};
    if( commonHeader.contains( VField::OVFParameter::Desc) )
        desc = commonHeader.requireAs<std::string>( VField::OVFParameter::Desc );

    for(std::size_t i = 0; i < cnt; i++)
    {
        //add frequency stamp to the file
        field.header().set(VField::OVFParameter::Desc,
            desc + (!desc.empty() ? "\n" : "") +
            std::format("f = {:.9g} Hz", freqInc * static_cast<double>(i)));

        //start copying data from mixed sources into vfield
        for( std::size_t j = 0; j <  bufTotal; j++ )
        {
            //for a case when reading from file buffer
            std::unique_ptr<float[]> importData;

            std::size_t offset = 2 * descriptor[j][0];
            std::size_t dist = 2 * ( descriptor[j][1] - descriptor[j][0] );
            auto* fieldData = field.data<float>();
            float* dest = (mType == VField::MeshType::Rectangular
                ? fieldData + offset
                : fieldData + (3 + vdim) * offset / vdim + 3 + offset % vdim);
            const float* curSection { nullptr };
            if( j < ramBufferCnt )
                curSection = hostBuffer + cnt * offset + i * dist; 
            else if( j == bufTotal - 2 || bufTotal == 1 )
                curSection = n2last_buffer + i * dist;
            else if( j == bufTotal - 1 )
                curSection = last_buffer + i * dist;
            else
            {
                fsBuffer.seekg( i * sizeof(float)/sizeof(std::ofstream::char_type) * dist , std::ios_base::cur);

                //reserve space for data
                importData = std::make_unique<float[]>( dist );
                fsBuffer.read( (std::ofstream::char_type*)importData.get(), sizeof(float)/sizeof(std::ofstream::char_type) * dist );
                fsBuffer.seekg( (cnt - i - 1) * sizeof(float)/sizeof(std::ofstream::char_type) * dist , std::ios_base::cur);
                curSection = importData.get();
            }

            //and copy data into destination buffer
            if(mType == VField::MeshType::Rectangular)
                std::copy_n(curSection, dist, dest);
            else
            {
                std::size_t initRemainder {vdim - offset % vdim};
                std::copy_n(curSection, initRemainder, dest);
                dest += initRemainder;

                std::size_t wholePts { (dist - initRemainder) / vdim };
                for(std::size_t k = 0; k < wholePts; k++)
                    std::copy_n(curSection + k * vdim + initRemainder, vdim, dest + (3 + vdim) * k);

                //and copy the last part
                std::copy_n(curSection + wholePts * vdim + initRemainder, (dist - initRemainder) % vdim, dest + (3 + vdim) * wholePts);
            }

            progVar += dist;
        }

        //seek to the beginning
        fsBuffer.seekg(0, std::ios_base::beg);

        //output the VField
        output << '\n';
        WriteSegment(output, field);
    }

    return true;
}

std::size_t parseMemSize(const std::string& sizeSpec)
{
    char *after{nullptr};
    auto size = strtod(sizeSpec.c_str(), &after);
    //if no conversion was done it throw an exception
    if( sizeSpec.c_str() == after )
        throw std::invalid_argument( "Memory specification \"" + sizeSpec + "\" is non-numeric!" );

    if( *after == '\0' || *(after+1) != '\0' )
        throw std::invalid_argument( "Unknown memory size suffix \""s + after + "\", expected K, M or G." );

    switch(*after)
    {
        case 'K':
            return std::llround(size * 1024);
        case 'M':
            return std::llround(size * 1024 * 1024);
        case 'G':
            return std::llround(size * 1024 * 1024 * 1024);
        default:
            throw std::invalid_argument( "Unknown memory size suffix \""s + after + "\", expected K, M or G." );
    }
}

//transform a header into one suitable for data transformed into frequency domain 
void transformHeader(VField::OVFHeader& head, const std::string& tStampPattern)
{
    //mandatory to set version to OVF 2.0, because others don't support vdim != 3
    head.setVersion(VField::OVFVersion::OVF2);

    //change value dimension accordingly
    if( head.contains(VField::OVFParameter::Vdim) )
        head.set(VField::OVFParameter::Vdim,
            head.requireAs<std::size_t>(VField::OVFParameter::Vdim) * 2);
    else
        head.set(VField::OVFParameter::Vdim, std::size_t{6});

    //delete the whole line with timestamp from description
    if( head.contains(VField::OVFParameter::Desc) )
    {
        std::regex tStampLineNLine("\\n.*"s + tStampPattern + ".*\\n", //matches internal lines
             std::regex_constants::ECMAScript | std::regex_constants::nosubs);
        std::regex tStampLineDel("(^|\\n).*"s + tStampPattern + ".*($|\\n)", //matches all lines
             std::regex_constants::ECMAScript | std::regex_constants::nosubs);

        auto desc = head.requireAs<std::string>(VField::OVFParameter::Desc);
        desc = std::regex_replace( desc, tStampLineNLine, "\n" ); //replace internal lines by newline symbol
        desc = std::regex_replace( desc, tStampLineDel, "" );     //and delete the rest of the line types

        if( desc.empty() )
            head.clear(VField::OVFParameter::Desc);
        else
            head.set(VField::OVFParameter::Desc, std::move(desc));
    }

    //generate new value labels and units if old ones were present and compliant with standard
    if( head.contains(VField::OVFParameter::Vlabels) || head.contains(VField::OVFParameter::Vunit) )
    {
        std::regex tokenPattern("^\\s*(\".*?\"|[^\\s]+)(?:\\s+|$)");
        auto Tokenize = [&head, &tokenPattern](VField::OVFParameter p)
        {
            std::vector<std::string> tokens{};
            auto str = head.requireAs<std::string>(p);

            std::smatch matchRes;
            while(std::regex_search(str, matchRes, tokenPattern))
            {
                tokens.push_back(matchRes[1]);
                str = matchRes.suffix();
            }

            return tokens;
        };

        if( head.contains(VField::OVFParameter::Vlabels) )
        {
            auto ValueLabels = Tokenize(VField::OVFParameter::Vlabels);
            std::string labelString;

            for(const auto& x: ValueLabels)
            {
                bool isQuoted = x.front() == '\"';

                if( !labelString.empty() ) labelString += " ";
                labelString += (isQuoted? "\""s : ""s) + "Re{" + (isQuoted? x.substr(2, x.length() - 1) : x) + "}" + (isQuoted? "\" " : " ");
                labelString += (isQuoted? "\""s : ""s) + "Im{" + (isQuoted? x.substr(2, x.length() - 1) : x) + "}" + (isQuoted? "\"" : "");
            }
            head.set(VField::OVFParameter::Vlabels, std::move(labelString));
        }
        if( head.contains(VField::OVFParameter::Vunit) )
        {
            auto ValueUnits = Tokenize(VField::OVFParameter::Vunit);
            if( ValueUnits.size() != 1 )
            {
                std::string unitString;

                for(const auto& x: ValueUnits) //double all units
                {
                    if( !unitString.empty() ) unitString += " ";
                    unitString += x + " " + x;
                }
                head.set(VField::OVFParameter::Vunit, std::move(unitString));
            }
        }
    }

    //add message about transform into title
    if( head.contains(VField::OVFParameter::Title) )
    {
        head.set(VField::OVFParameter::Title,
            "Temporal Fourier transform of \""s +
            head.requireAs<std::string>(VField::OVFParameter::Title) + "\"");
    }
}

//TODO: look if windows can deal with UTF here, maybe implement winmain with UTF-16 parameters
//TODO: include link to setargv.obg/wsetargv.obj in the windows build, look at https://docs.microsoft.com/en-us/cpp/c-runtime-library/link-options?view=vs-2019
//TODO: disable monitors when output is redirected to a file
int main(int argc, char** argv)
{
    std::vector<fname_type> fileList{};
    std::string TimeRegExStr {};
    std::string oFileName {};

    //system configuration
    int gpu { -1 };
    std::optional<std::size_t> maxRam{};
    std::optional<std::size_t> maxVRam{};
    bool no_norm { false };
    bool no_reinterp { false };
    //first parse command-line options
    try
    {
        //console width initiated on program start and is assumed to stay the same until output, because program_options doesn't have a way to alter it later
        boost::program_options::options_description desc("Usage: ovf-batch [options] files...\nOptions", cInfo.GetConsoleWidth());
        //populate options list
        desc.add_options()
            ("help,h", boost::program_options::bool_switch(), "Produce this help message.")
            ("version,v", boost::program_options::bool_switch(), "Get this software's version information.")
            ("input-files", boost::program_options::value<std::vector<fname_type>>(&fileList)->multitoken()->required( ), "Time sequence of vector fields in .ovf files." )
            ("output,o", boost::program_options::value<std::string>(&oFileName)->default_value("spectrum.ovf"), "Spectrum output file name.")
            ("gpu", boost::program_options::value<int>(&gpu)->default_value(-1), "GPU id for CUDA fft.")
            ("max-ram", boost::program_options::value<std::string>()->notifier([&maxRam](const std::string& sizeSpec){ maxRam = parseMemSize(sizeSpec); }), "Maximum ammount of RAM allocated on host machine for buffers.")
            ("max-vram", boost::program_options::value<std::string>()->notifier([&maxVRam](const std::string& sizeSpec){ maxVRam = parseMemSize(sizeSpec); }), "Maximum ammount of RAM allocated on GPU for transform.")
            ("time-regex", boost::program_options::value<std::string>(&TimeRegExStr)->default_value("Total simulation time:\\s+(.+?)\\s+s"), "Regex pattern to extract time from .ovf files.")
            ("no-norm", boost::program_options::bool_switch(&no_norm), "Don't normalize the fourier transform result.")
            ("no-reinterp", boost::program_options::bool_switch(&no_reinterp), "Don't reinterpolate the data to remove jitter.");

        //set position of input-files to automatically them without a switch
        boost::program_options::positional_options_description pos_desc;
        pos_desc.add("input-files", -1);

        //setting up for parsing
        boost::program_options::variables_map vmap;
        boost::program_options::command_line_parser parser(argc, argv);
        parser.options(desc);
        parser.positional(pos_desc);
        boost::program_options::store( parser.run(), vmap );

        if(vmap["help"].as<bool>())
        {
            std::cout << desc ;
            return 0;
        }
        if(vmap["version"].as<bool>())
        {
            std::cout << "OVFToolkit time domain FFT batch processing utility ver. " << OVFTOOLKIT_VERSION_STRING << "\n";
            std::cout << "Copyright (C) 2020 Artem Bondarenko\n" ;
            //TODO: add dependancies information into the message, gsl, fftw, CUDA and boost
            return 0;
        }

        boost::program_options::notify(vmap);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error while parsing command line: " << e.what() << "\n";
        return -1;
    }
    //virtual class so I can use fftw instead if I want to
    std::unique_ptr<FFTEngine<float>> fft_engine(new cuFFTEngine(gpu));
    std::future<bool> engineInit{};

    //try to validate file list beforehand
    std::size_t tSeriesLength = fileList.size();
    if( tSeriesLength < 2 )
    {
        std::cerr << "At least 2 files were expected to be provided to form a time series, a single file is already its own transform, aborting!\n";
        return 1;
    }
    {
        //else check if all the files exist
        std::vector<fname_type> missingFiles{};
        for(const auto& fname: fileList)
        {
            std::filesystem::path fPath(fname);
            if( !std::filesystem::exists(fPath) || !std::filesystem::is_regular_file(fPath) )
                missingFiles.push_back(fname);
        }
        if( !missingFiles.empty() )
        {
            std::cerr << "Following files were not found: \"" << missingFiles.front() << "\"";
            auto begin = ++missingFiles.begin();
            auto end = missingFiles.end();
            for(; begin != end; ++begin)
                std::cerr << ", \"" << *begin << "\"";
            std::cerr << "\n";
            return 1;
        }
    }

    //evaluation monitors
    std::atomic<std::size_t> progVar{};
    std::atomic<std::size_t> expectProg{fileList.size()};
    std::atomic<const fname_type::value_type*> lastFile { "No file imported yet." };
    std::thread MonitorThread{};

    if(!cInfo.isRedirected)
    {
        //time prefetch phase for profiling
        MonitorThread = std::thread([&] () -> void 
            {
                CMDMonitor monitor(std::cout);
                const auto expSizeStr = std::to_string(expectProg);

                std::string message {};

                bool lastRun = true;
                while(true)
                {
                    std::string message { "File " };
                    const std::size_t cCount = progVar;
                    const auto name = std::string { lastFile };
                    const auto cCountStr = std::to_string(cCount);

                    message += std::string( expSizeStr.length() - cCountStr.length(), ' ' ) + cCountStr + '/' + expSizeStr + ": \"" + name + '\"';

                    monitor.update(message);
                    if( cCount >= expectProg )
                        break;

                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(100ms);
                }
            });
    }

    std::vector<metaPair> filesMeta( fileList.size() );
    //comparison operator for time/handle pair
    auto metaCompPred = [](const metaPair& pair1, const metaPair& pair2) { return pair1.first < pair2.first;  };
    //parsing predicate
    const std::regex timeRegEx(TimeRegExStr, std::regex_constants::ECMAScript | std::regex_constants::optimize);
    auto parseMetaPred = [&lastFile, &progVar, &timeRegEx, &TimeRegExStr](const std::string& fName) -> metaPair
    {
        VField::VFieldFile file(fName, true); //only fetch the header, TODO: check if I want to output some file errors in here

        //if file is not single segment, give up LULW
        if (file.cntSegments() != 1)
        {
            std::cerr << "Encountered bad segment count in file: \"" << fName << "\": " << file.cntSegments() << "\n";
            //set last file to the one we processed 
            lastFile = fName.c_str();
            ++progVar;
            return { std::nullopt, std::move(file) };
        }

        std::optional<double> time{ std::nullopt };
        //and then check if header is oiro
        const VField::OVFHeader& ref = file.getSegmentHeader(0);
        std::smatch pat_matches{};
        if (ref.contains(VField::OVFParameter::Desc) && std::regex_search(ref.requireAs<std::string>(VField::OVFParameter::Desc), pat_matches, timeRegEx))
        {
            char* ret{ nullptr };
            auto str = pat_matches[1].str();
            double val = strtod(str.c_str(), &ret);
            if (ret != str.c_str())
                time = val;
        }
        else //could not parse time
            std::cerr << "Could not parse time from 'Description' field in file \"" << fName << "\", with regular expression \"" << TimeRegExStr << "\"."
            "Got \"" << (ref.contains(VField::OVFParameter::Desc) ? ref.requireAs<std::string>(VField::OVFParameter::Desc) : "*NOTHING*") << "\" in the description field!\n";
        
        //set last file to the one we processed 
        lastFile = fName.c_str();
        ++progVar;
        return {std::move(time), std::move(file)};
    };

    auto t_before = std::chrono::steady_clock::now();
    std::transform(ioPolicy, fileList.cbegin(), fileList.cend(), filesMeta.begin(), parseMetaPred);
    auto t_after = std::chrono::steady_clock::now();

    if( progVar != tSeriesLength )
    {
        std::cerr << "Failed to import one or more files, aborting!\n";
        progVar = tSeriesLength;
        if(!cInfo.isRedirected) MonitorThread.join();
        return -1;
    }
    if (!cInfo.isRedirected) MonitorThread.join();
    std::cout << "Done pre-fetching .ovf metadata for " << tSeriesLength << " files in " << std::chrono::duration<double>(t_after - t_before).count() << " seconds." << "\n";

    //process timestamp data
    //first check if all the times are present
    {
        std::string noTSFiles{};
        for(const auto& [timeOpt, handle]: filesMeta)
            if( !timeOpt.has_value() )
                noTSFiles += (noTSFiles.empty() ? ""s : ", "s) + handle.getCurrentPath();

        if (!noTSFiles.empty())
        {
            std::cout << "Following files were found to have no time stamp: " << noTSFiles << "\n";
            std::cout << "Aborting!\n";
            return -1;
        }
    }

    //it is safe to assume now that all the times are populated, sort everything by time
    if (!std::is_sorted(filesMeta.begin(), filesMeta.end(), metaCompPred))
    {
        std::cout << "File list received was not ordered by time, sorting it now!\n";
        std::sort(filesMeta.begin(), filesMeta.end(), metaCompPred);
    }

    //check the duplicates and transfer times into their own array
    std::vector<double> times(tSeriesLength);
    {
        //check the times
        std::string dupTSFiles{};
        bool encounteredDup {false};

        //first loop. merged duplicate check and time set check
        for (std::size_t i = 0; i < tSeriesLength; i++)
        {

            //push value onto the list
            times[i] = filesMeta[i].first.value();

            if (i > 0 && times[0] == times[i]) //duplicate check
            {
                if (!encounteredDup)
                {
                    encounteredDup = true;
                    if (!dupTSFiles.empty()) dupTSFiles += '\n';

                    std::ostringstream strStream;

                    strStream << std::scientific << std::setprecision(4);
                    strStream << times.front();

                    dupTSFiles += "t="s + strStream.str() + ": " + '\"' + filesMeta.front().second.getCurrentPath() + '\"';
                }

                dupTSFiles += ", \""s + filesMeta[i].second.getCurrentPath() + '\"';
            }
        }

        //duplicate check loop, starts from second value
        for(std::size_t i = 1; i < tSeriesLength - 1; i++)
        {
            //reset for next run
            if(encounteredDup)
            {
                dupTSFiles += '\n';
                encounteredDup = false;
            }

            for( std::size_t j = i + 1; j < tSeriesLength; j++ )
                if ( times[i] == times[j] )
                {
                    if(!encounteredDup)
                    {
                        encounteredDup = true;
                        if (!dupTSFiles.empty()) dupTSFiles += '\n';

                        std::ostringstream strStream;

                        strStream << std::scientific << std::setprecision(4);
                        strStream << times[i];

                        dupTSFiles += "t="s + strStream.str() + ": " + '\"' + filesMeta[i].second.getCurrentPath() + '\"';
                    }

                    dupTSFiles += ", \""s + filesMeta[j].second.getCurrentPath() + '\"';
                }
        }

        //outputting stuff
        if (!dupTSFiles.empty())
        {
            std::cout << "Following timestamps were duplicated:\n" << dupTSFiles << "\n";
            std::cout << "Aborting!\n";
            return -1;
        }
    }

    std::size_t VFSize{};
    std::unique_ptr<struct GPUBuffer> buffers[2]; //tripple buffering, yay
    struct {
        std::unique_ptr<float[]> data{};
        std::size_t cnt{};

        //occupied buffer count
        std::size_t occup{};
    } CollectorBuffer; //anonymous struct with data for buffer

    {
        //check if internal dimensions are compatible
        const auto expDim = filesMeta.front().second.getSegmentHeader(0).pointDimension();
        const auto expCnt = filesMeta.front().second.getSegmentHeader(0).pointCount();

        if(!expDim || !expCnt)
        {
            std::cerr << "First file has indeterminate point count or dimension!\n";
            return 1;
        }
        //guaranteed to be set by this point
        const auto mType = filesMeta.front().second.getSegmentHeader(0).meshType().value();
        VFSize = (*expDim - (mType == VField::MeshType::Rectangular? 0 : 3)) * *expCnt;
        //begin initialization of engines outside main thread once dimensions are known
        engineInit = std::async( std::launch::async, [&] ()
        {
            //TODO add code for fallback to cpu engine(fftw) later
            auto res = fft_engine -> Init( tSeriesLength, VFSize, maxVRam.value_or(0) );

            auto batch = fft_engine -> expectedBatch();
            auto cPoints = batch * (tSeriesLength/2 + 1);
            if( fft_engine -> isReady() )
            {
                auto maxRamBuffers = maxRam.value_or(0) / (sizeof(float) * 2 * cPoints);
                auto neededBuffers = (VFSize + batch - 1)/batch;//all the full buffers and one partially filled
                const std::size_t activeBuffers { std::min<std::size_t>(2, neededBuffers) };
                for( std::size_t i = 0; i < activeBuffers; i++ )
                {
                    buffers[i] = std::make_unique<GPUBuffer> ();
                    buffers[i] -> nSize = 2 * cPoints;
                    buffers[i] -> realPoints = batch;
                    buffers[i] -> data = std::make_unique<float[]>( buffers[i]->nSize );
                }

                //and allocate space for ram buffer
                if( neededBuffers > 2 && maxRamBuffers > 2 )
                {
                    CollectorBuffer.cnt = std::min(neededBuffers - 2, maxRamBuffers - 2);
                    CollectorBuffer.data = std::make_unique<float[]>( 2 * cPoints * CollectorBuffer.cnt );
                    CollectorBuffer.occup = 0;
                }
            }

            return res;
        });

        auto it = ++filesMeta.cbegin();
        auto end = filesMeta.cend();
        std::string badFiles {};
        for(; it != end; ++it)
        {
            const auto& head = it -> second.getSegmentHeader(0);
            if ( head.pointDimension() != expDim ||
                 head.pointCount()    != expCnt ||
                 head.meshType()       != mType    )
            {
                if(!badFiles.empty()) badFiles += ", ";
                badFiles += "\""s + it -> second.getCurrentPath() + "\"";
            }
        }

        if( !badFiles.empty() )
        {
            std::cout << "Following files have incompatible grids: " << badFiles << "\n";
            return 1;
        }

        const auto totSize =  VFSize * tSeriesLength;
        std::cout << "Found " << totSize << " values to be handled (" << printMemSize( sizeof(float) * totSize ) << " of data in single precision).\n"; 
    }

    
    //work on time array to set some more options
    double trueStep{ (times.back() - times.front())/(tSeriesLength - 1) };
    {
        std::vector<double> distances(++times.begin(), times.end());
        auto dIt = distances.begin();
        auto tIt = times.cbegin(); auto tEnd = --times.cend();
        while( tIt != tEnd )
            *dIt++ -= *tIt++ ;

        //TODO: evaluate as candidates for loop merger if compiler doesn't do that itself
        auto avTstep = Average(distances);
        auto maxTstep = std::max_element(distances.begin(), distances.end());
        auto minTstep = std::min_element(distances.begin(), distances.end());

        for(auto& x: distances) //square the distances for next step
            x *= x;
        auto TstepDisp = std::sqrt( Average(distances) - avTstep * avTstep );

        //output info about time steps
        std::cout << "Input array has even time step of " << trueStep << " seconds. Average time step is " << avTstep << " seconds, and time step dispersion is "
                        << TstepDisp << " seconds. \n";

        //find and report outliers ( >3 sigma )
        std::string outliers {};
        tIt = times.cbegin(); tEnd = times.cend();
        auto fIt = filesMeta.cbegin();
        double expectedTime = *tIt;
        for(; tIt != tEnd; ++tIt)
        {
            if( std::abs( *tIt - expectedTime ) > 3 * TstepDisp )
            {
                if( !outliers.empty() ) outliers += ", ";
                outliers += "\""s + fIt -> second.getCurrentPath() + "\" (dt/disp=" + std::to_string( (*tIt - expectedTime) / TstepDisp ) + ")";
            }

            ++fIt; expectedTime += trueStep;
        }
        if(!outliers.empty())
            std::cout << "Following files found to be far away from expected times: " << outliers << ";\n";

        //and then check if we still need to reinterpolate
        //abort interpolation iff dispersion is less than 5 rounding errors of float and there are no outliers
        if( !no_reinterp && ( outliers.empty() && TstepDisp <= 5 * std::numeric_limits<float>::epsilon() * trueStep ) )
            no_reinterp = true;
    }

    //wait here for GPU to finish initializing, and buffers being created
    engineInit.get();
    if( !fft_engine -> isReady() )
    {
        //TODO: add logic for initialize on cpu instead once that is finished
        std::cerr << "Failed to initialize a FFT engine, quiting!\n";
        return -1;
    }
    //initialize interpolation
    if( !no_reinterp && !fft_engine -> InitInterp(times.data()) )
            std::cerr << "Failed to initialize an interpolation!\n";

    //stuff for streaming buffers to gpu
    std::mutex rotLock; //mutex to acomplish buffer rotation
    std::condition_variable gpuRotate;
    const float norm { no_norm? 1.0f : (float)std::sqrt( trueStep ) };//scaling to get value in amplitude/sqrt(Hz)
    const double frequencyIncrement =
        FFTUtil::frequencyIncrement(tSeriesLength, trueStep);

    auto printState = [&] (BufferState state) -> std::string
    {
        switch(state)
        {
            case BufferState::FAIL:
                return "FAILURE";
            case BufferState::WAIT:
                return "WAITING";
            case BufferState::STOP:
                return "STOPPED";
            case BufferState::IMPORT:
                return "Read: "s + printMemSize(progVar * sizeof(float)) + "/" + printMemSize(expectProg * sizeof(float));
            case BufferState::POST:
                return "Post: "s + roundFloat( (double)progVar/expectProg ) + "%";
            case BufferState::PROCESS:
                return "PROCESSING";
            case BufferState::EXPORT:
                return "WRITING";
        }
        return "";
    };

    //prepare for work
    //after this main thread works with I/O
    const auto BatchSize = fft_engine -> expectedBatch();
    std::vector<std::array<std::size_t, 2>> segmentDescriptor;
    //open a temporary file for outputting results of fft
    std::filesystem::path tmpPath(".batchfft-temp");
    std::ofstream tmpFile (tmpPath, std::ios_base::out |
                                    std::ios_base::binary |
                                    std::ios_base::trunc );
    //created array of handle-index pairs
    std::vector< std::pair<std::size_t, const VField::VFieldFile> > indexedHandles{};
    indexedHandles.reserve(filesMeta.size());
    for (std::size_t i = 0; i < filesMeta.size(); i++)
        indexedHandles.emplace_back(i, filesMeta[i].second);
 
    auto exportData = [&] (GPUBuffer* buff)
    {
        if(CollectorBuffer.occup < CollectorBuffer.cnt)
        {
            std::copy_n( buff -> data.get(), (tSeriesLength / 2 + 1) * buff ->realPoints * 2, 
                    CollectorBuffer.data.get() + CollectorBuffer.occup * buff -> nSize );
            CollectorBuffer.occup++;
        }
        else
            tmpFile.write( (char*) buff -> data.get(), (tSeriesLength / 2 + 1) * buff -> realPoints * 2 * sizeof(float) / sizeof(std::ofstream::char_type) );

        buff -> state = BufferState::WAIT;
    };

    std::atomic<bool> monitorOn {true};
    if( BatchSize < VFSize )
    {
        std::thread gpuStreamThread( [&] ()
        {
            while(true)
            {
                //aquire mutex first with condvar and unique lock
                std::unique_lock<std::mutex> lock(rotLock);
                gpuRotate.wait(lock, [&](){return buffers[0] -> state == BufferState::PROCESS || buffers[0] -> state == BufferState::STOP;});

                if(buffers[0] -> state == BufferState::STOP && buffers[1] -> state == BufferState::STOP)
                    break;

                //do the data transform
                auto curBuffer = buffers[0].get();

                if( !fft_engine -> isReady() || curBuffer -> nSize < fft_engine -> expectedLength() || curBuffer -> nSize > 2 * fft_engine -> expectedLength() * fft_engine -> expectedBatch() || 
                    !fft_engine -> RunTransform(curBuffer -> data.get(), norm, fft_engine -> expectedBatch() - curBuffer -> realPoints ))
                {
                    curBuffer -> state = BufferState::FAIL;
                    lock.unlock();
                    gpuRotate.notify_all();
                    return; //stop if failure was encountered
                }

                curBuffer -> state = BufferState::EXPORT;

                lock.unlock();
                gpuRotate.notify_all();
            }
        });

        //and a monitor function
        if(!cInfo.isRedirected) MonitorThread = std::thread( [&] ()
            {
                using namespace std::chrono_literals;

                //static copy of buffer pointers, doesn't rotate
                GPUBuffer* buff[2];
                for(int i = 0; i < 2; i++)
                buff[i] = buffers[i].get();

                auto beginTime = std::chrono::steady_clock::now();

                const int tStampPadding { 10 };
                const int bufferPadding { 25 };

                Spinner spin[2];
                CMDMonitor monitor(std::cout);

                while(monitorOn)
                {
                    std::this_thread::sleep_for(150ms);
                    //output through that handy dandy function
                    std::string res {printTimeStamp( std::chrono::steady_clock::now() - beginTime )};
                    if( res.length() < tStampPadding ) res += std::string( tStampPadding - res.length(), ' ');

                    //output buffer states
                    for(int i = 0; i < 2; i++)
                    {
                        auto state = buff[i]->state;
                        auto prState = printState(state);
                        if(state == BufferState::PROCESS || state == BufferState::EXPORT)
                        {prState += spin[i].CurState(); ++spin[i]; }

                        res += "Buffer[" + std::to_string(i) + "]: " + prState;
                        if(prState.length() < bufferPadding && i != 1 ) res += std::string( bufferPadding - prState.length(), ' ' );
                    }

                     monitor.update(res);
                }
            });

        //TODO: handle errors !
        while(segmentDescriptor.empty() || segmentDescriptor.back()[1] < VFSize)
        {
            //gpu thread only lets swap happen after finishing working on data
            //only when it has failed, or when it is the first run that the following isn't true
            GPUBuffer* curBuffer = buffers[1].get();
            if( curBuffer -> state == BufferState::EXPORT )
                exportData( curBuffer );
            if( buffers[0] -> state == BufferState::FAIL ||
                    buffers[1] -> state == BufferState::FAIL )
            {
                monitorOn = false;
                gpuStreamThread.join();
                if(!cInfo.isRedirected) MonitorThread.join();
                std::cerr << "GPU thread failed!\n";
                tmpFile.close();
                std::filesystem::remove( tmpPath );
                return -1;
            }

            curBuffer -> state = BufferState::IMPORT;
            const std::size_t begin = segmentDescriptor.empty()? 0lu : segmentDescriptor.back()[1];
            readData(indexedHandles, curBuffer -> data.get(), begin,
                    expectProg, progVar, curBuffer -> realPoints );
            segmentDescriptor.push_back( {begin, begin + curBuffer -> realPoints} );
            curBuffer -> state = BufferState::PROCESS;

            std::unique_lock<std::mutex> lock(rotLock);
            gpuRotate.wait(lock, [&](){return buffers[0] -> state == BufferState::EXPORT || buffers[0] -> state == BufferState::FAIL || buffers[0] -> state == BufferState::WAIT;});
            std::swap(buffers[0], buffers[1]);
            lock.unlock();
            gpuRotate.notify_all();
        }
        if(buffers[1] -> state == BufferState::FAIL)
        {
            std::cerr << "GPU thread failed!\n";
            monitorOn = false;
            gpuStreamThread.join();
            if(!cInfo.isRedirected) MonitorThread.join();
            tmpFile.close();
            std::filesystem::remove( tmpPath );
            return -1;
        }
        buffers[1] -> state = BufferState::STOP;

        //and wait for the last one to finish processing
        std::unique_lock<std::mutex> lock(rotLock);
        gpuRotate.wait(lock, [&](){return buffers[0] -> state == BufferState::EXPORT || buffers[0] -> state == BufferState::FAIL;});
        lock.unlock();
        if(buffers[0] -> state == BufferState::FAIL)
        {
            std::cerr << "GPU thread failed!\n";
            monitorOn = false;
            gpuStreamThread.join();
            if(!cInfo.isRedirected) MonitorThread.join();
            tmpFile.close();
            std::filesystem::remove( tmpPath );
            return -1;
        }
        else 
            buffers[0] -> state = BufferState::STOP;

        gpuRotate.notify_all();
        gpuStreamThread.join();
        fft_engine.reset();

        //deinit the monitor 
        monitorOn = false;
        if(!cInfo.isRedirected) MonitorThread.join();
    } // endif( BatchSize < VFSize )
    else // if(BatchSize == VFSize)
    {
        std::cout << "All data fits into one buffer, evaluating sequentially.\n";
        GPUBuffer* buff = buffers[0].get();

        //special monitor for single batch
        if(!cInfo.isRedirected) MonitorThread = std::thread( [&] ()
            {
                using namespace std::chrono_literals;
                auto beginTime = std::chrono::steady_clock::now();

                const int tStampPadding { 10 };

                Spinner spin;
                CMDMonitor monitor(std::cout);

                while(monitorOn)
                {
                    std::this_thread::sleep_for(150ms);
                    //output through that handy dandy function
                    std::string res {printTimeStamp( std::chrono::steady_clock::now() - beginTime )};
                    if( res.length() < tStampPadding ) res += std::string( tStampPadding - res.length(), ' ');

                    res += printState( buff -> state );

                    monitor.update(res);
                }
            });

        buff -> state = BufferState::IMPORT;
        readData(indexedHandles, buff -> data.get(), 0lu,
                expectProg, progVar, buff -> realPoints );
        segmentDescriptor.push_back( {0lu, buff -> realPoints} );
        buff -> state = BufferState::PROCESS;

        //now launch kernel to do the processing
        if( buff -> nSize < fft_engine -> expectedLength() || buff -> nSize > 2 * fft_engine -> expectedLength() * fft_engine -> expectedBatch() || 
            !fft_engine -> RunTransform(buff -> data.get(), norm, fft_engine -> expectedBatch() - buff -> realPoints ))
        {
            monitorOn = false;
            MonitorThread.join();

            std::cerr << "Error processing the data, aborting!";
            return -1;
        }
        buff -> state = BufferState::STOP;

        monitorOn = false;
        MonitorThread.join();
        //and that's all, Pogchamp
    }

    //and close tmp file
    tmpFile.close();
    auto head = filesMeta.front().second.getSegmentHeader(0);
    transformHeader( head, TimeRegExStr );

    expectProg = (tSeriesLength/2 + 1) * VFSize * 2;
    progVar = 0;
    if(!cInfo.isRedirected) MonitorThread = std::thread([&progVar, &expectProg, &monitorOn, &oFileName]()
        {
            CMDMonitor monitor(std::cout);
            while(true)
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(150ms);

                auto curVal = progVar.load();
                monitor.update("Exporting spectrum into \""s + oFileName + "\": " + printMemSize( sizeof(float) * curVal ) + 
                    '/' + printMemSize( sizeof(float) * expectProg ) );
                if(curVal >= expectProg)
                return;
            }
        });

    if(BatchSize < VFSize) exportSpectrum( oFileName, segmentDescriptor, tmpPath, buffers[1] -> data.get(), buffers[0] -> data.get(),
                        head, tSeriesLength/2 + 1, frequencyIncrement, progVar,
                        CollectorBuffer.data.get(), CollectorBuffer.occup, nullptr );
    else exportSpectrum( oFileName, segmentDescriptor, tmpPath, buffers[0] -> data.get(), nullptr, head, tSeriesLength/2 + 1, frequencyIncrement,
                         progVar, CollectorBuffer.data.get(), CollectorBuffer.occup, nullptr );

    //clean up temp files
    std::filesystem::remove( tmpPath );
    if( progVar != expectProg )
    {
        std::cerr << "Unexpected error occured while exporting the spectrum!\n";
        return -1;
    }
    if(!cInfo.isRedirected) MonitorThread.join();
    else std::cout << "Exported a " << printMemSize( sizeof(float) * expectProg ) << " spectrum into \"" + oFileName + "\"\n";

    return 0;
}
