#pragma once
//define versions of OVF files
#include<regex>

namespace VField{
    //definitions of OVF version regexes
    enum class OVFVersion{
        OVF0,   //https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_0.0_format.html
        OVF1,   //https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html
        OVF2,   //https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html
        Unknown //self explanatory
    };
    //regexes to fit the OVF version
    const std::map<OVFVersion, std::regex> versions {
        //# OOMMF: irregular mesh v0.0
        {OVFVersion::OVF0, std::regex("^#\\s*OOMMF\\s*:.*\\s+v0.0\\s*$", std::regex_constants::icase | 
                                                                         std::regex_constants::ECMAScript)},
        //# OOMMF: rectangular mesh v1.0
        {OVFVersion::OVF1, std::regex("^#\\s*OOMMF\\s*:(.*)\\s+v(1.0|0.99|0.0a0)\\s$",
                                                                         std::regex_constants::icase | 
                                                                         std::regex_constants::ECMAScript)},
        //# OOMMF OVF 2.0
        {OVFVersion::OVF2, std::regex("^#\\s*OOMMF\\s+OVF\\s+2.0\\s*$",  std::regex_constants::icase | 
                                                                         std::regex_constants::ECMAScript)}
    };
    OVFVersion matchVersionString(const std::string& ref)
    {
        for(const auto& x: versions)
            if(std::regex_match(ref, x.second))
                return x.first;
        
        //else return unknown
        return OVFVersion::Unknown;
    }
}
