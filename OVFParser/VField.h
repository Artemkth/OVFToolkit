//header file for the main vector field storage container, template class VField
#pragma once
#include<iterator>
#include"OVFHeader.h"
#include"ovfparser_export.h"

namespace VField{
    class OVFPARSER_EXPORT VField
    {
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            StorageArray *data{nullptr};

            //common defines to be used by iterators to be compatible with algorithm library
            //T is supposed to be a floating point arithmetic type
            template<typename T>
            class CommonVFieldIterator{
                public:
                    using difference_type = std::ptrdiff_t;
                    using value_type = T*;
                    using pointer = T**;
                    using reference = void;
                    using iterator_category = std::random_access_iterator_tag;
            };
            
        public:
            //constructors and other general utility
            VField();
            explicit VField(const associatedType_t<pType::String>& version): VField()
            {Header.set(OVFParameter::VersionString, version);}
            //constructors for fully populating the internals
            template<typename T>
            VField(const OVFHeader& head, std::size_t size = 0, T* ref = nullptr)       : VField()
            { Header = head; if(ref!=nullptr) setData(ref, size); }
            template<typename T>
            VField(const OVFHeader& head, std::size_t size = 0, const T* ref = nullptr) : VField()
            { Header = head; if(ref!=nullptr) setData(ref, size); }
            ~VField();
            //copy and move c-tors
            VField(const VField&);
            VField& operator=(const VField&);
            //I would like to move it move it lol
            VField(VField&& ref)
            {std::swap(data, ref.data);}
            VField& operator=(VField&& ref)
            {std::swap(data, ref.data); return *this;}
            
            OVFHeader Header{};
            //number of bytes of current internally stored data
            std::size_t curDataInternalSize() const;
            //and number of data points
            std::size_t curDataPoints() const;
            //is data present
            bool isDataPresent() const; 
            //clearing out the storage
            void clearData();
            //initialize it empty
            template<typename T>
            inline void initData(const std::size_t& );
            
            //data access methods
            //setting data to whatever, 
            void setData(float*, const std::size_t&);
            void setData(double*, const std::size_t&);
            //same but with a copy, indicated by pointer being constant
            void setData(const float*, const std::size_t&);
            void setData(const double*, const std::size_t&);
            //setting specific elements, bool indicates success
            bool setPoint(const std::size_t&, const float&);
            bool setPoint(const std::size_t&, const double&);
            //get data
            template <typename T>
            const T* getData() const;
            //get a copy
            template <typename T>
            T* getDataCopy() const;
            //convert to specified type 
            template <typename T>
            void convert();

            //interfaces for validation and deduction
            //defined and realized in OVFGrammar.cpp!
            bool isAddressable() const;                                     //validate if there is enough information to traverse internal array
            bool isWeaklyAddressable() const;                               //validate if there is *just* enough information to traverse internal array
            bool isValid();                                                 //check if vector field is in spec
            std::string ValidationReport();                                 //full report of validation results, run validation if needed
            bool DeduceField(const OVFParameter&, bool UseDefault = true);  //try to deduce a field from data already known, use defaults for insignificant data if needed
            std::string DeduceRecursively(const std::size_t& max_iter = 5); //try to deduce out all of the missing required fields
            void Strip() noexcept;                                          //remove optional parameters
            
            //checking dimensions of internal data
            std::size_t pntCount() const noexcept;
            std::size_t pntDimension() const noexcept;

            //serialization using custom iterators
            //TODO: curb more of the iterator goodness into parent class above^
            template<typename T>
            class VFieldIterator;
            template<typename T>
            class ConstVFieldIterator;
            //and standard access fields
            template<typename T>
            VFieldIterator<T> begin();
            template<typename T>
            ConstVFieldIterator<T> begin() const;
            template<typename T>
            VFieldIterator<T> end();
            template<typename T>
            ConstVFieldIterator<T> end() const;
            template<typename T>
            inline ConstVFieldIterator<T> cbegin() const
            {return begin<T>();}
            template<typename T>
            inline ConstVFieldIterator<T> cend() const
            {return end<T>();}

            template<typename T>
            class ConstVFieldIterator: public CommonVFieldIterator<T>{
            private:
                using indexType = associatedType_t<pType::Uint>;
                //addressing bounds
                indexType pntCount{};
                indexType pntDimension{};
                //iterator position
                T* data {nullptr};
                const VField* parent {nullptr};
                explicit ConstVFieldIterator(const VField* par, T* ref, const indexType& pcnt, const indexType& pdim): 
                    parent(par), data(ref), pntCount(pcnt), pntDimension(pdim)
                {} 
            public:
                ConstVFieldIterator(const VField* ref = nullptr): parent(ref) {}
                ~ConstVFieldIterator() = default;
                ConstVFieldIterator(const ConstVFieldIterator&) = default;
                ConstVFieldIterator& operator= (const ConstVFieldIterator&) = default;
                //now to iterating, yay!
                ConstVFieldIterator& operator++() noexcept
                {data+=pntDimension; return *this;}
                ConstVFieldIterator operator++(int) noexcept
                {ConstVFieldIterator copy = *this; data+=pntDimension; return copy;} 
                ConstVFieldIterator& operator+=(const std::size_t& step) noexcept
                {data+= pntDimension * step; return *this;}
                friend ConstVFieldIterator operator+(ConstVFieldIterator it, const std::size_t step) noexcept
                {it+=step; return it;}
                ConstVFieldIterator& operator--() noexcept
                {data-=pntDimension; return *this;}
                ConstVFieldIterator operator--(int) noexcept
                {ConstVFieldIterator copy = *this; data-=pntDimension; return copy;} 
                ConstVFieldIterator& operator-=(const std::size_t& step) noexcept
                {data-= pntDimension * step; return *this;}
                friend ConstVFieldIterator operator-(ConstVFieldIterator it, const std::size_t step) noexcept
                {it-=step; return it;}
                typename CommonVFieldIterator<T>::difference_type operator-(const ConstVFieldIterator& ref) const noexcept 
                {if(parent != ref.parent) return 0u; return  (data - ref.data)/pntDimension;}
                bool operator == (const ConstVFieldIterator& ref) const noexcept
                {if(parent != ref.parent) return false; return data == ref.data;}
                bool operator != (const ConstVFieldIterator& ref) const noexcept
                {if(parent != ref.parent) return true; return data != ref.data;}
                //dereferencing stuff
                const T* operator*() const noexcept                           //dereferencing a list of points
                { return data; }
                const T& operator[](const indexType& coord) const noexcept    //dereferencing an individual point
                { return *(data + coord); }
                
                //friend class declaration to make conversion possible
                friend class VFieldIterator<T>;
                friend ConstVFieldIterator<T> VField::begin<T>() const;
                friend ConstVFieldIterator<T> VField::end<T>() const;
            };
            //and methods to get the iterators
            template<typename T>
            class VFieldIterator: public CommonVFieldIterator<T>{
            private:
                using indexType = associatedType_t<pType::Uint>;
                //addressing bounds
                indexType pntCount{};
                indexType pntDimension{};
                //iterator position
                T* data {nullptr};
                const VField* parent {nullptr};
                explicit VFieldIterator(const VField* par,T* ref, const indexType& pcnt, const indexType& pdim): 
                    parent(par), data(ref), pntCount(pcnt), pntDimension(pdim)
                {} 
            public:
                VFieldIterator(const VField* ref = nullptr): parent(ref) {}
                ~VFieldIterator() = default;
                VFieldIterator(const VFieldIterator&) = default;
                VFieldIterator& operator= (const VFieldIterator&) = default;

                //now to iterating, yay!
                VFieldIterator& operator++() noexcept
                {data+=pntDimension; return *this;}
                VFieldIterator operator++(int) noexcept
                {VFieldIterator copy = *this; data+=pntDimension; return copy;} 
                VFieldIterator& operator+=(const std::size_t& step) noexcept
                {data+= pntDimension * step; return *this;}
                friend VFieldIterator operator+(VFieldIterator it, const std::size_t step) noexcept
                {it+=step; return it;}
                VFieldIterator& operator--() noexcept
                {data-=pntDimension; return *this;}
                VFieldIterator operator--(int) noexcept
                {VFieldIterator copy = *this; data-=pntDimension; return copy;} 
                VFieldIterator& operator-=(const std::size_t& step) noexcept
                {data-= pntDimension * step; return *this;}
                friend VFieldIterator operator-(VFieldIterator it, const std::size_t step) noexcept
                {it-=step; return it;}
                typename CommonVFieldIterator<T>::difference_type operator-(const VFieldIterator& ref) const noexcept 
                {if(parent != ref.parent) return 0u; return  (data - ref.data)/pntDimension;};
                bool operator == (const VFieldIterator& ref) const noexcept
                {if(parent != ref.parent) return false; return data == ref.data;}
                bool operator != (const VFieldIterator& ref) const noexcept
                {if(parent != ref.parent) return true; return data != ref.data;}
                //dereferencing stuff

                operator ConstVFieldIterator<T>() const noexcept
                { return ConstVFieldIterator(parent, data, pntCount, pntDimension); }

                T* operator*() noexcept                           //dereferencing a list of points
                { return data; }
                T& operator[](const indexType& coord) noexcept    //dereferencing an individual point
                { return *(data + coord); }
                //friend class declaration to make conversion possible
                friend VFieldIterator<T> VField::begin<T>();
                friend VFieldIterator<T> VField::end<T>();
            };
    };
    
    //available specializations
    //templates for getting the internal array
    template<>
    OVFPARSER_EXPORT const float* VField::getData<float>() const;
    template<>
    OVFPARSER_EXPORT const double* VField::getData<double>() const;

    //template for getting a copy of internal array, changes to it will be not regarded
    template<>
    OVFPARSER_EXPORT float* VField::getDataCopy<float>() const;
    template<>
    OVFPARSER_EXPORT double* VField::getDataCopy<double>() const;
    //and instantiations of class methods for iterators :'(
    template<> OVFPARSER_EXPORT VField::VFieldIterator<float> VField::begin<float> ();
    template<> OVFPARSER_EXPORT VField::VFieldIterator<double> VField::begin<double> ();
    template<> OVFPARSER_EXPORT VField::ConstVFieldIterator<float> VField::begin<float> () const;
    template<> OVFPARSER_EXPORT VField::ConstVFieldIterator<double> VField::begin<double> () const;
    template<> OVFPARSER_EXPORT VField::VFieldIterator<float> VField::end<float> (); 
    template<> OVFPARSER_EXPORT VField::VFieldIterator<double> VField::end<double> (); 
    template<> OVFPARSER_EXPORT VField::ConstVFieldIterator<float> VField::end<float> () const; 
    template<> OVFPARSER_EXPORT VField::ConstVFieldIterator<double> VField::end<double> () const; 
    //instantiation of empty data setter
    template<> inline OVFPARSER_EXPORT void VField::initData<float>(const std::size_t& size)
    { setData(new float[size], size); }
    template<> inline OVFPARSER_EXPORT void VField::initData<double>(const std::size_t& size)
    { setData(new double[size], size); }
    //instantiation of conversions
    template<> OVFPARSER_EXPORT void VField::convert<float>();
    template<> OVFPARSER_EXPORT void VField::convert<double>();
}

