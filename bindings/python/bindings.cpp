#include <OVFDictionary.h>
#include <OVFParser.h>
#include <OVFWriter.h>
#include <VField.h>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstddef>
#include <format>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nb = nanobind;

namespace {

using VField::OVFParameter;

[[nodiscard]] std::string_view pythonName(OVFParameter parameter);

class SegmentHeader {
    std::variant<VField::OVFHeader, VField::VField*> storage_;

  public:
    SegmentHeader(): storage_(std::in_place_type<VField::OVFHeader>) {}
    explicit SegmentHeader(VField::OVFVersion version):
      storage_(std::in_place_type<VField::OVFHeader>, version) {}
    explicit SegmentHeader(const VField::OVFHeader& header): storage_(header) {}
    explicit SegmentHeader(VField::VField& field): storage_(&field) {}

    [[nodiscard]] bool attached() const noexcept
    { return std::holds_alternative<VField::VField*>(storage_); }

    [[nodiscard]] VField::OVFHeader& header() noexcept
    {
        if(auto* owned = std::get_if<VField::OVFHeader>(&storage_))
            return *owned;
        return std::get<VField::VField*>(storage_)->header();
    }

    [[nodiscard]] const VField::OVFHeader& header() const noexcept
    {
        if(const auto* owned = std::get_if<VField::OVFHeader>(&storage_))
            return *owned;
        return std::get<VField::VField*>(storage_)->header();
    }
};

[[nodiscard]] std::filesystem::path pythonPath(nb::handle value)
{
    nb::object converted = nb::module_::import_("os").attr("fspath")(value);
    if(!nb::isinstance<nb::str>(converted))
        throw nb::type_error("filesystem paths must resolve to str, not bytes");
    const auto text = nb::cast<std::string>(converted);
    return std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

[[noreturn]] void throwOSError(const std::string& message)
{
    PyErr_SetString(PyExc_OSError, message.c_str());
    throw nb::python_error();
}

[[noreturn]] void throwReadError(const VField::ReadError& error)
{
    switch(error.code)
    {
      case VField::ReadErrorCode::OpenFailed:
      case VField::ReadErrorCode::StreamFailure:
        throwOSError(error.message);
      case VField::ReadErrorCode::InvalidSegment:
        throw nb::index_error(error.message.c_str());
      case VField::ReadErrorCode::InvalidFormat:
        throw nb::value_error(error.message.c_str());
      case VField::ReadErrorCode::DataUnavailable:
        throw std::runtime_error(error.message);
    }
    std::unreachable();
}

[[noreturn]] void throwWriteError(const VField::WriteError& error)
{
    if(error.code == VField::WriteErrorCode::StreamFailure)
        throwOSError(error.message);
    throw nb::value_error(error.message.c_str());
}

class PythonReader {
    std::optional<VField::VFieldFile> file_;

    [[nodiscard]] VField::VFieldFile& openFile()
    {
        if(!file_)
            throw nb::value_error("I/O operation on closed OVF reader");
        return *file_;
    }

    [[nodiscard]] const VField::VFieldFile& openFile() const
    {
        if(!file_)
            throw nb::value_error("I/O operation on closed OVF reader");
        return *file_;
    }

    [[nodiscard]] std::size_t index(std::ptrdiff_t value) const
    {
        const auto count = openFile().segmentCount();
        if(value < 0)
            value += static_cast<std::ptrdiff_t>(count);
        if(value < 0 || static_cast<std::size_t>(value) >= count)
            throw nb::index_error("OVF segment index is out of range");
        return static_cast<std::size_t>(value);
    }

  public:
    PythonReader(nb::handle path, bool eager)
    {
        auto opened = VField::VFieldFile::open(
            pythonPath(path), eager ? VField::DataLoading::Eager
                                    : VField::DataLoading::Lazy);
        if(!opened)
            throwReadError(opened.error());
        file_.emplace(std::move(*opened));
    }

    [[nodiscard]] bool closed() const noexcept { return !file_; }
    [[nodiscard]] std::size_t size() const { return openFile().segmentCount(); }

    [[nodiscard]] VField::VField read(std::ptrdiff_t segment) const
    {
        auto result = openFile().copy(index(segment));
        if(!result)
            throwReadError(result.error());
        return std::move(*result);
    }

    [[nodiscard]] SegmentHeader header(std::ptrdiff_t segment) const
    {
        auto result = openFile().header(index(segment));
        if(!result)
            throwReadError(result.error());
        return SegmentHeader{result->get()};
    }

    void close() noexcept { file_.reset(); }
};

class PythonReaderIterator {
    PythonReader* reader_;
    std::size_t index_{};

  public:
    explicit PythonReaderIterator(PythonReader& reader) noexcept
      : reader_(&reader) {}

    [[nodiscard]] PythonReaderIterator& iter() noexcept { return *this; }

    [[nodiscard]] VField::VField next()
    {
        if(index_ >= reader_->size())
            throw nb::stop_iteration();
        return reader_->read(static_cast<std::ptrdiff_t>(index_++));
    }
};

class PythonWriter {
    std::filesystem::path path_;
    std::size_t expected_;
    std::size_t written_{};
    std::size_t deductionIterations_;
    std::unique_ptr<std::ofstream> output_;
    std::optional<VField::OVFVersion> version_;
    std::string deductionReport_;
    bool closed_{};

    void ensureOpen() const
    {
        if(closed_)
            throw nb::value_error("I/O operation on closed OVF writer");
    }

  public:
    PythonWriter(nb::handle path, std::size_t segments,
                 std::size_t deductionIterations)
      : path_(pythonPath(path)), expected_(segments),
        deductionIterations_(deductionIterations)
    {
        if(expected_ == 0)
            throw nb::value_error("segments must be greater than zero");
    }

    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] std::size_t written() const noexcept { return written_; }
    [[nodiscard]] const std::string& deductionReport() const noexcept
    { return deductionReport_; }

    void write(const VField::VField& source)
    {
        ensureOpen();
        if(written_ >= expected_)
            throw nb::value_error("more OVF segments were written than declared");

        VField::VField field{source};
        deductionReport_ = field.deduceRecursively(deductionIterations_);
        if(const auto validation = field.validate(); !validation)
            throw nb::value_error(std::format(
                "header deduction did not produce a valid field:{}\n{}",
                deductionReport_, validation.error().report).c_str());

        if(!output_)
        {
            version_ = field.header().version();
            output_ = std::make_unique<std::ofstream>(
                path_, std::ios_base::out | std::ios_base::binary |
                       std::ios_base::trunc);
            if(!output_->good())
                throwOSError("unable to open OVF output file");
            *output_ << field.header().requireAs<std::string>(OVFParameter::VersionString)
                     << "\n# Segment count: " << expected_ << '\n';
        }
        else if(field.header().version() != *version_)
            throw nb::value_error("all OVF segments must use the same version");

        if(written_ != 0)
            *output_ << '\n';
        if(auto result = VField::writeSegment(*output_, field); !result)
            throwWriteError(result.error());
        ++written_;
    }

    void close()
    {
        if(closed_)
            return;
        closed_ = true;
        if(written_ != expected_)
        {
            output_.reset();
            throw nb::value_error(std::format(
                "writer expected {} segments but received {}", expected_, written_).c_str());
        }
        if(output_)
        {
            output_->flush();
            if(!output_->good())
            {
                output_.reset();
                throwOSError("failed while writing OVF output file");
            }
            output_.reset();
        }
    }

    void abort() noexcept
    {
        closed_ = true;
        output_.reset();
    }
};

[[nodiscard]] constexpr bool isShapeDerived(OVFParameter parameter) noexcept
{
    return parameter == OVFParameter::Mtype ||
           parameter == OVFParameter::Pcount ||
           parameter == OVFParameter::Vdim ||
           parameter == OVFParameter::Xnodes ||
           parameter == OVFParameter::Ynodes ||
           parameter == OVFParameter::Znodes;
}

[[noreturn]] void throwShapeDerived(OVFParameter parameter)
{
    throw nb::attribute_error(std::format(
        "{} is derived from VField.data shape and is read-only",
        pythonName(parameter)).c_str());
}

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

void copyHeaderFrom(SegmentHeader& destination, const SegmentHeader& source)
{
    // Snapshot first so self-copy and two proxies attached to the same field
    // cannot clear source values while the replacement is in progress.
    const VField::OVFHeader sourceHeader{source.header()};
    auto& destinationHeader = destination.header();

    for(const auto parameter : VField::ParamUniverse)
    {
        if(VField::paramType(parameter) == VField::ParameterType::Other ||
           (destination.attached() && isShapeDerived(parameter)))
            continue;

        const auto value = sourceHeader.lookup(parameter);
        if(value)
            destinationHeader.set(parameter, value->get());
        else
            destinationHeader.clear(parameter);
    }
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

template<typename T>
[[nodiscard]] nb::object complexDataView(VField::VField& field,
                                         nb::handle owner)
{
    const auto pointDimension = field.pointDimension();
    if(pointDimension % 2 != 0)
        throw nb::value_error("complex_data requires an even final dimension");

    std::vector<std::size_t> shape;
    if(field.isGridAddressable())
    {
        const auto& header = field.header();
        shape = {
            header.requireAs<std::size_t>(OVFParameter::Znodes),
            header.requireAs<std::size_t>(OVFParameter::Ynodes),
            header.requireAs<std::size_t>(OVFParameter::Xnodes),
            pointDimension / 2};
    }
    else if(field.isWeaklyAddressable())
        shape = {field.pointCount(), pointDimension / 2};
    else
        throw nb::value_error("complex_data requires addressable field data");

    nb::ndarray<nb::numpy, std::complex<T>> array{
        reinterpret_cast<std::complex<T>*>(field.data<T>()),
        shape.size(), shape.data(), owner};
    return array.cast();
}

[[nodiscard]] nb::object fieldComplexData(VField::VField& field)
{
    if(!field.isDataPresent())
        return nb::none();
    nb::object owner = nb::cast(&field, nb::rv_policy::reference);
    if(field.stores<float>())
        return complexDataView<float>(field, owner);
    return complexDataView<double>(field, owner);
}

void setFieldData(VField::VField& field, nb::object value)
{
    if(value.is_none())
    {
        field.clearData();
        auto& header = field.header();
        header.clear<OVFParameter::Mtype>();
        header.clear<OVFParameter::Pcount>();
        header.clear<OVFParameter::Vdim>();
        header.clear<OVFParameter::Xnodes>();
        header.clear<OVFParameter::Ynodes>();
        header.clear<OVFParameter::Znodes>();
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
    if(array.ndim() != 2 && array.ndim() != 4)
        throw nb::value_error("data must have rank 2 (irregular) or rank 4 (rectangular)");
    for(std::size_t dimension{}; dimension < array.ndim(); ++dimension)
        if(array.shape(dimension) == 0)
            throw nb::value_error("data dimensions must be nonzero");
    if(array.ndim() == 2 && array.shape(1) <= 3)
        throw nb::value_error(
            "rank-2 irregular data requires XYZ plus at least one value component");

    if(array.dtype() == nb::dtype<float>())
        field.setData(static_cast<const float*>(array.data()), array.size());
    else if(array.dtype() == nb::dtype<double>())
        field.setData(static_cast<const double*>(array.data()), array.size());
    else
        throw nb::type_error("data dtype must be numpy.float32 or numpy.float64");

    auto& header = field.header();
    if(array.ndim() == 4)
    {
        header.set<OVFParameter::Mtype>(VField::MeshType::Rectangular);
        header.set<OVFParameter::Znodes>(array.shape(0));
        header.set<OVFParameter::Ynodes>(array.shape(1));
        header.set<OVFParameter::Xnodes>(array.shape(2));
        header.set<OVFParameter::Vdim>(array.shape(3));
        header.clear<OVFParameter::Pcount>();
    }
    else
    {
        header.set<OVFParameter::Mtype>(VField::MeshType::Irregular);
        header.set<OVFParameter::Pcount>(array.shape(0));
        header.set<OVFParameter::Vdim>(array.shape(1) - 3);
        header.clear<OVFParameter::Xnodes>();
        header.clear<OVFParameter::Ynodes>();
        header.clear<OVFParameter::Znodes>();
    }
}

using Coordinates = std::array<double, 3>;

struct MeshAxis {
    std::size_t nodes;
    double base;
    double step;
};

[[nodiscard]] MeshAxis meshAxis(
    const VField::OVFHeader& header, OVFParameter nodesParameter,
    OVFParameter baseParameter, OVFParameter stepParameter,
    OVFParameter minimumParameter, OVFParameter maximumParameter,
    std::string_view name)
{
    const auto nodesResult = header.lookupAs<std::size_t>(nodesParameter);
    if(!nodesResult || nodesResult->get() == 0)
        throw nb::value_error(std::format(
            "meshgrid requires a positive {}nodes value", name).c_str());
    const auto nodes = nodesResult->get();

    const auto minimumResult = header.lookupAs<double>(minimumParameter);
    const auto maximumResult = header.lookupAs<double>(maximumParameter);
    const auto stepResult = header.lookupAs<double>(stepParameter);

    double step{};
    if(stepResult)
        step = stepResult->get();
    else if(minimumResult && maximumResult)
        step = (maximumResult->get() - minimumResult->get()) /
               static_cast<double>(nodes);
    else
        throw nb::value_error(std::format(
            "meshgrid requires {}stepsize, or both {}min and {}max",
            name, name, name).c_str());

    if(!std::isfinite(step) || step <= 0.)
        throw nb::value_error(std::format(
            "meshgrid requires a finite, positive {} step size", name).c_str());

    const auto baseResult = header.lookupAs<double>(baseParameter);
    double base{};
    if(baseResult)
        base = baseResult->get();
    else if(minimumResult)
        base = minimumResult->get() + step / 2.;
    else if(maximumResult)
        base = maximumResult->get() -
               step * (static_cast<double>(nodes) - .5);
    else
        throw nb::value_error(std::format(
            "meshgrid requires {}base, {}min, or {}max",
            name, name, name).c_str());

    if(!std::isfinite(base))
        throw nb::value_error(std::format(
            "meshgrid requires a finite {} base coordinate", name).c_str());
    return MeshAxis{nodes, base, step};
}

[[nodiscard]] nb::tuple fieldMeshgrid(const VField::VField& field)
{
    const auto& header = field.header();
    if(header.meshType() != VField::MeshType::Rectangular ||
       !field.isGridAddressable())
        throw nb::value_error(
            "meshgrid requires rectangular rank-4 field data with consistent nodes");

    const auto x = meshAxis(header, OVFParameter::Xnodes,
        OVFParameter::Xbase, OVFParameter::Xstep,
        OVFParameter::Xmin, OVFParameter::Xmax, "x");
    const auto y = meshAxis(header, OVFParameter::Ynodes,
        OVFParameter::Ybase, OVFParameter::Ystep,
        OVFParameter::Ymin, OVFParameter::Ymax, "y");
    const auto z = meshAxis(header, OVFParameter::Znodes,
        OVFParameter::Zbase, OVFParameter::Zstep,
        OVFParameter::Zmin, OVFParameter::Zmax, "z");

    const auto numpy = nb::module_::import_("numpy");
    const auto coordinateAxis = [&](const MeshAxis& axis) {
        return numpy.attr("add")(
            numpy.attr("multiply")(numpy.attr("arange")(axis.nodes), axis.step),
            axis.base);
    };

    const auto xCoordinates = coordinateAxis(x);
    const auto yCoordinates = coordinateAxis(y);
    const auto zCoordinates = coordinateAxis(z);
    const nb::tuple grids = nb::cast<nb::tuple>(numpy.attr("meshgrid")(
        zCoordinates, yCoordinates, xCoordinates, nb::arg("indexing") = "ij"));
    return nb::make_tuple(grids[2], grids[1], grids[0]);
}

void setDummyHeader(VField::VField& field, const Coordinates& cellSize,
                    std::optional<Coordinates> origin)
{
    if(!field.isDataPresent() ||
       field.header().meshType() != VField::MeshType::Rectangular ||
       !field.isGridAddressable())
        throw nb::value_error(
            "dummy_header requires rectangular rank-4 data to be assigned first");

    for(const auto value : cellSize)
        if(!std::isfinite(value) || value <= 0.)
            throw nb::value_error("cell_size values must be finite and greater than zero");

    const Coordinates gridOrigin = origin.value_or(Coordinates{
        cellSize[0] / 2., cellSize[1] / 2., cellSize[2] / 2.});
    for(const auto value : gridOrigin)
        if(!std::isfinite(value))
            throw nb::value_error("origin values must be finite");

    const auto& oldHeader = field.header();
    const auto version = oldHeader.version();
    const auto xnodes = oldHeader.requireAs<std::size_t>(OVFParameter::Xnodes);
    const auto ynodes = oldHeader.requireAs<std::size_t>(OVFParameter::Ynodes);
    const auto znodes = oldHeader.requireAs<std::size_t>(OVFParameter::Znodes);
    const auto valueDimension =
        oldHeader.requireAs<std::size_t>(OVFParameter::Vdim);

    auto& header = field.header();
    header = VField::OVFHeader{version};
    header.setMeshType(VField::MeshType::Rectangular);
    header.set<OVFParameter::Xnodes>(xnodes);
    header.set<OVFParameter::Ynodes>(ynodes);
    header.set<OVFParameter::Znodes>(znodes);
    header.set<OVFParameter::Vdim>(valueDimension);
    header.set<OVFParameter::Xstep>(cellSize[0]);
    header.set<OVFParameter::Ystep>(cellSize[1]);
    header.set<OVFParameter::Zstep>(cellSize[2]);
    header.set<OVFParameter::Xbase>(gridOrigin[0]);
    header.set<OVFParameter::Ybase>(gridOrigin[1]);
    header.set<OVFParameter::Zbase>(gridOrigin[2]);
    field.deduceRecursively(5);

    if(const auto validation = field.validate(); !validation)
        throw nb::value_error(std::format(
            "failed to create a valid dummy header:\n{}",
            validation.error().report).c_str());
}

void setIsotropicDummyHeader(VField::VField& field, double cellSize,
                             std::optional<Coordinates> origin)
{
    setDummyHeader(field, Coordinates{cellSize, cellSize, cellSize}, origin);
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

NB_MODULE(_native, module)
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

    nb::class_<PythonReader>(module, "Reader")
        .def("__enter__", [](PythonReader& reader) -> PythonReader& {
            if(reader.closed())
                throw nb::value_error("cannot enter a closed OVF reader");
            return reader;
        }, nb::rv_policy::reference_internal)
        .def("__exit__", [](PythonReader& reader, nb::object, nb::object,
                            nb::object) {
            reader.close();
            return false;
        }, nb::arg("exception_type").none(), nb::arg("exception").none(),
           nb::arg("traceback").none())
        .def("__len__", &PythonReader::size)
        .def("__iter__", [](PythonReader& reader) {
            return PythonReaderIterator{reader};
        }, nb::keep_alive<0, 1>())
        .def("__getitem__", &PythonReader::read)
        .def("read", &PythonReader::read, nb::arg("segment") = 0)
        .def("header", &PythonReader::header, nb::arg("segment") = 0)
        .def("close", &PythonReader::close)
        .def_prop_ro("closed", &PythonReader::closed);

    nb::class_<PythonReaderIterator>(module, "ReaderIterator")
        .def("__iter__", &PythonReaderIterator::iter,
            nb::rv_policy::reference_internal)
        .def("__next__", &PythonReaderIterator::next);

    nb::class_<PythonWriter>(module, "Writer")
        .def("__enter__", [](PythonWriter& writer) -> PythonWriter& {
            if(writer.closed())
                throw nb::value_error("cannot enter a closed OVF writer");
            return writer;
        }, nb::rv_policy::reference_internal)
        .def("__exit__", [](PythonWriter& writer, nb::object exceptionType,
                            nb::object, nb::object) {
            if(exceptionType.is_none())
                writer.close();
            else
                writer.abort();
            return false;
        }, nb::arg("exception_type").none(), nb::arg("exception").none(),
           nb::arg("traceback").none())
        .def("write", &PythonWriter::write)
        .def("close", &PythonWriter::close)
        .def_prop_ro("closed", &PythonWriter::closed)
        .def_prop_ro("segments_written", &PythonWriter::written)
        .def_prop_ro("deduction_report", &PythonWriter::deductionReport);

    module.def("reader", [](nb::handle path, bool eager) {
        return PythonReader{path, eager};
    }, nb::arg("path"), nb::arg("eager") = false,
       "Open an OVF reader; data loading is lazy by default");
    module.def("writer", [](nb::handle path, std::size_t segments,
                            std::size_t deductionIterations) {
        return PythonWriter{path, segments, deductionIterations};
    }, nb::arg("path"), nb::arg("segments") = 1,
       nb::arg("deduction_iterations") = 5,
       "Create a streaming OVF writer with automatic header deduction");

    auto parameter = nb::enum_<OVFParameter>(module, "OVFParameter");
    for(const auto value : VField::ParamUniverse)
        if(VField::paramType(value) != VField::ParameterType::Other)
            parameter.value(pythonName(value).data(), value);

    nb::class_<SegmentHeader>(module, "SegmentHeader")
        .def(nb::init<>())
        .def(nb::init<VField::OVFVersion>())
        .def("__contains__", [](const SegmentHeader& proxy, nb::handle key) {
            try { return proxy.header().contains(parameterFromObject(key)); }
            catch(const nb::builtin_exception& error) {
                if(error.type() == nb::exception_type::key_error)
                    return false;
                throw;
            }
        })
        .def("__len__", [](const SegmentHeader& proxy) {
            return proxy.header().size();
        })
        .def("__iter__", [](const SegmentHeader& proxy) {
            return nb::iter(headerKeys(proxy.header()));
        })
        .def("__getitem__", [](const SegmentHeader& proxy, nb::handle key) {
            return headerValue(proxy.header(), key);
        })
        .def("__setitem__", [](SegmentHeader& proxy, nb::handle key,
                               nb::object value) {
            const auto parameter = parameterFromObject(key);
            if(proxy.attached() && isShapeDerived(parameter))
                throwShapeDerived(parameter);
            setHeaderValue(proxy.header(), key, std::move(value));
        }, nb::arg("key"), nb::arg("value").none())
        .def("__delitem__", [](SegmentHeader& proxy, nb::handle key) {
            const auto parameter = parameterFromObject(key);
            if(proxy.attached() && isShapeDerived(parameter))
                throwShapeDerived(parameter);
            if(!proxy.header().contains(parameter))
                throw nb::key_error("OVF header field is not set");
            proxy.header().clear(parameter);
        })
        .def("keys", [](const SegmentHeader& proxy) {
            return headerKeys(proxy.header());
        })
        .def("values", [](const SegmentHeader& proxy) {
            return headerValues(proxy.header());
        })
        .def("items", [](const SegmentHeader& proxy) {
            return headerItems(proxy.header());
        })
        .def("copy_from", &copyHeaderFrom, nb::arg("source"),
            "Replace metadata from source. Headers attached to VField retain "
            "their protected data-shape fields.")
        .def("get", [](const SegmentHeader& proxy, nb::handle key,
                       nb::object fallback) {
            const auto parameter = parameterFromObject(key);
            const auto value = proxy.header().lookup(parameter);
            return value ? pythonValue(value->get()) : std::move(fallback);
        }, nb::arg("key"), nb::arg("default").none() = nb::none())
        .def("validate", [](const SegmentHeader& proxy) {
            validateHeader(proxy.header());
        }, "Validate this header or raise ValueError with the report")
        .def_prop_ro("version", [](const SegmentHeader& proxy) {
            return proxy.header().version();
        })
        .def_prop_ro("point_count", [](const SegmentHeader& proxy) {
            return proxy.header().pointCount();
        })
        .def_prop_ro("point_dimension", [](const SegmentHeader& proxy) {
            return proxy.header().pointDimension();
        });

    nb::class_<VField::VField>(module, "VField")
        .def(nb::init<>())
        .def(nb::init<VField::OVFVersion>())
        .def_prop_ro("header", [](VField::VField& field) {
            return SegmentHeader{field};
        }, nb::keep_alive<0, 1>())
        .def_prop_rw("data", &fieldData, &setFieldData,
            nb::arg("value").none(),
            "Writable zero-copy NumPy view. Assignment replaces C++-owned "
            "storage and invalidates older views.")
        .def_prop_ro("complex_data", &fieldComplexData,
            "Writable zero-copy NumPy view pairing adjacent real and imaginary "
            "values. The final data dimension must be even.")
        .def_prop_ro("scalar_count", &VField::VField::scalarCount)
        .def_prop_ro("point_count", &VField::VField::pointCount)
        .def_prop_ro("point_dimension", &VField::VField::pointDimension)
        .def("meshgrid", &fieldMeshgrid,
            "Return (x, y, z) cell-center grids shaped like data[..., 0].")
        .def("dummy_header", &setDummyHeader,
            nb::arg("cell_size"), nb::arg("origin").none() = nb::none(),
            "Replace the header with defaults for rectangular data. cell_size "
            "is (x, y, z); origin defaults to cell_size / 2.")
        .def("dummy_header", &setIsotropicDummyHeader,
            nb::arg("cell_size"), nb::arg("origin").none() = nb::none(),
            "Replace the header with defaults for an isotropic rectangular grid.")
        .def("deduce", [](VField::VField& field, std::size_t iterations) {
            return field.deduceRecursively(iterations);
        }, nb::arg("max_iterations") = 5,
           "Deduce missing header fields in place and return the deduction report")
        .def("validate", &validateField,
            "Validate header and data or raise ValueError with the report");
}
