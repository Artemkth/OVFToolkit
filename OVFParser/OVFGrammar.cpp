//validation stuff for OVFs
#include<regex>
#include<map>
#include<vector>
#include<cmath>
#include"OVFDictionary.h"
#include"VField.h"
#include"OVFVersion.h"

namespace VField{
    //shared flags
    constexpr auto commonFlags = std::regex_constants::icase |          //ignore case while matching
                                 std::regex_constants::ECMAScript |
                                 std::regex_constants::optimize;        //optimize for speed, slower construction
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
            if(matchVersionString(ref.at<pType::String>(OVFParameter::VersionString)) == OVFVersion::OVF2)
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
        if(matchVersionString(ref.at<pType::String>(OVFParameter::VersionString)) == OVFVersion::OVF0)
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
            const std::string boundaryList { ref.at<pType::String>(OVFParameter::Bound)};
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
        //TODO: implement checking for version string having same mesh type specified!
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
            if((num = countTokens(ref.at<pType::String>(OVFParameter::Vunit))) != ref.at<pType::Uint>(OVFParameter::Vdim))
                return {false, prefix + "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.at<pType::String>(OVFParameter::Vunit), {OVFParameter::Vunit}};
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
            std::size_t num {countTokens(ref.at<pType::String>(OVFParameter::Vlabels))};
            if(num != 1 && num != ref.at<pType::Uint>(OVFParameter::Vdim))
                return {false, prefix + "Unexpected number of tokens: " + std::to_string(num) + " in parsing value labels: \n\t" + ref.at<pType::String>(OVFParameter::Vunit), {OVFParameter::Vlabels}};
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
        auto version = matchVersionString(ref.at<pType::String>(OVFParameter::VersionString));
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
    //returns a set of unique elements of u&v
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
    //append unique values, skips the initial check
    void appendUnique(std::vector<OVFParameter>& u, const std::vector<OVFParameter>& v)
    {
        for(const auto& x: v)
            if(std::find(u.begin(), u.end(), x) == u.end())
                u.push_back(x);
    }
    //full header validator
    std::tuple<bool, std::string, std::vector<OVFParameter>> ValidateHeader(const OVFHeader& ref)
    {
        bool valid {true};
        std::string log{"Checking the header of a file:"};
        std::vector<OVFParameter> problematicVars {};
        if(!ref.isSet(OVFParameter::VersionString))
            return{ false, log + " version string was not set, aborting!", {OVFParameter::VersionString}};
        //else execute the correct ruleset
        const auto version = matchVersionString(ref.at<pType::String>(OVFParameter::VersionString));
        if(Ruleset.find(version) == Ruleset.end())
            return{ false, log + " version reported does not have a ruleset implemented!", {OVFParameter::VersionString}};
        //otherwise it is safe to execute ruleset
        const auto& rules = Ruleset.at(version);
        for(const auto& rule: rules)
        {
            auto checkResult = rule(ref);
            valid &= std::get<0>(checkResult);
            log = log + '\n' + std::get<1>(checkResult);
            //no need to check for uniqueness before, initial array satisfies it by being empty
            appendUnique(problematicVars, std::get<2>(checkResult));
        }
        //TODO: add verification that header has the same mesh type as file title for OVF1!
        return{ valid, log, problematicVars};
    }
    
    //method telling if current data is isAddressable, i.e. if data structure within array is known
    bool VField::isAddressable() const
    {
        //if it is impossible to calculate number of values we already are in a bust
        const auto expected = expectedValueCount(Header);
        if(expected == 0)
            return false;
        //else check if expected value count is consistent with internal array
        if(expected != curDataPoints())
            return false;
        if(curDataPoints() == 0)
            return false;
        
        return true;
    }
    //and same for weekly addressable, i.e. there is enough data to traverse internal array, but it ends abruptly
    bool VField::isWeaklyAddressable() const
    {
        if(!Header.isSet(OVFParameter::VersionString))
            return false;
        auto version = matchVersionString(Header.at<pType::String>(OVFParameter::VersionString));
        if(curDataPoints() == 0 || version == OVFVersion::Unknown || !Header.isSet(OVFParameter::Mtype) || (version == OVFVersion::OVF2 && !Header.isSet(OVFParameter::Vdim)) )
            return false;
        const auto dim = ((Header.getMeshType() == OVFHeader::MeshType::irregular)? 3:0) +
                         ((version == OVFVersion::OVF1) ? 3 : Header.getUint(OVFParameter::Vdim));
        return curDataPoints() % dim == 0;
    }
    //return dimensionality
    inline std::size_t VField::pntDimension() const noexcept
    {
        if(!isWeaklyAddressable())
            return 0u;
        auto version = matchVersionString(Header.at<pType::String>(OVFParameter::VersionString));
        return ((Header.getMeshType() == OVFHeader::MeshType::irregular)? 3:0) +
               ((version == OVFVersion::OVF1) ? 3 : Header.getUint(OVFParameter::Vdim));
    }
    //return number of points and such
    std::size_t VField::pntCount() const noexcept
    {
        if(!isWeaklyAddressable())
            return 0u;
        return curDataPoints() / pntDimension(); //guaranteed to have 0 remainder
    }
    
    //implementation of validator from VField itself, checks both header and data
    bool VField::isValid()
    {
        //first check our own header
        const bool isHeaderValid = Header.validate();
        //great if it is valid, but also need to check if data is there=
        return isHeaderValid && isAddressable();
    }
    
    //report generator
    std::string VField::ValidationReport()
    {
        return (std::string)"Generating a VField class validation report: \n" +
               /*+*/        Header.ValidationReport() +
               /*+*/        "The data is compliant with describing header: \"" + (isAddressable()? "true":"false") + '\"';
    }
    
    ///////////////////
    //Deduction rules//
    ///////////////////
    //types for substitution pair generation, first 'bool' is if generation was successfull
    template<pType p>
    using sub_pair_t = std::pair<bool, associatedType_t<p>>;
    template<pType p>
    using sub_func_t = sub_pair_t<p> (*) (const VField&);
    
    //maps with default values
    const std::map< OVFParameter, sub_func_t<pType::Float> > FPDefaults {
        {   //default multiplier for value is 1.
            OVFParameter::Vmult,
            [](const VField&) -> sub_pair_t<pType::Float> {
                return {true, 1. };
            }
        },
        //default value for the base are at (0, 0, 0)
        {
            OVFParameter::Xbase,
            [](const VField&) -> sub_pair_t<pType::Float> {
                return {true, 0. };
            }
        },
        {
            OVFParameter::Ybase,
            [](const VField&) -> sub_pair_t<pType::Float> {
                return {true, 0. };
            }
        },
        {
            OVFParameter::Zbase,
            [](const VField&) -> sub_pair_t<pType::Float> {
                return {true, 0. };
            }
        }
    };
    const std::map< OVFParameter, sub_func_t<pType::Uint> > UINTDefaults {
        //nothing to have default values of yet
    };
    const std::map< OVFParameter, sub_func_t<pType::String> > StringDefaults {
        {
            OVFParameter::Title, 
            [](const VField& ref) -> sub_pair_t<pType::String>{
                if(!ref.Header.isSet(OVFParameter::VersionString))
                    return {false, ""};
                auto version = matchVersionString(ref.Header.getString(OVFParameter::VersionString));
                std::size_t dim {3};
                if(version == OVFVersion::OVF2 && !ref.Header.isSet(OVFParameter::Vdim))
                    return {false, ""};
                else if(version == OVFVersion::OVF2)
                    dim = ref.Header.getUint(OVFParameter::Vdim);
                return {true, (std::string)"Indescript " + std::to_string( dim) + "-dimensional vector field"};
            }
        },
        {
            OVFParameter::Munit,
            [](const VField&) -> sub_pair_t<pType::String>{
                return {true, "m m m"};
            }
        },
        {
            OVFParameter::Vlabels,
            [](const VField& ref) -> sub_pair_t<pType::String>{
                if(!ref.Header.isSet(OVFParameter::VersionString))
                    return {false, ""};
                auto version = matchVersionString(ref.Header.getString(OVFParameter::VersionString));
                std::size_t dim {3};
                if(version == OVFVersion::OVF2 && !ref.Header.isSet(OVFParameter::Vdim))
                    return {false, ""};
                else if(version == OVFVersion::OVF2)
                    dim = ref.Header.getUint(OVFParameter::Vdim);
                //and now form labels
                const char tradIndices[] {'x', 'y', 'z'};
                std::string labels{""};
                if(dim <= 3)
                {
                    for(std::size_t i = 0; i < dim; i++)
                    {
                        labels += 'A';
                        labels += tradIndices[i];
                        if( i == dim -1)
                            labels += ' ';
                    }
                }
                else
                {
                    for(std::size_t i = 0; i < dim; i++)
                    {
                        labels += 'A';
                        labels += std::to_string(i+1);
                        if( i == dim -1)
                            labels += ' ';
                    }
                }
                return {true, labels};
            }
        },
        {
            OVFParameter::Vunit,
            [](const VField& ref) -> sub_pair_t<pType::String>{
                if(!ref.Header.isSet(OVFParameter::VersionString))
                    return {false, ""};
                auto version = matchVersionString(ref.Header.getString(OVFParameter::VersionString));
                std::size_t dim {3};
                if(version == OVFVersion::OVF2 && !ref.Header.isSet(OVFParameter::Vdim))
                    return {false, ""};
                else if(version == OVFVersion::OVF2)
                    dim = ref.Header.getUint(OVFParameter::Vdim);
                //and for units
                std::string labels{""};
                for(std::size_t i = 0; i < dim; i++)
                {
                    labels += "\"a.u.\"";
                    if( i == dim -1)
                        labels += ' ';
                }
                return {true, labels};
            }
        },
        {
            OVFParameter::Bound,
            [](const VField&) -> sub_pair_t<pType::String>{
                return {true, ""};
            }
        }
    };
    
    template<typename T>
    inline T norm(const T* arr, std::size_t n)
    {
        T ret = static_cast<T>(0.);
        for(std::size_t i = 0; i < n; i++)
            ret += *(arr + i) * *(arr + i);
        return sqrt(ret);
    }
    
    //limits for a point
    inline sub_pair_t<pType::Float> coordMin( const VField& ref, const std::size_t& coordIndex)
    {
        //first check if data is accessible, rule doesn't work without it
        if(!ref.isAddressable() || coordIndex > 2)
            return {false, 0.};
        if(ref.Header.getMeshType() == OVFHeader::MeshType::rectangular)
        {
            switch(coordIndex){
                case(0):
                    return {true, ref.Header.getFloat(OVFParameter::Xbase) - ref.Header.getFloat(OVFParameter::Xstep)/2};
                case(1):
                    return {true, ref.Header.getFloat(OVFParameter::Ybase) - ref.Header.getFloat(OVFParameter::Ystep)/2};
                case(2):
                    return {true, ref.Header.getFloat(OVFParameter::Zbase) - ref.Header.getFloat(OVFParameter::Zstep)/2};
                default:
                    return {false, 0.};
            }
        }

        //else need to calculate it for non-rectangular grid :'(
        if(!ref.Header.isSet(OVFParameter::VersionString))
            return {false, 0};
        if(ref.curDataInternalSize() == 4)
            return {true, std::min_element(ref.cbegin<float>(), ref.cend<float>(), [&](const float* arr1, const float* arr2) {return arr1[coordIndex] < arr2[coordIndex]; })[coordIndex]};
        else if(ref.curDataInternalSize() == 8)
            return {true, std::min_element(ref.cbegin<double>(), ref.cend<double>(), [&](const double* arr1, const double* arr2) {return arr1[coordIndex] < arr2[coordIndex]; })[coordIndex]};
        return{false, 0};
    }

    inline sub_pair_t<pType::Float> coordMax( const VField& ref, const std::size_t& coordIndex)
    {
        //first check if data is accessible, rule doesn't work without it
        if(!ref.isAddressable() || coordIndex > 2)
            return {false, 0.};
        if(ref.Header.getMeshType() == OVFHeader::MeshType::rectangular)
        {
            switch(coordIndex){
                case(0):
                    return {true, ref.Header.getFloat(OVFParameter::Xbase) + ref.Header.getFloat(OVFParameter::Xstep) * (0.5 + ref.Header.getUint(OVFParameter::Xnodes))};
                case(1):
                    return {true, ref.Header.getFloat(OVFParameter::Ybase) + ref.Header.getFloat(OVFParameter::Ystep) * (0.5 + ref.Header.getUint(OVFParameter::Ynodes))};
                case(2):
                    return {true, ref.Header.getFloat(OVFParameter::Zbase) + ref.Header.getFloat(OVFParameter::Zstep) * (0.5 + ref.Header.getUint(OVFParameter::Znodes))};
                default:
                    return {false, 0.};
            }
        }

        //else need to calculate it for non-rectangular grid :'(
        if(!ref.Header.isSet(OVFParameter::VersionString))
            return {false, 0};
        if(ref.curDataInternalSize() == 4)
            return {true, std::max_element(ref.cbegin<float>(), ref.cend<float>(), [&](const float* arr1, const float* arr2) {return arr1[coordIndex] > arr2[coordIndex]; })[coordIndex]};
        else if(ref.curDataInternalSize() == 8)
            return {true, std::max_element(ref.cbegin<double>(), ref.cend<double>(), [&](const double* arr1, const double* arr2) {return arr1[coordIndex] > arr2[coordIndex]; })[coordIndex]};
        return{false, 0};
    }
    
    const std::map< OVFParameter, sub_func_t<pType::Float> > FPDeduction{
        //TODO: look into templating those!
        {
            OVFParameter::Vmin,
            [](const VField& ref) -> sub_pair_t<pType::Float>{
                associatedType_t<pType::Float> minVal {};
                //first check if data is accessible, rule doesn't work without it
                if(!ref.isAddressable())
                    return {false, minVal};
                //then check what is a dimension of argument
                if(!ref.Header.isSet(OVFParameter::VersionString))
                    return {false, minVal};
                auto version = matchVersionString(ref.Header.getString(OVFParameter::VersionString));
                std::size_t val_dim {3};
                if(version == OVFVersion::OVF2 && !ref.Header.isSet(OVFParameter::Vdim))
                    return {false, minVal};
                else if(version == OVFVersion::OVF2)
                    val_dim = ref.Header.getUint(OVFParameter::Vdim);
                const std::size_t offset {
                    static_cast<std::size_t>(ref.Header.getMeshType() == OVFHeader::MeshType::rectangular ? 0 : 3)
                };

                if(ref.curDataInternalSize() == 4)
                    return {true, norm(*std::min_element(ref.cbegin<float>(), ref.cend<float>(), [&](const float* arr1, const float* arr2){return norm(arr1+offset, val_dim) < norm(arr2+offset, val_dim);}) + offset, val_dim)};
                else if(ref.curDataInternalSize() == 8)
                    return {true, norm(*std::min_element(ref.cbegin<double>(), ref.cend<double>(), [&](const double* arr1, const double* arr2){return norm(arr1+offset, val_dim) < norm(arr2+offset, val_dim);}) + offset, val_dim)};
                return {false, minVal};
            }
        },
        {
            OVFParameter::Vmax,
            [](const VField& ref) -> sub_pair_t<pType::Float>{
                associatedType_t<pType::Float> maxVal {};
                //first check if data is accessible, rule doesn't work without it
                if(!ref.isAddressable())
                    return {false, maxVal};
                //then check what is a dimension of argument
                if(!ref.Header.isSet(OVFParameter::VersionString))
                    return {false, maxVal};
                auto version = matchVersionString(ref.Header.getString(OVFParameter::VersionString));
                std::size_t val_dim {3};
                if(version == OVFVersion::OVF2 && !ref.Header.isSet(OVFParameter::Vdim))
                    return {false, maxVal};
                else if(version == OVFVersion::OVF2)
                    val_dim = ref.Header.getUint(OVFParameter::Vdim);
                const std::size_t offset {
                    static_cast<std::size_t>(ref.Header.getMeshType() == OVFHeader::MeshType::rectangular ? 0 : 3)
                };
                if(ref.curDataInternalSize() == 4)
                    return {true, norm(*std::max_element(ref.cbegin<float>(), ref.cend<float>(), [&](const float* arr1, const float* arr2){return norm(arr1+offset, val_dim) > norm(arr2+offset, val_dim);}) + offset, val_dim)};
                else if(ref.curDataInternalSize() == 8)
                    return {true, norm(*std::max_element(ref.cbegin<double>(), ref.cend<double>(), [&](const double* arr1, const double* arr2){return norm(arr1+offset, val_dim) > norm(arr2+offset, val_dim);}) + offset, val_dim)};
                return {false, maxVal};
            }
        },
        {
            OVFParameter::Xmin,
            [](const VField& ref) -> sub_pair_t<pType::Float>{ return coordMin(ref, 0); }
        },
        {
            OVFParameter::Ymin,
            [](const VField& ref) -> sub_pair_t<pType::Float>{ return coordMin(ref, 1); }
        },
        {
            OVFParameter::Zmin,
            [](const VField& ref) -> sub_pair_t<pType::Float>{ return coordMin(ref, 2); }
        },
        {
            OVFParameter::Xmax,
            [](const VField& ref) -> sub_pair_t<pType::Float>{ return coordMax(ref, 0); }
        },
        {
            OVFParameter::Ymax,
            [](const VField& ref) -> sub_pair_t<pType::Float>{ return coordMax(ref, 1); }
        },
        {
            OVFParameter::Zmax,
            [](const VField& ref) -> sub_pair_t<pType::Float>{ return coordMax(ref, 2); }
        }
    };
    const std::map< OVFParameter, sub_func_t<pType::Uint> > UINTDeduction{
        {   //can get the point count for rectangular grid files
            OVFParameter::Pcount,
            [](const VField& ref) -> sub_pair_t<pType::Uint>{
                associatedType_t<pType::Uint> val{};
                if(!ref.Header.isSet(OVFParameter::Mtype))
                    return {false, val};
                if(ref.Header.getMeshType() == OVFHeader::MeshType::rectangular)
                {
                    if(!std::get<0>(isGridDefined(ref.Header)))
                        return {false, val};
                    val = ref.Header.at<pType::Uint>(OVFParameter::Xnodes) *
                          ref.Header.at<pType::Uint>(OVFParameter::Ynodes) *
                          ref.Header.at<pType::Uint>(OVFParameter::Znodes);
                    return {true, val};
                }
                return {false, val};
            }
        }
    };
    const std::map< OVFParameter, sub_func_t<pType::String> > StringDeduction{
        //nothing to do here
    };
    
    //and finaly a deduction interface for the class!
    //using rulesets given:
    //Deduction:    FPDeduction     UINTDeduction       StringDeduction
    //defaults:     FPDefaults      UINTDefaults        StringDefaults
        
    bool VField::DeduceField(const OVFParameter& p, bool UseDefault)
    {        
        switch(paramIndex(p))
        {
            case(pType::Float):
            {
                auto sresult = FPDeduction.find(p);
                if(sresult != FPDeduction.end())
                {
                    auto rule = sresult -> second;
                    auto sub = rule(*this);
                    if(sub.first)
                    {
                        Header.unset(p);
                        Header.set(p, sub.second);
                        return true;
                    }
                }
                if(UseDefault)
                {
                    sresult = FPDefaults.find(p);
                    if(sresult != FPDefaults.end())
                    {
                        auto rule = sresult -> second;
                        auto sub = rule(*this);
                        if(sub.first)
                        {
                            Header.unset(p);
                            Header.set(p, sub.second);
                            return true;
                        }
                    }
                }
                break;
            }    
            case(pType::Uint):
            {
                auto sresult = UINTDeduction.find(p);
                if(sresult != UINTDeduction.end())
                {
                    auto rule = sresult -> second;
                    auto sub = rule(*this);
                    if(sub.first)
                    {
                        Header.unset(p);
                        Header.set(p, sub.second);
                        return true;
                    }
                }
                if(UseDefault)
                {
                    sresult = UINTDefaults.find(p);
                    if(sresult != UINTDefaults.end())
                    {
                        auto rule = sresult -> second;
                        auto sub = rule(*this);
                        if(sub.first)
                        {
                            Header.unset(p);
                            Header.set(p, sub.second);
                            return true;
                        }
                    }
                }
                break;
            }
            case(pType::String):
            {
                auto sresult = StringDeduction.find(p);
                if(sresult != StringDeduction.end())
                {
                    auto rule = sresult -> second;
                    auto sub = rule(*this);
                    if(sub.first)
                    {
                        Header.unset(p);
                        if(sub.second != "")
                            Header.set(p, sub.second);
                        return true;
                    }
                }
                if(UseDefault)
                {
                    sresult = StringDefaults.find(p);
                    if(sresult != StringDefaults.end())
                    {
                        auto rule = sresult -> second;
                        auto sub = rule(*this);
                        if(sub.first)
                        {
                            Header.unset(p);
                            if(sub.second != "")
                                Header.set(p, sub.second);
                            return true;
                        }
                    }
                }
                break;
            }
            case(pType::Other):
            {
                return false;
            }
        }
        
        return false;
    }
    
    //recursive deduction
    inline std::string csvParamList(const std::vector<OVFParameter>& list)
    {
        std::string acc{""};
        for(const auto& x: list)
        {
            if (!acc.empty())
                acc+=", ";
            acc += ParameterName(x);
        }
        return acc;
    }
    
    //recursive deduction
    std::string VField::DeduceRecursively(const std::size_t& max_iter)
    {
        std::string result = {""};
        std::vector<OVFParameter> missingList{};
        std::size_t iterCnt{0};
        std::size_t lastCnt{};//counter for last step missing parameters
        do{
            lastCnt = missingList.size();
            auto res = ValidateHeader(this->Header);
            if(std::get<0>(res))
            {
                result+='\n';
                result+=(std::string)"Iteration #" + std::to_string(iterCnt)+ "suceeded!";
                break;
            }
            missingList = std::move(std::get<2>(res));
            result+='\n';
            result+= (std::string)"Iteration #" + std::to_string(iterCnt) + "failed, following arguments tripped the validation: {" + csvParamList(missingList) + " }";
            for(const auto x: missingList)
                DeduceField(x, true);
            iterCnt++;
        }while(iterCnt < max_iter && lastCnt != missingList.size());
        if(iterCnt == max_iter)
            result+= (std::string)"\n Maximum iterations reached, stopping!";
        if(lastCnt == missingList.size())
            result+= "\n Missing parameter list has stopped shrinking, stopping!";
        
        return result;
    }
}
