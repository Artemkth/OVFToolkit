//header file for the main vector field storage container, template class VField
#pragma once
#include<iterator>
#include"OVFHeader.h"
#include"ovfparser_export.h"

namespace VField{
    class OVFPARSER_EXPORT VField
    {
        public:
            template<typename T>
            class VFieldIterator;
            //and standard access fields
            //throw if isWeaklyAddressible() = false, or incorrect data type requested
            template<typename T>
            VFieldIterator<T> begin();
            template<typename T>
            VFieldIterator<T> end();
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            StorageArray *data{nullptr};

            //common defines to be used by iterators to be compatible with algorithm library
            //T is supposed to be a floating point arithmetic type
            template<typename T>
            class CommonVFieldIterator{
                private:
                    const VField* parent {nullptr};
                protected:
                    //addressing bounds
                    std::size_t pntDimension{};
                    //iterator position
                    T* it_data {nullptr};
                    //check if classes are related
                    bool isBrother(const CommonVFieldIterator& ref) const noexcept
                    {return ref.parent == parent && parent != nullptr;}
                public:
                    //interfaces for std::iterator_traits
                    using difference_type = std::ptrdiff_t;
                    using iterator_category = std::random_access_iterator_tag;

                    //friend function declarations to make initialization possible
                    friend VFieldIterator<T> VField::begin<T>();
                    friend VFieldIterator<T> VField::end<T>();

                    //iterator algebra interfaces
                    difference_type operator-(const CommonVFieldIterator& ref) const noexcept 
                    { if(!isBrother(ref)) return 0u; return  (it_data - ref.it_data)/pntDimension; }

                    bool operator == (const CommonVFieldIterator& ref) const noexcept
                    { if(!isBrother(ref)) return false; return it_data == ref.it_data; }

                    bool operator != (const CommonVFieldIterator& ref) const noexcept
                    { return !(*this == ref); }
            };
            
        public:
            //constructors and other general utility
            //*every* can throw iff out of memory (std::bad_alloc)
            VField();
            explicit VField(const associatedType_t<pType::String>& version): VField()
            { Header.set(OVFParameter::VersionString, version); }
            //constructors for fully populating the internals
            template<typename T>
            explicit VField(const OVFHeader& head, std::size_t size = 0, T* ref = nullptr)       : VField()
            { Header = head; if(ref!=nullptr) setData(ref, size); }
            template<typename T>
            explicit VField(const OVFHeader& head, std::size_t size = 0, const T* ref = nullptr) : VField()
            { Header = head; if(ref!=nullptr) setData(ref, size); }
            ~VField();
            //copy and move c-tors
            VField(const VField&);
            VField& operator=(const VField&);
            //I would like to move it move it lol
            VField(VField&& ref)
            {std::swap(Header, ref.Header), std::swap(data, ref.data);}
            VField& operator=(VField&& ref)
            {std::swap(Header, ref.Header), std::swap(data, ref.data); return *this;}

            //comparison operations
            bool isSameDataAs(const VField&) const noexcept;
            bool operator==(const VField&) const noexcept;
            bool operator!=(const VField& ref) const noexcept
            { return !(*this == ref); }
            
            //Header storing all the metadata
            OVFHeader Header{};

            //Access to internal data array
            //number of bytes of current internally stored data
            //return 0 if for some reason cannot be calculated
            std::size_t curDataInternalSize() const noexcept;
            //and number of data points
            std::size_t curDataPoints() const noexcept;
            //is data present
            bool isDataPresent() const noexcept; 
            //clearing out the storage
            void clearData() noexcept;
            //initialize it empty
            template<typename T>
            inline void initData( std::size_t );
            
            //data access methods
            //setting data to whatever, clears previous data 
            void setData(float*, std::size_t) noexcept;
            void setData(double*, std::size_t) noexcept;
            //same but with a copy, indicated by pointer being constant
            //throw when out of memory
            void setData(const float*, std::size_t);
            void setData(const double*, std::size_t);
            //setting specific elements, bool indicates success
            bool setPoint(std::size_t, const float&);
            bool setPoint(std::size_t, const double&);
            //get data, throw if trying to get wrong type
            template <typename T>
            const T* getData() const;
            //get a copy, perform a conversion if needed
            template <typename T>
            T* getDataCopy() const;
            //convert to specified type 
            template <typename T>
            void convert();
            //checking dimensions of internal data
            std::size_t pntCount() const noexcept;
            std::size_t pntDimension() const noexcept;

            //interfaces for validation and deduction
            //defined and realized in OVFGrammar.cpp!
            bool isAddressable() const noexcept;                                     //validate if there is enough information to traverse internal array
            bool isWeaklyAddressable() const noexcept;                               //validate if there is *just* enough information to traverse internal array
            bool isValid();                                                 //check if vector field is in spec
            std::string ValidationReport();                                 //full report of validation results, run validation if needed
            bool DeduceField(const OVFParameter&, bool UseDefault = true);  //try to deduce a field from data already known, use defaults for insignificant data if needed
            std::string DeduceRecursively(const std::size_t& max_iter = 5); //try to deduce out all of the missing required fields
            void Strip() noexcept;                                          //remove optional parameters


            template<typename T>
                class ConstVFieldIterator: public CommonVFieldIterator<T>
            {
                protected:
                    using CommonVFieldIterator<T>::it_data;
                    using CommonVFieldIterator<T>::pntDimension;
                public:
                    //complete the interface for iteraitor_traits
                    using value_type = T const *;
                    using pointer = T* const *;
                    using reference = void;

                    //c-tors
                    ConstVFieldIterator() = default;
                    //convert from base iterator like it is done by default
                    ConstVFieldIterator(const CommonVFieldIterator<T>& ref): CommonVFieldIterator<T>(ref) {}

                    //basic iterator arithmetics
                    ConstVFieldIterator& operator++() noexcept
                    {it_data+=pntDimension; return *this;}

                    ConstVFieldIterator operator++(int) noexcept
                    {CommonVFieldIterator copy = *this; it_data+=pntDimension; return copy;} 

                    ConstVFieldIterator& operator+=(const std::size_t& step) noexcept
                    {it_data+= pntDimension * step; return *this;}

                    friend ConstVFieldIterator operator+(ConstVFieldIterator it, const std::size_t step) noexcept
                    {it+=step; return it;}

                    ConstVFieldIterator& operator--() noexcept
                    {it_data-=pntDimension; return *this;}

                    ConstVFieldIterator operator--(int) noexcept
                    {CommonVFieldIterator copy = *this; it_data-=pntDimension; return copy;} 

                    ConstVFieldIterator& operator-=(const std::size_t& step) noexcept
                    {it_data-= pntDimension * step; return *this;}

                    friend ConstVFieldIterator operator-(ConstVFieldIterator it, const std::size_t step) noexcept
                    {it-=step; return it;}

                    //dereferencing stuff
                    const T* operator*() const noexcept                           //dereferencing a list of points
                    { return it_data; }
                    const T& operator[](const std::size_t& coord) const noexcept    //dereferencing an individual point
                    { return *(it_data + coord); }
            };
            template<typename T>
            class VFieldIterator: public CommonVFieldIterator<T>
            {
                protected:
                    using CommonVFieldIterator<T>::it_data;
                    using CommonVFieldIterator<T>::pntDimension;
                public:
                    using value_type = T *;
                    using pointer = T **;
                    using reference = void;

                    //c-tors
                    VFieldIterator() = default;
                    //convert from base iterator like it is done by default
                    VFieldIterator(const CommonVFieldIterator<T>& ref): CommonVFieldIterator<T>(ref) {}

                    //basic iterator arithmetics
                    VFieldIterator& operator++() noexcept
                    {it_data+=pntDimension; return *this;}

                    VFieldIterator operator++(int) noexcept
                    {CommonVFieldIterator copy = *this; it_data+=pntDimension; return copy;} 

                    VFieldIterator& operator+=(const std::size_t& step) noexcept
                    {it_data+= pntDimension * step; return *this;}

                    friend VFieldIterator operator+(VFieldIterator it, const std::size_t step) noexcept
                    {it+=step; return it;}

                    VFieldIterator& operator--() noexcept
                    {it_data-=pntDimension; return *this;}

                    VFieldIterator operator--(int) noexcept
                    {CommonVFieldIterator copy = *this; it_data-=pntDimension; return copy;} 

                    VFieldIterator& operator-=(const std::size_t& step) noexcept
                    {it_data-= pntDimension * step; return *this;}

                    friend VFieldIterator operator-(VFieldIterator it, const std::size_t step) noexcept
                    {it-=step; return it;}
                    //now to iterating, yay!
                    operator ConstVFieldIterator<T>() const noexcept
                    { return static_cast<CommonVFieldIterator<T>> (*this); }

                    T* operator*() noexcept                           //dereferencing a list of points
                    { return it_data; }
                    T& operator[](const std::size_t& coord) noexcept    //dereferencing an individual point
                    { return *(it_data + coord); }
            };

            //implementing access to const iterator through const_cast and iterator cast
            template<typename T>
            ConstVFieldIterator<T> begin() const
            {return const_cast<VField*>(this)->begin<T>();}

            template<typename T>
            ConstVFieldIterator<T> end() const
            {return const_cast<VField*>(this)->end<T>();}

            template<typename T>
            inline ConstVFieldIterator<T> cbegin() const
            {return begin<T>();}

            template<typename T>
            inline ConstVFieldIterator<T> cend() const
            {return end<T>();}
    };
    
    //available specializations
    //templates for getting the internal array
    template<>
    OVFPARSER_EXPORT const float* VField::getData<float>() const;
    template<>
    OVFPARSER_EXPORT const double* VField::getData<double>() const;

    //template for getting a copy of internal array, changes to it will be not regarded
    template<> OVFPARSER_EXPORT float*  VField::getDataCopy<float>  () const;
    template<> OVFPARSER_EXPORT double* VField::getDataCopy<double> () const;
    //and instantiations of class methods for iterators :'(
    template<> OVFPARSER_EXPORT VField::VFieldIterator<float>  VField::begin<float>  ();
    template<> OVFPARSER_EXPORT VField::VFieldIterator<double> VField::begin<double> ();
    template<> OVFPARSER_EXPORT VField::VFieldIterator<float>  VField::end<float>  (); 
    template<> OVFPARSER_EXPORT VField::VFieldIterator<double> VField::end<double> (); 
    //instantiation of empty data setter
    template<> inline OVFPARSER_EXPORT void VField::initData<float>(std::size_t size)
    { setData(new float[size], size); }
    template<> inline OVFPARSER_EXPORT void VField::initData<double>(std::size_t size)
    { setData(new double[size], size); }
    //instantiation of conversions
    template<> OVFPARSER_EXPORT void VField::convert<float>();
    template<> OVFPARSER_EXPORT void VField::convert<double>();
}

