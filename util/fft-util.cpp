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

//headers for parallelization, hurray for atomic future :D
#include<atomic>
#include<future>
#include<thread>

//parsing program options
#include<boost/program_options.hpp>

//file i/o handling
#include<OVFParser.h>
#include<OVFWriter.h>

//miscaleneous options from cmake
#include"OVFToolkitConfig.h"

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
            if(!ready)
                throw std::logic_error("Tried updating without initializing!");
            out << str;

            if( str.length() > cCount )
                pad_n( str.length() - cCount );
            out << '\r' << std::flush;

            cCount = str.length();
            lineData = str;//store a copy just in case
        }

        void prependLine(const std::string& str)
        {
            if(!ready)
                throw std::logic_error("Tried updating without initializing!");
            out << str;

            if( str.length() > cCount )
                pad_n( str.length() - cCount );
            out << '\n';

            out << lineData << '\r' << std::flush;
        }
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

//TODO: look if windows can deal with UTF here, maybe implement winmain with UTF-16 parameters
//TODO: include link to setargv.obg/wsetargv.obj in the windows build, look at https://docs.microsoft.com/en-us/cpp/c-runtime-library/link-options?view=vs-2019
int main(int argc, char** argv)
{
    std::vector<fname_type> fileList{};
    std::string TimeRegExStr {};
    //first parse command-line options
    try
    {
        //TODO: write code to get console width to make description more easily readable
        //https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
        //https://docs.microsoft.com/en-us/windows/console/getconsolescreenbufferinfo?redirectedfrom=MSDN
        boost::program_options::options_description desc("Usage: ovf-batch [options] files...\nOptions");
        //populate options list
        desc.add_options()
            ("help,h", "Produce this help message.")
            ("version,v", "Get this software's version information.")
            ("input-files", boost::program_options::value<std::vector<fname_type>>(&fileList)->multitoken()->required(), "Time sequence of vector fields in .ovf files." )
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

        if(vmap.count("help")) 
        {
            std::cout << desc ;
            return 0;
        }
        if(vmap.count("version"))
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

    //try to validate file list beforehand
    if( fileList.size() < 2 )
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
    std::atomic<std::size_t> importedCount {};
    std::atomic<const fname_type::value_type*> lastFile { "No file imported yet." };

    auto importMonitor = [&] () -> void 
    {
        CMDMonitor monitor(std::cout);
        const auto expSize = fileList.size();
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
    watch.join(); //TODO: check how it will fail if importedCount != fileList.size()

    std::vector<double> times{}; times.reserve( fileList.size() );
    std::cout << "Done pre-fetching .ovf metadata for " << fileList.size() << " files in " << std::chrono::duration<double>(t_after - t_before).count() << " seconds." << "\n";
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
        if(times.size() == fileList.size())   //equivalent to noTSFiles.empty()
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

        const auto totSize = (expDim - (mType == VField::OVFHeader::MeshType::rectangular? 0 : 3)) * expCnt * file_handles.size();
        std::cout << "Found " << totSize << " values to be handled (" << printMemSize( sizeof(float) * totSize ) << " of data in single precision).\n"; 
    }

    //check if files are sorted by timestamp
    if( !std::is_sorted(times.begin(), times.end()) )
    {
        std::cout << "File list received was not ordered by time, sorting it now!\n";
        auto tItBegin = times.begin();
        auto tItEnd   = times.begin();
        auto fItBegin = file_handles.begin();      

        std::vector<sort_helper<double, VField::VFieldFile>> helper; helper.reserve(times.size());
        for(; tItBegin != tItEnd; ++tItBegin)
            helper.emplace_back(&(*tItBegin), &(*fItBegin++));

        //TODO: see if this hack works, and if it can be replaced completely
        //CAUTION: I am surprised this even compiles :D
        std::sort( helper.begin(), helper.end(), [&](const sort_helper<double, VField::VFieldFile>& el1,
                                                     const sort_helper<double, VField::VFieldFile>& el2) { return *std::get<0>(el1) < *std::get<0>(el2); } );
    }

    //work on time array to set some more options
    double trueStep{ (times.back() - times.front())/(times.size() - 1) }; bool reinterp {false};
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

    //and now the work can begin

    return 0;
}

