//file for implementing interfaces of 'OVFHeader.h'
#include "OVFHeader.h"
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
                switch(paramType(x))
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
        //method to reset all fields
        void reset()
        {
            meshType.reset();
            for(auto& x: ParameterFields)
                switch(paramType(x.first))
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

    private:
        //version container
        std::optional<OVFVersion> version {std::nullopt};

    public:
        HeaderData() = default;
        ~HeaderData() = default;
        HeaderData(const HeaderData&) = default; 

        void resetVersion() noexcept
        { version = std::nullopt; }
        OVFVersion getVersion() noexcept
        {
            if(version.has_value())
                return version.value();

            if( !std::get<std::optional<associatedType_t<pType::String>>>
                    (ParameterFields.at(OVFParameter::VersionString)).has_value() )
                return OVFVersion::Unknown;

            //else parse the version
            version = matchVersionString( std::get<std::optional<associatedType_t<pType::String>>>
                                            (ParameterFields.at(OVFParameter::VersionString)).value() );
            return version.value();
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
    extern ValidationResult ValidateHeader(const OVFHeader& ref);
    ValidationResult OVFHeader::validate() const
    { return ValidateHeader(*this); }

    //Header storage default c-tor
    OVFHeader::OVFHeader(): OVFHeader(OVFVersion::OVF2) {}
    OVFHeader::OVFHeader(OVFVersion version):
        data(std::make_unique<OVFHeader::HeaderData>())
    { setVersion(version); }
    //d-tor
    OVFHeader::~OVFHeader() noexcept = default;
    //
    OVFHeader::OVFHeader(OVFHeader&& ) noexcept = default;
    OVFHeader& OVFHeader::operator=(OVFHeader&& ) noexcept = default;
    //copy c-tor
    OVFHeader::OVFHeader(const OVFHeader& ref)
    {
      data = std::make_unique<OVFHeader::HeaderData>() ;
      *data = *(ref.data);
    }
    OVFHeader& OVFHeader::operator= (const OVFHeader& ref)
    {
        *data = *ref.data;

        return *this;
    }
    //parameter type checker, courtesy of constexpr magic in the dictionary
    pType OVFHeader::paramType ( OVFParameter p) noexcept
    { return VField::paramType(p); }
    //setters
    void OVFHeader::set(OVFParameter param, const associatedType_t<pType::String>& val)
    {
        if(param == OVFParameter::VersionString)
            data -> resetVersion();
        std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[param]) = val;
    }
    void OVFHeader::set(OVFParameter param, const associatedType_t<pType::Uint>& val)
    {
        if(paramType(param) == pType::Float)
            set(param, static_cast<associatedType_t<pType::Float>>(val));
        std::get<std::optional<associatedType_t<pType::Uint>>>(data->ParameterFields[param]) = val;
    }
    void OVFHeader::set(OVFParameter param, const associatedType_t<pType::Float>& val)
    {
        std::get<std::optional<associatedType_t<pType::Float>>>(data->ParameterFields[param]) = val;
    }
    void OVFHeader::setVersion(OVFVersion version)
    { set(OVFParameter::VersionString, std::string{canonicalVersionString(version)}); }
    //check if a given field is set
    bool OVFHeader::isSet(OVFParameter refP) const noexcept
    {
        switch(paramType(refP))
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
                return false;
            default:
                return false;
        }
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
    }
    //reset function
    void OVFHeader::reset()
    {
        data->reset();
        setVersion(OVFVersion::OVF2);
    }

    //unset a parameter
    void OVFHeader::clear(OVFParameter p) noexcept
    {
        switch(paramType(p))
        {
            case(pType::Float):
                std::get<std::optional<associatedType_t<pType::Float>>>(data->ParameterFields[p]).reset();
                break;
            case(pType::Uint):
                std::get<std::optional<associatedType_t<pType::Uint>>>(data->ParameterFields[p]).reset();
                break;
            case(pType::String):
                std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[p]).reset();
                break;
            case(pType::Other):
                if(p == OVFParameter::Mtype)
                {
                    data->meshType.reset();
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
        if(p == OVFParameter::VersionString)
            data -> resetVersion();
        if(!isSet(p))
        {
            associatedType_t<pType::String> defValue{};
            set(p, defValue);
        }
        return std::get<std::optional<associatedType_t<pType::String>>>(data->ParameterFields[p]).value();
    }

    //calculate expected counts using internal structure knowledge, letting compiler optimize hell out of it
    std::size_t OVFHeader::expectedDimension() const noexcept
    {
        const auto version = data->getVersion();
        if( version == OVFVersion::Unknown || !data->meshType.has_value() )
            return 0;

        if( version != OVFVersion::OVF2 )
            return (data -> meshType == MeshType::rectangular)? 3 : 6;

        auto vdim = std::get<std::optional<associatedType_t<pType::Uint>>>
                        (data -> ParameterFields.at(OVFParameter::Vdim)).value_or(0);
        if( vdim == 0 )
            return 0;

        return (data -> meshType == MeshType::rectangular)? vdim : vdim + 3;
    }
    std::size_t OVFHeader::expectedPoints() const noexcept
    {
        const auto version = data->getVersion();
        if( version == OVFVersion::Unknown || !data->meshType.has_value() )
            return 0;

        return (data -> meshType == MeshType::rectangular) ?
        (std::get<std::optional<associatedType_t<pType::Uint>>>
                        (data -> ParameterFields.at(OVFParameter::Xnodes)).value_or(0) *
         std::get<std::optional<associatedType_t<pType::Uint>>>
                        (data -> ParameterFields.at(OVFParameter::Ynodes)).value_or(0) *
         std::get<std::optional<associatedType_t<pType::Uint>>>
                        (data -> ParameterFields.at(OVFParameter::Znodes)).value_or(0) ) :
        (std::get<std::optional<associatedType_t<pType::Uint>>>
                        (data -> ParameterFields.at(OVFParameter::Pcount)).value_or(0) );
    }
}
