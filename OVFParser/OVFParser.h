//describing file interfaces for OVFparser
#include"VField.h"
#include"Slice.h"
#include"ovfparser_export.h"
#include<memory>
#include<span>

namespace VField{
    //lets hope this doesn't have to be changed at any point
    using pathType = std::string;

    //I/O errors are reported through WorkLog; allocation failures may still throw.
    class OVFPARSER_EXPORT VFieldFile
    {
    private:
        //data
        pathType fPath{""};
        //opaque implementation storage
        struct FileData;
        std::unique_ptr<FileData> data{};

        VField& fetch(std::size_t) const;

    public:
        //TODO: Change into signed type to associate with array boundaries
        using slice_type = slice<associatedType_t<pType::Uint>>;

        //c++ housekeeping
        VFieldFile();                                                                       //default constructor makes the empty structure
        explicit VFieldFile(const pathType& name, bool prefetch = true): VFieldFile()       //reads the file upon construction
        { read(name, prefetch); }
        VFieldFile(const VFieldFile&);
        VFieldFile& operator= (const VFieldFile&);
        VFieldFile(VFieldFile&&) noexcept;
        VFieldFile& operator= (VFieldFile&&) noexcept;
        ~VFieldFile();

        //interfaces
        //logging functions
        const std::string& WorkLog() const;
        void clearLog() const;
        void logMessage(const std::string&) const;

        //get current file path
        const std::string& getCurrentPath() const
        { return fPath; }

        //general file i/o methods
        //basic read/write
        //read a file, prefetch indicated that only positions for data should be recovered
        bool read(const pathType&, bool prefetch = true) noexcept;
        
        //number of segments from the file
        //data access
        std::size_t cntSegments() const noexcept;
        //next two throw if index is outside of array, and force reading the file if prefetch == true
        VField& operator[] (std::size_t ) &;
        VField operator[] (std::size_t ) const & noexcept;
        const OVFHeader& getSegmentHeader(std::size_t ) const &;
        //you can free internal storage by yourself if needed to, since you have the explicit access
        bool isFetched(std::size_t ) const noexcept;
        bool hasData(std::size_t ) const noexcept;

        //contiguous access to all segments; pending prefetched data is loaded first
        std::span<VField> fieldView();
        std::span<const VField> fieldView() const;
        
        //slice read operations
        //first slice along the internal point count, array returned is only weakly addressable
        VField readSlice(const std::size_t&, const slice_type&) const noexcept;
        //and then same for slice along mesh of rectangular grid, returns a valid field
        VField readSlice(const std::size_t& sliceN, 
                           const slice_type& xslice,
                           const slice_type& yslice,
                           const slice_type& zslice) const noexcept;

    };
}
