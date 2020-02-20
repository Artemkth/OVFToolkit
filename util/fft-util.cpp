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

//headers for parallelization, hurray for atomic future :D
#include<atomic>
#include<future>
#include<thread>
#include<mutex>

//parsing program options
#include<boost/program_options.hpp>

//file i/o handling
#include<OVFParser.h>
#include<OVFWriter.h>

//miscaleneous options from cmake
#include"OVFToolkitConfig.h"

//fft engines
#include"cuda-backend.h"

//assert macro
#include<cassert>

using fname_type = std::string;
using namespace std::string_literals;

//prefetch metadata, form it into arrays of times and metadata
auto ParseMetadata(const std::vector<fname_type>& fList, const std::string& regex_str, std::atomic<std::size_t>& progCnt, std::atomic<const fname_type::value_type*>& curStr)
{
    //compile the regex for getting time stamp as best as we can
    std::regex timeRegex( regex_str, std::regex_constants::ECMAScript | std::regex_constants::optimize );

    //get the hint for how much thread can run on a machine, and if it is non-zero target at most 4 threads
    //otherwise run import single thread
    const auto concHint = std::thread::hardware_concurrency();
    const auto fCount = fList.size();
    const std::size_t tCount {std::min<std::size_t>(concHint == 0 ? 1 : std::min<std::size_t>(concHint, 4), fCount)};

    //main worker lambda, checkerboard process the file list
    auto worker = [&] (std::size_t shift) -> auto 
    {
        std::vector< std::pair<std::optional<double>, VField::VFieldFile> > results{};
        results.reserve( fList.size()/tCount + 1 ); //reserve space since we roughly know the size
        const auto regexThreadCopy = timeRegex;
        VField::VFieldFile handle{}; 
        std::smatch pat_matches{};

        auto begin = fList.begin();
        std::advance( begin, shift );
        auto end = fList.end();
        while( begin < end )
        {
            handle.read( *begin, true ); //only fetch the header, TODO: check if I want to output some file errors in here
            //if file is not single segment, give up LULW
            if( handle.cntSegments()!=1 )
            {
                std::cerr << "Encountered bad segment count in file: \"" << *begin << "\": " << handle.cntSegments() << "\n";
                results.push_back({std::nullopt, {}});
                std::advance( begin, tCount ); ++progCnt;
                //set last file to the one we processed 
                curStr = begin -> c_str();
                continue;
            }

            std::optional<double> time {std::nullopt};
            //and then check if header is oiro
            const VField::OVFHeader& ref = handle.getSegmentHeader(0);
            if( ref.isSet(VField::OVFParameter::Desc) && std::regex_search( ref.getString(VField::OVFParameter::Desc), pat_matches, regexThreadCopy ) )
            {
                char* ret {nullptr};
                auto str = pat_matches[1].str();
                double val = strtod(str.c_str(), &ret);
                if( ret != str.c_str() )
                    time = val;
            }
            else //could not parse time
                std::cerr << "Could not parse time from 'Description' field in file \"" << *begin << "\", with regular expression \""<< regex_str <<"\"."
                             "Got \"" << (ref.isSet(VField::OVFParameter::Desc) ? ref.getString(VField::OVFParameter::Desc) : "*NOTHING*") << "\" in the description field!\n";
            results.push_back({time, handle});

            //set last file to the one we processed 
            curStr = begin -> c_str();

            std::advance( begin, tCount ); ++progCnt;
        }

        return results;
    }; 

    //vector of futures with results of prefetching
    progCnt = 0;
    std::vector<std::future<
        std::vector< std::pair<std::optional<double>, VField::VFieldFile> >
        >> resultFuture{}; resultFuture.reserve(tCount);
    //initialize with async, last future is set to deferred to not waste a good thread LULW
    for(std::size_t i = 0; i < tCount - 1; i++)
        resultFuture.push_back(std::async( std::launch::async, worker, i));
    //alternative is creating promise and then forwarding result through it
    resultFuture.push_back( std::async( std::launch::deferred, worker, tCount - 1) );
    std::vector< std::vector<std::pair<std::optional<double>, VField::VFieldFile>> > results{}; results.reserve(tCount);
    for(auto& x: resultFuture)
        results.push_back( x.get() );

    //and reshape data to fill in the return structure
    std::vector<std::optional<double>> times{};   times.reserve(fCount);
    std::vector<VField::VFieldFile>    handles{}; handles.reserve(fCount);
    for(std::size_t i = 0; i < fCount; i++)
    {
        auto& value = results[ i%tCount ][ i/tCount ];
        times.push_back( std::move(value.first) );
        handles.push_back( std::move(value.second) );
    }

    return std::make_pair(std::move(times), std::move(handles));
}

class CMDMonitor
{
    private:
        std::ostream& out;
        std::size_t cCount {};//count of max possible characters in current line
        std::string lineData {};
        bool ready {false};

        //static magic
        static constexpr const char* cOff = "\e[?25l";
        static constexpr const char* cOn = "\e[?25h";

        void pad_n(std::size_t n)
        {out << std::string(n, ' ');}

    public:
        CMDMonitor(std::ostream& out_): out(out_), ready(true), cCount(0) { out << cOff;}
        ~CMDMonitor() {if (ready) out << cOn << '\n' << std::flush;}
        CMDMonitor(const CMDMonitor&) = delete; //DAS IST VERBOTTEN
        CMDMonitor& operator= (const CMDMonitor&) = delete;//DIESER AUCH

        //update
        void update(const std::string& str)
        { 
            assert(("Tried updating without initializing!", ready));
            out << str;

            if( str.length() > cCount )
                pad_n( str.length() - cCount );
            out << '\r' << std::flush;

            cCount = str.length();
            lineData = str;//store a copy just in case
        }

        void prependLine(const std::string& str)
        {
            assert(("Tried updating without initializing!", ready));
            out << str;

            if( str.length() > cCount )
                pad_n( str.length() - cCount );
            out << '\n';

            out << lineData << '\r' << std::flush;
        }
};

//getting *fancy* with ASCII :D
class Spiner 
{
    static constexpr const std::array<char, 4> charCycle {'-', '\\', '|', '/'};
    std::array<char, 4>::const_iterator currentState = charCycle.begin();

public:
    Spiner& operator++()
    { ++currentState; if(currentState == charCycle.end()) currentState = charCycle.begin(); return *this; }

    friend std::ostream& operator << (std::ostream& out, const Spiner& spin)
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

template<typename... T>
class sort_helper : public std::tuple<std::decay_t<T>* ...>
{
    using baseTuple = std::tuple<std::decay_t<T>*...>;
    
    template<typename U, std::size_t... I>
    void swap_content(sort_helper&& ref, std::integer_sequence<U, I...>)
    { (std::swap(*std::get<I>(*this), *std::get<I>(ref)),...); }
public:
    sort_helper() = default;
    sort_helper(std::decay_t<T>*... args): baseTuple(args...) {}

    sort_helper(const sort_helper&) = delete;
    sort_helper& operator=(const sort_helper&) = delete;

    //and move operators
    sort_helper(sort_helper&& ref) //TODO: investigate why std::forward doesn't work here, wtf!
    { swap_content(std::move(ref), std::make_index_sequence<std::tuple_size_v<baseTuple>>{}); }
    sort_helper& operator=(sort_helper&& ref)
    { swap_content(std::move(ref), std::make_index_sequence<std::tuple_size_v<baseTuple>>{}); return *this; }
};

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
        std::copy_n( field.getData<T>() + offset, cnt, arr );
    else
    {
        auto begin = field.cbegin<T>();
        auto end = field.cend<T>();
        const auto dim = field.Header.expectedDimension();

        std::copy_n(*begin + skip + offset, dim - skip - offset, arr);
        begin++;

        for(; begin != end; ++begin)
        {
            std::copy_n(*begin + skip, cnt > (dim - skip)? dim - skip : cnt % (dim - skip), arr);
            arr += dim - skip; cnt -= dim - skip;
        }
    }
}

//group import data
void readData( const std::vector<VField::VFieldFile>& handles, float* data,
               std::size_t offset,
               std::atomic<std::size_t>& progMax,
               std::atomic<std::size_t>& progress,
               std::size_t& impLen) 
{
    progress = 0;
    
    const auto& head = handles.front().getSegmentHeader(0);
    const auto mType = head.getMeshType();
    const auto dim   = head.expectedDimension();
    const auto pts   = head.expectedPoints();
    const auto len   = handles.size();
    const auto vdim  = dim - (mType == VField::OVFHeader::MeshType::rectangular? 0 : 3);
    impLen = std::min( impLen, vdim * pts - offset );
    progMax = impLen * len;

    const auto concHint = std::thread::hardware_concurrency();
    const std::size_t tCount {std::min<std::size_t>(concHint == 0 ? 1 : std::min<std::size_t>(concHint, 4), len)};

    //for irregular meshes offset skips over coordinate tripplets
    const auto adjBegin  = (mType == VField::OVFHeader::MeshType::rectangular? offset : dim * (offset/vdim) + offset % vdim)/dim;
    const auto adjEnd    = ((mType == VField::OVFHeader::MeshType::rectangular? offset + impLen : offset + dim * (impLen/vdim) + impLen % vdim ) + dim - 1)/dim + 1;

    auto importer = [&] (std::size_t off)
    {
        auto begin = handles.cbegin();
        auto end   = handles.end();
        auto* dest = data + impLen * off;

        std::advance(begin, off);
        while( begin < end )
        {
            auto slice = begin -> readSlice(0, {adjBegin, adjEnd, 1});

            if( slice.curDataInternalSize() == 4 )
                loadData<float>( slice, dest, offset%vdim, impLen, mType == VField::OVFHeader::MeshType::rectangular? 0 : 3 );
            else
                loadData<double>( slice, dest, offset%vdim, impLen, mType == VField::OVFHeader::MeshType::rectangular? 0 : 3 );

            std::advance(begin, tCount);
            dest += impLen * tCount;
            progress += impLen;
        }
    };

    std::vector<std::thread> workers; workers.reserve(tCount - 1);
    for(std::size_t i = 0; i < tCount - 1; i++)
        workers.emplace_back(importer, i);
    importer(tCount - 1);
    for(auto& x: workers)
        x.join();
}

//export into one yuge ovf with multiple segments, defaults to OVF version 2 trying to convert the headers
//descriptor has list of ranges of points produced during fft transform, hostBuffer has first bufferCnt ranges in it,
//the rest were written into file 'fileBuffer', freqInc gives the increment for frequencies in list of length cnt
bool exportSpectrum( const std::filesystem::path& outputFile,
                     const std::vector<std::array<std::size_t, 2>>& descriptor,
                     const std::filesystem::path& fileBuffer,
                     const VField::OVFHeader& commonHeader,
                     std::size_t cnt,
                     float freqInc,
                     float const* hostBuffer = nullptr,
                     std::size_t  ramBufferCnt  = 0,
                     float const* irregCoords= nullptr
                   )
{
    std::ifstream fsBuffer(fileBuffer, std::ios_base::in | std::ios_base::binary);
    if(!fsBuffer.good())
    {
        std::cerr << "Unable to open the buffer file: \"" << fileBuffer.c_str() << "\"!\n";
        return false;
    }
    const auto mType = commonHeader.getMeshType(); 
    const std::size_t vdim    = commonHeader.getUint( VField::OVFParameter::Vdim );
    const std::size_t pntCnt  = commonHeader.expectedPoints() * vdim; //guaranteed to be set because constructed earlier
    const std::size_t bufTotal= descriptor.size();
    assert(("Incompatible array dimensions!\n", vdim % 2 == 0 && pntCnt == descriptor.back()[1] * 2));

    //open output file
    std::ofstream output(outputFile, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    if(!output.good())
    {
        std::cerr << "Unable to open the output file: \"" << outputFile.c_str() << "\"!\n";
        return false;
    }

    //data, later to be put into VField container, disposed by VField's destructor
    float* data = new float[ commonHeader.expectedPoints() * commonHeader.expectedDimension() ];
    //copy the coordinates if buffer is irregular
    if(mType == VField::OVFHeader::MeshType::irregular)
    {
        assert(("Expected a coordinate field for transform", irregCoords != nullptr)); 
        for(std::size_t i = 0; i < pntCnt; i++)
            std::copy_n(irregCoords + i * 3, 3, data + i * (vdim + 3));
    }

    VField::VField field ( commonHeader );
    field.insertData( data, commonHeader.expectedPoints() * commonHeader.expectedDimension() );
    output << commonHeader.getString( VField::OVFParameter::VersionString ) << "\n" << "# Segment count: " << cnt;

    for(std::size_t i = 0; i < cnt; i++)
    {
        //add frequency stamp to the file
        const std::string& desc { commonHeader.getString( VField::OVFParameter::Desc) };
        field.Header.set( VField::OVFParameter::Desc, desc + (!desc.empty()? '\n' : '\0')+ "f = " + std::to_string(freqInc * i) + " Hz");

        //start copying data from mixed sources into vfield
        for( std::size_t j = 0; j <  bufTotal; j++ )
        {
            //for a case when reading from file buffer
            std::unique_ptr<float[]> importData;

            std::size_t offset = 2 * descriptor[j][0];
            std::size_t dist = 2 * ( descriptor[j][1] - descriptor[j][0] );
            float* dest = (mType == VField::OVFHeader::MeshType::rectangular ? data + offset : data + (3 + vdim) * offset/vdim + 3 + offset % vdim);
            const float* curSection { nullptr };
            if( j < ramBufferCnt )
                curSection = hostBuffer + ( offset * cnt + i * dist ) * vdim  + offset ; 
            else
            {
                fsBuffer.ignore( i * sizeof(float)/sizeof(std::ofstream::char_type) * dist );

                //reserve space for data
                importData = std::make_unique<float[]>( dist );
                fsBuffer.read( (std::ofstream::char_type*)importData.get(), sizeof(float)/sizeof(std::ofstream::char_type) * dist );
                fsBuffer.ignore( (cnt - i - 1) * sizeof(float)/sizeof(std::ofstream::char_type) * dist );
                curSection = importData.get();
            }

            //and copy data into destination buffer
            if(mType == VField::OVFHeader::MeshType::rectangular)
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
        }

        //seek to the beginning
        fsBuffer.seekg(0, std::ios_base::beg);

        //output the VField
        output << '\n';
        WriteSegment(output, field);
    }

    return true;
}

//TODO: look if windows can deal with UTF here, maybe implement winmain with UTF-16 parameters
//TODO: include link to setargv.obg/wsetargv.obj in the windows build, look at https://docs.microsoft.com/en-us/cpp/c-runtime-library/link-options?view=vs-2019
int main(int argc, char** argv)
{
    std::vector<fname_type> fileList{};
    std::string TimeRegExStr {};
    std::string oFileName {};
    //first parse command-line options
    try
    {
        //TODO: write code to get console width to make description more easily readable
        //https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
        //https://docs.microsoft.com/en-us/windows/console/getconsolescreenbufferinfo?redirectedfrom=MSDN
        boost::program_options::options_description desc("Usage: ovf-batch [options] files...\nOptions");
        //populate options list
        desc.add_options()
            ("help,h", boost::program_options::bool_switch(), "Produce this help message.")
            ("version,v", boost::program_options::bool_switch(), "Get this software's version information.")
            ("input-files", boost::program_options::value<std::vector<fname_type>>(&fileList)->multitoken()->required(), "Time sequence of vector fields in .ovf files." )
            ("output,o", boost::program_options::value<std::string>(&oFileName)->default_value("spectrum.ovf"), "Spectrum output file name.")
            ("max-ram", boost::program_options::value<std::string>(), "Maximum ammount of RAM allocated on host machine for buffers.")
            ("time-regex", boost::program_options::value<std::string>(&TimeRegExStr)->default_value("Total simulation time:\\s+(.+?)\\s+s"), "Regex pattern to extract time from .ovf files.");

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
    cuFFTEngine gpuFFT;

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
    std::future<std::string> gpuInit{};

    //evaluation monitors
    std::atomic<std::size_t> importedCount {};
    std::atomic<const fname_type::value_type*> lastFile { "No file imported yet." };

    auto importMonitor = [&] () -> void 
    {
        CMDMonitor monitor(std::cout);
        const auto expSize = tSeriesLength;
        const auto expSizeStr = std::to_string(expSize);

        std::string message {};
        const auto cnt = std::to_string(expSize);

        bool lastRun = true;
        while(true)
        {
            std::string message { "File " };
            const std::size_t cCount = importedCount;
            const auto name = std::string { lastFile };
            const auto cCountStr = std::to_string(importedCount);

            message += std::string ( expSizeStr.length() - cCountStr.length(), ' ' );
            message += cCountStr;
            message += '/';
            message += expSizeStr;
            message += ": \"";
            message += name;
            message += '\"';

            monitor.update(message);
            if( cCount >= expSize )
                break;

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        } 
    };

    //time prefetch phase for profiling
    std::thread watch(importMonitor);
    auto t_before = std::chrono::steady_clock::now();
    auto [timeOpt, file_handles] = ParseMetadata(fileList, TimeRegExStr, importedCount, lastFile);
    auto t_after = std::chrono::steady_clock::now();
    watch.join(); //TODO: check how it will fail if importedCount != tSeriesLength

    std::vector<double> times{}; times.reserve( tSeriesLength );
    std::cout << "Done pre-fetching .ovf metadata for " << tSeriesLength << " files in " << std::chrono::duration<double>(t_after - t_before).count() << " seconds." << "\n";
    {
        //check the times
        auto dOptIt = timeOpt.cbegin();
        auto dOptEnd = timeOpt.cend();
        auto fileIt = file_handles.cbegin();
        std::string noTSFiles{};
        std::string dupTSFiles{};
        bool encounteredDup {false};

        //iterators for duplicate checks
        std::vector<VField::VFieldFile>::const_iterator curFileIt {};

        //first loop. merged duplicate check and time set check
        for(; dOptIt != dOptEnd; ++dOptIt)
        {
            if( !dOptIt -> has_value() )
                noTSFiles += ( noTSFiles.empty() ? ""s :", "s) + fileIt -> getCurrentPath();
            else
            {
                if(times.empty())
                { curFileIt = fileIt; }
                if(!times.empty() && times.front() == *dOptIt)
                {
                    if(!encounteredDup)
                    {
                        if(!dupTSFiles.empty()) dupTSFiles += '\n';
                        dupTSFiles += "t="s + std::to_string(times.front()) + ": \"" + curFileIt -> getCurrentPath() + "\", ";
                    }
                    else
                        dupTSFiles += ", "s;

                    dupTSFiles += "\""s + fileIt -> getCurrentPath() + '\"';
                }
                times.push_back( dOptIt -> value() );
            }

            fileIt++;
        }

        //duplicate check loop, starts from second value
        if(times.size() == tSeriesLength)   //equivalent to noTSFiles.empty()
        {
            auto cValIt = ++times.cbegin();
            auto endIt = times.cend();
            while(++cValIt != endIt)
            {
                //reset for next run
                curFileIt++;
                if(encounteredDup) dupTSFiles += '\n';
                encounteredDup = false;

                auto startVal = cValIt;
                while( ++startVal != endIt )
                    if ( *startVal == *cValIt )
                    {
                        if(!encounteredDup)
                        {
                            if(!dupTSFiles.empty()) dupTSFiles += '\n';
                            dupTSFiles += "t="s + std::to_string(times.front()) + ": \"" + curFileIt -> getCurrentPath() + "\", ";
                        }
                        else
                            dupTSFiles += ", "s;

                        dupTSFiles += "\""s + fileIt -> getCurrentPath() + '\"';
                    }
            }
        }

        //outputting stuff
        if( !noTSFiles.empty() )
            std::cout << "Following files were found to have no time stamp: " << noTSFiles << "\n";
        if( !dupTSFiles.empty() )
            std::cout << "Following timestamps were duplicated:\n" << dupTSFiles << "\n";
        if( !noTSFiles.empty() || !dupTSFiles.empty() )
            std::cout << "Aborting!\n";
    }

    std::size_t VFSize{};
    std::unique_ptr<struct GPUBuffer> buffers[2]; //tripple buffering, yay
    {
        //check if internal dimensions are compatible
        const auto expDim = file_handles.front().getSegmentHeader(0).expectedDimension();
        const auto expCnt = file_handles.front().getSegmentHeader(0).expectedPoints();

        if( expDim == 0 || expCnt == 0)
        {
            std::cerr << "First file has ill-formatted data: " << expCnt << " points with of " << expDim << " dimensions!\n";
            return 1;
        }
        //guaranteed to be set by this point
        const auto mType = file_handles.front().getSegmentHeader(0).getMeshType();
        VFSize = (expDim - (mType == VField::OVFHeader::MeshType::rectangular? 0 : 3)) * expCnt;
        //begin initialization of engines outside main thread once dimensions are known
        auto GPUBuffersInit = [&] ()
        {
            auto res = gpuFFT.Init( tSeriesLength, VFSize, 0 );
            auto cPoints = gpuFFT.expectedBatch() * (gpuFFT.expectedLength()/2 + 1);
            if( gpuFFT.isReady() )
                for( auto& x: buffers )
                {
                    x = std::make_unique<GPUBuffer> ();
                    x -> nSize = 2 * cPoints;
                    x -> realPoints = gpuFFT.expectedBatch();
                    x -> data = std::make_unique<float[]>( x->nSize );
                }

            return res;
        };
        gpuInit = std::async( std::launch::async, GPUBuffersInit );

        auto begin = ++file_handles.cbegin();
        auto end = file_handles.cend();
        std::string badFiles {};
        for(; begin != end; ++begin)
        {
            const auto& head = begin -> getSegmentHeader(0);
            if ( head.expectedDimension() != expDim ||
                 head.expectedPoints()    != expCnt ||
                 head.getMeshType()       != mType    )
            {
                if(!badFiles.empty()) badFiles += ", ";
                badFiles += "\""s + begin -> getCurrentPath() + "\"";
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

    //check if files are sorted by timestamp
    if( !std::is_sorted(times.begin(), times.end()) )
    {
        std::cout << "File list received was not ordered by time, sorting it now!\n";
        auto tItBegin = times.begin();
        auto tItEnd   = times.begin();
        auto fItBegin = file_handles.begin();      

        std::vector<sort_helper<double, VField::VFieldFile>> helper; helper.reserve(tSeriesLength);
        for(; tItBegin != tItEnd; ++tItBegin)
            helper.emplace_back(&(*tItBegin), &(*fItBegin++));

        //TODO: see if this hack works, and if it can be replaced completely
        //CAUTION: I am surprised this even compiles :D
        std::sort( helper.begin(), helper.end(), [&](const sort_helper<double, VField::VFieldFile>& el1,
                                                     const sort_helper<double, VField::VFieldFile>& el2) { return *std::get<0>(el1) < *std::get<0>(el2); } );
    }

    //work on time array to set some more options
    double trueStep{ (times.back() - times.front())/(tSeriesLength - 1) }; bool reinterp {false};
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
        auto fIt = file_handles.cbegin();
        double expectedTime = *tIt;
        for(; tIt != tEnd; ++tIt)
        {
            if( std::abs( *tIt - expectedTime ) > 3 * TstepDisp )
            {
                if( !outliers.empty() ) outliers += ", ";
                outliers += "\""s + fIt -> getCurrentPath() + "\" (dt=" + std::to_string( *tIt - expectedTime ) + ")";
            }

            ++fIt; expectedTime += trueStep;
        }
        if(!outliers.empty())
            std::cout << "Following files found to be far away from expected times: " << outliers << '\n';
    }

    //wait here for GPU to finish initializing, and buffers being created
    std::cout << gpuInit.get() << "\n";

    //stuff for streaming buffers to gpu
    std::mutex rotLock; //mutex to acomplish buffer rotation
    std::condition_variable gpuRotate;
    //float norm = std::sqrt(times.back() - times.front()) / (std::sqrt(gpuFFT.expectedLength()) );
    float norm = 1.0f;
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

            if( !gpuFFT.isReady() || curBuffer -> nSize < gpuFFT.expectedLength() || curBuffer -> nSize > 2 * gpuFFT.expectedLength() * gpuFFT.expectedBatch() || 
                !gpuFFT.RunTransform(curBuffer -> data.get(), norm, gpuFFT.expectedBatch() - curBuffer -> realPoints ))
            {
                curBuffer -> state = BufferState::FAIL;
                return; //stop if failure was encountered
            }

            curBuffer -> state = BufferState::EXPORT;
        }
    });

    std::atomic<std::size_t> progVar{};
    std::atomic<std::size_t> expectProg{};
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
    //and a monitor function
    std::atomic<bool> monitorOn {true};
    std::thread MonitorThread( [&] ()
    {
        using namespace std::chrono_literals;

        //static copy of buffer pointers, doesn't rotate
        GPUBuffer* buff[2];
        for(int i = 0; i < 2; i++)
            buff[i] = buffers[i].get();

        auto beginTime = std::chrono::steady_clock::now();

        const int tStampPadding { 10 };
        const int bufferPadding { 25 };

        Spiner spin[2];
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
                if(prState.length() < bufferPadding ) res += std::string( bufferPadding - prState.length(), ' ' );
            }

            monitor.update(res);
        }
    });

    //after this main thread works with I/O
    const auto BatchSize = gpuFFT.expectedBatch();
    std::vector<std::array<std::size_t, 2>> segmentDescriptor;
    //open a temporary file for outputting results of fft
    std::filesystem::path tmpPath(".batchfft-temp");
    std::ofstream tmpFile (tmpPath, std::ios_base::out |
                                    std::ios_base::binary |
                                    std::ios_base::trunc );
 
    auto exportData = [&] (GPUBuffer* buff)
    {
        tmpFile.write( (char*) buff -> data.get(), (tSeriesLength / 2 + 1) * buff -> realPoints * 2 * sizeof(float) / sizeof(std::ofstream::char_type) );

        buff -> state = BufferState::WAIT;
    };

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
            std::cerr << "GPU thread failed!\n";
            return -1;
        }


        curBuffer -> state = BufferState::IMPORT;
        const std::size_t begin = segmentDescriptor.empty()? 0lu : segmentDescriptor.back()[1];
        readData( file_handles, curBuffer -> data.get(), begin, 
                expectProg, progVar, curBuffer -> realPoints );
        segmentDescriptor.push_back( {begin, begin + curBuffer -> realPoints} );
        curBuffer -> state = BufferState::PROCESS;

        rotLock.lock();
        std::swap(buffers[0], buffers[1]);
        rotLock.unlock();
        gpuRotate.notify_all();
    }
    //guaranteed to be filled after last rotation
    exportData(buffers[1].get());
    buffers[1] -> state = BufferState::STOP;

    //and wait for the last one to finish processing
    rotLock.lock();
    exportData(buffers[0].get());
    buffers[0] -> state = BufferState::STOP;
    rotLock.unlock();

    gpuRotate.notify_all();
    gpuStreamThread.join();
    for(auto& x : buffers)
        x.reset();

    //deinit the monitor 
    monitorOn = false;
    MonitorThread.join();

    //and close tmp file
    tmpFile.close();
    auto head = file_handles.front().getSegmentHeader(0);
    head.at<VField::pType::String>(VField::OVFParameter::VersionString) = "# OOMMF OVF 2.0";
    if( head.isSet(VField::OVFParameter::Vdim) )
        head.at<VField::pType::Uint>(VField::OVFParameter::Vdim) *= 2;
    else
        head.at<VField::pType::Uint>(VField::OVFParameter::Vdim) = 6;

    exportSpectrum( oFileName, segmentDescriptor, tmpPath, head, tSeriesLength/2 + 1, 1/(2. * (times.back() - times.front())), nullptr, 0, nullptr );

    //clean up temp files
    std::filesystem::remove( tmpPath );

    return 0;
}

