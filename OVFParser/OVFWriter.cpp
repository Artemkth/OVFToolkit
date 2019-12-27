#include"OVFWriter.h"
#include"OVFDictionary.h"

namespace VField
{
    //first defining the rules for writing out a header using make_array helper template
    inline std::string WriteHeader(std::ostream& out, const OVFHeader& header) noexcept
    {
    }

    std::string WriteSegment(std::ostream& out, const VField& field) noexcept
    {
    }
}

