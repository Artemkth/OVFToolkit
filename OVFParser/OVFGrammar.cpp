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
    using validator = std::tuple<bool, std::string, std::vector<OVFParameter>> (*)(const OVFHeader&);
    //and a method to check if grid is defined
    std::tuple<bool, std::string, std::vector<OVFParameter>> isGridDefined(const OVFHeader& ref)
    {
        const std::string prefix = "Checking if grid was defined:\n";
        std::vector<OVFParameter> problemParams{};
        
        if(!ref.isSet(OVFParameter::Mtype))
            return {false, prefix+"Mesh type was not defined!", {OVFParameter::Mtype}};
        
        if(ref.getMeshType() == OVFHeader::MeshType::irregular)
        {
            if(!ref.isSet(OVFParameter::Pcount))
                return {false, prefix+"In a file with with irregular mesh point count was not specified", {OVFParameter::Pcount}};
            if(ref.getUint(OVFParameter::Pcount) <= 0)
                return {false, prefix+"Non-positive point count was specified for a irregular mesh", {OVFParameter::Pcount}};
            return {true, prefix + "SUCCESS", problemParams};
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
        
        for(const auto& x: rectGridParameters)
        {
            if(!ref.isSet(x))
                problemParams.push_back(x);
        }
        if(!ref.isSet(OVFParameter::VersionString))
            problemParams.push_back(OVFParameter::VersionString);
        else
        {
            if(matchVersionString(ref.get<pType::String>(OVFParameter::VersionString)) == OVFVersion::OVF2)
                if(!ref.isSet(OVFParameter::Vdim))
                    problemParams.push_back(OVFParameter::Vdim);
        }
        
        if(problemParams.size() == 0)
            return {true, prefix + "SUCCESS", {}};
        
        //else form error message
        std::string errMessage{prefix + "Following required parameters to define rectangular grid were not found:"};
        for(const auto& x: problemParams)
        {
            errMessage += "\n\t";
            errMessage += ParameterName(x);
        }
        return {false, errMessage, problemParams};
    }
    //checking physical constrains, i.e. if values are sane
    std::tuple<bool, std::string, std::vector<OVFParameter>> checkPhysicalConstraints(const OVFHeader& ref)
    {
        const std::string prefix = "Checking a sanity of physical values: ";
        std::vector<OVFParameter> problemParams{};
        std::vector<std::string> problems {};
        //TODO: check into nuking 2 reduntant checks here, those are done before ruleset is called in order to get the version
        if(!ref.isSet(OVFParameter::VersionString))
            return{false, prefix+"\n\tVersion string was not set", {OVFParameter::VersionString}};
        //nothing to check for OVF v0.0
        if(matchVersionString(ref.get<pType::String>(OVFParameter::VersionString)) == OVFVersion::OVF0)
            return {true, prefix + "SUCCESS", {}};
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
                    {
                        problemParams.push_back(x);
                        problems.push_back(std::string("Encountered a non-finite value '") + ParameterName(x) + "' = " +
                        std::to_string(val) + "\n");
                    }
                }
        }
        if constexpr(std::numeric_limits<associatedType<pType::Uint>>::has_infinity ||
                     std::numeric_limits<associatedType<pType::Uint>>::has_quiet_NaN )
        {
            for(const auto& x: UINTParamList)
                if(ref.isSet(x))
                {
                    auto val = ref.getFloat(x);
                    if(!std::isfinite(val))
                    {
                        problemParams.push_back(x);
                        problems.push_back(std::string("Encountered a non-finite value '") + ParameterName(x) + "' = " +
                        std::to_string(val) + "\n");
                    }
                }
        }
        auto gridProblems = isGridDefined(ref);
        if( std::get<0>(gridProblems) )
        {
            problems.push_back("Grid parameters were not defined!!");
            for(const auto& x: std::get<2>(gridProblems))
                problemParams.push_back(x);
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
                    {
                        problems.push_back(std::string("The value '") + ParameterName(x) + "' =" + std::to_string(val) + ", was not positively defined!");
                        problemParams.push_back(x);
                    }
                }
                else if(paramIndex(x) == pType::Float)
                {
                    auto val = ref.getFloat(x);
                    if(val <= 0)
                    {
                        problems.push_back(std::string("The value '") + ParameterName(x) + "' =" + std::to_string(val) + ", was not positively defined!");
                        problemParams.push_back(x);
                    }
                }
            }
        }
        
        if(problems.size() == 0)
            return {true, prefix + "SUCCESS", {}};
        
        std::string accum { prefix};
        for(const auto& x: problems)
        {
            accum += "\n\t";
            accum += x;
        }
        
        return {false, accum, problemParams};
    }
    
    //nothing is disallowed lol
    const auto OVF0Rules = std::vector<validator> {};
    //then rules for OVF1
    const auto OVF1Rules = std::vector<validator>{
        [](const OVFHeader& ref) -> std::tuple<bool, std::string, std::vector<OVFParameter>>
        {
            const std::string prefix = "Checking if all required fields were filled";
            std::vector<OVFParameter> problemParams{};
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
            for(const auto& x: RequiredParameters)
                if(!ref.isSet(x))
                    problemParams.push_back(x);
                
            if(problemParams.size() == 0)
                return {true, prefix + "SUCCESS", {}};
            
            //else form error message
            std::string errMessage{"Following required parameters(for OVF 1.0) were not found:"};
            for(const auto& x: problemParams)
            {
                errMessage += "\n\t";
                errMessage += ParameterName(x) ;
            }
            return {false, errMessage, problemParams};
        },
        //check if grid is defined
        isGridDefined,
        //check that the boundary list, if present, is a list of tripples of points
        [](const OVFHeader& ref) -> std::tuple<bool, std::string, std::vector<OVFParameter>> 
        {
            const std::string prefix = "Checking if 'boundarylist' is ill-formed:\n";
            if(!ref.isSet(OVFParameter::Bound))
                return {true, prefix + "SUCCESS", {}}; //nothing to check
            //get the boundary vertex list
            const std::string boundaryList { ref.get<pType::String>(OVFParameter::Bound)};
            //count how many tokens there are, validating if they are convertible to double
            auto cnt = countTokens(boundaryList, [](const std::string& ref){try{std::stod(ref);}catch(const std::logic_error& e){return false;} return true;});
            if( cnt == 0)
                return{false, prefix + "A string in 'boundarylist' contains invalid tokens: \n\t" + boundaryList, {OVFParameter::Bound}};
            if( cnt % 3 != 0)
                return{false, prefix + "Bounding box vortex list should have tripplets of coordinates, " + std::to_string(cnt) + 
                    " values were read in 'boundarylist': \n\t" + boundaryList, {OVFParameter::Bound}};
            if( cnt < 12 )
                return{false, prefix + "Not enough points to set a bounding volume, at least 4 vertices needed, got" + std::to_string(cnt/3) + 
                    " vortexes in 'boundarylist': \n\t" + boundaryList, {OVFParameter::Bound}};
            
            return {true, prefix + "SUCCESS", {}};
        }
    };
    //then rules for OVF2
    const auto OVF2Rules = std::vector<validator>{
        [](const OVFHeader& ref) -> std::tuple<bool, std::string, std::vector<OVFParameter>> 
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
                return {true, prefix + "SUCCESS", {}};
            
            //else form error message
            std::string errMessage{"Following required parameters(for OVF 1.0) were not found:"};
            for(const auto& x: missingList)
            {
                errMessage += "\n\t";
                errMessage += ParameterName(x) ;
            }
            return {false, errMessage, missingList};
        },
        isGridDefined,
        //check if value units has correct number of tokens
        [](const OVFHeader& ref) -> std::tuple<bool, std::string, std::vector<OVFParameter>>
        {
            const std::string prefix = "Checking if 'valueunits' are ill-formed:\n";
            //should not reach here normally
            if(!ref.isSet(OVFParameter::Vunit))
                return {false, prefix + "Value units are not set yet", {OVFParameter::Vunit}};
            if(!ref.isSet(OVFParameter::Vdim))
                return {false, prefix + "Value dimensions are not set yet", {OVFParameter::Vdim}};
            std::size_t num {0};
            if((num = countTokens(ref.get<pType::String>(OVFParameter::Vunit))) != ref.get<pType::Uint>(OVFParameter::Vdim))
                return {false, prefix + "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.get<pType::String>(OVFParameter::Vunit), {OVFParameter::Vunit}};
            return {true, prefix + "SUCCESS", {}};
        },
        //check if value labels has correct number of tokens
        [](const OVFHeader& ref) -> std::tuple<bool, std::string, std::vector<OVFParameter>>
        {
            const std::string prefix = "Checking if 'valuelabels' is ill-formed:\n";
            //should not reach here normally
            if(!ref.isSet(OVFParameter::Vlabels))
                return {false, prefix + "Value labels are not set yet", {OVFParameter::Vlabels}};
            if(!ref.isSet(OVFParameter::Vdim))
                return {false, prefix + "Value dimensions are not set yet", {OVFParameter::Vdim}};
            std::size_t num {countTokens(ref.get<pType::String>(OVFParameter::Vlabels))};
            if(num != 1 && num != ref.get<pType::Uint>(OVFParameter::Vdim))
                return {false, prefix + "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.get<pType::String>(OVFParameter::Vunit), {OVFParameter::Vlabels}};
            return {true, prefix + "SUCCESS", {}};
        }
    };
    //OMEGA map for rulesets
    const std::map<OVFVersion, std::vector<validator>> Ruleset{
        {OVFVersion::OVF0, OVF0Rules},
        {OVFVersion::OVF1, OVF1Rules},
        {OVFVersion::OVF2, OVF2Rules}
    };
    
    //count expected number of points
    std::size_t expectedValueCount(const OVFHeader& ref)
    {
        if(!std::get<0>(isGridDefined(ref)))
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
    
    //appending and adding unique elements
    std::vector<OVFParameter> makeUnion(const std::vector<OVFParameter>& u, const std::vector<OVFParameter>& v)
    {
        std::vector<OVFParameter> accumulator{};
        for(const auto& x: u)
            if(std::find(accumulator.begin(), accumulator.end(), x) == accumulator.end())
                accumulator.push_back(x);
        for(const auto& x: v)
            if(std::find(accumulator.begin(), accumulator.end(), x) == accumulator.end())
                accumulator.push_back(x);
        return accumulator;
    }
    //header validator
    std::tuple<bool, std::string, std::vector<OVFParameter>> ValidateHeader(const OVFHeader& ref)
    {
        //TODO implement
        return{ true, "", {}};
    }
}
