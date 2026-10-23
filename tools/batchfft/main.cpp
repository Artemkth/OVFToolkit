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
#include<expected>
#include<functional>

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
#ifdef OVFTOOLKIT_HAS_CUFFT
#include"cuda-backend.h"
#endif
#ifdef OVFTOOLKIT_HAS_FFTW
#include"fftw-backend.h"
#endif
#include"fft-frequency.h"

//console backend
#include"console.h"

//assert macro
#include<cassert>

#if defined(_WIN32)
#include<windows.h>
#endif

using fname_type = std::string;
using namespace std::string_literals;

std::filesystem::path pathFromUtf8(std::string_view text)
{
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path{std::u8string(first, first + text.size())};
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

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
    // Only physical field values enter this buffer and the FFT backends.
    std::unique_ptr<float[]> transformData{};
    std::size_t nSize{0};
    std::size_t realPoints{0};
    // Irregular-mesh xyz positions bypass the FFT and are reattached verbatim.
    std::vector<float> spatialCoordinates{};

    BufferState state{ BufferState::WAIT };
};

struct CollectorBuffer
{
    std::unique_ptr<float[]> data{};
    std::size_t count{};
    std::size_t occupied{};
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

template<typename T>
void loadPointValues(const VField::VField& field, float* destination,
                     std::size_t valueDimension, float* coordinates)
{
    const auto meshType = field.header().meshType().value();
    const auto storedDimension = field.header().pointDimension().value();
    const auto points = field.pointCount();
    const auto* source = field.data<T>();
    for(std::size_t point = 0; point < points; ++point)
    {
        const auto* sourcePoint = source + point * storedDimension;
        if(meshType == VField::MeshType::Irregular)
        {
            if(coordinates)
                std::transform(sourcePoint, sourcePoint + 3,
                               coordinates + 3 * point,
                               [](T value) { return static_cast<float>(value); });
            sourcePoint += 3;
        }
        std::transform(sourcePoint, sourcePoint + valueDimension,
                       destination + point * valueDimension,
                       [](T value) { return static_cast<float>(value); });
    }
}

//group import data
void readData( const std::vector<std::pair<std::size_t, const VField::VFieldFile>>& handles,
               float* transformValues,
               std::size_t offset,
               std::atomic<std::size_t>& progMax,
               std::atomic<std::size_t>& progress,
               std::size_t& impLen,
               std::vector<float>* spatialCoordinates = nullptr )
{
    progress = 0;
    
    const auto& head = handles.front().second.header(0).value().get();
    const auto mType = head.meshType().value();
    const auto dim   = head.pointDimension().value();
    const auto pts   = head.pointCount().value();
    const auto len   = handles.size();
    const auto vdim  = dim - (mType == VField::MeshType::Rectangular? 0 : 3);
    impLen = std::min( impLen, vdim * pts - offset );
    if(offset % vdim != 0 || impLen % vdim != 0)
        throw std::logic_error("FFT batch is not aligned to complete OVF points");
    progMax = impLen * len;

    const auto firstPoint = offset / vdim;
    const auto pointCount = impLen / vdim;
    if(spatialCoordinates && mType == VField::MeshType::Irregular)
        spatialCoordinates->resize(3 * pointCount);

    auto importer = [&](const std::pair<std::size_t, VField::VFieldFile>& handle)
    {
        auto slice = handle.second.readSlice(0, firstPoint, pointCount).value();
        // Coordinates are copied once into a side buffer. They never enter
        // transformValues, which is the only storage passed to RunTransform.
        auto* coordinateOutput = spatialCoordinates && handle.first == 0
            ? spatialCoordinates->data() : nullptr;

        if (slice.scalarSizeBytes() == 4)
            loadPointValues<float>(slice, transformValues + impLen * handle.first,
                                   vdim, coordinateOutput);
        else
            loadPointValues<double>(slice, transformValues + impLen * handle.first,
                                    vdim, coordinateOutput);

        progress += impLen;
    };

    std::for_each(ioPolicy, handles.cbegin(), handles.cend(), importer);
}

std::vector<VField::OVFHeader> spectrumHeaders(
    const VField::OVFHeader& commonHeader, std::size_t frequencyCount,
    double frequencyIncrement)
{
    std::vector<VField::OVFHeader> headers(frequencyCount, commonHeader);
    const auto description = commonHeader.contains(VField::OVFParameter::Desc)
        ? commonHeader.requireAs<std::string>(VField::OVFParameter::Desc)
        : std::string{};
    for(std::size_t frequency = 0; frequency < frequencyCount; ++frequency)
        headers[frequency].set(VField::OVFParameter::Desc,
            description + (!description.empty() ? "\n" : "") +
            std::format("f = {:.9g} Hz",
                        frequencyIncrement * static_cast<double>(frequency)));
    return headers;
}

bool streamSpectrumBatch(VField::OVFStreamWriter& writer,
                         const GPUBuffer& buffer,
                         std::size_t valueDimension,
                         VField::MeshType meshType,
                         std::atomic<std::size_t>& progress)
{
    const auto pointCount = buffer.realPoints / valueDimension;
    const auto outputValueDimension = 2 * valueDimension;
    using FloatPoints = VField::md::mdspan<
        const float, VField::md::dextents<std::size_t, 2>,
        VField::md::layout_right>;
    std::vector<float> irregularPoints;
    if(meshType == VField::MeshType::Irregular)
    {
        if(buffer.spatialCoordinates.size() != 3 * pointCount)
        {
            std::cerr << "Irregular FFT batch has an incomplete coordinate side buffer\n";
            return false;
        }
        irregularPoints.resize(pointCount * (outputValueDimension + 3));
    }

    for(std::size_t frequency = 0; frequency < writer.segmentCount(); ++frequency)
    {
        const auto* frequencyData = buffer.transformData.get() +
            frequency * 2 * buffer.realPoints;
        auto& sink = writer.segment(frequency);
        if(meshType == VField::MeshType::Rectangular)
            sink << FloatPoints{frequencyData, pointCount, outputValueDimension};
        else
        {
            for(std::size_t point = 0; point < pointCount; ++point)
            {
                auto* destination = irregularPoints.data() +
                    point * (outputValueDimension + 3);
                std::copy_n(buffer.spatialCoordinates.data() + 3 * point, 3,
                            destination);
                std::copy_n(frequencyData + outputValueDimension * point,
                            outputValueDimension, destination + 3);
            }
            sink << FloatPoints{irregularPoints.data(), pointCount,
                                outputValueDimension + 3};
        }
        if(!sink)
        {
            std::cerr << "Failed to stream frequency segment " << frequency
                      << ": " << sink.error()->message << '\n';
            return false;
        }
        progress += 2 * buffer.realPoints;
    }
    return true;
}

std::vector<float> readIrregularCoordinates(const VField::VFieldFile& file)
{
    const auto& header = file.header(0).value().get();
    if(header.meshType() != VField::MeshType::Irregular)
        return {};
    const auto pointCount = header.pointCount().value();
    const auto pointDimension = header.pointDimension().value();
    auto field = file.readSlice(0, 0, pointCount).value();
    std::vector<float> coordinates(3 * pointCount);
    if(field.scalarSizeBytes() == sizeof(float))
    {
        const auto* source = field.data<float>();
        for(std::size_t point = 0; point < pointCount; ++point)
            std::copy_n(source + point * pointDimension, 3,
                        coordinates.data() + 3 * point);
    }
    else
    {
        const auto* source = field.data<double>();
        for(std::size_t point = 0; point < pointCount; ++point)
            std::transform(source + point * pointDimension,
                           source + point * pointDimension + 3,
                           coordinates.data() + 3 * point,
                           [](double value) { return static_cast<float>(value); });
    }
    return coordinates;
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
    const std::size_t pointCount = commonHeader.pointCount().value();
    const std::size_t valueScalarCount = pointCount * vdim;
    const std::size_t bufTotal= descriptor.size();
    assert(("Incompatible array dimensions!\n", vdim % 2 == 0 &&
            valueScalarCount == descriptor.back()[1] * 2));

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
        for(std::size_t i = 0; i < pointCount; i++)
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
                assert(offset % vdim == 0 && dist % vdim == 0);
                const auto batchPoints = dist / vdim;
                for(std::size_t point = 0; point < batchPoints; ++point)
                    std::copy_n(curSection + point * vdim, vdim,
                                dest + point * (vdim + 3));
            }

            progVar += dist;
        }

        //seek to the beginning
        fsBuffer.seekg(0, std::ios_base::beg);

        //output the VField
        output << '\n';
        if(auto result = writeSegment(output, field); !result)
        {
            std::cerr << "Failed to write frequency segment " << i << ": "
                      << result.error().message << '\n';
            return false;
        }
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
void printGreeting()
{
    std::cout << "OVFToolkit time-domain FFT batch processing utility "
              << OVFTOOLKIT_VERSION_STRING << '\n'
              << "Copyright (c) 2020-2026 Artem Bondarenko\n"
              << "Available FFT engines:";
#ifdef OVFTOOLKIT_HAS_CUFFT
    std::cout << " gpu (cuFFT)";
#endif
#ifdef OVFTOOLKIT_HAS_FFTW
#ifdef OVFTOOLKIT_HAS_CUFFT
    std::cout << ',';
#endif
    std::cout << " fftw";
#endif
    std::cout << "\n\n" << std::flush;
}

struct BatchOptions
{
    std::vector<fname_type> inputFiles{};
    std::string timeRegex{};
    std::string outputFile{};
#if defined(OVFTOOLKIT_HAS_CUFFT)
    std::string engineName{"gpu"};
    int gpu{-1};
    std::optional<std::size_t> maxVRam{};
#else
    std::string engineName{"fftw"};
#endif
    std::optional<std::size_t> maxRam{};
    bool disableNormalization{};
    bool disableReinterpolation{};
    bool forceBufferedExport{};
};

std::expected<BatchOptions, int> parseCommandLine(int argc, char** argv)
{
    BatchOptions options;
    try
    {
        boost::program_options::options_description description(
            "Usage: ovf-batch [options] files...\nOptions",
            cInfo.GetConsoleWidth());
        description.add_options()
            ("help,h", boost::program_options::bool_switch(),
             "Produce this help message.")
            ("version,v", boost::program_options::bool_switch(),
             "Get this software's version information.")
            ("input-files",
             boost::program_options::value<std::vector<fname_type>>(
                 &options.inputFiles)->multitoken()->required(),
             "Time sequence of vector fields in .ovf files.")
            ("output,o", boost::program_options::value<std::string>(
                 &options.outputFile)->default_value("spectrum.ovf"),
             "Spectrum output file name.")
#ifdef OVFTOOLKIT_HAS_MULTIPLE_FFT_ENGINES
            ("engine", boost::program_options::value<std::string>(
                 &options.engineName)->default_value("gpu"),
             "FFT engine: gpu (default) or fftw.")
#endif
#ifdef OVFTOOLKIT_HAS_CUFFT
            ("gpu", boost::program_options::value<int>(&options.gpu)
                 ->default_value(-1),
             "GPU id for CUDA fft.")
#endif
            ("max-ram", boost::program_options::value<std::string>()->notifier(
                 [&options](const std::string& value)
                 { options.maxRam = parseMemSize(value); }),
             "Maximum ammount of RAM allocated on host machine for buffers.")
#ifdef OVFTOOLKIT_HAS_CUFFT
            ("max-vram", boost::program_options::value<std::string>()->notifier(
                 [&options](const std::string& value)
                 { options.maxVRam = parseMemSize(value); }),
             "Maximum ammount of RAM allocated on GPU for transform.")
#endif
            ("time-regex", boost::program_options::value<std::string>(
                 &options.timeRegex)->default_value(
                     "Total simulation time:\\s+(.+?)\\s+s"),
             "Regex pattern to extract time from .ovf files.")
            ("no-norm", boost::program_options::bool_switch(
                 &options.disableNormalization),
             "Don't normalize the fourier transform result.")
            ("no-reinterp", boost::program_options::bool_switch(
                 &options.disableReinterpolation),
             "Don't reinterpolate the data to remove jitter.")
            ("buffered-export", boost::program_options::bool_switch(
                 &options.forceBufferedExport),
             "Use the legacy temporary-file and sequential spectrum export path.");

        boost::program_options::positional_options_description positional;
        positional.add("input-files", -1);
        boost::program_options::variables_map variables;
        auto parser = boost::program_options::command_line_parser(argc, argv)
            .options(description).positional(positional);
        boost::program_options::store(parser.run(), variables);

        if(variables["help"].as<bool>())
        {
            printGreeting();
            std::cout << description;
            return std::unexpected(0);
        }
        if(variables["version"].as<bool>())
        {
            printGreeting();
            return std::unexpected(0);
        }

        boost::program_options::notify(variables);
        printGreeting();
        return options;
    }
    catch(const std::exception& error)
    {
        std::cerr << "Error while parsing command line: " << error.what() << "\n";
        return std::unexpected(-1);
    }
}

std::expected<std::unique_ptr<FFTEngine<float>>, int>
createEngine(const BatchOptions& options)
{
    if(options.engineName == "gpu")
    {
#ifdef OVFTOOLKIT_HAS_CUFFT
        int deviceCount{};
        const auto status = cudaGetDeviceCount(&deviceCount);
        if(status != cudaSuccess || deviceCount == 0 ||
           options.gpu >= deviceCount || options.gpu < -1)
        {
            std::cerr << "The requested GPU FFT engine is unavailable";
            if(status != cudaSuccess)
                std::cerr << ": " << cudaGetErrorString(status);
            else if(options.gpu >= deviceCount || options.gpu < -1)
                std::cerr << ": invalid GPU id " << options.gpu;
            std::cerr << ".\n";
            return std::unexpected(1);
        }
        return std::make_unique<cuFFTEngine>(options.gpu);
#else
        std::cerr << "The GPU FFT engine is unavailable in this build.\n";
        return std::unexpected(1);
#endif
    }
    if(options.engineName == "fftw")
    {
#ifdef OVFTOOLKIT_HAS_FFTW
        return std::make_unique<FFTWEngine>();
#else
        std::cerr << "The FFTW engine is unavailable in this build.\n";
        return std::unexpected(1);
#endif
    }
    std::cerr << "Unknown FFT engine '" << options.engineName << "'.\n";
    return std::unexpected(1);
}

bool validateInputFiles(const std::vector<fname_type>& files)
{
    if(files.size() < 2)
    {
        std::cerr << "At least 2 files were expected to be provided to form a "
                     "time series, a single file is already its own transform, "
                     "aborting!\n";
        return false;
    }

    std::vector<fname_type> missing;
    for(const auto& name: files)
    {
        const auto path = pathFromUtf8(name);
        if(!std::filesystem::exists(path) ||
           !std::filesystem::is_regular_file(path))
            missing.push_back(name);
    }
    if(missing.empty())
        return true;

    std::cerr << "Following files were not found: \"" << missing.front() << "\"";
    for(auto iterator = std::next(missing.begin()); iterator != missing.end();
        ++iterator)
        std::cerr << ", \"" << *iterator << "\"";
    std::cerr << '\n';
    return false;
}

std::thread metadataMonitor(std::atomic<std::size_t>& progress,
                            std::atomic<std::size_t>& expected,
                            std::atomic<const char*>& lastFile)
{
    if(cInfo.isRedirected)
        return {};
    return std::thread([&]
    {
        CMDMonitor monitor(std::cout);
        const auto width = std::to_string(expected.load()).size();
        while(true)
        {
            const auto current = progress.load();
            const auto currentText = std::to_string(current);
            const auto name = std::string{lastFile.load()};
            monitor.update("File " + std::string(width - currentText.size(), ' ') +
                           currentText + '/' + std::to_string(expected.load()) +
                           ": \"" + name + '\"');
            if(current >= expected)
                return;
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }
    });
}

std::expected<std::vector<metaPair>, int>
prefetchMetadata(const BatchOptions& options)
{
    std::atomic<std::size_t> progress{};
    std::atomic<std::size_t> expected{options.inputFiles.size()};
    std::atomic<const char*> lastFile{"No file imported yet."};
    auto monitor = metadataMonitor(progress, expected, lastFile);
    std::vector<metaPair> metadata(options.inputFiles.size());
    const std::regex timePattern(options.timeRegex,
        std::regex_constants::ECMAScript | std::regex_constants::optimize);

    auto parse = [&](const std::string& name) -> metaPair
    {
        auto opened = VField::VFieldFile::open(pathFromUtf8(name));
        if(!opened)
        {
            std::cerr << std::format("Failed to read '{}': {}\n", name,
                                     opened.error().message);
            lastFile = name.c_str();
            ++progress;
            return {std::nullopt, VField::VFieldFile{}};
        }
        auto file = std::move(*opened);
        if(file.segmentCount() != 1)
        {
            std::cerr << "Encountered bad segment count in file: \"" << name
                      << "\": " << file.segmentCount() << '\n';
            lastFile = name.c_str();
            ++progress;
            return {std::nullopt, std::move(file)};
        }

        std::optional<double> time;
        const auto& header = file.header(0).value().get();
        std::smatch matches;
        if(header.contains(VField::OVFParameter::Desc) &&
           std::regex_search(header.requireAs<std::string>(
                                 VField::OVFParameter::Desc),
                             matches, timePattern))
        {
            char* end{};
            const auto text = matches[1].str();
            const auto value = std::strtod(text.c_str(), &end);
            if(end != text.c_str())
                time = value;
        }
        else
            std::cerr << "Could not parse time from 'Description' field in file \""
                      << name << "\", with regular expression \""
                      << options.timeRegex << "\".Got \""
                      << (header.contains(VField::OVFParameter::Desc)
                          ? header.requireAs<std::string>(VField::OVFParameter::Desc)
                          : "*NOTHING*")
                      << "\" in the description field!\n";
        lastFile = name.c_str();
        ++progress;
        return {time, std::move(file)};
    };

    const auto before = std::chrono::steady_clock::now();
    std::transform(ioPolicy, options.inputFiles.cbegin(), options.inputFiles.cend(),
                   metadata.begin(), parse);
    const auto after = std::chrono::steady_clock::now();
    expected = progress.load();
    if(monitor.joinable())
        monitor.join();
    std::cout << "Done pre-fetching .ovf metadata for " << metadata.size()
              << " files in "
              << std::chrono::duration<double>(after - before).count()
              << " seconds.\n";
    return metadata;
}

std::expected<std::vector<double>, int>
prepareTimeline(std::vector<metaPair>& metadata)
{
    std::string missing;
    for(const auto& [time, file]: metadata)
        if(!time)
            std::format_to(std::back_inserter(missing), "{}{}",
                missing.empty() ? "" : ", ", pathToUtf8(file.path()));
    if(!missing.empty())
    {
        std::cout << "Following files were found to have no time stamp: "
                  << missing << "\nAborting!\n";
        return std::unexpected(-1);
    }

    const auto byTime = [](const metaPair& left, const metaPair& right)
    { return left.first < right.first; };
    if(!std::is_sorted(metadata.begin(), metadata.end(), byTime))
    {
        std::cout << "File list received was not ordered by time, sorting it now!\n";
        std::sort(metadata.begin(), metadata.end(), byTime);
    }

    std::vector<double> times(metadata.size());
    std::transform(metadata.begin(), metadata.end(), times.begin(),
        [](const metaPair& item) { return *item.first; });
    std::string duplicates;
    for(std::size_t first{}; first < times.size();)
    {
        auto last = first + 1;
        while(last < times.size() && times[last] == times[first])
            ++last;
        if(last - first > 1)
        {
            std::ostringstream formattedTime;
            formattedTime << std::scientific << std::setprecision(4)
                          << times[first];
            if(!duplicates.empty())
                duplicates += '\n';
            std::format_to(std::back_inserter(duplicates), "t={}: \"{}\"",
                formattedTime.str(), pathToUtf8(metadata[first].second.path()));
            for(auto index = first + 1; index < last; ++index)
                std::format_to(std::back_inserter(duplicates), ", \"{}\"",
                    pathToUtf8(metadata[index].second.path()));
        }
        first = last;
    }
    if(!duplicates.empty())
    {
        std::cout << "Following timestamps were duplicated:\n" << duplicates
                  << "\nAborting!\n";
        return std::unexpected(-1);
    }
    return times;
}

struct FieldLayout
{
    std::size_t valueCount{};
    std::size_t valueDimension{};
    VField::MeshType meshType{VField::MeshType::Rectangular};
};

std::expected<FieldLayout, int>
validateFieldLayouts(const std::vector<metaPair>& metadata)
{
    const auto& first = metadata.front().second.header(0).value().get();
    const auto pointDimension = first.pointDimension();
    const auto pointCount = first.pointCount();
    if(!pointDimension || !pointCount)
    {
        std::cerr << "First file has indeterminate point count or dimension!\n";
        return std::unexpected(1);
    }

    FieldLayout layout;
    layout.meshType = first.meshType().value();
    layout.valueDimension = *pointDimension -
        (layout.meshType == VField::MeshType::Rectangular ? 0 : 3);
    layout.valueCount = layout.valueDimension * *pointCount;
    std::string incompatible;
    for(auto iterator = std::next(metadata.cbegin()); iterator != metadata.cend();
        ++iterator)
    {
        const auto& header = iterator->second.header(0).value().get();
        if(header.pointDimension() != pointDimension ||
           header.pointCount() != pointCount ||
           header.meshType() != layout.meshType)
        {
            if(!incompatible.empty())
                incompatible += ", ";
            std::format_to(std::back_inserter(incompatible), "\"{}\"",
                           pathToUtf8(iterator->second.path()));
        }
    }
    if(!incompatible.empty())
    {
        std::cout << "Following files have incompatible grids: "
                  << incompatible << '\n';
        return std::unexpected(1);
    }
    const auto total = layout.valueCount * metadata.size();
    std::cout << "Found " << total << " values to be handled ("
              << printMemSize(sizeof(float) * total)
              << " of data in single precision).\n";
    return layout;
}

struct SamplingInfo
{
    double step{};
    bool disableReinterpolation{};
};

SamplingInfo analyzeSampling(const std::vector<double>& times,
                             const std::vector<metaPair>& metadata,
                             bool disableReinterpolation)
{
    SamplingInfo result{
        (times.back() - times.front()) / (times.size() - 1),
        disableReinterpolation};
    std::vector<double> distances(std::next(times.begin()), times.end());
    std::transform(distances.begin(), distances.end(), times.begin(),
                   distances.begin(), std::minus<>{});
    const auto average = Average(distances);
    for(auto& distance: distances)
        distance *= distance;
    const auto dispersion = std::sqrt(Average(distances) - average * average);
    std::cout << "Input array has even time step of " << result.step
              << " seconds. Average time step is " << average
              << " seconds, and time step dispersion is " << dispersion
              << " seconds. \n";

    std::string outliers;
    auto expectedTime = times.front();
    for(std::size_t index{}; index < times.size(); ++index,
        expectedTime += result.step)
    {
        if(std::abs(times[index] - expectedTime) > 3 * dispersion)
        {
            if(!outliers.empty())
                outliers += ", ";
            std::format_to(std::back_inserter(outliers),
                "\"{}\" (dt/disp={})", pathToUtf8(metadata[index].second.path()),
                (times[index] - expectedTime) / dispersion);
        }
    }
    if(!outliers.empty())
        std::cout << "Following files found to be far away from expected times: "
                  << outliers << ";\n";
    if(!result.disableReinterpolation && outliers.empty() &&
       dispersion <= 5 * std::numeric_limits<float>::epsilon() * result.step)
        result.disableReinterpolation = true;
    return result;
}

bool initializeEngineAndBuffers(
    FFTEngine<float>& engine, const BatchOptions& options,
    const FieldLayout& layout, std::size_t sampleCount,
    std::array<std::unique_ptr<GPUBuffer>, 2>& buffers,
    CollectorBuffer& collector, bool& bufferedExport,
    bool& automaticBufferedExport)
{
    auto memoryLimit = options.maxRam.value_or(0);
#ifdef OVFTOOLKIT_HAS_CUFFT
    if(options.engineName == "gpu")
        memoryLimit = options.maxVRam.value_or(0);
#endif
    const auto initialized = engine.Init(sampleCount, layout.valueCount,
        memoryLimit, layout.valueDimension);
    const auto batch = engine.expectedBatch();
    const auto complexPoints = batch * (sampleCount / 2 + 1);
    if(!engine.isReady())
        return initialized;

    const auto bufferBytes = sizeof(float) * 2 * complexPoints;
    const auto ramBufferCount = options.maxRam.value_or(0) / bufferBytes;
    const auto requiredBuffers =
        (layout.valueCount + batch - 1) / batch;
    automaticBufferedExport = options.maxRam.has_value() &&
        requiredBuffers <= ramBufferCount;
    bufferedExport = options.forceBufferedExport || automaticBufferedExport;
    const auto activeBuffers = std::min<std::size_t>(2, requiredBuffers);
    for(std::size_t index{}; index < activeBuffers; ++index)
    {
        buffers[index] = std::make_unique<GPUBuffer>();
        buffers[index]->nSize = 2 * complexPoints;
        buffers[index]->realPoints = batch;
        buffers[index]->transformData =
            std::make_unique<float[]>(buffers[index]->nSize);
    }
    if(bufferedExport && requiredBuffers > 2 && ramBufferCount > 2)
    {
        collector.count = std::min(requiredBuffers - 2, ramBufferCount - 2);
        collector.data =
            std::make_unique<float[]>(2 * complexPoints * collector.count);
    }
    return initialized;
}

int batchMain(int argc, char** argv)
{
    auto parsed = parseCommandLine(argc, argv);
    if(!parsed)
        return parsed.error();
    auto options = std::move(*parsed);
    if(!validateInputFiles(options.inputFiles))
        return 1;

    auto selectedEngine = createEngine(options);
    if(!selectedEngine)
        return selectedEngine.error();
    auto fft_engine = std::move(*selectedEngine);
    const auto tSeriesLength = options.inputFiles.size();
    bool bufferedExport { false };
    bool automaticBufferedExport { false };

    //evaluation monitors
    std::atomic<std::size_t> progVar{};
    std::atomic<std::size_t> expectProg{options.inputFiles.size()};
    std::thread MonitorThread{};

    auto prefetched = prefetchMetadata(options);
    if(!prefetched)
        return prefetched.error();
    auto filesMeta = std::move(*prefetched);

    auto preparedTimeline = prepareTimeline(filesMeta);
    if(!preparedTimeline)
        return preparedTimeline.error();
    auto times = std::move(*preparedTimeline);

    auto checkedLayout = validateFieldLayouts(filesMeta);
    if(!checkedLayout)
        return checkedLayout.error();
    const auto layout = *checkedLayout;
    const auto VFSize = layout.valueCount;
    const auto valueDimension = layout.valueDimension;
    const auto meshType = layout.meshType;
    std::array<std::unique_ptr<GPUBuffer>, 2> buffers;
    CollectorBuffer collector;
    auto engineInit = std::async(std::launch::async, [&]
    {
        return initializeEngineAndBuffers(*fft_engine, options, layout,
            tSeriesLength, buffers, collector, bufferedExport,
            automaticBufferedExport);
    });

    const auto sampling = analyzeSampling(times, filesMeta,
                                           options.disableReinterpolation);
    const auto trueStep = sampling.step;

    //wait here for the selected engine to initialize and buffers to be created
    engineInit.get();
    if( !fft_engine -> isReady() )
    {
        std::cerr << "Failed to initialize the " << options.engineName
                  << " FFT engine, quitting!\n";
        return -1;
    }
    if(automaticBufferedExport && !options.forceBufferedExport)
        std::cout << "The complete spectrum fits within --max-ram; using the "
                     "in-memory sequential export path.\n";
    //initialize interpolation
    if(!sampling.disableReinterpolation &&
       !fft_engine->InitInterp(times.data()))
            std::cerr << "Failed to initialize an interpolation!\n";

    //stuff for streaming buffers to gpu
    std::mutex rotLock; //mutex to acomplish buffer rotation
    std::condition_variable gpuRotate;
    const float norm { options.disableNormalization
        ? 1.0f : static_cast<float>(std::sqrt(trueStep)) };
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
    const auto frequencyCount = tSeriesLength / 2 + 1;
    const auto outputPath = pathFromUtf8(options.outputFile);
    auto head = filesMeta.front().second.header(0).value().get();
    transformHeader(head, options.timeRegex);
    std::optional<VField::OVFStreamWriter> spectrumWriter;
    if(!bufferedExport)
    {
        auto headers = spectrumHeaders(head, frequencyCount, frequencyIncrement);
        auto prepared = VField::OVFStreamWriter::create(
            outputPath, headers, sizeof(float));
        if(!prepared)
        {
            std::cerr << "Unable to prepare spectrum output: "
                      << prepared.error().message << '\n';
            return -1;
        }
        spectrumWriter.emplace(std::move(*prepared));
    }
    std::vector<std::array<std::size_t, 2>> segmentDescriptor;
    //open a temporary file for outputting results of fft
    std::filesystem::path tmpPath(".batchfft-temp");
    std::ofstream tmpFile;
    if(bufferedExport)
    {
        tmpFile.open(tmpPath, std::ios_base::out | std::ios_base::binary |
                              std::ios_base::trunc);
        if(!tmpFile.good())
        {
            std::cerr << "Unable to open temporary spectrum file "
                      << pathToUtf8(tmpPath) << '\n';
            return -1;
        }
    }
    //created array of handle-index pairs
    std::vector< std::pair<std::size_t, const VField::VFieldFile> > indexedHandles{};
    indexedHandles.reserve(filesMeta.size());
    for (std::size_t i = 0; i < filesMeta.size(); i++)
        indexedHandles.emplace_back(i, filesMeta[i].second);
 
    bool exportFailed{};
    auto exportData = [&] (GPUBuffer* buff)
    {
        if(!bufferedExport)
        {
            if(!streamSpectrumBatch(*spectrumWriter, *buff, valueDimension,
                                    meshType, progVar))
                exportFailed = true;
        }
        else if(collector.occupied < collector.count)
        {
            std::copy_n(buff -> transformData.get(),
                    frequencyCount * buff ->realPoints * 2,
                    collector.data.get() + collector.occupied * buff -> nSize );
            collector.occupied++;
        }
        else
            tmpFile.write((char*)buff -> transformData.get(), frequencyCount *
                buff -> realPoints * 2 * sizeof(float) /
                sizeof(std::ofstream::char_type) );

        buff -> state = BufferState::WAIT;
    };

    auto cleanupTemp = [&]
    {
        if(bufferedExport)
        {
            tmpFile.close();
            std::filesystem::remove(tmpPath);
        }
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
                    !fft_engine -> RunTransform(curBuffer -> transformData.get(), norm, fft_engine -> expectedBatch() - curBuffer -> realPoints ))
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
            if(exportFailed)
            {
                buffers[0]->state = BufferState::STOP;
                buffers[1]->state = BufferState::STOP;
                gpuRotate.notify_all();
                monitorOn = false;
                gpuStreamThread.join();
                if(!cInfo.isRedirected) MonitorThread.join();
                cleanupTemp();
                return -1;
            }
            if( buffers[0] -> state == BufferState::FAIL ||
                    buffers[1] -> state == BufferState::FAIL )
            {
                monitorOn = false;
                gpuStreamThread.join();
                if(!cInfo.isRedirected) MonitorThread.join();
                std::cerr << "GPU thread failed!\n";
                cleanupTemp();
                return -1;
            }

            curBuffer -> state = BufferState::IMPORT;
            const std::size_t begin = segmentDescriptor.empty()? 0lu : segmentDescriptor.back()[1];
            readData(indexedHandles, curBuffer -> transformData.get(), begin,
                    expectProg, progVar, curBuffer -> realPoints,
                    &curBuffer -> spatialCoordinates );
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
            cleanupTemp();
            return -1;
        }
        if(!bufferedExport && buffers[1]->state == BufferState::EXPORT)
            exportData(buffers[1].get());
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
            cleanupTemp();
            return -1;
        }
        else
        {
            if(!bufferedExport)
                exportData(buffers[0].get());
            buffers[0] -> state = BufferState::STOP;
        }

        if(exportFailed)
        {
            monitorOn = false;
            buffers[1]->state = BufferState::STOP;
            gpuRotate.notify_all();
            gpuStreamThread.join();
            if(!cInfo.isRedirected) MonitorThread.join();
            cleanupTemp();
            return -1;
        }

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
        readData(indexedHandles, buff -> transformData.get(), 0lu,
                expectProg, progVar, buff -> realPoints,
                &buff -> spatialCoordinates );
        segmentDescriptor.push_back( {0lu, buff -> realPoints} );
        buff -> state = BufferState::PROCESS;

        //now launch kernel to do the processing
        if( buff -> nSize < fft_engine -> expectedLength() || buff -> nSize > 2 * fft_engine -> expectedLength() * fft_engine -> expectedBatch() || 
            !fft_engine -> RunTransform(buff -> transformData.get(), norm, fft_engine -> expectedBatch() - buff -> realPoints ))
        {
            monitorOn = false;
            if(MonitorThread.joinable()) MonitorThread.join();

            std::cerr << "Error processing the data, aborting!";
            return -1;
        }
        if(!bufferedExport)
            exportData(buff);
        buff -> state = BufferState::STOP;

        monitorOn = false;
        if(MonitorThread.joinable()) MonitorThread.join();
        //and that's all, Pogchamp
    }

    //and close tmp file
    if(bufferedExport) tmpFile.close();

    expectProg = frequencyCount * VFSize * 2;
    progVar = 0;
    if(bufferedExport && !cInfo.isRedirected) MonitorThread = std::thread([&progVar, &expectProg, &monitorOn, &options]()
        {
            CMDMonitor monitor(std::cout);
            while(true)
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(150ms);

                auto curVal = progVar.load();
                monitor.update("Exporting spectrum into \""s + options.outputFile + "\": " + printMemSize( sizeof(float) * curVal ) +
                    '/' + printMemSize( sizeof(float) * expectProg ) );
                if(curVal >= expectProg)
                return;
            }
        });

    if(bufferedExport)
    {
        const auto coordinates = readIrregularCoordinates(filesMeta.front().second);
        const auto* coordinateData = coordinates.empty() ? nullptr : coordinates.data();
        bool exported{};
        if(BatchSize < VFSize) exported = exportSpectrum( outputPath, segmentDescriptor, tmpPath, buffers[1] -> transformData.get(), buffers[0] -> transformData.get(),
                            head, frequencyCount, frequencyIncrement, progVar,
                            collector.data.get(), collector.occupied,
                            coordinateData );
        else exported = exportSpectrum( outputPath, segmentDescriptor, tmpPath, buffers[0] -> transformData.get(), nullptr, head, frequencyCount, frequencyIncrement,
                             progVar, collector.data.get(), collector.occupied,
                             coordinateData );
        std::filesystem::remove(tmpPath);
        if(!exported)
        {
            progVar = expectProg.load();
            if(!cInfo.isRedirected) MonitorThread.join();
            return -1;
        }
    }
    else
    {
        if(auto result = spectrumWriter->finalize(); !result)
        {
            std::cerr << "Unable to finalize spectrum output: "
                      << result.error().message << '\n';
            return -1;
        }
        progVar = expectProg.load();
    }
    if( progVar != expectProg )
    {
        std::cerr << "Unexpected error occured while exporting the spectrum!\n";
        return -1;
    }
    if(bufferedExport && !cInfo.isRedirected) MonitorThread.join();
    else std::cout << "Exported a " << printMemSize( sizeof(float) * expectProg ) << " spectrum into \"" + options.outputFile + "\"\n";

    return 0;
}

#if defined(_WIN32)
namespace {
    std::string utf8Argument(const wchar_t* argument)
    {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            argument, -1, nullptr, 0, nullptr, nullptr);
        if(size == 0)
            throw std::runtime_error("Unable to decode a Windows command-line argument");
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1,
            result.data(), size, nullptr, nullptr);
        result.pop_back();
        return result;
    }
}

int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for(int index = 0; index < argc; ++index)
        arguments.push_back(utf8Argument(argv[index]));

    std::vector<char*> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for(auto& argument : arguments)
        argumentPointers.push_back(argument.data());
    return batchMain(argc, argumentPointers.data());
}
#else
int main(int argc, char** argv)
{ return batchMain(argc, argv); }
#endif
