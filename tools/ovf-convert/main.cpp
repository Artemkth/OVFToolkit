#include <OVFDictionary.h>
#include <OVFParser.h>

#include <boost/program_options.hpp>

#include <hdf5.h>

#include <vtkCellArray.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkStructuredGrid.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertex.h>
#include <vtkSmartPointer.h>
#include <vtkXMLStructuredGridWriter.h>
#include <vtkXMLUnstructuredGridWriter.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace po = boost::program_options;

constexpr std::string_view version = "0.2";

class H5Handle {
    hid_t id_{-1};
    herr_t (*close_)(hid_t){};

  public:
    H5Handle() = default;
    H5Handle(hid_t id, herr_t (*close)(hid_t)) : id_(id), close_(close)
    {
        if(id_ < 0)
            throw std::runtime_error("HDF5 operation failed");
    }
    ~H5Handle()
    {
        if(id_ >= 0)
            close_(id_);
    }
    H5Handle(const H5Handle&) = delete;
    H5Handle& operator=(const H5Handle&) = delete;
    H5Handle(H5Handle&& other) noexcept
      : id_(std::exchange(other.id_, -1)), close_(other.close_) {}
    H5Handle& operator=(H5Handle&&) = delete;
    [[nodiscard]] operator hid_t() const noexcept { return id_; }
};

[[nodiscard]] std::string jsonEscape(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 2);
    for(const unsigned char character : value)
    {
        switch(character)
        {
          case '"': result += "\\\""; break;
          case '\\': result += "\\\\"; break;
          case '\b': result += "\\b"; break;
          case '\f': result += "\\f"; break;
          case '\n': result += "\\n"; break;
          case '\r': result += "\\r"; break;
          case '\t': result += "\\t"; break;
          default:
            if(character < 0x20)
                result += std::format("\\u{:04x}", character);
            else
                result += static_cast<char>(character);
        }
    }
    return result;
}

[[nodiscard]] std::string versionName(VField::OVFVersion value)
{
    switch(value)
    {
      case VField::OVFVersion::OVF0: return "OVF 0";
      case VField::OVFVersion::OVF1: return "OVF 1";
      case VField::OVFVersion::OVF2: return "OVF 2";
      case VField::OVFVersion::Unknown: return "unknown";
    }
    std::unreachable();
}

[[nodiscard]] bool redundantMetadata(VField::OVFParameter parameter)
{
    using enum VField::OVFParameter;
    return parameter == Xnodes || parameter == Ynodes || parameter == Znodes ||
           parameter == Pcount;
}

void writeJsonValue(std::ostream& output, const VField::ParameterValue& value)
{
    std::visit([&output](const auto& item) {
        using T = std::remove_cvref_t<decltype(item)>;
        if constexpr(std::same_as<T, std::string>)
            output << '"' << jsonEscape(item) << '"';
        else if constexpr(std::same_as<T, VField::MeshType>)
            output << '"'
                   << (item == VField::MeshType::Rectangular ? "rectangular" : "irregular")
                   << '"';
        else
            output << item;
    }, value);
}

[[nodiscard]] std::string metadataKey(VField::OVFParameter parameter,
                                      VField::OVFVersion ovfVersion)
{
    if(parameter == VField::OVFParameter::VersionString)
        return "version_string";
    if(const auto token = VField::paramToken(parameter, ovfVersion))
        return std::string{*token};
    return std::string{VField::paramName(parameter)};
}

void writeHeaderJson(std::ostream& output, const VField::OVFHeader& header,
                     std::size_t index, std::string_view locationKey,
                     std::string_view location)
{
    output << "    {\n"
           << "      \"index\": " << index << ",\n"
           << "      \"" << locationKey << "\": \""
           << jsonEscape(location) << "\",\n"
           << "      \"ovf_version\": \"" << versionName(header.version()) << "\",\n"
           << "      \"header\": {";

    bool first = true;
    for(const auto parameter : VField::ParamUniverse)
    {
        if(redundantMetadata(parameter) ||
           VField::paramType(parameter) == VField::ParameterType::Other)
            continue;
        const auto value = header.lookup(parameter);
        if(!value)
            continue;
        output << (first ? "\n" : ",\n")
               << "        \"" << jsonEscape(metadataKey(parameter, header.version()))
               << "\": ";
        writeJsonValue(output, value->get());
        first = false;
    }
    if(!first)
        output << '\n';
    output << "      }\n    }";
}

template<typename T>
[[nodiscard]] hid_t hdfNativeType();
template<> [[nodiscard]] hid_t hdfNativeType<float>() { return H5T_NATIVE_FLOAT; }
template<> [[nodiscard]] hid_t hdfNativeType<double>() { return H5T_NATIVE_DOUBLE; }

template<typename T>
void writeInterleavedDataset(hid_t group, std::string_view name,
                             const T* data, hsize_t points,
                             hsize_t tupleWidth, hsize_t firstComponent,
                             hsize_t components)
{
    const std::array<hsize_t, 2> fileDimensions{points, components};
    H5Handle fileSpace{H5Screate_simple(2, fileDimensions.data(), nullptr), H5Sclose};
    const std::string datasetName{name};
    H5Handle dataset{H5Dcreate2(group, datasetName.c_str(), hdfNativeType<T>(),
                               fileSpace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Dclose};

    const std::array<hsize_t, 1> memoryDimensions{points * tupleWidth};
    H5Handle memorySpace{H5Screate_simple(1, memoryDimensions.data(), nullptr), H5Sclose};
    for(hsize_t component{}; component < components; ++component)
    {
        const hsize_t memoryStart = firstComponent + component;
        const hsize_t memoryStride = tupleWidth;
        const hsize_t memoryCount = points;
        if(H5Sselect_hyperslab(memorySpace, H5S_SELECT_SET, &memoryStart,
                              &memoryStride, &memoryCount, nullptr) < 0)
            throw std::runtime_error("could not select HDF5 memory hyperslab");

        const std::array<hsize_t, 2> fileStart{0, component};
        const std::array<hsize_t, 2> fileCount{points, 1};
        if(H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, fileStart.data(),
                              nullptr, fileCount.data(), nullptr) < 0 ||
           H5Dwrite(dataset, hdfNativeType<T>(), memorySpace, fileSpace,
                    H5P_DEFAULT, data) < 0)
            throw std::runtime_error("could not write HDF5 hyperslab");
    }
}

template<typename T>
void writeHdfSegment(hid_t file, std::size_t index, const VField::VField& field)
{
    const std::string groupName = std::format("/segments/{:06}", index);
    H5Handle group{H5Gcreate2(file, groupName.c_str(), H5P_DEFAULT,
                             H5P_DEFAULT, H5P_DEFAULT), H5Gclose};
    const auto& header = field.header();
    const auto points = static_cast<hsize_t>(field.pointCount());
    const auto tupleWidth = static_cast<hsize_t>(field.pointDimension());
    const T* data = field.data<T>();

    if(header.meshType() == VField::MeshType::Rectangular)
    {
        const std::array<hsize_t, 4> dimensions{
            header.requireAs<std::size_t>(VField::OVFParameter::Znodes),
            header.requireAs<std::size_t>(VField::OVFParameter::Ynodes),
            header.requireAs<std::size_t>(VField::OVFParameter::Xnodes),
            tupleWidth};
        H5Handle space{H5Screate_simple(4, dimensions.data(), nullptr), H5Sclose};
        H5Handle dataset{H5Dcreate2(group, "values", hdfNativeType<T>(), space,
                                   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Dclose};
        if(H5Dwrite(dataset, hdfNativeType<T>(), H5S_ALL, H5S_ALL,
                    H5P_DEFAULT, data) < 0)
            throw std::runtime_error("could not write rectangular HDF5 data");
    }
    else
    {
        if(tupleWidth < 3)
            throw std::runtime_error("irregular OVF tuples do not contain XYZ coordinates");
        writeInterleavedDataset(group, "points", data, points, tupleWidth, 0, 3);
        writeInterleavedDataset(group, "values", data, points, tupleWidth, 3,
                                tupleWidth - 3);
    }
}

void writeHdf(const fs::path& outputPath, VField::VFieldFile& input, bool force)
{
    fs::path jsonPath = outputPath;
    jsonPath.replace_extension(".json");
    if(!force && (fs::exists(outputPath) || fs::exists(jsonPath)))
        throw std::runtime_error("output already exists; pass --force to replace it");

    H5Handle file{H5Fcreate(outputPath.string().c_str(), H5F_ACC_TRUNC,
                           H5P_DEFAULT, H5P_DEFAULT), H5Fclose};
    H5Handle segments{H5Gcreate2(file, "/segments", H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT), H5Gclose};
    (void)segments;

    std::ofstream json{jsonPath};
    if(!json)
        throw std::runtime_error(std::format("could not open {}", jsonPath.string()));
    json << std::setprecision(std::numeric_limits<double>::max_digits10)
         << "{\n  \"format\": \"OVFToolkit HDF5 1\",\n"
         << "  \"source\": \"" << jsonEscape(input.path().generic_string()) << "\",\n"
         << "  \"segments\": [\n";

    for(std::size_t index{}; index < input.segmentCount(); ++index)
    {
        auto loaded = input.load(index);
        if(!loaded)
            throw std::runtime_error(loaded.error().message);
        const auto& field = loaded->get();
        if(!field.isAddressable())
            throw std::runtime_error(std::format("OVF segment {} is not addressable", index));
        if(field.stores<float>())
            writeHdfSegment<float>(file, index, field);
        else if(field.stores<double>())
            writeHdfSegment<double>(file, index, field);
        else
            throw std::runtime_error(std::format("OVF segment {} has no numeric data", index));

        if(index)
            json << ",\n";
        writeHeaderJson(json, field.header(), index, "hdf5_group",
                        std::format("/segments/{:06}", index));
        input.unload(index);
        std::cout << std::format("Converted segment {}/{}\n", index + 1,
                                 input.segmentCount());
    }
    json << "\n  ]\n}\n";
}

[[nodiscard]] fs::path vtkSegmentPath(const fs::path& requested,
                                      std::size_t index, std::size_t count)
{
    if(count == 1)
        return requested;
    return requested.parent_path() /
           std::format("{}.segment-{:06}{}", requested.stem().string(), index,
                       requested.extension().string());
}

template<typename T>
vtkSmartPointer<vtkDataArray> makeVtkValues(const VField::VField& field,
                                            std::size_t firstComponent)
{
    using Array = std::conditional_t<std::same_as<T, float>, vtkFloatArray,
                                     vtkDoubleArray>;
    const auto tupleWidth = field.pointDimension();
    const auto components = tupleWidth - firstComponent;
    vtkNew<Array> values;
    values->SetName("field");
    values->SetNumberOfComponents(static_cast<int>(components));
    const T* source = field.data<T>();
    if(firstComponent == 0)
    {
        // The writer completes before VFieldFile::unload(), so VTK may borrow
        // the packed rectangular array without allocating a second copy.
        values->SetArray(const_cast<T*>(source),
                         static_cast<vtkIdType>(field.scalarCount()), 1);
        return values;
    }
    values->SetNumberOfTuples(static_cast<vtkIdType>(field.pointCount()));
    for(std::size_t point{}; point < field.pointCount(); ++point)
        for(std::size_t component{}; component < components; ++component)
            values->SetTypedComponent(static_cast<vtkIdType>(point),
                                      static_cast<int>(component),
                                      source[point * tupleWidth + firstComponent + component]);
    return values;
}

template<typename T>
void writeStructuredVtk(const fs::path& path, const VField::VField& field)
{
    if(!field.isGridAddressable())
        throw std::runtime_error(".vts output requires a rectangular addressable grid");
    const auto& header = field.header();
    const auto xnodes = header.requireAs<std::size_t>(VField::OVFParameter::Xnodes);
    const auto ynodes = header.requireAs<std::size_t>(VField::OVFParameter::Ynodes);
    const auto znodes = header.requireAs<std::size_t>(VField::OVFParameter::Znodes);
    constexpr auto vtkDimensionLimit =
        static_cast<std::size_t>(std::numeric_limits<int>::max());
    if(xnodes > vtkDimensionLimit || ynodes > vtkDimensionLimit ||
       znodes > vtkDimensionLimit)
        throw std::runtime_error("grid dimensions exceed the VTK structured-grid limit");
    const auto coordinate = [&header](VField::OVFParameter base,
                                      VField::OVFParameter step, std::size_t index) {
        return header.requireAs<double>(base) +
               header.requireAs<double>(step) * static_cast<double>(index);
    };

    vtkNew<vtkPoints> points;
    points->SetDataType(std::same_as<T, float> ? VTK_FLOAT : VTK_DOUBLE);
    points->SetNumberOfPoints(static_cast<vtkIdType>(field.pointCount()));
    vtkIdType point{};
    for(std::size_t z{}; z < znodes; ++z)
        for(std::size_t y{}; y < ynodes; ++y)
            for(std::size_t x{}; x < xnodes; ++x)
                points->SetPoint(point++,
                    coordinate(VField::OVFParameter::Xbase, VField::OVFParameter::Xstep, x),
                    coordinate(VField::OVFParameter::Ybase, VField::OVFParameter::Ystep, y),
                    coordinate(VField::OVFParameter::Zbase, VField::OVFParameter::Zstep, z));

    vtkNew<vtkStructuredGrid> grid;
    grid->SetDimensions(static_cast<int>(xnodes), static_cast<int>(ynodes),
                        static_cast<int>(znodes));
    grid->SetPoints(points);
    grid->GetPointData()->AddArray(makeVtkValues<T>(field, 0));

    vtkNew<vtkXMLStructuredGridWriter> writer;
    writer->SetFileName(path.string().c_str());
    writer->SetInputData(grid);
    writer->SetDataModeToAppended();
    writer->EncodeAppendedDataOff();
    if(writer->Write() != 1)
        throw std::runtime_error(std::format("VTK failed to write {}", path.string()));
}

template<typename T>
void writeUnstructuredVtk(const fs::path& path, const VField::VField& field)
{
    if(field.header().meshType() != VField::MeshType::Irregular ||
       !field.isAddressable() || field.pointDimension() < 3)
        throw std::runtime_error(".vtu output requires an addressable irregular mesh");
    const auto tupleWidth = field.pointDimension();
    const T* source = field.data<T>();

    vtkNew<vtkPoints> points;
    points->SetDataType(std::same_as<T, float> ? VTK_FLOAT : VTK_DOUBLE);
    points->SetNumberOfPoints(static_cast<vtkIdType>(field.pointCount()));
    vtkNew<vtkCellArray> vertices;
    for(std::size_t point{}; point < field.pointCount(); ++point)
    {
        points->SetPoint(static_cast<vtkIdType>(point), source[point * tupleWidth],
                         source[point * tupleWidth + 1], source[point * tupleWidth + 2]);
        const vtkIdType id = static_cast<vtkIdType>(point);
        vertices->InsertNextCell(1, &id);
    }

    vtkNew<vtkUnstructuredGrid> grid;
    grid->SetPoints(points);
    grid->SetCells(VTK_VERTEX, vertices);
    grid->GetPointData()->AddArray(makeVtkValues<T>(field, 3));

    vtkNew<vtkXMLUnstructuredGridWriter> writer;
    writer->SetFileName(path.string().c_str());
    writer->SetInputData(grid);
    writer->SetDataModeToAppended();
    writer->EncodeAppendedDataOff();
    if(writer->Write() != 1)
        throw std::runtime_error(std::format("VTK failed to write {}", path.string()));
}

void writeVtk(const fs::path& requestedPath, VField::VFieldFile& input,
              bool force, std::string_view extension)
{
    fs::path jsonPath = requestedPath;
    jsonPath.replace_extension(".json");
    if(!force && fs::exists(jsonPath))
        throw std::runtime_error(std::format(
            "{} already exists; pass --force to replace it", jsonPath.string()));
    if(!force)
        for(std::size_t index{}; index < input.segmentCount(); ++index)
        {
            const auto path = vtkSegmentPath(requestedPath, index, input.segmentCount());
            if(fs::exists(path))
                throw std::runtime_error(std::format(
                    "{} already exists; pass --force to replace it", path.string()));
        }
    std::ofstream json{jsonPath};
    if(!json)
        throw std::runtime_error(std::format("could not open {}", jsonPath.string()));
    json << std::setprecision(std::numeric_limits<double>::max_digits10)
         << "{\n  \"format\": \"OVFToolkit VTK metadata 1\",\n"
         << "  \"source\": \"" << jsonEscape(input.path().generic_string()) << "\",\n"
         << "  \"segments\": [\n";

    for(std::size_t index{}; index < input.segmentCount(); ++index)
    {
        const auto path = vtkSegmentPath(requestedPath, index, input.segmentCount());
        auto loaded = input.load(index);
        if(!loaded)
            throw std::runtime_error(loaded.error().message);
        const auto& field = loaded->get();
        const auto dispatch = [&]<typename T>() {
            if(extension == ".vts")
                writeStructuredVtk<T>(path, field);
            else
                writeUnstructuredVtk<T>(path, field);
        };
        if(field.stores<float>())
            dispatch.template operator()<float>();
        else if(field.stores<double>())
            dispatch.template operator()<double>();
        else
            throw std::runtime_error(std::format("OVF segment {} has no numeric data", index));
        if(index)
            json << ",\n";
        writeHeaderJson(json, field.header(), index, "vtk_file",
                        path.filename().generic_string());
        input.unload(index);
        std::cout << std::format("Wrote {} ({}/{})\n", path.string(), index + 1,
                                 input.segmentCount());
    }
    json << "\n  ]\n}\n";
}

[[nodiscard]] std::string lowercaseExtension(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

} // namespace

int main(int argc, char** argv)
try
{
    fs::path inputPath;
    fs::path outputPath;
    bool force{};
    po::options_description options{"Options"};
    options.add_options()
        ("help,h", "Show this help message")
        ("version,v", "Show version information")
        ("force,f", po::bool_switch(&force), "Replace existing output files")
        ("input", po::value<fs::path>(&inputPath)->required(), "Input .ovf file")
        ("output", po::value<fs::path>(&outputPath)->required(),
         "Output .vts, .vtu, or .h5 file");
    po::positional_options_description positional;
    positional.add("input", 1).add("output", 1);

    po::variables_map variables;
    po::store(po::command_line_parser(argc, argv).options(options)
                  .positional(positional).run(), variables);
    if(variables.contains("help"))
    {
        std::cout << "Usage: ovf-convert [options] input.ovf output.{vts|vtu|h5}\n\n"
                  << options << '\n';
        return 0;
    }
    if(variables.contains("version"))
    {
        std::cout << "ovf-convert " << version << '\n';
        return 0;
    }
    po::notify(variables);

    const std::string extension = lowercaseExtension(outputPath);
    if(extension != ".vts" && extension != ".vtu" && extension != ".h5")
        throw std::runtime_error("output extension must be .vts, .vtu, or .h5");

    auto opened = VField::VFieldFile::open(inputPath, VField::DataLoading::Lazy);
    if(!opened)
        throw std::runtime_error(opened.error().message);
    auto input = std::move(*opened);
    if(extension == ".h5")
        writeHdf(outputPath, input, force);
    else
        writeVtk(outputPath, input, force, extension);
    return 0;
}
catch(const po::error& error)
{
    std::cerr << "ovf-convert: " << error.what() << '\n';
    return 2;
}
catch(const std::exception& error)
{
    std::cerr << "ovf-convert: " << error.what() << '\n';
    return 1;
}
