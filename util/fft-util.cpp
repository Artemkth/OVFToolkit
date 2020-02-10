#include<iostream>
#include<sstream>
#include<filesystem>
#include<stdexcept>
#include<string>
#include<regex>
#include<algorithm>

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

//prefetch metadata, form it into arrays of times and metadata
auto ParseMetadata(const std::vector<fname_type>& fList, const std::string& regex_str, std::atomic<std::size_t>& progCnt)
{
    //compile the regex for getting time stamp as best as we can
    std::regex timeRegex( regex_str, std::regex_constants::ECMAScript | std::regex_constants::optimize );

    //get the hint for how much thread can run on a machine, and if it is non-zero target at most 4 threads
    //otherwise run import single thread
    const auto concHint = std::thread::hardware_concurrency();
    const std::size_t tCount {concHint == 0 ? 1 : std::min<decltype(concHint)>(concHint, 4)};

    //main worker lambda
    auto worker = [&] () -> auto {}; 
}

//TODO: look if windows can deal with UTF here, maybe implement winmain with UTF-16 parameters
//TODO: include link to setargv.obg/wsetargv.obj in the windows build, look at https://docs.microsoft.com/en-us/cpp/c-runtime-library/link-options?view=vs-2019
int main(int argc, char** argv)
{
    std::vector<fname_type> fileList{};
    //first parse command-line options
    try
    {
        boost::program_options::options_description desc("Usage: ovf-batch [options] files...\nOptions");
        //populate options list
        desc.add_options()
            ("help,h", "Produce this help message.")
            ("version,v", "Get this software's version information.")
            ("input-files", boost::program_options::value<std::vector<fname_type>>(&fileList)->multitoken()->required(), "Time sequence of vector fields in .ovf files." );

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

    //list of prefetched data
    return 0;
}

