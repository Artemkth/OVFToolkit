#pragma once
//small toolkit for working with OVF files(not to be confused with extension, OVF files can be of all the different types like .oef, .obf, .omf, etc.)
//rules for files were taken from specification on NIST website: https://math.nist.gov/oommf/doc/userguide12b3/userguide/Vector_Field_File_Format_OV.html
//OVF 1.0: https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html
//OVF 2.0: https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html
//to save on clutter all the string interfaces should be defined through char*, saving data encoding-agnostic when possible
//problems with system specific encoding should be handled elsewhere
//only standard header cluttering output will be exception, which is everywhere anyway and has good integration

namespace VField{
    //OVF syntaxis dictionaries
    //parameter types
    enum class pType {
        Other,//for service fields and such
        Uint,//storing in std::size_t
        Float,//storing in double
        String//storing in char*
    };
    
    //default variable types
    template<pType>
    struct associatedType;
    template<>
    struct associatedType<pType::Uint>
    {
        using type = std::size_t;
    };
    template<>
    struct associatedType<pType::Float>
    {
        using type = double;
    };
    template<>
    struct associatedType<pType::Other>
    {
        using type = void;
    };
    template<>
    struct associatedType<pType::String>
    {
        using type = char *;
    };
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
    
    //Header container+utilities class
    class OVFHeader{
    private:
        //class data in pimpl
        struct HeaderData;
        HeaderData * data{nullptr};
        
    public:
        //enum class with Mesh type
        
        //c-tor and d-tor of header
        OVFHeader();
        //doing the whole set
        
        ~OVFHeader();
        //the one where version string is explicitly known beforehand, useful when parsing a file, to be defined outside
        OVFHeader(const char*);
        
        //Public interfaces of the header
        //first common utils
        static pType paramType (const OVFParameter&);
        
        //retrieve methods
        const associatedType_t<pType::String> getString(const OVFParameter&) const;
        const associatedType_t<pType::Uint> getUint(const OVFParameter&) const;
        const associatedType_t<pType::Float> getFloat(const OVFParameter&) const;
        
        //universal retrieve
        template<pType p>
        const associatedType_t<p> get(const OVFParameter& ref) const
        {
            if (paramType(ref) != p)
                throw std::logic_error("OVFHeader: Tried to retrieve a wrong type!");
            
            if constexpr(p == pType::String)
                return getString(ref);
            else if constexpr(p == pType::Uint)
                return getUint(ref);
            else if constexpr(p == pType::Float)
                return getFloat(ref);
        }
        
        //setters
        void set(const OVFParameter&, const associatedType_t<pType::String>& );
        void set(const OVFParameter&, const associatedType_t<pType::Uint>& );
        void set(const OVFParameter&, const associatedType_t<pType::Float>& );
    };
    
    //delete implementation for 'Other' type, so compiler will throw an error
    template<>
    const associatedType_t<pType::Other> OVFHeader::get<pType::Other>(const OVFParameter& ref) const = delete;
}
