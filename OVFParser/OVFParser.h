//describing file interfaces for OVFparser
#include"VField.h"
#include"Slice.h"

namespace VField{
    //lets hope this doesn't have to be changed at any point
    using pathType = std::string;

    //all exceptions should be handled inside the class, resulting in exceptions being posted into a log file
    class VFieldFile
    {
    private:
        //data
        pathType fPath{""};
        //and again a PIMPLE blob LULW
        struct FileData;
        FileData* data{nullptr};
    public:
        using slice_type = slice<associatedType_t<pType::Uint>>;

        //c++ housekeeping
        VFieldFile() noexcept;                                                         //default constructor makes the empty structure
        inline explicit VFieldFile(const pathType&, bool prefetch = true) noexcept;    //reads the file upon construction
        VFieldFile(const VFieldFile&) noexcept;
        VFieldFile& operator= (const VFieldFile&) noexcept;
        VFieldFile(VFieldFile&&) = default;
        VFieldFile& operator= (VFieldFile&&) = default;
        ~VFieldFile() noexcept;

        //interfaces
        //logging functions
        const std::string& WorkLog() const;
        void clearLog() const;
        void logMessage(const std::string&) const;

        //general file i/o methods
        //basic read/write
        //read a file, prefetch indicated that only positions for data should be recovered
        bool read(const pathType&, bool prefetch = true) noexcept;
        //write a file from data present
        bool write(const pathType&) const noexcept;   //doesn't change internals, only changes host FS stuff
        
        //number of segments from the file
        //data access
        std::size_t cntSegments() const noexcept;
        //next two throw if index is outside of array, and force reading the file if prefetch == true
        VField& operator[] (const std::size_t& )  noexcept;
        VField&& operator[] (const std::size_t& ) const noexcept;
        //you can free internal storage by yourself if needed to, since you have the explicit access
        bool isFetched(const std::size_t& ) const noexcept;
        
        //slice read operations
        //first slice along the internal point count, array returned is only weakly addressable
        VField&& readSlice(const std::size_t&, const slice_type&) const noexcept;
        //and then same for slice along mesh of rectangular grid, returns a valid field
        VField&& readSlice(const std::size_t& sliceN, 
                           const slice_type& xslice,
                           const slice_type& yslice,
                           const slice_type& zslice) const noexcept;

        //forward iterators to access internals for using algorithms
        class FieldIterator{
            VFieldFile *parent {nullptr};
            std::size_t pos{ 0 };
        public:
            FieldIterator() = default;
            FieldIterator(VFieldFile* ref, std::size_t pos_): parent(ref), pos(pos_) {}
            //dereference into a VField object, read file if prefetch was true
            VField& operator* ()
            { return (*parent)[pos]; }
            VField&& slice( const slice_type& Slice)
            { return parent -> readSlice(pos, Slice); }

            FieldIterator& operator++()
            { pos++; return *this; }
            FieldIterator operator++(int)
            { auto copy = *this; pos++; return copy; }
            bool operator== (const FieldIterator& ref) const 
            { if(parent != ref.parent) return false; return pos == ref.pos;}
            inline bool operator!= (const FieldIterator& ref) const
            { return ! (*this == ref); }
            friend class VFieldFile;
        };
        class ConstFieldIterator{
        };
        
        //iterators to begining and ending
        FieldIterator begin()
        {return FieldIterator(this, 0);}
        ConstFieldIterator begin() const;
        ConstFieldIterator cbegin() const
        {return begin();}
        FieldIterator end()
        {return FieldIterator(this, cntSegments());}
        ConstFieldIterator end() const;
        ConstFieldIterator cend() const
        {return end();}
        
        //some methods for populating the VFieldFile
        std::size_t insert(FieldIterator, VField&&);
        std::size_t insert(FieldIterator it, const VField& ref)
        {return insert(it, VField(ref));}
        template<typename T>
        std::size_t insert(FieldIterator it, T begin, T end)
        {
            if(begin == end)
                return 0;
            std::size_t size {0};
            do{
                size += insert(it++, *begin);
            }while( ++begin != end );
            return size;
        }
        std::size_t remove(FieldIterator, FieldIterator);
        std::size_t push_back(VField&& ref)
        {
            if(cntSegments() == 0)
                return insert(begin(), ref);
            auto last = end();
            last.pos -= 1;
            return insert(last, ref); 
        }
        inline std::size_t push_back(const VField& ref)
        {return push_back(VField(ref)); }
    };

    //implementation of constructor from a file name
    VFieldFile::VFieldFile(const pathType& name, bool prefetch) noexcept: VFieldFile()
    { read(name, prefetch); }
}

