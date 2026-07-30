//small toolkit for working with OVF files(not to be confused with extension, OVF files can be of all the different types like .oef, .obf, .omf, etc.)
//rules for files were taken from specification on NIST website: https://math.nist.gov/oommf/doc/userguide12b3/userguide/Vector_Field_File_Format_OV.html
//OVF 1.0: https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html
//OVF 2.0: https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html
//to save on clutter all the string interfaces should be defined through char*, saving data encoding-agnostic when possible
//problems with system specific encoding should be handled elsewhere
//only standard header cluttering output will be string, with exceptions included inside, which is everywhere anyway and has good integration
#pragma once
#include<string>
#include<memory>
#include<expected>
#include<vector>
#include<stdexcept>
#include"ovfparser_export.h" //generated with cmake, shared lib export macros

namespace VField{
    /** @brief Recognised revisions of the OOMMF vector-field file format. */
    enum class OVFVersion {
        /** @see https://math.nist.gov/oommf/doc/userguide20b0/userguide/OVF_0.0_format.html
         *  @see https://web.archive.org/web/%2A/https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_0.0_format.html */
        OVF0,
        /** @see https://math.nist.gov/oommf/doc/userguide20b0/userguide/OVF_1.0_format.html
         *  @see https://web.archive.org/web/%2A/https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html */
        OVF1,
        /** @see https://math.nist.gov/oommf/doc/userguide21a1/userguide-xml/sec_ovf20format.html
         *  @see https://web.archive.org/web/%2A/https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html */
        OVF2,
        Unknown
    };

    //OVF syntaxis dictionaries
    //parameter types
    enum class pType {
        Other,//for service fields and such
        Uint,//storing in std::size_t
        Float,//storing in double
        String//storing in std::string
    };
    
    //default variable types
    template<pType>
    struct associatedType;
    template<> struct associatedType<pType::Uint>
    { using type = std::size_t; };
    template<> struct associatedType<pType::Float>
    { using type = double; };
    template<> struct associatedType<pType::String>
    { using type = std::string; };

    template<pType p>
    using associatedType_t = typename associatedType<p>::type;
    
    //Using a very zeny hack to avoid redefining stuff at .cpp file, please avoid deleting element 'Invalid'
    //also it is dependent on UB about elements being serialized in internal representation, i.e. structure being guaranteed to be ordinate
    //look at https://groups.google.com/a/isocpp.org/forum/#!topic/std-discussion/q4qoM7Wsdso to see when more accurate option is introduced
    //WARNING: Assigning value to any of the enum members will break the hack in other file
    //declare once, use forever
    //list of all the recognized token types
    enum class OVFParameter {
        VersionString,                     //OVF revision version
        Open, Close,                       //opening and closing of the block
        Comment,                           //comments beginning with #
        Title,                             //title of the file
        Segcnt,                            //segment count
        Desc,                              //description
        Munit,                             //mesh unit
        Vunit,                             //value unit
        Vmult,                             //value multiplier
        Vlabels,                           //value labels
        Vdim,                              //value dimension
        Xmin, Ymin, Zmin, Xmax, Ymax, Zmax,//grid specifications
        Bound,                             //boundaries, list of triplets
        Vmax, Vmin,                        //boundary of values
        Mtype,                             //mesh type
        Pcount,                            //number of points
        Xbase, Ybase, Zbase,               //beginning point for coords
        Xstep, Ystep, Zstep,               //coordinate steps
        Xnodes, Ynodes, Znodes,            //number of nodes for rectangular meshes
        Empty,                             //empty line '# (space)'
        Unknown,                           //unknown something after #
        Invalid                            //invalid syntaxis
    };

    struct ValidationError {
        std::string report;
        std::vector<OVFParameter> parameters;
    };

    using ValidationResult = std::expected<void, ValidationError>;
    
    //Header container+utilities class
    class OVFPARSER_EXPORT OVFHeader{
    private:
        //class data in pimpl
        struct HeaderData;
        std::unique_ptr<HeaderData> data{};
        
    public:
        //enum class with Mesh type
        enum class MeshType {irregular, rectangular};
        
        //c-tor and d-tor of a header
        OVFHeader();
        ~OVFHeader() noexcept;
        //the one where version string is explicitly known beforehand, useful when parsing a file, to be defined outside
        explicit OVFHeader(const associatedType_t<pType::String>& ref) : OVFHeader()
        { set(OVFParameter::VersionString, ref); }
        explicit OVFHeader(OVFVersion);
        //copy stuff
        OVFHeader(const OVFHeader&);
        OVFHeader& operator=(const OVFHeader&);
        //move stuff
        OVFHeader(OVFHeader&& ref) noexcept;
        OVFHeader& operator=(OVFHeader&& ref) noexcept;
        //comparison operators
        bool operator==(const OVFHeader& ref) const noexcept;
        
        //Public interfaces of the header
        //first common utils
        static pType paramType (OVFParameter) noexcept;
        
        //retrieve methods
        //const identifier is ignored unless associatedType_t is a c array, or c++ container like std::string
        const associatedType_t<pType::String>& getString(OVFParameter) const &;
        const associatedType_t<pType::Uint>& getUint(OVFParameter) const &;
        const associatedType_t<pType::Float>& getFloat(OVFParameter) const &;

        //check if field was set method
        bool isSet(OVFParameter) const noexcept;
        
        //setters, throw if incorrect variant was chosen
        void set(OVFParameter, const associatedType_t<pType::String>& );
        void set(OVFParameter, const associatedType_t<pType::Uint>& );
        void set(OVFParameter, const associatedType_t<pType::Float>& );
        void setVersion(OVFVersion);
        
        //unset a value
        void clear(OVFParameter) noexcept;
        
        //mesh type
        MeshType getMeshType() const noexcept;
        void setMesh(MeshType) noexcept;
        
        //reset function
        void reset();
        //Run the complete validation every time. A successful validation is silent;
        //a failed result contains the report and offending parameters.
        [[nodiscard]] ValidationResult validate() const;
        //template for getting access to data by reference, throws when wrong data type is requested for a given parameter
        template<pType p>
        associatedType_t<p>& at(OVFParameter) &;           //will check the type, and throw if incorrect one is used!
        template<pType pt>
        const OVFPARSER_NO_EXPORT associatedType_t<pt>& at(OVFParameter p) const &
        {
            if(!isSet(p)) throw std::logic_error("Cannot access an unitialized field!");
            //hacky way to do that, but casting this to non-const to use normal at operator
            return const_cast<OVFHeader*>(this)->at<pt>(p);
        }

        //expected counts, return 0 if indeterminate
        std::size_t expectedPoints() const noexcept;
        std::size_t expectedDimension() const noexcept;
    };
    
    //allowed instantiations for at template
    template<> OVFPARSER_EXPORT associatedType_t<pType::Uint>& OVFHeader::at<pType::Uint> (OVFParameter p) &;
    template<> OVFPARSER_EXPORT associatedType_t<pType::Float>& OVFHeader::at<pType::Float> (OVFParameter p) &;
    template<> OVFPARSER_EXPORT associatedType_t<pType::String>& OVFHeader::at<pType::String> (OVFParameter p) &;
}
