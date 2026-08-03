#include<map>
#include<variant>
#include<algorithm>
#include<vector>
#include<utility>
#include<iomanip>
#include<cstdint>
#include<format>
#include"OVFWriter.h"
#include"OVFDictionary.h"
//endian conversion
#include<boost/endian/conversion.hpp>

namespace VField
{
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
            {return head.isSet(OVFParameter::Mtype) && head.getMeshType() == OVFHeader::MeshType::rectangular;},
            {
                OVFParameter::Xnodes, OVFParameter::Ynodes, OVFParameter::Znodes,
                OVFParameter::Xstep,  OVFParameter::Ystep,  OVFParameter::Zstep,
                OVFParameter::Xbase,  OVFParameter::Ybase,  OVFParameter::Zbase
            }
        },
        FieldSpecifier{
            [](const OVFHeader& head)
            {return head.isSet(OVFParameter::Mtype) && head.getMeshType() == OVFHeader::MeshType::irregular;},
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
            {return head.isSet(OVFParameter::Mtype) && head.getMeshType() == OVFHeader::MeshType::rectangular;},
            {
                OVFParameter::Xnodes, OVFParameter::Ynodes, OVFParameter::Znodes,
                OVFParameter::Xstep,  OVFParameter::Ystep,  OVFParameter::Zstep,
                OVFParameter::Xbase,  OVFParameter::Ybase,  OVFParameter::Zbase
            }
        },
        FieldSpecifier{
            [](const OVFHeader& head)
            {return head.isSet(OVFParameter::Mtype) && head.getMeshType() == OVFHeader::MeshType::irregular;},
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
            std::string desc = header.getString(p);
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
            out << (header.getMeshType() == OVFHeader::MeshType::rectangular?  "rectangular" : "irregular") << "\n";
            return;
        }
        //else
        out << "# " << getName(ver, p) << ": ";
        switch(paramType(p))
        {
            case(pType::Uint):
                out << header.getUint(p);
                break;
            case(pType::String):
                out << header.getString(p);
                break;
            case(pType::Float):
                out << header.getFloat(p);
                break;
            default:
                //TODO: come up with something 
                break;
        }
        out << '\n';
    }

    //first defining the rules for writing out a header using make_array helper template
    inline WriteResult WriteHeader(std::ostream& out, const OVFVersion& version, const OVFHeader& header) noexcept
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
                            if(required && !header.isSet(p))
                            {
                                std::format_to(std::back_inserter(log),
                                    "{}WriteHeader: required field '{}' was not found!",
                                    log.empty() ? "" : "\n", paramName(p));
                                continue;
                            }
                            if(optional && !header.isSet(p)) //falling through if it is just an option
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
        const T* outData = field.getData<T>();
        if( (boost::endian::order::native == boost::endian::order::little && version == OVFVersion::OVF1) ||
                boost::endian::order::native == boost::endian::order::big )
        {
            buff = field.getDataCopy<T>();
            outData = buff.data();
            boost::endian::endian_reverse_inplace( *reinterpret_cast<typename UintAnalogue<T>::type*>(&tVal) );
        }
        out.write(reinterpret_cast<const std::ostream::char_type*>(&tVal), 
                sizeof(T)/sizeof(std::ostream::char_type));
        for(T& value: buff)
            boost::endian::endian_reverse_inplace(
                *reinterpret_cast<typename UintAnalogue<T>::type*>(&value));
        const char* outBuff = reinterpret_cast<const std::ostream::char_type*>(outData);
        out.write(outBuff, field.curDataPoints() * sizeof(T)/sizeof(char));
    }

    WriteResult WriteSegment(std::ostream& out, const VField& field) noexcept
    {
        if( !out.good())
            return std::unexpected("WriteSegment: Stream given was not good, aborting!");
        if( !field.isWeaklyAddressable())
            return std::unexpected("WriteSegment: Vector field should at least be weakly addressable, aborting!");
        //set modifiers for 'text-mode' values
        out << std::setprecision(8);
        
        auto version = matchVersionString(field.Header.at<pType::String>(OVFParameter::VersionString));
        out << "# Begin: Segment\n# Begin: Header\n";
        auto headerResult = WriteHeader(out, version, field.Header);
        std::string report = headerResult ? std::string{} : std::move(headerResult.error());
        out << "# End: Header\n# Begin: Data binary "<<field.curDataInternalSize() << "\n";
        switch(field.curDataInternalSize())
        {
            case(4):
                WriteBinaryData<float>(out, version, field);
                break;
            case(8):
                WriteBinaryData<double>(out, version, field);
                break;
            default:
                std::format_to(std::back_inserter(report),
                    "{}WriteSegment: somehow got invalid internal data size! Please check 'isWeaklyAddressable' for bugs!",
                    report.empty() ? "" : "\n");
        }
        out << "# End: Data binary " <<field.curDataInternalSize() << "\n" << "# End: Segment";
        out.flush();
        if(!out.good())
        {
            std::format_to(std::back_inserter(report),
                "{}WriteSegment: filesystem error occurred while writing the segment!",
                report.empty() ? "" : "\n");
        }
        if(!report.empty())
            return std::unexpected(std::move(report));
        return {};
    }
}
