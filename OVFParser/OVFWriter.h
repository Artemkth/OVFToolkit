#pragma once
//Declaration of file-exporting interfaces for the vector-field files
#include<fstream>
#include"VField.h"

//for outputting contents of VFields
std::string WriteSegment(std::ostream&, const VField&) noexcept;

//writing a file with a single field, binary only
std::string WriteOVF(const std::string& fName, const VField& ref) noexcept
{
    if(!ref.isSet(OVFParameter::VersionString))
	return "WriteOVF: Version string was not set, aborting!";
    std::string log{""};
    std::ofstream file(fName, std::ios_base::out | std::ios_base::bin | std::ios_base::trunc);
    if(!file.good())
	return "WriteOVF: Unable to open a file!";
    file << ref.Header.getString(OVFParameter::VersionString) << "\n";
    file << "# Segment count: 1" << "\n";
    log = WriteSegment(file, ref);
    if(!file.good())
	return 
	     (std::string)"WriteOVF: Error while writing out a file! segment writer returned:\n" + log ;
    return log;
}

//and writing a text file, only a single segment supported!
std::string WriteTextOVF(const std::string& fName, const VField& ref) noexcept;

//writing a whole bunch of files
template<typename T>
inline std::string WriteOVF(T begin, T end) noexcept
{
    //begin by determining number of segments
    std::size_t size { 0 };
    for(auto it = begin; it != end; ++it)
	size++;
    if(!begin.isSet(OVFParameter::VersionString))
	return "WriteOVF: Version string was not set, aborting!";
    //TODO: look into checking if version strings are the same
    std::string log{""};
    std::ofstream file(fName, std::ios_base::out | std::ios_base::bin | std::ios_base::trunc);
    if(!file.good())
	return "WriteOVF: Unable to open a file!";
    file << begin -> Header.getString(OVFParameter::VersionString) << "\n";
    file << "# Segment count: "<< size << "\n";
    for(auto it = begin; it != end; ++it)
    {
	log = WriteSegment(file, *it);
        if(!file.good())
	return 
	     (std::string)"WriteOVF: Error while writing out a file! segment writer returned:\n" + log ;
    }
    return log;
}

