//describing file interfaces for OVFparser
#include"VField.h"
#include"Slice.h"
#include"ovfparser_export.h"

namespace VField{
    //lets hope this doesn't have to be changed at any point
    using pathType = std::string;

    //all exceptions should be handled inside the class, resulting in exceptions being posted into a log file
    class OVFPARSER_EXPORT VFieldFile
    {
    private:
        //data
        pathType fPath{""};
        //and again a PIMPLE blob LULW
        struct FileData;
        FileData* data{nullptr};

        //base iterator
        class VFieldFileIteratorBase{
            protected:
                VFieldFile* parent {nullptr};
                std::size_t pos{};

                bool isBrother(const VFieldFileIteratorBase& ref) const noexcept
                {return ref.parent == parent && parent != nullptr;}

            public:
                using difference_type = std::ptrdiff_t;
                using iterator_category = std::forward_iterator_tag;

                //comparisons
                bool operator == (const VFieldFileIteratorBase& ref) const noexcept
                { return isBrother(ref) && pos == ref.pos; }
                bool operator != (const VFieldFileIteratorBase& ref) const noexcept
                { return !(*this == ref); }

                //friends for construction
                friend class VFieldFile;
        };

    public:
        //TODO: Change into signed type to associate with array boundaries
        using slice_type = slice<associatedType_t<pType::Uint>>;

        //c++ housekeeping
        VFieldFile() noexcept;                                                                       //default constructor makes the empty structure
        explicit VFieldFile(const pathType& name, bool prefetch = true) noexcept: VFieldFile()       //reads the file upon construction
        { read(name, prefetch); }
        VFieldFile(const VFieldFile&) noexcept;
        VFieldFile& operator= (const VFieldFile&) noexcept;
        VFieldFile(VFieldFile&& ref)
        { std::swap(fPath, ref.fPath); std::swap(data, ref.data); }
        VFieldFile& operator= (VFieldFile&& ref)
        { std::swap(fPath, ref.fPath); std::swap(data, ref.data); return *this; }
        ~VFieldFile() noexcept;

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
        bool isFetched(const std::size_t& ) const noexcept;
        
        //slice read operations
        //first slice along the internal point count, array returned is only weakly addressable
        VField readSlice(const std::size_t&, const slice_type&) const noexcept;
        //and then same for slice along mesh of rectangular grid, returns a valid field
        VField readSlice(const std::size_t& sliceN, 
                           const slice_type& xslice,
                           const slice_type& yslice,
                           const slice_type& zslice) const noexcept;

        //forward iterators to access internals for using algorithms
        class ConstFieldIterator : public VFieldFileIteratorBase{
        public:
            //conversion and construction
            ConstFieldIterator() = default;
            ConstFieldIterator(const VFieldFileIteratorBase& ref): VFieldFileIteratorBase(ref) {}

            using value_type = const VField;
            using pointer = const VField*;
            using reference = const VField&;
            //dereference into a VField object, read file if prefetch was true
            VField operator* ()
            { return (*reinterpret_cast<const VFieldFile*>(parent))[pos]; }
            VField slice( const slice_type& Slice)
            { return parent -> readSlice(pos, Slice); }
            const OVFHeader& getHeader()
            { return parent -> getSegmentHeader(pos); }

            ConstFieldIterator& operator++()
            { pos++; return *this; }
            ConstFieldIterator operator++(int)
            { auto copy = *this; pos++; return copy; }
        };
        class FieldIterator : public VFieldFileIteratorBase{
        public:
            //conversion and construction
            FieldIterator() = default;
            FieldIterator(const VFieldFileIteratorBase& ref): VFieldFileIteratorBase(ref) {}

            using value_type = VField;
            using pointer = VField*;
            using reference = VField&;

            VField& operator* ()
            { return (*parent)[pos]; }
            VField slice( const slice_type& Slice )
            { return parent -> readSlice(pos, Slice); }
            const OVFHeader& getHeader()
            { return parent -> getSegmentHeader(pos); }

            FieldIterator& operator++()
            { pos++; return *this; }
            FieldIterator operator++(int)
            { auto copy = *this; pos++; return copy; }

            operator ConstFieldIterator()
            { return static_cast<VFieldFileIteratorBase> (*this); }
        };
        
        //iterators to begining and ending
        FieldIterator begin()
        {
            VFieldFileIteratorBase res;
            res.parent = this;
            res.pos = 0;
            return res;
        }
        FieldIterator end()
        {
            VFieldFileIteratorBase res;
            res.parent = this;
            res.pos = cntSegments();
            return res;
        }
        ConstFieldIterator begin() const
        { return const_cast<VFieldFile*>(this) -> begin(); }
        ConstFieldIterator cbegin() const
        { return this -> begin(); }
        ConstFieldIterator end() const
        { return const_cast<VFieldFile*>(this) -> end(); }
        ConstFieldIterator cend() const
        { return this -> end(); }
    };
}

