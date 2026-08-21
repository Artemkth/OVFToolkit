#pragma once

// Public file-writing interfaces for OOMMF vector-field files.
#include "VField.h"
#include "ovfparser_export.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

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

    class OVFStreamWriter;

    /** @brief Sequential point sink for one prepared binary OVF segment. */
    class OVFPARSER_EXPORT OVFSegmentSink {
        friend class OVFStreamWriter;
      public:
        struct State;

      private:
        State* state_{};
        std::size_t segment_{};
        std::optional<WriteError> error_{};

        OVFSegmentSink(State* state, std::size_t segment) noexcept;
        WriteResult writeContiguous(std::span<const float> values,
                                    std::size_t points,
                                    std::size_t dimension);
        WriteResult writeContiguous(std::span<const double> values,
                                    std::size_t points,
                                    std::size_t dimension);

      public:
        OVFSegmentSink() = default;
        OVFSegmentSink(const OVFSegmentSink&) = delete;
        OVFSegmentSink& operator=(const OVFSegmentSink&) = delete;
        OVFSegmentSink(OVFSegmentSink&&) noexcept = default;
        OVFSegmentSink& operator=(OVFSegmentSink&&) noexcept = default;

        /** @brief Append a contiguous, point-major rank-two view. */
        template<class Element, class Extents>
          requires (Extents::rank() == 2) &&
            (std::same_as<std::remove_cv_t<Element>, float> ||
             std::same_as<std::remove_cv_t<Element>, double>)
        WriteResult write(md::mdspan<Element, Extents, md::layout_right,
                          md::default_accessor<Element>> points)
        {
            const auto count = static_cast<std::size_t>(points.extent(0));
            const auto dimension = static_cast<std::size_t>(points.extent(1));
            const auto scalarCount = count * dimension;
            using Scalar = std::remove_cv_t<Element>;
            return writeContiguous(
                std::span<const Scalar>{points.data_handle(), scalarCount},
                count, dimension);
        }

        /** @brief Stream a point-major view and retain the first error. */
        template<class Element, class Extents>
          requires (Extents::rank() == 2) &&
            (std::same_as<std::remove_cv_t<Element>, float> ||
             std::same_as<std::remove_cv_t<Element>, double>)
        OVFSegmentSink& operator<<(
            md::mdspan<Element, Extents, md::layout_right,
                       md::default_accessor<Element>> points)
        {
            if(!error_)
                if(auto result = write(points); !result)
                    error_ = std::move(result.error());
            return *this;
        }

        [[nodiscard]] std::size_t pointsWritten() const noexcept;
        [[nodiscard]] std::size_t pointsRemaining() const noexcept;
        [[nodiscard]] bool complete() const noexcept;
        [[nodiscard]] bool good() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept { return good(); }
        [[nodiscard]] const std::optional<WriteError>& error() const noexcept
        { return error_; }
    };

    /**
     * @brief Header-first writer whose segment sinks accept points independently.
     *
     * Segment payload regions are reserved without writing placeholder arrays.
     * Every sink remains sequential even though the underlying file is updated
     * with positioned writes.
     */
    class OVFPARSER_EXPORT OVFStreamWriter {
        std::unique_ptr<OVFSegmentSink::State> state_{};
        std::vector<OVFSegmentSink> sinks_{};

        OVFStreamWriter(std::unique_ptr<OVFSegmentSink::State> state,
                        std::size_t segments);

      public:
        OVFStreamWriter() = default;
        ~OVFStreamWriter();
        OVFStreamWriter(const OVFStreamWriter&) = delete;
        OVFStreamWriter& operator=(const OVFStreamWriter&) = delete;
        OVFStreamWriter(OVFStreamWriter&&) noexcept;
        OVFStreamWriter& operator=(OVFStreamWriter&&) noexcept;

        /** @brief Prepare all binary segments and return their sequential sinks. */
        [[nodiscard]] static std::expected<OVFStreamWriter, WriteError>
          create(const std::filesystem::path& path,
                 std::span<const OVFHeader> headers,
                 std::size_t scalarSizeBytes = sizeof(float));

        [[nodiscard]] std::size_t segmentCount() const noexcept;
        [[nodiscard]] OVFSegmentSink& segment(std::size_t index);
        [[nodiscard]] const OVFSegmentSink& segment(std::size_t index) const;

        /** @brief Flush and validate that every declared point was written. */
        WriteResult finalize();
        /** @brief Close an incomplete writer without final validation. */
        void abort() noexcept;
    };

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
