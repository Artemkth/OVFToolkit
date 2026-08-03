#pragma once

// Public file-writing interfaces for OOMMF vector-field files.
#include "VField.h"
#include "ovfparser_export.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>

namespace VField {

    /** @brief Broad category of an OVF writing failure. */
    enum class WriteErrorCode {
        EmptyInput,
        InvalidHeader,
        IncompatibleVersions,
        UnsupportedVersion,
        StreamFailure,
        InvalidField
    };

    /** @brief Structured diagnostic returned by writing operations. */
    struct WriteError {
        /** Failure category. */
        WriteErrorCode code;
        /** Human-readable diagnostic. */
        std::string message;
        /** Destination path, when writing a file. */
        std::filesystem::path path{};
        /** Segment involved in the failure, when applicable. */
        std::optional<std::size_t> segment{};
    };

    /** @brief Successful write, or a structured writing failure. */
    using WriteResult = std::expected<void, WriteError>;

    /** @brief Serialize one complete OVF segment to @p output. */
    OVFPARSER_EXPORT WriteResult writeSegment(std::ostream& output,
                                              const VField& field);

    /** @brief Write one field as a single-segment OVF file. */
    OVFPARSER_EXPORT WriteResult writeOVF(const std::filesystem::path& path,
                                          const VField& field);

    /**
     * @brief Write a forward range of fields without modifying their headers.
     *
     * Every segment must already declare the same OVF version as the first.
     */
    template<std::ranges::forward_range Range>
      requires std::same_as<
          std::remove_cvref_t<std::ranges::range_reference_t<Range>>, VField>
    WriteResult writeOVF(const std::filesystem::path& path, Range&& fields)
    {
        auto begin = std::ranges::begin(fields);
        const auto end = std::ranges::end(fields);
        if(begin == end)
            return std::unexpected(WriteError{
                WriteErrorCode::EmptyInput, "No segments were provided", path});

        const auto& firstHeader = (*begin).header();
        if(!firstHeader.contains(OVFParameter::VersionString))
            return std::unexpected(WriteError{
                WriteErrorCode::InvalidHeader,
                "The first segment has no OVF version signature", path, 0});
        const auto version = firstHeader.version();

        std::size_t count{};
        for(const auto& field : fields)
        {
            if(!field.header().contains(OVFParameter::VersionString) ||
               field.header().version() != version)
                return std::unexpected(WriteError{
                    WriteErrorCode::IncompatibleVersions,
                    "All segments must declare the same OVF version", path, count});
            ++count;
        }

        std::ofstream output(path, std::ios_base::out | std::ios_base::binary |
                                   std::ios_base::trunc);
        if(!output.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "Unable to open output file", path});

        output << firstHeader.template requireAs<std::string>(OVFParameter::VersionString)
               << "\n# Segment count: " << count << '\n';
        std::size_t segment{};
        for(const auto& field : fields)
        {
            if(segment != 0) output << '\n';
            if(auto result = writeSegment(output, field); !result)
            {
                auto failure = std::move(result.error());
                failure.path = path;
                failure.segment = segment;
                return std::unexpected(std::move(failure));
            }
            ++segment;
        }
        if(!output.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "Failed while writing output file", path});
        return {};
    }

} // namespace VField
