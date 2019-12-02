//validation stuff for OVFs
#include<regex>
#include<map>
#include"OVFDictionary.h"
#include"VField.h"

namespace VField{
    //shared flags
    constexpr auto commonFlags = std::regex_constants::icase |          //ignore case while matching
                                 std::regex_constants::ECMAScript |
                                 std::regex_constants::optimize;        //optimize for speed, slower construction
    //definitions of OVF version regexes
    enum class OVFVersion{
        OVF0,   //https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_0.0_format.html
        OVF1,   //https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html
        OVF2,   //https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html
        Unknown //self explanatory
    }
    //regexes to fit the OVF version
    const std::map<OVFVersion, std::string> versions {
        //# OOMMF: irregular mesh v0.0
        {OVF0, std::regex("^#\\s*OOMMF\\s*:.*v0.0\\s*$", commonFlags)},
        //# OOMMF: rectangular mesh v1.0
        {OVF1, std::regex("^#\\s*OOMMF\\s*:(.*)v(1.0|0.99|0.0a0)\\s$", commonFlags)},
        //# OOMMF OVF 2.0
        {OVF2, std::regex("^#\\s*OOMMF\\s*OVF\\s*2.0\\s*$", commonFlags)}
    };
    OVFVersion matchVersionString(const std::string& ref)
    {
        for(const auto x: versions)
            if(std::regex_match(ref, x.second))
                return x.first;
        
        //else return unknown
        return OVFVersion::Unknnown;
    }
}
