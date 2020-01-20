//file for implementing interfaces of 'OVFHeader.h'
#include"OVFDictionary.h"
#include<map>
#include<algorithm>
#include<vector>
#include<utility>
#include<variant>
#include<optional>

namespace VField{
    //a commonly used type here
    using Field = 
            std::variant< std::optional<associatedType_t<pType::Uint>>,
                          std::optional<associatedType_t<pType::Float>>,
                          std::optional<associatedType_t<pType::String>> >;

    const std::map<OVFParameter, Field> defParameters
    {
        []() -> auto {
            std::map<OVFParameter, Field> pmap {}; 
            constexpr auto valParamList = DictionaryHelpers::makeUnion(FPParamList, UINTParamList, StringParamList);
            for(const auto& x: valParamList)
            {
                switch(paramIndex(x))
                {
                    case(pType::Uint):
                        pmap.emplace(x, std::optional<associatedType_t<pType::Uint>>{});
                        break;
                    case(pType::Float):
                        pmap.emplace(x, std::optional<associatedType_t<pType::Float>>{});
                        break;
                    case(pType::String):
                        pmap.emplace(x, std::optional<associatedType_t<pType::String>>{});
                        break;
                    case(pType::Other):
                        //hope it never reaches here :p
                        break;
                }
            }
            return pmap;
        }()
    };

    struct OVFHeader::HeaderData
    {
        //internal storage of variables
        std::map<OVFParameter, Field> ParameterFields = defParameters ;
        //mesh type
        std::optional<MeshType> meshType{};
        //last check results
        bool isChecked{false};
        bool isValid{false};
        std::string ValidationReport{};

        //does nothing, but just to insure that there is c-tor
        HeaderData() = default;
        HeaderData(const HeaderData&) = default;
        //method to reset all fields
        void reset()
        {
            meshType.reset();
            for(auto& x: ParameterFields)
                switch(paramIndex(x.first))
                {
                    case(pType::Uint):
                        std::get<std::optional<associatedType_t<pType::Uint>>>(x.second).reset();
                        break;
                    case(pType::Float):
                        std::get<std::optional<associatedType_t<pType::Float>>>(x.second).reset();
                        break;
                    case(pType::String):
                        std::get<std::optional<associatedType_t<pType::String>>>(x.second).reset();
                        break;
                    case(pType::Other):
                        break;
                }
        }
    };

    //comparison implementation
    bool OVFHeader::operator==(const OVFHeader& ref) const noexcept
    {
        if(data -> meshType != ref.data -> meshType)
            return false;
        return data -> ParameterFields == ref.data -> ParameterFields;
    }

    //validation stuff
    //Implemented in OVFGrammar.cpp!
    extern std::tuple<bool, std::string, std::vector<OVFParameter>> ValidateHeader(const OVFHeader& ref);
    //and interfaces declared
    bool OVFHeader::validate()
    {
        if(data->isChecked)
            return data->isValid;
        //else do a legit check
        const auto result = ValidateHeader(*this);
        data->isValid = std::get<0>(result);
        data->ValidationReport = std::get<1>(result);
        data->isChecked = true;
        return data->isValid;
    }
    const associatedType_t<pType::String> OVFHeader::ValidationReport()
    {
        if(!data->isChecked)
            validate();
        return data->ValidationReport;
    }

    //Header storage default c-tor
    OVFHeader::OVFHeader()
    {
        data = new HeaderData();
    }
    //d-tor
    OVFHeader::~OVFHeader() noexcept
    {
        delete data;
    }
    //copy c-tor
    OVFHeader::OVFHeader(const OVFHeader& ref)
    {
        data = new HeaderData();
        *data = *(ref.data);
    }
    OVFHeader& OVFHeader::operator= (const OVFHeader& ref)
    {
        auto data_copy = new HeaderData(*ref.data);
        std::swap(data, data_copy);
        delete data_copy;

        return *this;
    }
    //parameter type checker, courtesy of constexpr magic in the dictionary
    pType OVFHeader::paramType ( OVFParameter p) noexcept
    { return paramIndex(p); }
    //setters
    void OVFHeader::set(OVFParameter param, const associatedType_t<pType::String>& val) noexcept
    {
        data->ParameterFields[param] = std::make_optional<associatedType_t<pType::String>>(val);
        data->isChecked = false;
    }
    void OVFHeader::set(OVFParameter param, const associatedType_t<pType::Uint>& val) noexcept
    {
        data->ParameterFields[param] = std::make_optional<associatedType_t<pType::Uint>>(val);
        data->isChecked = false;
    }
    void OVFHeader::set(OVFParameter param, const associatedType_t<pType::Float>& val) noexcept
    {
        data->ParameterFields[param] = std::make_optional<associatedType_t<pType::Float>>(val);
        data->isChecked = false;
    }
    //check if a given field is set
    bool OVFHeader::isSet(OVFParameter refP) const noexcept
    {
        switch(paramIndex(refP))
        {
            case(pType::String):
                return std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[refP]) != std::nullopt;
            case(pType::Uint):
                return std::get<std::optional<associatedType_t<pType::Uint>>>(data->ParameterFields[refP]) != std::nullopt;
            case(pType::Float):
                return std::get<std::optional<associatedType_t<pType::Float>>>(data->ParameterFields[refP]) != std::nullopt;
            case(pType::Other):
                if(refP == OVFParameter::Mtype)
                    return data->meshType != std::nullopt;
            default:
                return false;
        }
        //just to silence the compiler
        //lambs are silent now
        return false;
    }
    //getters
    const associatedType_t<pType::String>& OVFHeader::getString(OVFParameter param) const &
    {
        return std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[param]).value();
    }
    const associatedType_t<pType::Uint>& OVFHeader::getUint(OVFParameter param) const &
    {
        return std::get<std::optional<associatedType_t<pType::Uint>>>(data->ParameterFields[param]).value();
    }
    const associatedType_t<pType::Float>& OVFHeader::getFloat(OVFParameter param) const &
    {
        return std::get<std::optional<associatedType_t<pType::Float>>>(data->ParameterFields[param]).value();
    }

    //mesh type functions
    OVFHeader::MeshType OVFHeader::getMeshType() const noexcept
    {
        return data->meshType.value();
    }
    void OVFHeader::setMesh(MeshType ref) noexcept
    {
        data->meshType = ref;
        //invalidating a checked status
        data->isChecked = false;
    }
    //reset function
    void OVFHeader::reset()
    {data->reset();data->isChecked = false;};

    //unset a parameter
    void OVFHeader::unset(OVFParameter p) noexcept
    {
        switch(paramIndex(p))
        {
            case(pType::Float):
                std::get<std::optional<associatedType_t<pType::Float>>>(data->ParameterFields[p]).reset();
                data->isChecked = false;
                break;
            case(pType::Uint):
                std::get<std::optional<associatedType_t<pType::Uint>>>(data->ParameterFields[p]).reset();
                data->isChecked = false;
                break;
            case(pType::String):
                std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[p]).reset();
                data->isChecked = false;
                break;
            case(pType::Other):
                if(p == OVFParameter::Mtype)
                {
                    data->meshType.reset();
                    data->isChecked = false;
                    break;
                }
        }
    }
    //interfaces through bracket operator
    template<> associatedType_t<pType::Uint>& OVFHeader::at<pType::Uint> (OVFParameter p) & 
    {
        if(!isSet(p))
        {
            associatedType_t<pType::Uint> defValue{};
            set(p, defValue);
        }
        return std::get<std::optional<associatedType_t<pType::Uint>>>(data->ParameterFields[p]).value();
    }
    template<> associatedType_t<pType::Float>& OVFHeader::at<pType::Float> (OVFParameter p) &
    {
        if(!isSet(p))
        {
            associatedType_t<pType::Float> defValue{};
            set(p, defValue);
        }
        return std::get<std::optional<associatedType_t<pType::Float>>>(data->ParameterFields[p]).value();
    }
    template<> associatedType_t<pType::String>& OVFHeader::at<pType::String> (OVFParameter p) &
    {
        if(!isSet(p))
        {
            associatedType_t<pType::String> defValue{};
            set(p, defValue);
        }
        return std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[p]).value();
    }
}

