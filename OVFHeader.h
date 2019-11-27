#pragma once
//small toolkit for working with OVF files(not to be confused with extension, OVF files can be of all the different types like .oef, .obf, .omf, etc.)
//rules for files were taken from specification on NIST website: https://math.nist.gov/oommf/doc/userguide12b3/userguide/Vector_Field_File_Format_OV.html
//OVF 1.0: https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_1.0_format.html
//OVF 2.0: https://math.nist.gov/oommf/doc/userguide12b3/userguide/OVF_2.0_format.html
//to save on clutter all the string interfaces should be defined through char*, saving data encoding-agnostic when possible
//problems with system specific encoding should be handled elsewhere
//only standard header cluttering output will be exception, which is everywhere anyway and has good integration
#include<exception>

namespace VField{

//Class for a field, needs to scream when it is set multiple times, or being accessed unitialized
//just an intermediate container to keep things clean
//has to have everything defined in header since it is template class
template<typename T>
struct HeaderField{
private:
    //internal telling if the value was set
    bool Set{false};
    //value storage, initialized using default constructor when aplicable
    T value{};

public:
    //can be assigned a value
    HeaderField operator=(const T& ref)
    {
        if(Set)
            throw std::logic_error("HeaderField: Trying to reinitialise the field without resetting");
        
        value = ref;
        Set = true;
        
        return *this;
    }
    //or constructed with one
    constexpr HeaderField(const T& ref):value(ref), Set(true) {}
    //otherwise start not set
    constexpr HeaderField() = default;
    
    //set status getter
    constexpr bool IsSet() const
    {
        return Set;
    }
    //conversion back to T, like when getting the value back
    constexpr operator T () const
    {
        if(!Set)
            throw std::logic_error("HeaderField: Reading unitialized field");
    
        return value;
    }
    
    constexpr T getValue() const
    {
        if(!Set)
            throw std::logic_error("HeaderField: Reading unitialized field");
    
        return value;
    }
    
    constexpr bool operator==(const T& ref) const
    {
        if(!Set)
            throw std::logic_error("HeaderField: Trying to compare with unitialized field");
        
        return value == ref;
    }
    
    constexpr void reset()
    {
        Set = false;
    }
};


//OVF syntaxis dictionaries
//parameter types
enum class pType {
    Other,//for service fields and such
    Uint,//storing in std::size_t
    Float,//storing in double
    String//storing in char*
};

//Using a very zeny hack to avoid redefining stuff at .cpp file, please avoid deleting element 'Invalid'
//also it is dependent on UB about elements being serialized in internal representation, i.e. structure being guaranteed to be ordinate
//look at https://groups.google.com/a/isocpp.org/forum/#!topic/std-discussion/q4qoM7Wsdso to see when more accurate option is introduced
//WARNING: Assigning value to any of the enum members will break the hack in other file
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

//type lookup function
constexpr pType ParameterType(const OVFParameter&);

class OVFHeader{
private:
    //Is grid expected to be rectangular?
    HeaderField<bool> isRect{};
    //class data in pimpl
    struct HeaderData;
    HeaderData * data{nullptr};
public:
    //constructors of header
    OVFHeader() = default;
    //the one where version string is explicitly known beforehand, useful when parsing a file, to be defined outside
    OVFHeader(const char* const);

    //interface to get a class of parameter
    
    //get a field from header
    //delete ability to call it from anything but specified rules
    template<typename T>
    HeaderField<T>& operator[](const OVFParameter& pname) = delete;
};
}

