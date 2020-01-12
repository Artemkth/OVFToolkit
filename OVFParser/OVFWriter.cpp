#include<map>
#include<variant>
#include<algorithm>
#include<vector>
#include<utility>
#include"OVFUtil.h"
#include"OVFWriter.h"
#include"OVFDictionary.h"
//endian conversion
#include<boost/endian/conversion.hpp>

namespace VField
{
    //header writing paraphernalia 
    const std::map<OVFParameter, std::variant<
                                    std::string,
                                    std::string (*)(const OVFVersion)>>
                                        TokenNames
    {
        { OVFParameter::Title,          "Title"                 },
        { OVFParameter::Desc,           "Desc"                  },
        { OVFParameter::Segcnt,         "Segment count"         },
        { OVFParameter::Munit,          "meshunit"              },
        { OVFParameter::Vunit, 
          [](const OVFVersion ver) -> std::string 
            { return ver == OVFVersion::OVF1? "valueunit" : "valueunits"; }
        },
        { OVFParameter::Vmult,          "valuemultiplier"       },
        { OVFParameter::Vdim,           "valuedim"              },
        { OVFParameter::Vlabels,        "valuelabels"           },
        { OVFParameter::Xmin,           "xmin"                  },
        { OVFParameter::Xmax,           "xmax"                  },
        { OVFParameter::Ymin,           "ymin"                  },
        { OVFParameter::Ymax,           "ymax"                  },
        { OVFParameter::Zmin,           "zmin"                  },
        { OVFParameter::Zmax,           "zmax"                  },
        { OVFParameter::Bound,          "boundary"              },
        { OVFParameter::Vmax,           "ValueRangeMaxMag"      },
        { OVFParameter::Vmin,           "ValueRangeMinMax"      },
        { OVFParameter::Mtype,          "Meshtype"              },
        { OVFParameter::Pcount,         "pointcount"            },
        { OVFParameter::Xbase,          "xbase"                 },
        { OVFParameter::Ybase,          "ybase"                 },
        { OVFParameter::Zbase,          "zbase"                 },
        { OVFParameter::Xstep,          "xstepsize"             },
        { OVFParameter::Ystep,          "ystepsize"             },
        { OVFParameter::Zstep,          "zstepsize"             },
        { OVFParameter::Xnodes,         "xnodes"                },
        { OVFParameter::Ynodes,         "ynodes"                },
        { OVFParameter::Znodes,         "znodes"                }
    };
    inline std::string getName(const OVFVersion ver, const OVFParameter par)
    {
        auto val = TokenNames.at(par);
        switch(val.index())
        {
        case(0): //plain string
            return std::get<0>(val);
        case(1): //predicate returning string
            return std::get<1>(val)(ver);
        default: //no need to do default case unless somebody doesn't initialize a field in a map
                 //but just in case
            return "";
        }
    }

    //then some of the functions to write warious parts of header
    //T is iterator to pair of <OVFParameter, bool>
    template<typename T>
    inline std::string writeField(std::ostream& out, const OVFHeader& header,
                                                    T begin, T end)
    {
        std::string log{};
        for(; begin != end; begin++ )
        {
            if(!header.isSet(begin -> first) && begin -> second)
            {
                if(!log.empty())
                    log += "\n";
                log += (std::string)"writeField: parameter \"" + ParameterName(begin -> first) +
                    "\" was not set! skipping!";
                continue;
            }
            if(TokenNames.find(begin -> first) == TokenNames.end())
            {
                if(!log.empty())
                    log += "\n";
                log += (std::string)"writeField: parameter \"" + ParameterName(begin -> first) +
                    "\" has no token name defined yet! skipping!";
                continue;
            }
            //else write stuff out
            if(begin -> first == OVFParameter::Desc)
            {
                std::string desc = header.getString(begin -> first);
                std::regex pat("^\\s*(.*?)\\s*\n", std::regex_constants::ECMAScript);
                std::smatch sm;
                while(!desc.empty() && std::regex_search(desc, sm, pat))
                {
                    out << "# " << ParameterName(OVFParameter::Desc) << ": ";
                    out << sm[1].str() << "\n";
                    desc = sm.suffix();
                }
                continue;
            }
            out << "# " << ParameterName(begin -> first) << ": ";
            switch(paramIndex(begin -> first))
            {
            case(pType::Uint):
                out << header.getUint(begin -> first);
                break;
            case(pType::String):
                out << header.getString(begin -> first);
                break;
            case(pType::Float):
                out << header.getFloat(begin -> first);
                break;
            default:
                //TODO: come up with something 
                break;
            }
            out << '\n';
        }
        return log;
    }

    constexpr auto OVFTitles = DictionaryHelpers::make_array(
            std::make_pair(OVFParameter::Title, true),
            std::make_pair(OVFParameter::Desc,  false),
            std::make_pair(OVFParameter::Vunit, true),
            std::make_pair(OVFParameter::Munit, true)
        );
    //first defining the rules for writing out a header using make_array helper template
    inline std::string WriteHeader(std::ostream& out, const OVFVersion& version, const OVFHeader& header) noexcept
    {
        if( !header.isSet(OVFParameter::VersionString) )
            return "WriteHeader: Version wasn't set in header, aborting!";
        if( version == OVFVersion::OVF0 )
            return ""; //nothing to output LULW

        //otherwise start writing
        if( version == OVFVersion::OVF1 || version == OVFVersion::OVF2 )
        {
            std::string log = "";
            //start by generating a header
            std::vector<std::pair<OVFParameter, bool>> Titles {OVFTitles.begin(), OVFTitles.end()};
            if(version == OVFVersion::OVF1)
                Titles.push_back(std::make_pair(OVFParameter::Vmult, true));
            writeField(out, header, Titles.begin(), Titles.end());
            //write a small comment
            out << "## Grid parameters: \n";
            //TODO: finish! 
            return log;
        }

        return (std::string)"WriteHeader: Unknown or unhandled version encountered! Version string: \"" + 
            header.getString(OVFParameter::VersionString) + "\";";
    }

    //and a template binary data writer
    template<typename T>
    struct UintAnalogue {};
    template<> struct UintAnalogue<float> {using type = std::uint32_t;};
    template<> struct UintAnalogue<double> {using type = std::uint64_t;};

    template<typename T>
    inline void WriteBinaryData(std::ostream& out, const OVFVersion& version, const VField& field)
    {
        out<<TestVal<T>;
        T* buff {nullptr};
        if( (boost::endian::order::native == boost::endian::order::big && version == OVFVersion::OVF1) ||
                boost::endian::order::native == boost::endian::order::little )
            buff = field.getDataCopy<T>();
        if(buff != nullptr)
            for( std::size_t i = 0; i < field.curDataPoints(); i++)
                boost::endian::endian_reverse_inplace( *reinterpret_cast<typename UintAnalogue<T>::type*>(buff + i) );
        const char* outBuff = reinterpret_cast<const std::ostream::char_type*>(
                (buff != nullptr)? buff : field.getData<T>());  
        out.write(outBuff, field.curDataPoints() * sizeof(T)/sizeof(char));
        delete[] buff;
    }

    std::string WriteSegment(std::ostream& out, const VField& field) noexcept
    {
        if( !out.good())
            return "WriteSegment: Stream given was not good, aborting!";
        if( !field.isWeaklyAddressable())
            return "WriteSegment: Vector field should at least be weakly addressable, aborting!";
        
        auto version = matchVersionString(field.Header.at<pType::String>(OVFParameter::VersionString));
        out << "# Begin: Segment\n# Begin: Header\n";
        auto log = WriteHeader(out, version, field.Header);
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
                if(!log.empty())
                    log+="\n";
                log += "WriteSegment: somehow got invalid internal data size! Please check 'isWeaklyAddressable' for bugs!";
        }
        out << "# End: Data binary " <<field.curDataInternalSize() << "\n" << "# End: Segment\n";
        out.flush();
        if(!out.good())
        {
            if(!log.empty())
                log += "\n";
            log += "WriteSegment: filesystem error occured while writing the segment!";
        }
        return log;
    }
}

