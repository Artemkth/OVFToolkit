// OOMMF vector-field header types and lightweight runtime access.
// Format references:
// https://math.nist.gov/oommf/doc/userguide12b3/userguide/Vector_Field_File_Format_OV.html
// https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html
// https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html
#pragma once

#include <cstddef>
#include <concepts>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ovfparser_export.h"

namespace VField {

    /** @brief Recognised revisions of the OOMMF vector-field file format. */
    enum class OVFVersion {
        /** @see https://math.nist.gov/oommf/doc/userguide20b0/userguide/OVF_0.0_format.html */
        OVF0,
        /** @see https://math.nist.gov/oommf/doc/userguide20b0/userguide/OVF_1.0_format.html */
        OVF1,
        /** @see https://math.nist.gov/oommf/doc/userguide21a1/userguide-xml/sec_ovf20format.html */
        OVF2,
        Unknown
    };

    /** @brief Runtime storage category of an OVF parameter. */
    enum class ParameterType {
        Other,
        Unsigned,
        Floating,
        String,
        Mesh
    };

    /** @brief Mesh organisation described by an OVF header. */
    enum class MeshType {
        Irregular,
        Rectangular
    };

    /** @brief All recognised OVF header and service parameters. */
    enum class OVFParameter {
        VersionString,
        Open, Close,
        Comment,
        Title,
        Segcnt,
        Desc,
        Munit,
        Vunit,
        Vmult,
        Vlabels,
        Vdim,
        Xmin, Ymin, Zmin, Xmax, Ymax, Zmax,
        Bound,
        Vmax, Vmin,
        Mtype,
        Pcount,
        Xbase, Ybase, Zbase,
        Xstep, Ystep, Zstep,
        Xnodes, Ynodes, Znodes,
        Empty,
        Unknown,
        Invalid
    };

    /** @brief Value storage used by the runtime-selected parameter API. */
    using ParameterValue = std::variant<std::size_t, double, std::string, MeshType>;

    /** @brief Reason a header lookup could not return the requested value. */
    enum class HeaderAccessErrorCode {
        MissingParameter,
        WrongType,
        UnsupportedParameter
    };

    /** @brief Non-allocating diagnostic produced by header lookup. */
    struct HeaderAccessError {
        /** Parameter whose access failed. */
        OVFParameter parameter;
        /** Kind of access failure. */
        HeaderAccessErrorCode code;
        /** Requested or dictionary-declared category, when known. */
        std::optional<ParameterType> expected{};
        /** Stored category, when a value of the wrong type was found. */
        std::optional<ParameterType> actual{};
    };

    /**
     * @brief Result containing a borrowed immutable header value.
     * @tparam T Requested value type.
     *
     * The reference remains valid until the same header is mutated or destroyed.
     */
    template<typename T>
    using HeaderValueResult = std::expected<std::reference_wrapper<const T>, HeaderAccessError>;

    /** @brief Diagnostic returned by complete header validation. */
    struct ValidationError {
        /** Human-readable aggregate report. */
        std::string report;
        /** Parameters implicated by the report. */
        std::vector<OVFParameter> parameters;
    };

    /** @brief Successful validation, or a complete validation report. */
    using ValidationResult = std::expected<void, ValidationError>;

    /** @brief C++ value type corresponding to a runtime parameter category. */
    template<ParameterType>
    struct parameter_cpp_type;
    /** @brief Mapping for unsigned-integer parameters. */
    template<> struct parameter_cpp_type<ParameterType::Unsigned> {
        /** Mapped unsigned value type. */ using type = std::size_t;
    };
    /** @brief Mapping for floating-point parameters. */
    template<> struct parameter_cpp_type<ParameterType::Floating> {
        /** Mapped floating value type. */ using type = double;
    };
    /** @brief Mapping for text parameters. */
    template<> struct parameter_cpp_type<ParameterType::String> {
        /** Mapped text value type. */ using type = std::string;
    };
    /** @brief Mapping for mesh-organisation parameters. */
    template<> struct parameter_cpp_type<ParameterType::Mesh> {
        /** Mapped mesh value type. */ using type = MeshType;
    };

    /** @brief C++ type corresponding to @p Type. */
    template<ParameterType Type>
    using parameter_cpp_type_t = typename parameter_cpp_type<Type>::type;

    /** @brief Dictionary-provided traits for @p Parameter. */
    template<OVFParameter Parameter>
    struct parameter_traits;

    /** @brief Dictionary-derived C++ value type for @p Parameter. */
    template<OVFParameter Parameter>
    using parameter_value_t = typename parameter_traits<Parameter>::value_type;

    /** @brief A type that can be stored directly in ParameterValue. */
    template<typename T>
    concept ParameterValueAlternative =
        std::same_as<std::remove_cvref_t<T>, std::size_t> ||
        std::same_as<std::remove_cvref_t<T>, double> ||
        std::same_as<std::remove_cvref_t<T>, std::string> ||
        std::same_as<std::remove_cvref_t<T>, MeshType>;

    /**
     * @brief Metadata container for one OVF segment.
     *
     * Including this header provides the lightweight runtime API. Include
     * OVFDictionary.h as well to define the parameter-deducing template API.
     */
    class OVFPARSER_EXPORT OVFHeader {
        struct HeaderData;
        std::unique_ptr<HeaderData> data_{};

      public:
        /** @brief Construct an empty OVF 2 header. */
        OVFHeader();
        /**
         * @brief Construct an empty header with the canonical signature for a revision.
         * @param version Format revision; `Unknown` is rejected.
         * @throws std::invalid_argument if @p version is `Unknown`.
         */
        explicit OVFHeader(OVFVersion version);
        /**
         * @brief Construct a header preserving an exact accepted version signature.
         * @param versionSignature OVF signature line, including its leading `#`.
         * @throws std::invalid_argument if the signature is not recognised.
         */
        explicit OVFHeader(std::string_view versionSignature);
        /** @brief Destroy the header and its stored metadata. */
        ~OVFHeader() noexcept;

        /** @brief Deep-copy @p other. */
        OVFHeader(const OVFHeader& other);
        /** @brief Deep-copy @p other into this header. */
        OVFHeader& operator=(const OVFHeader& other);
        /** @brief Move @p other, leaving it equivalent to a default header. */
        OVFHeader(OVFHeader&& other) noexcept;
        /** @brief Move-assign @p other, leaving it equivalent to a default header. */
        OVFHeader& operator=(OVFHeader&& other) noexcept;

        /** @return Whether both headers contain identical metadata and version state. */
        [[nodiscard]] bool operator==(const OVFHeader& other) const noexcept;

        /**
         * @param parameter Parameter to inspect.
         * @return Whether the parameter is storable and currently has a value.
         */
        [[nodiscard]] bool contains(OVFParameter parameter) const noexcept;
        /**
         * @param parameter Parameter to retrieve.
         * @return Borrowed value, or a missing/unsupported-parameter diagnostic.
         */
        [[nodiscard]] HeaderValueResult<ParameterValue>
          lookup(OVFParameter parameter) const noexcept;

        /**
         * @brief Look up a runtime-selected parameter as a requested value type.
         * @tparam T One of the alternatives in ParameterValue.
         * @param parameter Parameter to retrieve.
         * @return Borrowed typed value, or an access diagnostic.
         */
        template<ParameterValueAlternative T>
        [[nodiscard]] HeaderValueResult<std::remove_cvref_t<T>>
          lookupAs(OVFParameter parameter) const noexcept
        {
            using Value = std::remove_cvref_t<T>;
            auto result = lookup(parameter);
            if(!result)
                return std::unexpected(result.error());
            if(const auto* value = std::get_if<Value>(&result->get()))
                return std::cref(*value);
            return std::unexpected(HeaderAccessError{
                parameter,
                HeaderAccessErrorCode::WrongType,
                parameterTypeFor<Value>(),
                parameterTypeOf(result->get())});
        }

        /**
         * @brief Return a required runtime-selected value.
         * @tparam T One of the alternatives in ParameterValue.
         * @param parameter Parameter to retrieve.
         * @return Immutable reference to the stored value.
         * @throws std::bad_expected_access<HeaderAccessError> on failed access.
         */
        template<ParameterValueAlternative T>
        [[nodiscard]] const std::remove_cvref_t<T>&
          requireAs(OVFParameter parameter) const
        { return lookupAs<T>(parameter).value().get(); }

        /**
         * @brief Set a runtime-selected parameter after validating its category.
         * @param parameter Parameter to modify.
         * @param value New value. An unsigned value is promoted for floating parameters.
         * @throws std::invalid_argument for unsupported parameters or wrong value types.
         */
        void set(OVFParameter parameter, ParameterValue value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, std::string_view value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, const char* value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, const std::string& value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, std::string&& value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, std::size_t value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, double value);
        /** @copydoc set(OVFParameter,ParameterValue) */
        void set(OVFParameter parameter, MeshType value);
        /** @brief Remove @p parameter if it is stored. */
        void clear(OVFParameter parameter) noexcept;

        /** @return Parsed revision of the stored version signature. */
        [[nodiscard]] OVFVersion version() const noexcept;
        /** @brief Replace the version signature with the canonical signature for @p version. */
        void setVersion(OVFVersion version);

        /** @return Mesh organisation, or `nullopt` when it has not been set. */
        [[nodiscard]] std::optional<MeshType> meshType() const noexcept;
        /** @brief Set the mesh organisation. */
        void setMeshType(MeshType type);
        /** @brief Remove the mesh organisation. */
        void clearMeshType() noexcept;

        /** @brief Clear all metadata and install the canonical signature for @p version. */
        void reset(OVFVersion version = OVFVersion::OVF2);
        /** @return Success, or a freshly generated complete diagnostic report. */
        [[nodiscard]] ValidationResult validate() const;

        /**
         * @return Number of mesh points, or `nullopt` for incomplete, invalid, or
         * overflowing metadata.
         */
        [[nodiscard]] std::optional<std::size_t> pointCount() const noexcept;
        /** @return Vector dimension, or `nullopt` when it is absent or zero. */
        [[nodiscard]] std::optional<std::size_t> pointDimension() const noexcept;

        /** @tparam Parameter Dictionary-selected parameter to inspect. */
        template<OVFParameter Parameter>
        [[nodiscard]] bool contains() const noexcept
        { return contains(Parameter); }

        /**
         * @tparam Parameter Dictionary-selected parameter to retrieve.
         * @return Borrowed correctly typed value, or an access diagnostic.
         */
        template<OVFParameter Parameter>
        [[nodiscard]] HeaderValueResult<parameter_value_t<Parameter>> lookup() const noexcept;

        /**
         * @tparam Parameter Dictionary-selected parameter to modify.
         * @param value New value of the dictionary-derived type.
         */
        template<OVFParameter Parameter>
        void set(parameter_value_t<Parameter> value);

        /** @tparam Parameter Dictionary-selected parameter to remove. */
        template<OVFParameter Parameter>
        void clear() noexcept
        { clear(Parameter); }

      private:
        template<ParameterValueAlternative T>
        static consteval ParameterType parameterTypeFor()
        {
            using Value = std::remove_cvref_t<T>;
            if constexpr(std::same_as<Value, std::size_t>) return ParameterType::Unsigned;
            if constexpr(std::same_as<Value, double>) return ParameterType::Floating;
            if constexpr(std::same_as<Value, std::string>) return ParameterType::String;
            return ParameterType::Mesh;
        }

        static ParameterType parameterTypeOf(const ParameterValue& value) noexcept
        {
            return std::visit([]<typename T>(const T&) {
                return parameterTypeFor<T>();
            }, value);
        }
    };

} // namespace VField
