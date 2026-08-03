#pragma once

// Public file-reading interfaces for OOMMF vector-field files.
#include "VField.h"
#include "ovfparser_export.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VField {

    /** @brief Select whether field arrays are loaded while parsing the file. */
    enum class DataLoading {
        Lazy,
        Eager
    };

    /** @brief Broad category of a file-reading failure. */
    enum class ReadErrorCode {
        OpenFailed,
        InvalidFormat,
        StreamFailure,
        InvalidSegment,
        DataUnavailable
    };

    /** @brief Structured diagnostic returned by VFieldFile operations. */
    struct ReadError {
        /** Failure category. */
        ReadErrorCode code;
        /** Human-readable diagnostic. */
        std::string message;
        /** Source path involved in the operation. */
        std::filesystem::path path{};
        /** Segment involved in the failure, when applicable. */
        std::optional<std::size_t> segment{};
    };

    /** @brief Successful value of @p T, or a structured reading failure. */
    template<typename T>
    using ReadResult = std::expected<T, ReadError>;

    /** @brief Parsed OVF file with optional lazy loading of segment data. */
    class OVFPARSER_EXPORT VFieldFile {
        struct FileData;

        std::filesystem::path path_{};
        std::unique_ptr<FileData> data_{};

        [[nodiscard]] ReadResult<std::reference_wrapper<VField>>
          fetch(std::size_t index);
        void logMessage(const std::string& message);
        [[nodiscard]] ReadError error(ReadErrorCode code,
                                      std::string message,
                                      std::optional<std::size_t> segment = {}) const;

      public:
        /** @brief Construct an empty file handle. */
        VFieldFile();
        /** @brief Destroy the handle and any loaded segment data. */
        ~VFieldFile();
        /** @brief Deep-copy parsed metadata and loaded data. */
        VFieldFile(const VFieldFile&);
        /** @brief Deep-copy parsed metadata and loaded data. */
        VFieldFile& operator=(const VFieldFile&);
        /** @brief Transfer the parsed file state. */
        VFieldFile(VFieldFile&&) noexcept;
        /** @brief Transfer the parsed file state. */
        VFieldFile& operator=(VFieldFile&&) noexcept;

        /** @brief Parse @p path into a new file handle. */
        [[nodiscard]] static ReadResult<VFieldFile>
          open(const std::filesystem::path& path,
               DataLoading loading = DataLoading::Lazy);

        /** @brief Replace this handle by parsing @p path from its beginning. */
        [[nodiscard]] ReadResult<void>
          read(const std::filesystem::path& path,
               DataLoading loading = DataLoading::Lazy);

        /** @return Path supplied to the most recent read operation. */
        [[nodiscard]] const std::filesystem::path& path() const noexcept;
        /** @return Number of parsed segments. */
        [[nodiscard]] std::size_t segmentCount() const noexcept;

        /** @brief Access a segment header without loading its field data. */
        [[nodiscard]] ReadResult<std::reference_wrapper<const OVFHeader>>
          header(std::size_t index) const;
        /** @brief Load and borrow a segment. */
        [[nodiscard]] ReadResult<std::reference_wrapper<VField>>
          load(std::size_t index);
        /** @brief Load a segment into an independent value. */
        [[nodiscard]] ReadResult<VField> copy(std::size_t index) const;

        /** @return Whether the segment currently owns an in-memory data array. */
        [[nodiscard]] bool dataLoaded(std::size_t index) const noexcept;
        /** @return Whether data is loaded or can be loaded from the source file. */
        [[nodiscard]] bool dataAvailable(std::size_t index) const noexcept;
        /** @brief Release loaded data while retaining lazy-loading metadata. */
        bool unload(std::size_t index) noexcept;

        /** @brief Load every segment and return contiguous mutable access. */
        [[nodiscard]] ReadResult<std::span<VField>> fields();
        /** @brief Copy every segment after loading its data. */
        [[nodiscard]] ReadResult<std::vector<VField>> fieldsCopy() const;

        /** @brief Read a contiguous half-open range of points from one segment. */
        [[nodiscard]] ReadResult<VField>
          readSlice(std::size_t segment, std::size_t firstPoint,
                    std::size_t pointCount) const;
    };

} // namespace VField
