//validation stuff for OVFs
#include<regex>
#include<map>
#include<vector>
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
        if(!ref.isSet(OVFParameter::Mtype))
            return {false, "Mesh type was not defined!"};
        
        if(ref.getMeshType() == OVFHeader::MeshType::irregular)
        {
            if(!ref.isSet(OVFParameter::Pcount) )
                return {false, "In a file with with irregular mesh point count was not specified"};
            return {true, ""};
        }
        
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
        
        if(missingList.size() == 0)
            return {true, ""};
        
        //else form error message
        std::string errMessage{"Following required parameters to define rectangular grid were not found:"};
        for(const auto& x: missingList)
        {
            errMessage += "\n\t";
            errMessage += ParameterName(x) ;
        }
        return {false, errMessage};
    }
    
    //nothing is disallowed lol
    const std::array<validator, 0> OVF0Rules {};
    //then rules for OVF1
    const auto OVF1Rules = DictionaryHelpers::make_array<validator>(
        [](const OVFHeader& ref) -> std::pair<bool, std::string> 
        {
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
                return {true, ""};
            
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
            if(!ref.isSet(OVFParameter::Bound))
                return {true, ""}; //nothing to check
            //get the boundary vertex list
            const std::string boundaryList { ref.get<pType::String>(OVFParameter::Bound)};
            //count how many tokens there are, validating if they are convertible to double
            auto cnt = countTokens(boundaryList, [](const std::string& ref){try{std::stod(ref);}catch(const std::logic_error& e){return false;} return true;});
            if( cnt == 0)
                return{false, "A string in 'boundarylist' contains invalid tokens: \n\t" + boundaryList};
            if( cnt % 3 != 0)
                return{false, "Bounding box vortex list should have tripplets of coordinates, " + std::to_string(cnt) + 
                    " values were read in 'boundarylist': \n\t" + boundaryList};
            if( cnt < 12 )
                return{false, "Not enough points to set a bounding volume, at least 4 vertices needed, got" + std::to_string(cnt/3) + 
                    " vortexes in 'boundarylist': \n\t" + boundaryList};
            
            return {true, ""};
        }
    );
    //then rules for OVF2
    const auto OVF2Rules = DictionaryHelpers::make_array<validator>(
        [](const OVFHeader& ref) -> std::pair<bool, std::string> 
        {
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
                return {true, ""};
            
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
            //should not reach here normally
            if(!ref.isSet(OVFParameter::Vunit))
                return {false, "Value units are not set yet"};
            if(!ref.isSet(OVFParameter::Vdim))
                return {false, "Value dimensions are not set yet"};
            std::size_t num {0};
            if((num = countTokens(ref.get<pType::String>(OVFParameter::Vunit))) != ref.get<pType::Uint>(OVFParameter::Vdim))
                return {false, "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.get<pType::String>(OVFParameter::Vunit)};
            return {true, ""};
        },
        //check if value labels has correct number of tokens
        [](const OVFHeader& ref) -> std::pair<bool, std::string>
        {
            //should not reach here normally
            if(!ref.isSet(OVFParameter::Vlabels))
                return {false, "Value labels are not set yet"};
            if(!ref.isSet(OVFParameter::Vdim))
                return {false, "Value dimensions are not set yet"};
            std::size_t num {countTokens(ref.get<pType::String>(OVFParameter::Vlabels))};
            if(num != 1 && num != ref.get<pType::Uint>(OVFParameter::Vdim))
                return {false, "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.get<pType::String>(OVFParameter::Vunit)};
            return {true, ""};
        }
    );
}
