//validation stuff for OVFs
#include <algorithm>
#include<regex>
#include<map>
#include <type_traits>
#include<vector>
#include<cmath>
#include<format>
#include<limits>
#include"OVFDictionary.h"
#include"VField.h"

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
    
    ValidationResult failure(std::string report, std::vector<OVFParameter> parameters)
    {
        return std::unexpected(ValidationError{std::move(report), std::move(parameters)});
    }

    //and then version rulesets
    using validator = ValidationResult (*)(const OVFHeader&);
    //and a method to check if grid is defined
    ValidationResult isGridDefined(const OVFHeader& ref)
    {
        const std::string prefix = "Checking if grid was defined: ";
        std::vector<OVFParameter> problemParams{};

        if(!ref.contains(OVFParameter::Mtype))
            return failure(std::format("{}Mesh type was not defined!", prefix), {OVFParameter::Mtype});

        if(ref.meshType() == MeshType::Irregular)
        {
            if(!ref.contains(OVFParameter::Pcount))
                return failure(std::format("{}In a file with irregular mesh point count was not specified", prefix), {OVFParameter::Pcount});
            if(ref.requireAs<std::size_t>(OVFParameter::Pcount) <= 0)
                return failure(std::format("{}Non-positive point count was specified for an irregular mesh", prefix), {OVFParameter::Pcount});
            return {};
        }
        
        //next check is for regular mesh parameters only
        const auto rectGridParameters = std::array{
            OVFParameter::Xbase,
            OVFParameter::Ybase,
            OVFParameter::Zbase,
            OVFParameter::Xstep,
            OVFParameter::Ystep,
            OVFParameter::Zstep,
            OVFParameter::Xnodes,
            OVFParameter::Ynodes,
            OVFParameter::Znodes
        };
        
        for(const auto& x: rectGridParameters)
        {
            if(!ref.contains(x))
                problemParams.push_back(x);
        }
        if(!ref.contains(OVFParameter::VersionString))
            problemParams.push_back(OVFParameter::VersionString);
        else
        {
            if(ref.version() == OVFVersion::OVF2)
                if(!ref.contains(OVFParameter::Vdim))
                    problemParams.push_back(OVFParameter::Vdim);
        }
        
        if(problemParams.size() == 0)
            return {};
        
        //else form error message
        std::string errMessage{std::format("{}Following required parameters to define rectangular grid were not found:", prefix)};
        for(const auto& x: problemParams)
        {
            std::format_to(std::back_inserter(errMessage), "\n\t{}", paramName(x));
        }
        return failure(std::move(errMessage), std::move(problemParams));
    }
    //checking if strings are single line
    ValidationResult CheckStrings(const OVFHeader& ref)
    {
        std::string log = "Checking if string parameters are compliant: ";
        std::vector<OVFParameter> faultyStrings{};
        for(const auto& x: StringParamList)
        {
            if(x == OVFParameter::Desc) //skip the only parameter allowed to have multiple lines
                continue;
            if(!ref.contains(x))           //ditto if parameter was not set == nothing to check
                continue;
            const auto& param = ref.requireAs<std::string>(x);
            if(param.find('\n') != std::string::npos)
                faultyStrings.push_back(x);
        }
        if(faultyStrings.empty())
            return {};

        log = std::format("{}\nThe following string parameters have a newline in them:", log);
        for(const auto& x: faultyStrings)
        {
            std::format_to(std::back_inserter(log), "\n{}", paramName(x));
        }
        return failure(std::move(log), std::move(faultyStrings));
    }
    //checking physical constrains, i.e. if values are sane
    ValidationResult checkPhysicalConstraints(const OVFHeader& ref)
    {
        const std::string prefix = "Checking a sanity of physical values: ";
        std::vector<OVFParameter> problemParams{};
        std::vector<std::string> problems {};
        //TODO: check into nuking 2 reduntant checks here, those are done before ruleset is called in order to get the version
        if(!ref.contains(OVFParameter::VersionString))
            return failure(std::format("{}\n\tVersion string was not set", prefix), {OVFParameter::VersionString});
        //nothing to check for OVF v0.0
        if(ref.version() == OVFVersion::OVF0)
            return {};
        //otherwise checking all the parameters
        //first check is for parameters being limited
        if constexpr(std::numeric_limits<parameter_cpp_type_t<ParameterType::Floating>>::has_infinity ||
                     std::numeric_limits<parameter_cpp_type_t<ParameterType::Floating>>::has_quiet_NaN )
        {
            for(const auto& x: FPParamList)
                if(ref.contains(x))
                {
                    auto val = ref.requireAs<double>(x);
                    if(!std::isfinite(val))
                    {
                        problemParams.push_back(x);
                        problems.push_back(std::format("Encountered a non-finite value '{}' = {}\n", paramName(x), val));
                    }
                }
        }
        if constexpr(std::numeric_limits<parameter_cpp_type_t<ParameterType::Unsigned>>::has_infinity ||
                     std::numeric_limits<parameter_cpp_type_t<ParameterType::Unsigned>>::has_quiet_NaN )
        {
            for(const auto& x: UINTParamList)
                if(ref.contains(x))
                {
                    auto val = ref.requireAs<double>(x);
                    if(!std::isfinite(val))
                    {
                        problemParams.push_back(x);
                        problems.push_back(std::format("Encountered a non-finite value '{}' = {}\n", paramName(x), val));
                    }
                }
        }
        auto gridProblems = isGridDefined(ref);
        if(!gridProblems)
        {
            problems.push_back("Grid parameters were not defined!!");
            for(const auto& x: gridProblems.error().parameters)
                problemParams.push_back(x);
        }
        else 
        {
            constexpr auto posDefined = std::array{
                OVFParameter::Xnodes,
                OVFParameter::Ynodes,
                OVFParameter::Znodes,
                OVFParameter::Xstep,
                OVFParameter::Ystep,
                OVFParameter::Zstep
            };
            static_assert(DictionaryHelpers::isSubset(posDefined, DictionaryHelpers::makeUnion(FPParamList, UINTParamList)), "Only floating point and UINT params are allowed");
            //check if required params are positively defined
            for(const auto& x: posDefined)
            {
                if(paramType(x) == ParameterType::Unsigned)
                {
                    auto val = ref.requireAs<std::size_t>(x);
                    if(val <= 0)
                    {
                        problems.push_back(std::format("The value '{}' = {}, was not positively defined!", paramName(x), val));
                        problemParams.push_back(x);
                    }
                }
                else if(paramType(x) == ParameterType::Floating)
                {
                    auto val = ref.requireAs<double>(x);
                    if(val <= 0)
                    {
                        problems.push_back(std::format("The value '{}' = {}, was not positively defined!", paramName(x), val));
                        problemParams.push_back(x);
                    }
                }
            }
        }
        
        if(problems.size() == 0)
            return {};
        
        std::string accum { prefix};
        for(const auto& x: problems)
        {
            accum += "\n\t";
            accum += x;
        }
        
        return failure(std::move(accum), std::move(problemParams));
    }
    
    //nothing is disallowed lol
    const auto OVF0Rules = std::vector<validator> {};
    //then rules for OVF1
    const auto OVF1Rules = std::vector<validator>{
        [](const OVFHeader& ref) -> ValidationResult
        {
            const std::string prefix = "Checking if all required fields were filled: ";
            std::vector<OVFParameter> problemParams{};
            //check if all required field are present
            const auto RequiredParameters = std::array {
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
            };
            for(const auto& x: RequiredParameters)
                if(!ref.contains(x))
                    problemParams.push_back(x);
                
            if(problemParams.size() == 0)
                return {};
            
            //else form error message
            std::string errMessage{"Following required parameters(for OVF 1.0) were not found:"};
            for(const auto& x: problemParams)
            {
                std::format_to(std::back_inserter(errMessage), "\n\t{}", paramName(x));
            }
            return failure(std::move(errMessage), std::move(problemParams));
        },
        CheckStrings,
        //check if grid is defined
        isGridDefined,
        //check that the boundary list, if present, is a list of tripples of points
        [](const OVFHeader& ref) -> ValidationResult
        {
            const std::string prefix = "Checking if 'boundarylist' is ill-formed:\n";
            if(!ref.contains(OVFParameter::Bound))
                return {}; //nothing to check
            //get the boundary vertex list
            const std::string boundaryList { ref.requireAs<std::string>(OVFParameter::Bound)};
            //count how many tokens there are, validating if they are convertible to double
            auto cnt = countTokens(boundaryList, [](const std::string& ref){try{std::stod(ref);}catch(const std::logic_error&){return false;} return true;});
            if( cnt == 0)
                return failure(std::format("{}A string in 'boundarylist' contains invalid tokens: \n\t{}", prefix, boundaryList), {OVFParameter::Bound});
            if( cnt % 3 != 0)
                return failure(std::format("{}Bounding box vortex list should have triplets of coordinates, {} values were read in 'boundarylist': \n\t{}", prefix, cnt, boundaryList), {OVFParameter::Bound});
            if( cnt < 12 )
                return failure(std::format("{}Not enough points to set a bounding volume; at least 4 vertices are needed, got {} vertices in 'boundarylist': \n\t{}", prefix, cnt / 3, boundaryList), {OVFParameter::Bound});
            
            return {};
        }
        //TODO: implement checking for version string having same mesh type specified!
    };
    //then rules for OVF2
    const auto OVF2Rules = std::vector<validator>{
        [](const OVFHeader& ref) -> ValidationResult
        {
            const std::string prefix = "Checking if all required fields were filled: ";
            //check if all required field are present
            const auto RequiredParameters = std::array{
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
            };
            std::vector<OVFParameter> missingList{};
            for(const auto& x: RequiredParameters)
                if(!ref.contains(x))
                    missingList.push_back(x);
                
            if(missingList.size() == 0)
                return {};
            
            //else form error message
            std::string errMessage{"Following required parameters(for OVF 2.0) were not found:"};
            for(const auto& x: missingList)
            {
                std::format_to(std::back_inserter(errMessage), "\n\t{}", paramName(x));
            }
            return failure(std::move(errMessage), std::move(missingList));
        },
        CheckStrings,
        isGridDefined,
        //check if value units has correct number of tokens
        [](const OVFHeader& ref) -> ValidationResult
        {
            const std::string prefix = "Checking if 'valueunits' are ill-formed:\n";
            //should not reach here normally
            if(!ref.contains(OVFParameter::Vunit))
                return failure(std::format("{}Value units are not set yet", prefix), {OVFParameter::Vunit});
            if(!ref.contains(OVFParameter::Vdim))
                return failure(std::format("{}Value dimensions are not set yet", prefix), {OVFParameter::Vdim});
            std::size_t num {0};
            if((num = countTokens(ref.requireAs<std::string>(OVFParameter::Vunit))) != ref.requireAs<std::size_t>(OVFParameter::Vdim) && num != 1)
                return failure(std::format("{}Unexpected number of tokens: {} while parsing value units:\n\t{}", prefix, num, ref.requireAs<std::string>(OVFParameter::Vunit)), {OVFParameter::Vunit});
            return {};
        },
        //check if value labels has correct number of tokens
        [](const OVFHeader& ref) -> ValidationResult
        {
            const std::string prefix = "Checking if 'valuelabels' is ill-formed: ";
            //should not reach here normally
            if(!ref.contains(OVFParameter::Vlabels))
                return failure(std::format("{}Value labels are not set yet", prefix), {OVFParameter::Vlabels});
            if(!ref.contains(OVFParameter::Vdim))
                return failure(std::format("{}Value dimensions are not set yet", prefix), {OVFParameter::Vdim});
            std::size_t num {countTokens(ref.requireAs<std::string>(OVFParameter::Vlabels))};
            if(num != 1 && num != ref.requireAs<std::size_t>(OVFParameter::Vdim))
                return failure(std::format("{}Unexpected number of tokens: {} while parsing value labels:\n\t{}", prefix, num, ref.requireAs<std::string>(OVFParameter::Vlabels)), {OVFParameter::Vlabels});
            return {};
        }
    };
    //OMEGA map for rulesets
    const std::map<OVFVersion, std::vector<validator>> Ruleset{
        {OVFVersion::OVF0, OVF0Rules},
        {OVFVersion::OVF1, OVF1Rules},
        {OVFVersion::OVF2, OVF2Rules}
    };
    
    //count expected number of points
    std::optional<std::size_t> expectedValueCount(const OVFHeader& ref)
    {
        if(ref.version() == OVFVersion::OVF0)
            return std::nullopt;
        const auto dimension = ref.pointDimension();
        const auto points = ref.pointCount();
        if(!dimension || !points ||
           *points > std::numeric_limits<std::size_t>::max() / *dimension)
            return std::nullopt;
        return *dimension * *points;
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
    ValidationResult ValidateHeader(const OVFHeader& ref)
    {
        std::string report;
        std::vector<OVFParameter> problematicVars {};
        if(!ref.contains(OVFParameter::VersionString))
            return failure("Header version string was not set, aborting!", {OVFParameter::VersionString});
        //else execute the correct ruleset
        const auto version = ref.version();
        if(Ruleset.find(version) == Ruleset.end())
            return failure("Header version does not have a ruleset implemented!", {OVFParameter::VersionString});
        //otherwise it is safe to execute ruleset
        const auto& rules = Ruleset.at(version);
        for(const auto& rule: rules)
        {
            if(auto checkResult = rule(ref); !checkResult)
            {
                std::format_to(std::back_inserter(report), "{}{}",
                               report.empty() ? "" : "\n", checkResult.error().report);
                appendUnique(problematicVars, checkResult.error().parameters);
            }
        }
        //TODO: add verification that header has the same mesh type as file title for OVF1!
        if(report.empty())
            return {};
        return failure(std::move(report), std::move(problematicVars));
    }
    
    //method telling if current data is isAddressable, i.e. if data structure within array is known
    bool VField::isAddressable() const noexcept
    {
        //if it is impossible to calculate number of values we already are in a bust
        const auto expected = expectedValueCount(header_);
        if(!expected)
            return false;
        //else check if expected value count is consistent with internal array
        if(*expected != scalarCount())
            return false;
        if(scalarCount() == 0)
            return false;
        
        return true;
    }
    //and same for weekly addressable, i.e. there is enough data to traverse internal array, but it ends abruptly
    bool VField::isWeaklyAddressable() const noexcept
    {
        if(!header_.contains(OVFParameter::VersionString))
            return false;
        auto version = header_.version();
        if(scalarCount() == 0 || version == OVFVersion::Unknown || !header_.contains(OVFParameter::Mtype) || (version == OVFVersion::OVF2 && !header_.contains(OVFParameter::Vdim)) )
            return false;
        const auto dimension = header_.pointDimension();
        return dimension && scalarCount() % *dimension == 0;
    }

    bool VField::isGridAddressable() const noexcept
    {
        if(!isWeaklyAddressable() ||
           header_.meshType() != MeshType::Rectangular ||
           !header_.contains(OVFParameter::Xnodes) ||
           !header_.contains(OVFParameter::Ynodes) ||
           !header_.contains(OVFParameter::Znodes))
            return false;

        const auto xnodes = header_.requireAs<std::size_t>(OVFParameter::Xnodes);
        const auto ynodes = header_.requireAs<std::size_t>(OVFParameter::Ynodes);
        const auto znodes = header_.requireAs<std::size_t>(OVFParameter::Znodes);
        constexpr auto maximum = std::numeric_limits<std::size_t>::max();
        if(xnodes == 0 || ynodes == 0 || znodes == 0 ||
           xnodes > maximum / ynodes || xnodes * ynodes > maximum / znodes)
            return false;

        return pointCount() == xnodes * ynodes * znodes;
    }
    //return dimensionality
    std::size_t VField::pointDimension() const noexcept
    {
        if(!isWeaklyAddressable())
            return 0u;
        return header_.pointDimension().value_or(0);
    }
    //return number of points and such
    std::size_t VField::pointCount() const noexcept
    {
        if(!isWeaklyAddressable())
            return 0u;
        return scalarCount() / pointDimension(); //guaranteed to have 0 remainder
    }
    
    //implementation of validator from VField itself, checks both header and data
    ValidationResult VField::validate() const
    {
        if(auto headerValidation = header_.validate(); !headerValidation)
            return headerValidation;
        if(!isAddressable())
            return failure("Field data does not match the describing header", {});
        return {};
    }

    template<class View, class Predicate>
      auto min_over_rows(const View& view, Predicate&& predicate)
      {
        static_assert(View::rank() > 0);

        if (view.extent(0) == 0)
          throw std::invalid_argument("min_over_rows: empty dimension 0");

        using index_type = typename View::index_type;

        return std::ranges::min(
            std::views::iota(index_type{0}, view.extent(0))
            | std::views::transform([&](index_type row) {
              return std::invoke(predicate, view, row);
              }));
      }

    template<class View, class Predicate>
      auto max_over_rows(const View& view, Predicate&& predicate)
      {
        static_assert(View::rank() > 0);

        if (view.extent(0) == 0)
          throw std::invalid_argument("max_over_rows: empty dimension 0");

        using index_type = typename View::index_type;

        return std::ranges::max(
            std::views::iota(index_type{0}, view.extent(0))
            | std::views::transform([&](index_type row) {
              return std::invoke(predicate, view, row);
              }));
      }

    ///////////////////
    //Deduction rules//
    ///////////////////
    //types for substitution pair generation, first 'bool' is if generation was successfull
    template<ParameterType p>
    using sub_pair_t = std::pair<bool, parameter_cpp_type_t<p>>;
    template<ParameterType p>
    using sub_func_t = sub_pair_t<p> (*) (const VField&);
    
    //maps with default values
    const std::map< OVFParameter, sub_func_t<ParameterType::Floating> > FPDefaults {
        {   //default multiplier for value is 1.
            OVFParameter::Vmult,
            [](const VField&) -> sub_pair_t<ParameterType::Floating> {
                return {true, 1. };
            }
        },
        //default value for the base are at (Xstep, Ystep, Zstep)/2 
        {
            OVFParameter::Xbase,
            [](const VField& field) -> sub_pair_t<ParameterType::Floating> {
                if(field.header().contains(OVFParameter::Xstep))
                    return {true, field.header().requireAs<double>(OVFParameter::Xstep)/2 };
                else return {false, 0};
            }
        },
        {
            OVFParameter::Ybase,
            [](const VField& field) -> sub_pair_t<ParameterType::Floating> {
                if(field.header().contains(OVFParameter::Ystep))
                    return {true, field.header().requireAs<double>(OVFParameter::Ystep)/2 };
                else return {false, 0};
            }
        },
        {
            OVFParameter::Zbase,
            [](const VField& field) -> sub_pair_t<ParameterType::Floating> {
                if(field.header().contains(OVFParameter::Zstep))
                    return {true, field.header().requireAs<double>(OVFParameter::Zstep)/2 };
                else return {false, 0};
            }
        }
    };
    const std::map< OVFParameter, sub_func_t<ParameterType::Unsigned> > UINTDefaults {
        //nothing to have default values of yet
    };
    const std::map< OVFParameter, sub_func_t<ParameterType::String> > StringDefaults {
        {
            OVFParameter::Title, 
            [](const VField& ref) -> sub_pair_t<ParameterType::String>{
                if(!ref.header().contains(OVFParameter::VersionString))
                    return {false, ""};
                auto version = ref.header().version();
                std::size_t dim {3};
                if(version == OVFVersion::OVF2 && !ref.header().contains(OVFParameter::Vdim))
                    return {false, ""};
                else if(version == OVFVersion::OVF2)
                    dim = ref.header().requireAs<std::size_t>(OVFParameter::Vdim);
                return {true, (std::string)"Indescript " + std::to_string( dim) + "-dimensional vector field"};
            }
        },
        {
            OVFParameter::Munit,
            [](const VField&) -> sub_pair_t<ParameterType::String>{
                return {true, "m m m"};
            }
        },
        {
            OVFParameter::Vlabels,
            [](const VField& ref) -> sub_pair_t<ParameterType::String>{
                if(!ref.header().contains(OVFParameter::VersionString))
                    return {false, ""};
                auto version = ref.header().version();
                std::size_t dim {3};
                if(version == OVFVersion::OVF2 && !ref.header().contains(OVFParameter::Vdim))
                    return {false, ""};
                else if(version == OVFVersion::OVF2)
                    dim = ref.header().requireAs<std::size_t>(OVFParameter::Vdim);
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
            [](const VField& ref) -> sub_pair_t<ParameterType::String>{
                if(!ref.header().contains(OVFParameter::VersionString))
                    return {false, ""};
                auto version = ref.header().version();
                std::size_t dim {3};
                if(version == OVFVersion::OVF2 && !ref.header().contains(OVFParameter::Vdim))
                    return {false, ""};
                else if(version == OVFVersion::OVF2)
                    dim = ref.header().requireAs<std::size_t>(OVFParameter::Vdim);
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
            [](const VField&) -> sub_pair_t<ParameterType::String>{
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
    inline sub_pair_t<ParameterType::Floating> coordMin( const VField& ref, const std::size_t& coordIndex)
    {
        //first check if data is accessible, rule doesn't work without it
        if(!ref.isAddressable() || coordIndex > 2)
            return {false, 0.};
        if(ref.header().meshType() == MeshType::Rectangular)
        {
            switch(coordIndex){
                case(0):
                    return {true, ref.header().requireAs<double>(OVFParameter::Xbase) - ref.header().requireAs<double>(OVFParameter::Xstep)/2};
                case(1):
                    return {true, ref.header().requireAs<double>(OVFParameter::Ybase) - ref.header().requireAs<double>(OVFParameter::Ystep)/2};
                case(2):
                    return {true, ref.header().requireAs<double>(OVFParameter::Zbase) - ref.header().requireAs<double>(OVFParameter::Zstep)/2};
                default:
                    return {false, 0.};
            }
        }

        //else need to calculate it for non-rectangular grid :'(
        //TODO: isAddressable probably already requires this!
        if(!ref.header().contains(OVFParameter::VersionString))
            return {false, 0};

        auto coordPred = [coordIndex] ( const auto& ref, std::size_t i )
          { return ref[i, coordIndex]; };

        if (ref.stores<float>() )
          return{true, min_over_rows( ref.pointView<float>(), coordPred) };
        else if (ref.stores<double>() )
          return{true, min_over_rows( ref.pointView<double>(), coordPred) };
        
        return{false, 0};
    }

    inline sub_pair_t<ParameterType::Floating> coordMax( const VField& ref, const std::size_t& coordIndex)
    {
        //first check if data is accessible, rule doesn't work without it
        if(!ref.isAddressable() || coordIndex > 2)
            return {false, 0.};
        if(ref.header().meshType() == MeshType::Rectangular)
        {
            switch(coordIndex){
                case(0):
                    return {true, ref.header().requireAs<double>(OVFParameter::Xbase) + ref.header().requireAs<double>(OVFParameter::Xstep) * (0.5 + ref.header().requireAs<std::size_t>(OVFParameter::Xnodes))};
                case(1):
                    return {true, ref.header().requireAs<double>(OVFParameter::Ybase) + ref.header().requireAs<double>(OVFParameter::Ystep) * (0.5 + ref.header().requireAs<std::size_t>(OVFParameter::Ynodes))};
                case(2):
                    return {true, ref.header().requireAs<double>(OVFParameter::Zbase) + ref.header().requireAs<double>(OVFParameter::Zstep) * (0.5 + ref.header().requireAs<std::size_t>(OVFParameter::Znodes))};
                default:
                    return {false, 0.};
            }
        }

        //else need to calculate it for non-rectangular grid :'(
        auto coordPred = [coordIndex] ( const auto& ref, std::size_t i )
          { return ref[i, coordIndex]; };

        if (ref.stores<float>() )
          return{true, max_over_rows( ref.pointView<float>(), coordPred) };
        else if (ref.stores<double>() )
          return{true, max_over_rows( ref.pointView<double>(), coordPred) };
        
        return{false, 0};
    }
    
    const std::map< OVFParameter, sub_func_t<ParameterType::Floating> > FPDeduction{
        //TODO: look into templating those!
        {
            OVFParameter::Vmin,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{
                parameter_cpp_type_t<ParameterType::Floating> minVal {};
                //first check if data is accessible, rule doesn't work without it
                if(!ref.isAddressable())
                    return {false, minVal};
                //then check what is a dimension of argument
                if(!ref.header().contains(OVFParameter::VersionString))
                    return {false, minVal};
                auto version = ref.header().version();
                std::size_t val_dim {3};
                if(version == OVFVersion::OVF2 && !ref.header().contains(OVFParameter::Vdim))
                    return {false, minVal};
                else if(version == OVFVersion::OVF2)
                    val_dim = ref.header().requireAs<std::size_t>(OVFParameter::Vdim);
                const std::size_t offset {
                    static_cast<std::size_t>(ref.header().meshType() == MeshType::Rectangular ? 0 : 3)
                };

                auto normPred = [offset, val_dim](const auto& ref, std::size_t row) {
                  static_assert(std::remove_cvref_t<decltype(ref)>::rank() == 2);

                  // offset + val_dim is the one-past-the-end index.
                  assert(offset <= ref.extent(1));
                  assert(offset + val_dim == ref.extent(1));

                  using result_type =
                    decltype(std::hypot(ref[row, offset], ref[row, offset]));

                  result_type norm{};

                  for (std::size_t column = offset; column < ref.extent(1); ++column)
                    norm = std::hypot(norm, ref[row, column]);

                  return norm;
                };


                if(ref.stores<float>())
                  return {true, min_over_rows( ref.pointView<float>(), normPred ) };
                else if (ref.stores<double>())
                  return {true, min_over_rows( ref.pointView<double>(), normPred ) };

                return {false, minVal};
            }
        },
        {
            OVFParameter::Vmax,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{
                parameter_cpp_type_t<ParameterType::Floating> maxVal {};
                //first check if data is accessible, rule doesn't work without it
                if(!ref.isAddressable())
                    return {false, maxVal};
                //then check what is a dimension of argument
                if(!ref.header().contains(OVFParameter::VersionString))
                    return {false, maxVal};
                auto version = ref.header().version();
                std::size_t val_dim {3};
                if(version == OVFVersion::OVF2 && !ref.header().contains(OVFParameter::Vdim))
                    return {false, maxVal};
                else if(version == OVFVersion::OVF2)
                    val_dim = ref.header().requireAs<std::size_t>(OVFParameter::Vdim);
                const std::size_t offset {
                    static_cast<std::size_t>(ref.header().meshType() == MeshType::Rectangular ? 0 : 3)
                };

                auto normPred = [offset, val_dim](const auto& ref, std::size_t row) {
                  static_assert(std::remove_cvref_t<decltype(ref)>::rank() == 2);

                  // offset + val_dim is the one-past-the-end index.
                  assert(offset <= ref.extent(1));
                  assert(offset + val_dim == ref.extent(1));

                  using result_type =
                    decltype(std::hypot(ref[row, offset], ref[row, offset]));

                  result_type norm{};

                  for (std::size_t column = offset; column < ref.extent(1); ++column)
                    norm = std::hypot(norm, ref[row, column]);

                  return norm;
                };


                if(ref.stores<float>())
                  return {true, max_over_rows( ref.pointView<float>(), normPred ) };
                else if (ref.stores<double>())
                  return {true, max_over_rows( ref.pointView<double>(), normPred ) };

                return {false, maxVal};
            }
        },
        {
            OVFParameter::Xmin,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{ return coordMin(ref, 0); }
        },
        {
            OVFParameter::Ymin,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{ return coordMin(ref, 1); }
        },
        {
            OVFParameter::Zmin,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{ return coordMin(ref, 2); }
        },
        {
            OVFParameter::Xmax,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{ return coordMax(ref, 0); }
        },
        {
            OVFParameter::Ymax,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{ return coordMax(ref, 1); }
        },
        {
            OVFParameter::Zmax,
            [](const VField& ref) -> sub_pair_t<ParameterType::Floating>{ return coordMax(ref, 2); }
        }
    };
    const std::map< OVFParameter, sub_func_t<ParameterType::Unsigned> > UINTDeduction{
        {   //can get the point count for rectangular grid files
            OVFParameter::Pcount,
            [](const VField& ref) -> sub_pair_t<ParameterType::Unsigned>{
                parameter_cpp_type_t<ParameterType::Unsigned> val{};
                if(!ref.header().contains(OVFParameter::Mtype))
                    return {false, val};
                if(ref.header().meshType() == MeshType::Rectangular)
                {
                    if(!isGridDefined(ref.header()))
                        return {false, val};
                    val = ref.header().requireAs<std::size_t>(OVFParameter::Xnodes) *
                          ref.header().requireAs<std::size_t>(OVFParameter::Ynodes) *
                          ref.header().requireAs<std::size_t>(OVFParameter::Znodes);
                    return {true, val};
                }
                return {false, val};
            }
        }
    };
    const std::map< OVFParameter, sub_func_t<ParameterType::String> > StringDeduction{
        //nothing to do here
    };
    
    //and finaly a deduction interface for the class!
    //using rulesets given:
    //Deduction:    FPDeduction     UINTDeduction       StringDeduction
    //defaults:     FPDefaults      UINTDefaults        StringDefaults
        
    bool VField::deduceField(const OVFParameter& p, bool UseDefault)
    {        
        switch(paramType(p))
        {
            case(ParameterType::Floating):
            {
                auto sresult = FPDeduction.find(p);
                if(sresult != FPDeduction.end())
                {
                    auto rule = sresult -> second;
                    auto sub = rule(*this);
                    if(sub.first)
                    {
                        header_.clear(p);
                        header_.set(p, sub.second);
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
                            header_.clear(p);
                            header_.set(p, sub.second);
                            return true;
                        }
                    }
                }
                break;
            }    
            case(ParameterType::Unsigned):
            {
                auto sresult = UINTDeduction.find(p);
                if(sresult != UINTDeduction.end())
                {
                    auto rule = sresult -> second;
                    auto sub = rule(*this);
                    if(sub.first)
                    {
                        header_.clear(p);
                        header_.set(p, sub.second);
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
                            header_.clear(p);
                            header_.set(p, sub.second);
                            return true;
                        }
                    }
                }
                break;
            }
            case(ParameterType::String):
            {
                auto sresult = StringDeduction.find(p);
                if(sresult != StringDeduction.end())
                {
                    auto rule = sresult -> second;
                    auto sub = rule(*this);
                    if(sub.first)
                    {
                        header_.clear(p);
                        if(sub.second != "")
                            header_.set(p, sub.second);
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
                            header_.clear(p);
                            if(sub.second != "")
                                header_.set(p, sub.second);
                            return true;
                        }
                    }
                }
                break;
            }
            case(ParameterType::Other):
            case(ParameterType::Mesh):
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
            acc += paramName(x);
        }
        return acc;
    }
    
    //recursive deduction
    std::string VField::deduceRecursively(const std::size_t& max_iter)
    {
        std::string result = {""};
        std::vector<OVFParameter> missingList{};
        std::size_t iterCnt{0};
        std::size_t lastCnt{};//counter for last step missing parameters
        do{
            lastCnt = missingList.size();
            auto res = ValidateHeader(this->header());
            if(res)
            {
                result+='\n';
                std::format_to(std::back_inserter(result), "Iteration #{} succeeded!", iterCnt);
                break;
            }
            missingList = std::move(res.error().parameters);
            result+='\n';
            std::format_to(std::back_inserter(result),
                           "Iteration #{} failed, following arguments tripped the validation: {{{} }}",
                           iterCnt, csvParamList(missingList));
            for(const auto x: missingList)
                deduceField(x, true);
            iterCnt++;
        }while(iterCnt < max_iter && lastCnt != missingList.size());
        if(iterCnt == max_iter)
            result+= (std::string)"\n Maximum iterations reached, stopping!";
        if(lastCnt == missingList.size())
            result+= "\n Missing parameter list has stopped shrinking, stopping!";
        
        return result;
    }

    //strip list
    constexpr auto OVFOptional = 
       std::array{
            OVFParameter::Vmin,
            OVFParameter::Vmax,
            OVFParameter::Bound,
            OVFParameter::Desc
       };

    //strip optional parameters
    void VField::strip() noexcept
    {
        if(!header_.contains(OVFParameter::VersionString))
            return;
        auto version = header_.version();
        if(version == OVFVersion::OVF1 || version == OVFVersion::OVF2)
            for(const auto& par: OVFOptional)
                header_.clear(par);
        return;
    }
}
