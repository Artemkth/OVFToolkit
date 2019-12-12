//implemetation of reading parts of interfaces defined in OVFParser
//alongside with internal data-structure, and associated fuckery
#include<fstream>
#include<utility>
#include<map>
#include<vector>
#include<array>
#include<regex>
#include"OVFParser.h"

namespace VField{
    //first internal data of OVFReader
    struct VFieldFile::FileData
    {
        //segment storage
        //                      field        data begin pos      data size
        std::vector<std::tuple<VField, std::ifstream::pos_type, std::size_t>>
            segments{};
        //and the log storage
        std::string log{""};

        FileData() = default;
        FileData(const FileData&) = default;
    };
    
    //logger stuff
    inline void VFieldFile::logMessage(const std::string& msg) const
    {
        if(!data -> log.empty()) data->log += '\n';
        data->log += msg;
    }
    void VFieldFile::clearLog() const
    { data -> log = ""; }
    const std::string& VFieldFile::WorkLog() const
    { return data -> log; }
    
    //housekeeping
    VFieldFile::VFieldFile() noexcept
    {data = new FileData();}
    VFieldFile::~VFieldFile() noexcept
    {delete data;}
    VFieldFile::VFieldFile(const VFieldFile& ref) noexcept
    {
        try{
            data = new FileData(*ref.data);
        } catch (const std::exception& e){
            logMessage((std::string)"Error occured while copying data: " + e.what());
            return;
        }
        fPath = ref.fPath;
    }
    VFieldFile& VFieldFile::operator= (const VFieldFile& ref) noexcept
    {
        try{
            auto buf = new FileData(*ref.data);
            std::swap(data, buf);
            delete buf;
        } catch (const std::exception& e){
            logMessage((std::string)"Error occured while copying data: " + e.what());
            return *this;
        }
        fPath = ref.fPath;
        return *this;
    }
    
    //access stuff
    std::size_t VFieldFile::cntSegments() const noexcept
    { return data->segments.size();}
    //check if some data exists
    bool VFieldFile::isFetched(const std::size_t& index) const noexcept
    {
        if(index >= data->segments.size())
        {
            logMessage((std::string)"VFieldFile::isFetched: Index '" + std::to_string(index) +
                    "' is out of range [0, " + std::to_string(data->segments.size()) + ')');
            return false;
        }
        return std::get<0>(data->segments[index]).isDataPresent();
    }
    //iterator helpers 
    std::size_t VFieldFile::insert(VFieldFile::FieldIterator it, VField&& ref)
    {
        try{
            //checking for exception since VField can be pretty large
            data->segments.insert(data->segments.begin() + it.pos,
                    {ref, std::ifstream::pos_type(), ref.pntCount()});
        }catch (const std::exception& e) {
            logMessage("VFieldFile::insert: trouble inserting at position: "+ std::to_string(it.pos)+
                    ", exception was: " + e.what());
            return 0;
        }
        return 1;
    }
    std::size_t VFieldFile::remove(VFieldFile::FieldIterator it1, VFieldFile::FieldIterator it2)
    {
        data->segments.erase(
                data->segments.begin() + it1.pos,
                data->segments.end() + it2.pos);
        return 1;
    }

    //translate slice into range specifier
    inline std::array<std::size_t, 3> translateSlice(const VField& ref, const VFieldFile::slice_type& Slice)
    {
        auto end = ref.pntCount();
        return
        {
            !Slice.begin.isSpecial()? Slice.begin.getPos() : (Slice.begin == slice_pnt::begin ? 0 : end),
            !Slice.end.isSpecial()? Slice.end.getPos() : (Slice.end == slice_pnt::begin ? 0 : end),
            Slice.stride
        };
    }
    
    //////////////////////////////////////////////////////////
    /// main code for reading stuff based off of regexes /////
    //////////////////////////////////////////////////////////
    ///Break compilation if the float or double are not standard,
    //very sorry, the file is in binary :p
    static_assert(std::numeric_limits<double>::is_iec559, "The systems double is not IEC559 compatible");
    static_assert(std::numeric_limits<float>::is_iec559, "The systems float is not IEC559 compatible");
    //check if numerics are double by default
    static_assert(sizeof(1.0) == sizeof(double), "Double literals function unexpectedly");
    //and just for kicks
    static_assert(sizeof(1.0f) == sizeof(float), "Fload literals function unexpectedly");
    
    //and then regex generators
    
}

