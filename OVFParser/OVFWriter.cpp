#include<map>
#include<variant>
#include<algorithm>
#include<vector>
#include<utility>
#include<iomanip>
#include<cstdint>
#include<format>
#include<mutex>
#include<limits>
#include"OVFWriter.h"
#include"OVFDictionary.h"
//endian conversion
#include<boost/endian/conversion.hpp>

#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
#endif

namespace VField
{
    using InternalWriteResult = std::expected<void, std::string>;
    inline std::string_view getName(OVFVersion version, OVFParameter parameter)
    { return paramToken(parameter, version).value(); }

    //next a specification for OVF 'recepies' is required!
    using FieldSpecifier = 
    //                         isRequireda *or* is to be outputed in this instance        
            std::pair<std::variant<bool, bool (*)(const OVFHeader&)>, std::vector<OVFParameter>>;
    using CookingStep = 
        std::variant<
            std::string,   //a comment to be left in file
            FieldSpecifier //or a rule for a specific field
        >;
    //when a cooking rule is reached it is outputed as comment if it is set as string,
    //else a value at OVFParameter is considered for output. If variant holds 'false' outputting may
    //be skipped if field is not set. If variant holds a predicate outputting will be attempted if it is 
    //returning true for current header

    //recepies for different version headers, executed sequentially
    const std::vector<CookingStep> OVF1Recepy{
        FieldSpecifier{true, {OVFParameter::Title}},
        FieldSpecifier{false, {OVFParameter::Desc}},
        "Data specifications:",
        FieldSpecifier{true, {OVFParameter::Vunit, OVFParameter::Munit, OVFParameter::Vmult}},
        "Grid specifications:",
        FieldSpecifier{true, {OVFParameter::Mtype}},
        FieldSpecifier{
            [](const OVFHeader& head)
            {return head.contains(OVFParameter::Mtype) && head.meshType() == MeshType::Rectangular;},
            {
                OVFParameter::Xnodes, OVFParameter::Ynodes, OVFParameter::Znodes,
                OVFParameter::Xstep,  OVFParameter::Ystep,  OVFParameter::Zstep,
                OVFParameter::Xbase,  OVFParameter::Ybase,  OVFParameter::Zbase
            }
        },
        FieldSpecifier{
            [](const OVFHeader& head)
            {return head.contains(OVFParameter::Mtype) && head.meshType() == MeshType::Irregular;},
            {OVFParameter::Pcount}
        },
        "Miscellaneous data:",
        FieldSpecifier{true,  { OVFParameter::Xmin, OVFParameter::Ymin, OVFParameter::Zmin,
                                OVFParameter::Xmax, OVFParameter::Ymax, OVFParameter::Zmax  }},
        FieldSpecifier{false, { OVFParameter::Bound, OVFParameter::Vmin, OVFParameter::Vmax }}
    };
    const std::vector<CookingStep> OVF2Recepy{
        FieldSpecifier{true, {OVFParameter::Title}},
        FieldSpecifier{false, {OVFParameter::Desc}},
        "Data specifications:",
        FieldSpecifier{true, {OVFParameter::Vlabels, OVFParameter::Vdim, OVFParameter::Vunit, OVFParameter::Munit}},
        "Grid specifications:",
        FieldSpecifier{true, {OVFParameter::Mtype}},
        FieldSpecifier{
            [](const OVFHeader& head)
            {return head.contains(OVFParameter::Mtype) && head.meshType() == MeshType::Rectangular;},
            {
                OVFParameter::Xnodes, OVFParameter::Ynodes, OVFParameter::Znodes,
                OVFParameter::Xstep,  OVFParameter::Ystep,  OVFParameter::Zstep,
                OVFParameter::Xbase,  OVFParameter::Ybase,  OVFParameter::Zbase
            }
        },
        FieldSpecifier{
            [](const OVFHeader& head)
            {return head.contains(OVFParameter::Mtype) && head.meshType() == MeshType::Irregular;},
            {OVFParameter::Pcount}
        },
        "Miscellaneous data:",
        FieldSpecifier{true,  { OVFParameter::Xmin, OVFParameter::Ymin, OVFParameter::Zmin,
                                OVFParameter::Xmax, OVFParameter::Ymax, OVFParameter::Zmax  }}
    };
    //It's a piece of cake to bake a pretty cake :D
    //recepy index
    const std::map<OVFVersion, const std::vector<CookingStep>&> recepyIndex{
        {OVFVersion::OVF1, OVF1Recepy},
        {OVFVersion::OVF2, OVF2Recepy}
    };

    //then some of the functions to write warious parts of header
    inline void writeField(std::ostream& out, const OVFHeader& header, OVFVersion ver, const OVFParameter p)
    {
        //first handling special cases of Description(multiline string output), and Meshtype (predefined string)
        if( p == OVFParameter::Desc)
        {
            std::string desc = header.requireAs<std::string>(p);
            std::regex pat("(.+?)\\s*(?:\n|$)", std::regex_constants::ECMAScript);
            std::smatch sm;
            while(!desc.empty() && std::regex_search(desc, sm, pat))
            {
                out << "# " << getName(ver, p) << ": ";
                out << sm[1].str() << "\n";
                desc = sm.suffix();
            }
            return;
        }
        if(p == OVFParameter::Mtype)
        {
            out << "# " << getName(ver, p) << ": ";
            out << (header.meshType() == MeshType::Rectangular?  "rectangular" : "irregular") << "\n";
            return;
        }
        //else
        out << "# " << getName(ver, p) << ": ";
        switch(paramType(p))
        {
            case(ParameterType::Unsigned):
                out << header.requireAs<std::size_t>(p);
                break;
            case(ParameterType::String):
                out << header.requireAs<std::string>(p);
                break;
            case(ParameterType::Floating):
                out << header.requireAs<double>(p);
                break;
            default:
                //TODO: come up with something 
                break;
        }
        out << '\n';
    }

    //first defining the rules for writing out a header using make_array helper template
    inline InternalWriteResult writeHeader(std::ostream& out, OVFVersion version,
                                           const OVFHeader& header)
    {
        //start by finding a ruleset if possible
        auto it = recepyIndex.find(version);
        if(it == recepyIndex.end())
            return std::unexpected("WriteHeader: Unknown or unimplemented version encountered, aborting!");

        //logger
        std::string log {""};
        for(const auto& rule: it->second)
            switch(rule.index())
            {
            case(0)://string, easy!
                out << "## " << std::get<std::string> (rule) << "\n";
                break;
            case(1)://other rule
                {
                    const auto& specifier = std::get<FieldSpecifier> (rule);
                    const bool required { (specifier.first.index() == 0 && std::get<bool>(specifier.first)) ||
                                          (specifier.first.index() == 1 && std::get<1>(specifier.first)(header)) };
                    const bool optional { specifier.first.index() == 0 && !std::get<bool>(specifier.first) };
                    //only have anything to do if 'required || optional'
                    if(required || optional)
                        for(const auto& p: specifier.second)
                        {
                            if(required && !header.contains(p))
                            {
                                std::format_to(std::back_inserter(log),
                                    "{}WriteHeader: required field '{}' was not found!",
                                    log.empty() ? "" : "\n", paramName(p));
                                continue;
                            }
                            if(optional && !header.contains(p)) //falling through if it is just an option
                                continue;
                            writeField(out, header, version, p);
                        }
                    break;
                }
            }
        if(!log.empty())
            return std::unexpected(std::move(log));
        return {};
    }

    //and a template binary data writer
    template<typename T>
    struct UintAnalogue {};
    template<> struct UintAnalogue<float> {using type = std::uint32_t;};
    template<> struct UintAnalogue<double> {using type = std::uint64_t;};

    template<typename T>
    inline void WriteBinaryData(std::ostream& out, const OVFVersion& version, const VField& field)
    {
        auto tVal {TestVal<T>};
        std::vector<T> buff;
        const T* outData = field.data<T>();
        if( (boost::endian::order::native == boost::endian::order::little && version == OVFVersion::OVF1) ||
                boost::endian::order::native == boost::endian::order::big )
        {
            buff = field.dataCopy<T>();
            outData = buff.data();
            boost::endian::endian_reverse_inplace( *reinterpret_cast<typename UintAnalogue<T>::type*>(&tVal) );
        }
        out.write(reinterpret_cast<const std::ostream::char_type*>(&tVal), 
                sizeof(T)/sizeof(std::ostream::char_type));
        for(T& value: buff)
            boost::endian::endian_reverse_inplace(
                *reinterpret_cast<typename UintAnalogue<T>::type*>(&value));
        const char* outBuff = reinterpret_cast<const std::ostream::char_type*>(outData);
        out.write(outBuff, static_cast<std::streamsize>(field.dataSizeBytes()));
    }

    WriteResult writeSegment(std::ostream& out, const VField& field)
    {
        if( !out.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "The output stream is not writable"});
        if( !field.isWeaklyAddressable())
            return std::unexpected(WriteError{
                WriteErrorCode::InvalidField,
                "The vector field is not weakly addressable"});
        //set modifiers for 'text-mode' values
        out << std::setprecision(8);
        
        auto version = field.header().version();
        out << "# Begin: Segment\n# Begin: Header\n";
        auto headerResult = writeHeader(out, version, field.header());
        std::string report = headerResult ? std::string{} : std::move(headerResult.error());
        out << "# End: Header\n# Begin: Data binary "<<field.scalarSizeBytes() << "\n";
        switch(field.scalarSizeBytes())
        {
            case(4):
                WriteBinaryData<float>(out, version, field);
                break;
            case(8):
                WriteBinaryData<double>(out, version, field);
                break;
            default:
                std::format_to(std::back_inserter(report),
                    "{}writeSegment: invalid internal scalar size; check isWeaklyAddressable()",
                    report.empty() ? "" : "\n");
        }
        out << "# End: Data binary " <<field.scalarSizeBytes() << "\n" << "# End: Segment";
        out.flush();
        if(!out.good())
        {
            std::format_to(std::back_inserter(report),
                "{}writeSegment: stream failure while writing the segment",
                report.empty() ? "" : "\n");
        }
        if(!report.empty())
            return std::unexpected(WriteError{
                WriteErrorCode::InvalidHeader, std::move(report)});
        return {};
    }

    WriteResult writeOVF(const std::filesystem::path& path, const VField& field)
    {
        if(!field.header().contains(OVFParameter::VersionString))
            return std::unexpected(WriteError{
                WriteErrorCode::InvalidHeader,
                "The segment has no OVF version signature", path, 0});

        std::ofstream output(path, std::ios_base::out | std::ios_base::binary |
                                   std::ios_base::trunc);
        if(!output.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "Unable to open output file", path});

        output << field.header().requireAs<std::string>(OVFParameter::VersionString)
               << "\n# Segment count: 1\n";
        auto result = writeSegment(output, field);
        if(!result)
        {
            result.error().path = path;
            result.error().segment = 0;
            return result;
        }
        if(!output.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "Failed while writing output file", path});
        return {};
    }

    struct OVFSegmentSink::State
    {
        struct Region {
            std::uint64_t offset{};
            std::size_t pointCount{};
            std::size_t pointDimension{};
            std::size_t pointsWritten{};
        };

        std::filesystem::path path{};
        std::fstream output{};
        std::vector<Region> regions{};
        std::size_t scalarSizeBytes{};
        OVFVersion version{OVFVersion::Unknown};
        std::mutex mutex{};
        bool closed{};
    };

    namespace {
        bool prepareSparseOutput(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if(handle == INVALID_HANDLE_VALUE)
                return false;
            DWORD returned{};
            // Sparse support is a performance hint. Filesystems without it
            // still receive the same seek-and-overwrite construction.
            (void)DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0,
                                  nullptr, 0, &returned, nullptr);
            CloseHandle(handle);
            return true;
#else
            (void)path;
            return false;
#endif
        }

        WriteError streamError(OVFSegmentSink::State& state,
                               std::string message,
                               std::optional<std::size_t> segment = {})
        {
            return {WriteErrorCode::StreamFailure, std::move(message),
                    state.path, segment};
        }

        template<class Scalar>
        WriteResult writeStreamPoints(OVFSegmentSink::State& state,
                                      std::size_t segment,
                                      std::span<const Scalar> values,
                                      std::size_t points,
                                      std::size_t dimension)
        {
            std::scoped_lock lock(state.mutex);
            if(state.closed || segment >= state.regions.size())
                return std::unexpected(streamError(
                    state, "The streaming OVF writer is closed", segment));
            if(sizeof(Scalar) != state.scalarSizeBytes)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidField,
                    std::format("Segment expects {}-byte scalars, not {}-byte scalars",
                                state.scalarSizeBytes, sizeof(Scalar)),
                    state.path, segment});

            auto& region = state.regions[segment];
            if(dimension != region.pointDimension ||
               values.size() != points * dimension)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidField,
                    std::format("Segment point dimension is {}, but streamed view has dimension {}",
                                region.pointDimension, dimension),
                    state.path, segment});
            if(points > region.pointCount - region.pointsWritten)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidField,
                    "Streamed point view exceeds the segment's declared point count",
                    state.path, segment});
            if(points == 0)
                return {};

            const auto scalarOffset = region.pointsWritten * region.pointDimension;
            const auto byteOffset = region.offset + scalarOffset * sizeof(Scalar);
            state.output.seekp(static_cast<std::streamoff>(byteOffset), std::ios_base::beg);
            if(!state.output.good())
                return std::unexpected(streamError(
                    state, "Failed to seek to the segment payload", segment));

            if((boost::endian::order::native == boost::endian::order::little &&
                state.version == OVFVersion::OVF1) ||
               boost::endian::order::native == boost::endian::order::big)
            {
                std::vector<Scalar> converted(values.begin(), values.end());
                for(auto& value : converted)
                    boost::endian::endian_reverse_inplace(
                        *reinterpret_cast<typename UintAnalogue<Scalar>::type*>(&value));
                state.output.write(reinterpret_cast<const char*>(converted.data()),
                    static_cast<std::streamsize>(converted.size() * sizeof(Scalar)));
            }
            else
                state.output.write(reinterpret_cast<const char*>(values.data()),
                    static_cast<std::streamsize>(values.size_bytes()));

            if(!state.output.good())
                return std::unexpected(streamError(
                    state, "Failed while writing segment points", segment));
            region.pointsWritten += points;
            return {};
        }
    }

    OVFSegmentSink::OVFSegmentSink(State* state, std::size_t segment) noexcept:
        state_(state), segment_(segment) {}

    WriteResult OVFSegmentSink::writeContiguous(std::span<const float> values,
                                                std::size_t points,
                                                std::size_t dimension)
    { return writeStreamPoints(*state_, segment_, values, points, dimension); }

    WriteResult OVFSegmentSink::writeContiguous(std::span<const double> values,
                                                std::size_t points,
                                                std::size_t dimension)
    { return writeStreamPoints(*state_, segment_, values, points, dimension); }

    std::size_t OVFSegmentSink::pointsWritten() const noexcept
    {
        return state_ && segment_ < state_->regions.size()
            ? state_->regions[segment_].pointsWritten : 0;
    }
    std::size_t OVFSegmentSink::pointsRemaining() const noexcept
    {
        if(!state_ || segment_ >= state_->regions.size()) return 0;
        const auto& region = state_->regions[segment_];
        return region.pointCount - region.pointsWritten;
    }
    bool OVFSegmentSink::complete() const noexcept
    { return state_ && pointsRemaining() == 0; }
    bool OVFSegmentSink::good() const noexcept
    { return state_ && !error_ && !state_->closed && state_->output.good(); }

    OVFStreamWriter::OVFStreamWriter(std::unique_ptr<OVFSegmentSink::State> state,
                                     std::size_t segments):
        state_(std::move(state))
    {
        sinks_.reserve(segments);
        for(std::size_t segment = 0; segment < segments; ++segment)
            sinks_.push_back(OVFSegmentSink{state_.get(), segment});
    }

    OVFStreamWriter::~OVFStreamWriter() { abort(); }
    OVFStreamWriter::OVFStreamWriter(OVFStreamWriter&&) noexcept = default;
    OVFStreamWriter& OVFStreamWriter::operator=(OVFStreamWriter&&) noexcept = default;

    std::expected<OVFStreamWriter, WriteError> OVFStreamWriter::create(
        const std::filesystem::path& path, std::span<const OVFHeader> headers,
        std::size_t scalarSizeBytes)
    {
        if(headers.empty())
            return std::unexpected(WriteError{
                WriteErrorCode::EmptyInput, "No segment headers were provided", path});
        if(scalarSizeBytes != sizeof(float) && scalarSizeBytes != sizeof(double))
            return std::unexpected(WriteError{
                WriteErrorCode::InvalidField,
                "Binary OVF streaming supports only 4-byte and 8-byte scalars", path});

        if(!headers.front().contains(OVFParameter::VersionString))
            return std::unexpected(WriteError{
                WriteErrorCode::InvalidHeader,
                "The first segment has no OVF version signature", path, 0});
        const auto version = headers.front().version();
        if(version != OVFVersion::OVF1 && version != OVFVersion::OVF2)
            return std::unexpected(WriteError{
                WriteErrorCode::UnsupportedVersion,
                "Streaming output supports OVF versions 1 and 2", path, 0});
        auto state = std::make_unique<OVFSegmentSink::State>();
        state->path = path;
        state->scalarSizeBytes = scalarSizeBytes;
        state->version = version;
        state->regions.reserve(headers.size());
        const bool precreated = prepareSparseOutput(path);
        auto openMode = std::ios_base::in | std::ios_base::out |
                        std::ios_base::binary;
        if(!precreated) openMode |= std::ios_base::trunc;
        state->output.open(path, openMode);
        if(!state->output.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "Unable to open output file", path});

        state->output << std::setprecision(8);
        state->output << headers.front().requireAs<std::string>(OVFParameter::VersionString)
                      << "\n# Segment count: " << headers.size() << '\n';
        for(std::size_t segment = 0; segment < headers.size(); ++segment)
        {
            const auto& header = headers[segment];
            if(header.version() != version)
                return std::unexpected(WriteError{
                    WriteErrorCode::IncompatibleVersions,
                    "All segments must declare the same OVF version", path, segment});
            const auto pointCount = header.pointCount();
            const auto pointDimension = header.pointDimension();
            if(!pointCount || !pointDimension)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidHeader,
                    "Segment header has no determinate point shape", path, segment});
            if(segment != 0) state->output << '\n';
            state->output << "# Begin: Segment\n# Begin: Header\n";
            if(auto result = writeHeader(state->output, version, header); !result)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidHeader, std::move(result.error()), path, segment});
            state->output << "# End: Header\n# Begin: Data binary "
                          << scalarSizeBytes << '\n';

            if(scalarSizeBytes == sizeof(float))
            {
                auto test = TestVal<float>;
                if((boost::endian::order::native == boost::endian::order::little &&
                    version == OVFVersion::OVF1) ||
                   boost::endian::order::native == boost::endian::order::big)
                    boost::endian::endian_reverse_inplace(
                        *reinterpret_cast<std::uint32_t*>(&test));
                state->output.write(reinterpret_cast<const char*>(&test), sizeof(test));
            }
            else
            {
                auto test = TestVal<double>;
                if((boost::endian::order::native == boost::endian::order::little &&
                    version == OVFVersion::OVF1) ||
                   boost::endian::order::native == boost::endian::order::big)
                    boost::endian::endian_reverse_inplace(
                        *reinterpret_cast<std::uint64_t*>(&test));
                state->output.write(reinterpret_cast<const char*>(&test), sizeof(test));
            }

            const auto offset = state->output.tellp();
            if(offset < 0 || *pointCount > std::numeric_limits<std::size_t>::max() / *pointDimension)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidHeader,
                    "Segment payload size is not representable", path, segment});
            const auto scalarCount = *pointCount * *pointDimension;
            if(scalarCount > static_cast<std::size_t>(
                    std::numeric_limits<std::streamoff>::max()) / scalarSizeBytes)
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidHeader,
                    "Segment payload is too large for stream offsets", path, segment});
            state->regions.push_back({static_cast<std::uint64_t>(offset),
                                      *pointCount, *pointDimension, 0});
            state->output.seekp(static_cast<std::streamoff>(scalarCount * scalarSizeBytes),
                                std::ios_base::cur);
            state->output << "# End: Data binary " << scalarSizeBytes
                          << "\n# End: Segment";
            if(!state->output.good())
                return std::unexpected(WriteError{
                    WriteErrorCode::StreamFailure,
                    "Failed while preparing segment payload regions", path, segment});
        }
        state->output.flush();
        if(!state->output.good())
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "Failed while preparing output file", path});
        return OVFStreamWriter{std::move(state), headers.size()};
    }

    std::size_t OVFStreamWriter::segmentCount() const noexcept
    { return sinks_.size(); }
    OVFSegmentSink& OVFStreamWriter::segment(std::size_t index)
    { return sinks_.at(index); }
    const OVFSegmentSink& OVFStreamWriter::segment(std::size_t index) const
    { return sinks_.at(index); }

    WriteResult OVFStreamWriter::finalize()
    {
        if(!state_ || state_->closed)
            return std::unexpected(WriteError{
                WriteErrorCode::StreamFailure, "The streaming OVF writer is closed",
                state_ ? state_->path : std::filesystem::path{}});
        for(std::size_t segment = 0; segment < sinks_.size(); ++segment)
        {
            if(sinks_[segment].error())
                return std::unexpected(*sinks_[segment].error());
            if(!sinks_[segment].complete())
                return std::unexpected(WriteError{
                    WriteErrorCode::InvalidField,
                    std::format("Segment is incomplete: {} points remain",
                                sinks_[segment].pointsRemaining()),
                    state_->path, segment});
        }
        state_->output.flush();
        if(!state_->output.good())
            return std::unexpected(streamError(*state_, "Failed to flush output file"));
        state_->closed = true;
        state_->output.close();
        return {};
    }

    void OVFStreamWriter::abort() noexcept
    {
        if(state_ && !state_->closed)
        {
            state_->closed = true;
            state_->output.close();
        }
    }
}
