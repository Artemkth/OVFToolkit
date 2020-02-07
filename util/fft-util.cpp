#include<iostream>
#include<filesystem>
#include<stdexcept>
#include<string>

//parsing program options
#include<boost/program_options.hpp>

//file i/o handling
#include<OVFParser.h>
#include<OVFWriter.h>

//miscaleneous options from cmake
#include"OVFToolkitConfig.h"

using fname_type = std::string;

//TODO: look if windows can deal with UTF here, maybe implement winmain with UTF-16 parameters
//TODO: include link to setargv.obg/wsetargv.obj in the windows build, look at https://docs.microsoft.com/en-us/cpp/c-runtime-library/link-options?view=vs-2019
int main(int argc, char** argv)
{
    std::vector<fname_type> fileList{};
    //first parse command-line options
    try
    {
        boost::program_options::options_description desc("Usage: ovf-batch [options] file...\nOptions");
        //populate options list
        desc.add_options()
            ("help,h", "Produce this help message.")
            ("version,v", "Get this software's version information.") ;

        boost::program_options::variables_map vmap;
        boost::program_options::store(
                boost::program_options::parse_command_line(argc, argv, desc), vmap);
        boost::program_options::notify(vmap);

        if(vmap.count("help")) 
        {
            std::cout << desc ;
            return 0;
        }
        if(vmap.count("version"))
        {
            std::cout << "OVFToolkit time domain FFT batch processing utility ver. " << OVFTOOLKIT_VERSION_STRING << "\n";
            std::cout << "Copyright (C) 2020 Artem Bondarenko\n" ;
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error while parsing command line: " << e.what() << "\n";
        return -1;
    }
    return 0;
}
