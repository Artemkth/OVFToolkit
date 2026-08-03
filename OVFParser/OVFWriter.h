#pragma once
//Declaration of file-exporting interfaces for the vector-field files
#include<expected>
#include<fstream>
#include<format>
#include"VField.h"
#include"ovfparser_export.h"

namespace VField
{
    using WriteResult = std::expected<void, std::string>;

    //for outputting contents of VFields
    OVFPARSER_EXPORT WriteResult WriteSegment(std::ostream&, const VField&) noexcept;

    //writing a file with a single field, binary only
    inline OVFPARSER_NO_EXPORT WriteResult WriteOVF(const std::string& fName, const VField& ref) noexcept
    {
        if(!ref.header().contains(OVFParameter::VersionString))
            return std::unexpected("WriteOVF: Version string was not set, aborting!");
        std::ofstream file(fName, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        if(!file.good())
            return std::unexpected(std::format("WriteOVF: Unable to open '{}'!", fName));
        file << ref.header().requireAs<std::string>(OVFParameter::VersionString) << "\n";
        file << "# Segment count: 1" << "\n";
        auto result = WriteSegment(file, ref);
        if(!file.good())
            return std::unexpected(std::format("WriteOVF: Error while writing '{}'.{}",
                fName, result ? "" : std::format(" Segment writer reported:\n{}", result.error())));
        return result;
    }

    //and writing a text file, only a single segment supported!
    //TODO: implement
    OVFPARSER_EXPORT WriteResult WriteTextOVF(const std::string& fName, const VField& ref) noexcept;

    //writing a whole bunch of segments
    template<typename T>
    OVFPARSER_NO_EXPORT inline WriteResult WriteOVF(const std::string& fName, T begin, T end) noexcept
    {
        //begin by determining number of segments
        std::size_t size { 0 };
        for(auto it = begin; it != end; ++it)
            size++;
        if(begin == end)
            return std::unexpected("WriteOVF: No segments were provided!");
        if(!begin->header().contains(OVFParameter::VersionString))
            return std::unexpected("WriteOVF: Version string was not set, aborting!");
        //making sure version strings are the same
        const auto version = begin->header().template requireAs<std::string>(
            OVFParameter::VersionString);
        for(auto it = begin; it != end; it++)
            it->header().set(OVFParameter::VersionString, version);
        std::ofstream file(fName, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        if(!file.good())
            return std::unexpected(std::format("WriteOVF: Unable to open '{}'!", fName));
        file << version << "\n";
        file << "# Segment count: "<< size << "\n";
        for(auto it = begin; it != end; ++it)
        {
            if(it != begin)
                file << "\n";
            auto result = WriteSegment(file, *it);
            if(!file.good())
                return std::unexpected(std::format("WriteOVF: Error while writing '{}'.{}",
                    fName, result ? "" : std::format(" Segment writer reported:\n{}", result.error())));
            if(!result)
                return result;
        }
        return {};
    }
}
