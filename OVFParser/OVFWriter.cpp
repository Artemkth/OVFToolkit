#include<map>
#include<variant>
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
            { return ver == OVFVersion::OVF1? "meshunit" : "meshunits"; }
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
    //first defining the rules for writing out a header using make_array helper template
    inline std::string WriteHeader(std::ostream& out, const OVFHeader& header) noexcept
    {
    }

    std::string WriteSegment(std::ostream& out, const VField& field) noexcept
    {
        if( !out.good())
            return "WriteSegment: Stream given was not good, aborting!";
        if( !out.isWeaklyAddressable())
            return "WriteSegment: Vector field should at least be weakly addressable, aborting!";
        
        auto version = matchVersionString(field.Header.at<pType::String>(OVFParameter::VersionString));
        out << "# Begin: Segment\n# Begin: Header\n";
        auto log = WriteHeader(out, field.Header);
        out << "# End: Header\n# Begin: Data binary "<<field.curDataInternalSize() << "\n";
        switch(field.curDataInternalSize())
        {
            case(4):
            {
                out<<TestVal<float>;
                float* buff {nullptr};
                if( (boost::endian::order::native == boost::endian::order::big && version == OVFVersion::OVF1) ||
                    boost::endian::ordern::native == boost::endian::order::little )
                    buff = field.getDataCopy<float>();
                if(buff != nullptr)
                    for( std::size_t i = 0; i < field.curDataPoints(); i++)
                        boost::endian::reverse_inplace( *reinterpret_cast<std::uint32_t*>(buff + i) );
                const char* outBuff = reinterpret_cast<const std::ostream::charT*>(
                        (buff != nullptr)? buff, field.getData<float>());  
                out.write(outBuff, field.curDataPoints() * sizeof(float)/sizeof(char));
                delete[] buff;
                break;
            }
            case(8):
            {
                out<<TestVal<double>;
                double* buff {nullptr};
                if( (boost::endian::order::native == boost::endian::order::big && version == OVFVersion::OVF1) ||
                    boost::endian::ordern::native == boost::endian::order::little )
                    buff = field.getDataCopy<double>();
                if(buff != nullptr)
                    for( std::size_t i = 0; i < field.curDataPoints(); i++)
                        boost::endian::reverse_inplace( *reinterpret_cast<std::uint64_t*>(buff + i) );
                const char* outBuff = reinterpret_cast<const std::ostream::charT*>(
                        (buff != nullptr)? buff, field.getData<double>());  
                out.write(outBuff, field.curDataPoints() * sizeof(double)/sizeof(char));
                delete[] buff;
                break;
            }
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

