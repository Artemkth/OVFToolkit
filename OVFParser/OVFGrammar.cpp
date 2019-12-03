//validation stuff for OVFs
#include<regex>
#include<map>
#include<vector>
#include<cmath>
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
    };
    //regexes to fit the OVF version
    const std::map<OVFVersion, std::regex> versions {
        //# OOMMF: irregular mesh v0.0
        {OVFVersion::OVF0, std::regex("^#\\s*OOMMF\\s*:.*\\s+v0.0\\s*$", commonFlags)},
        //# OOMMF: rectangular mesh v1.0
        {OVFVersion::OVF1, std::regex("^#\\s*OOMMF\\s*:(.*)\\s+v(1.0|0.99|0.0a0)\\s$", commonFlags)},
        //# OOMMF OVF 2.0
        {OVFVersion::OVF2, std::regex("^#\\s*OOMMF\\s+OVF\\s+2.0\\s*$", commonFlags)}
    };
    OVFVersion matchVersionString(const std::string& ref)
    {
        for(const auto& x: versions)
            if(std::regex_match(ref, x.second))
                return x.first;
        
        //else return unknown
        return OVFVersion::Unknown;
    }
    //tokenization regex
    //splits the string into expression separated by while spaces,
    //unless some part is taken in quotes, in which case it will be counted as a separate token
    // \s*(".*?"|[^\s]+)(?:\s+|$)
    std::regex tokenRegex("^\\s*(\".*?\"|[^\\s]+)(?:\\s+|$)", commonFlags);
    
    std::size_t countTokens(std::string str, bool (*validateToken)(const std::string&) = [](const std::string&){return true;})
    {
        std::size_t count {0};
        std::smatch sm;
        while(std::regex_search(str, sm, tokenRegex))
        {
            if(!validateToken(sm[1].str()))//check the submatch group 1, non white space characters
                return 0;
            count++;
            str = sm.suffix();
        }
        return count;
    }
    
    //and then version rulesets
    using validator = std::pair<bool, std::string> (*)(const OVFHeader&);
    //and a method to check if grid is defined
    std::pair<bool, std::string> isGridDefined(const OVFHeader& ref)
    {
        const std::string prefix = "Checking if grid was defined:\n";
        
        if(!ref.isSet(OVFParameter::Mtype))
            return {false, prefix+"Mesh type was not defined!"};
        
        if(ref.getMeshType() == OVFHeader::MeshType::irregular)
        {
            if(!ref.isSet(OVFParameter::Pcount) )
                return {false, prefix+"In a file with with irregular mesh point count was not specified"};
            if(ref.getUint(OVFParameter::Pcount) <= 0)
                return {false, prefix+"Non-positive point count was specified for a irregular mesh"};
            return {true, prefix + "SUCCESS"};
        }
        
        //next check is for regular mesh parameters only
        const auto rectGridParameters = DictionaryHelpers::make_array(
            OVFParameter::Xbase,
            OVFParameter::Ybase,
            OVFParameter::Zbase,
            OVFParameter::Xstep,
            OVFParameter::Ystep,
            OVFParameter::Zstep,
            OVFParameter::Xnodes,
            OVFParameter::Ynodes,
            OVFParameter::Znodes
        );
        
        std::vector<OVFParameter> missingList{};
        for(const auto& x: rectGridParameters)
        {
            if(!ref.isSet(x))
                missingList.push_back(x);
        }
        if(!ref.isSet(OVFParameter::VersionString))
            missingList.push_back(OVFParameter::VersionString);
        else
        {
            if(matchVersionString(ref.get<pType::String>(OVFParameter::VersionString)) == OVFVersion::OVF2)
                if(!ref.isSet(OVFParameter::Vdim))
                    missingList.push_back(OVFParameter::Vdim);
        }
        
        if(missingList.size() == 0)
            return {true, prefix + "SUCCESS"};
        
        //else form error message
        std::string errMessage{prefix + "Following required parameters to define rectangular grid were not found:"};
        for(const auto& x: missingList)
        {
            errMessage += "\n\t";
            errMessage += ParameterName(x);
        }
        return {false, errMessage};
    }
    //checking physical constrains, i.e. if values are sane
    std::pair<bool, std::string> checkPhysicalConstraints(const OVFHeader& ref)
    {
        const std::string prefix = "Checking a sanity of physical values: ";
        std::vector<std::string> problems {};
        //TODO: check into nuking 2 reduntant checks here, those are done before ruleset is called in order to get the version
        if(!ref.isSet(OVFParameter::VersionString))
            return{false, prefix+"\n\tVersion string was not set"};
        //nothing to check for OVF v0.0
        if(matchVersionString(ref.get<pType::String>(OVFParameter::VersionString)) == OVFVersion::OVF0)
            return {true, prefix + "SUCCESS"};
        //otherwise checking all the parameters
        //first check is for parameters being limited
        if constexpr(std::numeric_limits<associatedType<pType::Float>>::has_infinity ||
                     std::numeric_limits<associatedType<pType::Float>>::has_quiet_NaN )
        {
            for(const auto& x: FPParamList)
                if(ref.isSet(x))
                {
                    auto val = ref.getFloat(x);
                    if(!std::isfinite(val))
                        problems.push_back(std::string("Encountered a non-finite value '") + ParameterName(x) + "' = " +
                        std::to_string(val) + "\n");
                }
        }
        if constexpr(std::numeric_limits<associatedType<pType::Uint>>::has_infinity ||
                     std::numeric_limits<associatedType<pType::Uint>>::has_quiet_NaN )
        {
            for(const auto& x: UINTParamList)
                if(ref.isSet(x))
                {
                    auto val = ref.getUint(x);
                    if(!std::isfinite(val))
                        problems.push_back(std::string("Encountered a non-finite value '") + ParameterName(x) + "' = " +
                        std::to_string(val) + "\n");
                }
        }
        if(!isGridDefined(ref).first)
        {
            problems.push_back("Grid parameters were not defined!!");
        }
        else 
        {
            constexpr auto posDefined = DictionaryHelpers::make_array(
                OVFParameter::Xnodes,
                OVFParameter::Ynodes,
                OVFParameter::Znodes,
                OVFParameter::Xstep,
                OVFParameter::Ystep,
                OVFParameter::Zstep);
            static_assert(!DictionaryHelpers::isSubset(posDefined, DictionaryHelpers::join(FPParamList, UINTParamList)), "Only floating point and UINT params are allowed");
            //check if required params are positively defined
            for(const auto& x: posDefined)
            {
                if(paramIndex(x) == pType::Uint)
                {
                    auto val = ref.getUint(x);
                    if(val <= 0)
                        problems.push_back(std::string("The value '") + ParameterName(x) + "' =" + std::to_string(val) + ", was not positively defined!");
                }
                else if(paramIndex(x) == pType::Float)
                {
                    auto val = ref.getUint(x);
                    if(val <= 0)
                        problems.push_back((std::string)"The value '" + ParameterName(x) + "' =" + std::to_string(val) + ", was not positively defined!");
                }
            }
        }
        
        if(problems.size() == 0)
            return {true, prefix + "SUCCESS"};
        
        std::string accum { prefix};
        for(const auto& x: problems)
        {
            accum += "\n\t";
            accum += x;
        }
        
        return {false, accum};
    }
    
    //nothing is disallowed lol
    const std::array<validator, 0> OVF0Rules {};
    //then rules for OVF1
    const auto OVF1Rules = DictionaryHelpers::make_array<validator>(
        [](const OVFHeader& ref) -> std::pair<bool, std::string>
        {
            const std::string prefix = "Checking if all required fields were filled";
            //check if all required field are present
            const auto RequiredParameters = DictionaryHelpers::make_array(
                OVFParameter::Title,
                OVFParameter::Munit,
                OVFParameter::Vunit,
                OVFParameter::Vmult,
                OVFParameter::Xmin,
                OVFParameter::Xmax,
                OVFParameter::Ymin,
                OVFParameter::Ymax,
                OVFParameter::Zmin,
                OVFParameter::Zmax
            );
            std::vector<OVFParameter> missingList{};
            for(const auto& x: RequiredParameters)
                if(!ref.isSet(x))
                    missingList.push_back(x);
                
            if(missingList.size() == 0)
                return {true, prefix + "SUCCESS"};
            
            //else form error message
            std::string errMessage{"Following required parameters(for OVF 1.0) were not found:"};
            for(const auto& x: missingList)
            {
                errMessage += "\n\t";
                errMessage += ParameterName(x) ;
            }
            return {false, errMessage};
        },
        //check if grid is defined
        isGridDefined,
        //check that the boundary list, if present, is a list of tripples of points
        [](const OVFHeader& ref) -> std::pair<bool, std::string> 
        {
            const std::string prefix = "Checking if 'boundarylist' is ill-formed:\n";
            if(!ref.isSet(OVFParameter::Bound))
                return {true, prefix + "SUCCESS"}; //nothing to check
            //get the boundary vertex list
            const std::string boundaryList { ref.get<pType::String>(OVFParameter::Bound)};
            //count how many tokens there are, validating if they are convertible to double
            auto cnt = countTokens(boundaryList, [](const std::string& ref){try{std::stod(ref);}catch(const std::logic_error& e){return false;} return true;});
            if( cnt == 0)
                return{false, prefix + "A string in 'boundarylist' contains invalid tokens: \n\t" + boundaryList};
            if( cnt % 3 != 0)
                return{false, prefix + "Bounding box vortex list should have tripplets of coordinates, " + std::to_string(cnt) + 
                    " values were read in 'boundarylist': \n\t" + boundaryList};
            if( cnt < 12 )
                return{false, prefix + "Not enough points to set a bounding volume, at least 4 vertices needed, got" + std::to_string(cnt/3) + 
                    " vortexes in 'boundarylist': \n\t" + boundaryList};
            
            return {true, prefix + "SUCCESS"};
        }
    );
    //then rules for OVF2
    const auto OVF2Rules = DictionaryHelpers::make_array<validator>(
        [](const OVFHeader& ref) -> std::pair<bool, std::string> 
        {
            const std::string prefix = "Checking if all required fields were filled";
            //check if all required field are present
            const auto RequiredParameters = DictionaryHelpers::make_array(
                OVFParameter::Title,
                OVFParameter::Munit,
                OVFParameter::Vdim,
                OVFParameter::Vunit,
                OVFParameter::Vlabels,
                OVFParameter::Xmin,
                OVFParameter::Xmax,
                OVFParameter::Ymin,
                OVFParameter::Ymax,
                OVFParameter::Zmin,
                OVFParameter::Zmax
            );
            std::vector<OVFParameter> missingList{};
            for(const auto& x: RequiredParameters)
                if(!ref.isSet(x))
                    missingList.push_back(x);
                
            if(missingList.size() == 0)
                return {true, prefix + "SUCCESS"};
            
            //else form error message
            std::string errMessage{"Following required parameters(for OVF 1.0) were not found:"};
            for(const auto& x: missingList)
            {
                errMessage += "\n\t";
                errMessage += ParameterName(x) ;
            }
            return {false, errMessage};
        },
        isGridDefined,
        //check if value units has correct number of tokens
        [](const OVFHeader& ref) -> std::pair<bool, std::string>
        {
            const std::string prefix = "Checking if 'valueunits' are ill-formed:\n";
            //should not reach here normally
            if(!ref.isSet(OVFParameter::Vunit))
                return {false, prefix + "Value units are not set yet"};
            if(!ref.isSet(OVFParameter::Vdim))
                return {false, prefix + "Value dimensions are not set yet"};
            std::size_t num {0};
            if((num = countTokens(ref.get<pType::String>(OVFParameter::Vunit))) != ref.get<pType::Uint>(OVFParameter::Vdim))
                return {false, prefix + "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.get<pType::String>(OVFParameter::Vunit)};
            return {true, prefix + "SUCCESS"};
        },
        //check if value labels has correct number of tokens
        [](const OVFHeader& ref) -> std::pair<bool, std::string>
        {
            const std::string prefix = "Checking if 'valuelabels' is ill-formed:\n";
            //should not reach here normally
            if(!ref.isSet(OVFParameter::Vlabels))
                return {false, prefix + "Value labels are not set yet"};
            if(!ref.isSet(OVFParameter::Vdim))
                return {false, prefix + "Value dimensions are not set yet"};
            std::size_t num {countTokens(ref.get<pType::String>(OVFParameter::Vlabels))};
            if(num != 1 && num != ref.get<pType::Uint>(OVFParameter::Vdim))
                return {false, prefix + "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.get<pType::String>(OVFParameter::Vunit)};
            return {true, prefix + "SUCCESS"};
        }
    );
    
    //count expected number of points
    std::size_t expectedValueCount(const OVFHeader& ref)
    {
        if(!isGridDefined(ref).first)
            return 0u;
        auto version = matchVersionString(ref.get<pType::String>(OVFParameter::VersionString));
        if(version == OVFVersion::OVF0)
            return 0u;
        std::size_t dimensionality = ((ref.getMeshType() == OVFHeader::MeshType::irregular)? 3:0) +
                                     ((version == OVFVersion::OVF2) ? ref.getUint(OVFParameter::Vdim) : 3);
        if(ref.getMeshType() == OVFHeader::MeshType::irregular)
            return dimensionality * ref.getUint(OVFParameter::Pcount);
        else
            return dimensionality * ref.getUint(OVFParameter::Xnodes) * ref.getUint(OVFParameter::Ynodes) * ref.getUint(OVFParameter::Znodes);
    }
    
    //header validator
    //std::pair<bool,     
}
