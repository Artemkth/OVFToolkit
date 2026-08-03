#include "OVFHeader.h"

#include <array>
#include <limits>
#include <stdexcept>

#include "OVFDictionary.h"

namespace VField {
    namespace {
        constexpr std::size_t parameterCount =
            static_cast<std::size_t>(OVFParameter::Invalid) + 1;

        constexpr bool isDeclaredParameter(OVFParameter parameter) noexcept
        {
            return static_cast<std::size_t>(parameter) < parameterCount;
        }

        constexpr std::size_t parameterIndex(OVFParameter parameter) noexcept
        { return static_cast<std::size_t>(parameter); }

        HeaderAccessError missingParameter(
            OVFParameter parameter, ParameterType expected) noexcept
        {
            return {parameter, HeaderAccessErrorCode::MissingParameter, expected};
        }

        HeaderAccessError unsupportedParameter(OVFParameter parameter) noexcept
        {
            return {parameter, HeaderAccessErrorCode::UnsupportedParameter};
        }
    }

    struct OVFHeader::HeaderData {
        std::array<std::optional<ParameterValue>, parameterCount> values{};
        OVFVersion parsedVersion{OVFVersion::Unknown};
    };

    OVFHeader::OVFHeader(): OVFHeader(OVFVersion::OVF2) {}

    OVFHeader::OVFHeader(OVFVersion revision): data_(std::make_unique<HeaderData>())
    { setVersion(revision); }

    OVFHeader::OVFHeader(std::string_view signature):
      data_(std::make_unique<HeaderData>())
    { set(OVFParameter::VersionString, signature); }

    OVFHeader::~OVFHeader() noexcept = default;
    OVFHeader::OVFHeader(OVFHeader&&) noexcept = default;
    OVFHeader& OVFHeader::operator=(OVFHeader&&) noexcept = default;

    OVFHeader::OVFHeader(const OVFHeader& other):
      data_(std::make_unique<HeaderData>(*other.data_)) {}

    OVFHeader& OVFHeader::operator=(const OVFHeader& other)
    {
        if(this != &other)
            *data_ = *other.data_;
        return *this;
    }

    bool OVFHeader::operator==(const OVFHeader& other) const noexcept
    { return data_->values == other.data_->values; }

    bool OVFHeader::contains(OVFParameter parameter) const noexcept
    {
        return isDeclaredParameter(parameter) &&
            data_->values[parameterIndex(parameter)].has_value();
    }

    HeaderValueResult<ParameterValue>
      OVFHeader::lookup(OVFParameter parameter) const noexcept
    {
        if(!isDeclaredParameter(parameter) ||
           parameterDescriptor(parameter).type == ParameterType::Other)
            return std::unexpected(unsupportedParameter(parameter));
        if(!contains(parameter))
            return std::unexpected(missingParameter(
                parameter, parameterDescriptor(parameter).type));
        return std::cref(*data_->values[parameterIndex(parameter)]);
    }

    void OVFHeader::set(OVFParameter parameter, ParameterValue value)
    {
        if(!isDeclaredParameter(parameter))
            throw std::invalid_argument("Cannot set an undeclared OVF parameter");

        const auto expected = parameterDescriptor(parameter).type;
        if(expected == ParameterType::Other)
            throw std::invalid_argument("Cannot assign a value to an OVF service parameter");

        const auto actual = parameterTypeOf(value);
        if(expected != actual)
            throw std::invalid_argument("OVF parameter value has the wrong runtime type");

        if(parameter == OVFParameter::VersionString)
            data_->parsedVersion = matchVersionString(std::get<std::string>(value));
        data_->values[parameterIndex(parameter)] = std::move(value);
    }

    void OVFHeader::set(OVFParameter parameter, std::string_view value)
    { set(parameter, ParameterValue{std::in_place_type<std::string>, value}); }

    void OVFHeader::set(OVFParameter parameter, const char* value)
    { set(parameter, std::string_view{value}); }

    void OVFHeader::set(OVFParameter parameter, const std::string& value)
    { set(parameter, ParameterValue{value}); }

    void OVFHeader::set(OVFParameter parameter, std::string&& value)
    { set(parameter, ParameterValue{std::move(value)}); }

    void OVFHeader::set(OVFParameter parameter, std::size_t value)
    {
        if(isDeclaredParameter(parameter) &&
           parameterDescriptor(parameter).type == ParameterType::Floating)
            set(parameter, ParameterValue{static_cast<double>(value)});
        else
            set(parameter, ParameterValue{value});
    }

    void OVFHeader::set(OVFParameter parameter, double value)
    { set(parameter, ParameterValue{value}); }

    void OVFHeader::set(OVFParameter parameter, MeshType value)
    { set(parameter, ParameterValue{value}); }

    void OVFHeader::clear(OVFParameter parameter) noexcept
    {
        if(!isDeclaredParameter(parameter))
            return;
        data_->values[parameterIndex(parameter)].reset();
        if(parameter == OVFParameter::VersionString)
            data_->parsedVersion = OVFVersion::Unknown;
    }

    OVFVersion OVFHeader::version() const noexcept
    { return data_->parsedVersion; }

    void OVFHeader::setVersion(OVFVersion revision)
    { set(OVFParameter::VersionString, canonicalVersionString(revision)); }

    std::optional<MeshType> OVFHeader::meshType() const noexcept
    {
        auto result = lookupAs<MeshType>(OVFParameter::Mtype);
        if(!result)
            return std::nullopt;
        return result->get();
    }

    void OVFHeader::setMeshType(MeshType type)
    { set(OVFParameter::Mtype, type); }

    void OVFHeader::clearMeshType() noexcept
    { clear(OVFParameter::Mtype); }

    void OVFHeader::reset(OVFVersion revision)
    {
        data_->values.fill(std::nullopt);
        data_->parsedVersion = OVFVersion::Unknown;
        setVersion(revision);
    }

    extern ValidationResult ValidateHeader(const OVFHeader& header);
    ValidationResult OVFHeader::validate() const
    { return ValidateHeader(*this); }

    std::optional<std::size_t> OVFHeader::pointDimension() const noexcept
    {
        const auto mesh = meshType();
        if(version() == OVFVersion::Unknown || !mesh)
            return std::nullopt;
        if(version() != OVFVersion::OVF2)
            return *mesh == MeshType::Rectangular ? 3 : 6;

        auto dimension = lookupAs<std::size_t>(OVFParameter::Vdim);
        if(!dimension || dimension->get() == 0)
            return std::nullopt;
        if(*mesh == MeshType::Rectangular)
            return dimension->get();
        if(dimension->get() > std::numeric_limits<std::size_t>::max() - 3)
            return std::nullopt;
        return dimension->get() + 3;
    }

    std::optional<std::size_t> OVFHeader::pointCount() const noexcept
    {
        const auto mesh = meshType();
        if(version() == OVFVersion::Unknown || !mesh)
            return std::nullopt;
        if(*mesh == MeshType::Irregular) {
            auto count = lookupAs<std::size_t>(OVFParameter::Pcount);
            if(!count || count->get() == 0)
                return std::nullopt;
            return count->get();
        }

        auto x = lookupAs<std::size_t>(OVFParameter::Xnodes);
        auto y = lookupAs<std::size_t>(OVFParameter::Ynodes);
        auto z = lookupAs<std::size_t>(OVFParameter::Znodes);
        if(!x || !y || !z || x->get() == 0 || y->get() == 0 || z->get() == 0)
            return std::nullopt;
        constexpr auto maximum = std::numeric_limits<std::size_t>::max();
        if(x->get() > maximum / y->get() ||
           x->get() * y->get() > maximum / z->get())
            return std::nullopt;
        return x->get() * y->get() * z->get();
    }

} // namespace VField
