#include <OVFDictionary.h>
#include <VField.h>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nb = nanobind;

namespace {

using VField::OVFParameter;

[[nodiscard]] std::string normalized(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for(const unsigned char character : text)
        if(std::isalnum(character))
            result += static_cast<char>(std::tolower(character));
    return result;
}

[[nodiscard]] std::string_view pythonName(OVFParameter parameter)
{
    if(parameter == OVFParameter::VersionString)
        return "version";
    if(const auto token = VField::paramToken(parameter, VField::OVFVersion::OVF2))
        return *token;
    throw std::logic_error("OVF service parameters have no Python name");
}

[[nodiscard]] OVFParameter parameterFromString(std::string_view identifier)
{
    const auto wanted = normalized(identifier);
    for(const auto parameter : VField::ParamUniverse)
        if(VField::paramType(parameter) != VField::ParameterType::Other &&
           wanted == normalized(pythonName(parameter)))
            return parameter;
    throw nb::key_error(std::format("unknown OVF header field '{}'", identifier).c_str());
}

[[nodiscard]] OVFParameter parameterFromObject(nb::handle identifier)
{
    if(nb::isinstance<nb::str>(identifier))
        return parameterFromString(nb::cast<std::string>(identifier));
    try
    {
        const auto parameter = nb::cast<OVFParameter>(identifier);
        if(VField::paramType(parameter) == VField::ParameterType::Other)
            throw nb::key_error("OVF service parameters do not store header values");
        return parameter;
    }
    catch(const nb::cast_error&)
    {
        throw nb::type_error("header keys must be strings or OVFParameter values");
    }
}

[[nodiscard]] nb::object headerValue(const VField::OVFHeader& header,
                                     nb::handle identifier)
{
    const auto parameter = parameterFromObject(identifier);
    const auto value = header.lookup(parameter);
    if(!value)
        throw nb::key_error(std::format("OVF header field '{}' is not set",
                                        pythonName(parameter)).c_str());
    return std::visit([](const auto& item) { return nb::cast(item); }, value->get());
}

[[nodiscard]] nb::object pythonValue(const VField::ParameterValue& value)
{
    return std::visit([](const auto& item) { return nb::cast(item); }, value);
}

[[nodiscard]] nb::list headerKeys(const VField::OVFHeader& header)
{
    nb::list result;
    for(const auto parameter : header.keys())
        result.append(nb::cast(parameter));
    return result;
}

[[nodiscard]] nb::list headerValues(const VField::OVFHeader& header)
{
    nb::list result;
    for(const auto value : header.values())
        result.append(pythonValue(value.get()));
    return result;
}

[[nodiscard]] nb::list headerItems(const VField::OVFHeader& header)
{
    nb::list result;
    for(const auto& [parameter, value] : header.items())
        result.append(nb::make_tuple(nb::cast(parameter), pythonValue(value.get())));
    return result;
}

void setHeaderValue(VField::OVFHeader& header, nb::handle identifier,
                    nb::object value)
{
    const auto parameter = parameterFromObject(identifier);
    if(value.is_none())
    {
        header.clear(parameter);
        return;
    }
    try
    {
        switch(VField::paramType(parameter))
        {
          case VField::ParameterType::Unsigned:
            header.set(parameter, nb::cast<std::size_t>(value));
            return;
          case VField::ParameterType::Floating:
            header.set(parameter, nb::cast<double>(value));
            return;
          case VField::ParameterType::String:
            header.set(parameter, nb::cast<std::string>(value));
            return;
          case VField::ParameterType::Mesh:
            if(nb::isinstance<nb::str>(value))
            {
                const auto mesh = normalized(nb::cast<std::string>(value));
                if(mesh == "rectangular")
                    header.setMeshType(VField::MeshType::Rectangular);
                else if(mesh == "irregular")
                    header.setMeshType(VField::MeshType::Irregular);
                else
                    throw nb::value_error("mesh type must be 'rectangular' or 'irregular'");
            }
            else
                header.setMeshType(nb::cast<VField::MeshType>(value));
            return;
          case VField::ParameterType::Other:
            break;
        }
    }
    catch(const nb::cast_error&)
    {
        throw nb::type_error(std::format("wrong value type for OVF header field '{}'",
                                         pythonName(parameter)).c_str());
    }
    throw nb::key_error("OVF service parameters do not store header values");
}

template<typename T>
[[nodiscard]] nb::object dataView(VField::VField& field, nb::handle owner)
{
    std::vector<std::size_t> shape;
    if(field.isGridAddressable())
    {
        const auto& header = field.header();
        shape = {
            header.requireAs<std::size_t>(OVFParameter::Znodes),
            header.requireAs<std::size_t>(OVFParameter::Ynodes),
            header.requireAs<std::size_t>(OVFParameter::Xnodes),
            field.pointDimension()};
    }
    else if(field.isWeaklyAddressable())
        shape = {field.pointCount(), field.pointDimension()};
    else
        shape = {field.scalarCount()};

    nb::ndarray<nb::numpy, T> array{
        field.data<T>(), shape.size(), shape.data(), owner};
    return array.cast();
}

[[nodiscard]] nb::object fieldData(VField::VField& field)
{
    if(!field.isDataPresent())
        return nb::none();
    nb::object owner = nb::cast(&field, nb::rv_policy::reference);
    if(field.stores<float>())
        return dataView<float>(field, owner);
    return dataView<double>(field, owner);
}

void setFieldData(VField::VField& field, nb::object value)
{
    if(value.is_none())
    {
        field.clearData();
        return;
    }
    using Array = nb::ndarray<nb::numpy, nb::c_contig>;
    Array array;
    try
    {
        array = nb::cast<Array>(value);
    }
    catch(const nb::cast_error&)
    {
        throw nb::type_error("data must be a C-contiguous NumPy ndarray or None");
    }
    if(array.dtype() == nb::dtype<float>())
        field.setData(static_cast<const float*>(array.data()), array.size());
    else if(array.dtype() == nb::dtype<double>())
        field.setData(static_cast<const double*>(array.data()), array.size());
    else
        throw nb::type_error("data dtype must be numpy.float32 or numpy.float64");
}

void validateHeader(const VField::OVFHeader& header)
{
    if(const auto result = header.validate(); !result)
        throw nb::value_error(result.error().report.c_str());
}

void validateField(const VField::VField& field)
{
    if(const auto result = field.validate(); !result)
        throw nb::value_error(result.error().report.c_str());
}

} // namespace

NB_MODULE(ovftoolkit, module)
{
    module.doc() = "Opaque Python bindings for OVFToolkit";

    nb::enum_<VField::OVFVersion>(module, "OVFVersion")
        .value("OVF0", VField::OVFVersion::OVF0)
        .value("OVF1", VField::OVFVersion::OVF1)
        .value("OVF2", VField::OVFVersion::OVF2)
        .value("Unknown", VField::OVFVersion::Unknown);
    nb::enum_<VField::MeshType>(module, "MeshType")
        .value("Irregular", VField::MeshType::Irregular)
        .value("Rectangular", VField::MeshType::Rectangular);

    auto parameter = nb::enum_<OVFParameter>(module, "OVFParameter");
    for(const auto value : VField::ParamUniverse)
        if(VField::paramType(value) != VField::ParameterType::Other)
            parameter.value(pythonName(value).data(), value);

    nb::class_<VField::OVFHeader>(module, "OVFHeader")
        .def(nb::init<>())
        .def(nb::init<VField::OVFVersion>())
        .def("__contains__", [](const VField::OVFHeader& header, nb::handle key) {
            try { return header.contains(parameterFromObject(key)); }
            catch(const nb::builtin_exception& error) {
                if(error.type() == nb::exception_type::key_error)
                    return false;
                throw;
            }
        })
        .def("__len__", &VField::OVFHeader::size)
        .def("__iter__", [](const VField::OVFHeader& header) {
            return nb::iter(headerKeys(header));
        })
        .def("__getitem__", &headerValue)
        .def("__setitem__", &setHeaderValue,
            nb::arg("key"), nb::arg("value").none())
        .def("__delitem__", [](VField::OVFHeader& header, nb::handle key) {
            const auto value = parameterFromObject(key);
            if(!header.contains(value))
                throw nb::key_error("OVF header field is not set");
            header.clear(value);
        })
        .def("keys", &headerKeys)
        .def("values", &headerValues)
        .def("items", &headerItems)
        .def("get", [](const VField::OVFHeader& header, nb::handle key,
                       nb::object fallback) {
            const auto parameter = parameterFromObject(key);
            const auto value = header.lookup(parameter);
            return value ? pythonValue(value->get()) : std::move(fallback);
        }, nb::arg("key"), nb::arg("default").none() = nb::none())
        .def("validate", &validateHeader)
        .def_prop_ro("version", &VField::OVFHeader::version)
        .def_prop_ro("point_count", &VField::OVFHeader::pointCount)
        .def_prop_ro("point_dimension", &VField::OVFHeader::pointDimension);

    nb::class_<VField::VField>(module, "VField")
        .def(nb::init<>())
        .def(nb::init<VField::OVFVersion>())
        .def_prop_ro("header",
            static_cast<VField::OVFHeader& (VField::VField::*)() noexcept>(
                &VField::VField::header), nb::rv_policy::reference_internal)
        .def_prop_rw("data", &fieldData, &setFieldData,
            nb::arg("value").none(),
            "Writable zero-copy NumPy view. Assignment replaces C++-owned "
            "storage and invalidates older views.")
        .def_prop_ro("scalar_count", &VField::VField::scalarCount)
        .def_prop_ro("point_count", &VField::VField::pointCount)
        .def_prop_ro("point_dimension", &VField::VField::pointDimension)
        .def("validate", &validateField);
}
