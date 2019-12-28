#include<map>
#include<variant>
#include"OVFVersion.h"
#include"OVFWriter.h"
#include"OVFDictionary.h"

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
    }
}

