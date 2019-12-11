//header file for the main vector field storage container, template class VField
#pragma once
#include"OVFHeader.h"

namespace VField{
    class VField
    {
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            StorageArray *data{nullptr};
            
        public:
            //constructors and other general utility
            VField();
            explicit VField(const associatedType_t<pType::String>& version): VField()
            {Header.set(OVFParameter::VersionString, version);}
            ~VField();
            //copy and move c-tors
            VField(const VField&);
            VField& operator=(const VField&);
            //I would like to move it move it lol
            VField(VField&& ref) = default;
            VField& operator=(VField&&) = default;
            
            OVFHeader Header{};
            //number of bytes of current internally stored data
            std::size_t curDataInternalSize() const;
            //and number of data points
            std::size_t curDataPoints() const;
            //is data present
            bool isDataPresent() const; 
            //clearing out the storage
            void clearData();
            
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
            //getting a data point method is redundant since you can do it on your own
            
            //interfaces for validation and deduction
            //defined and realized in OVFGrammar.cpp!
            bool isAddressable() const;                                     //validate if there is enough information to traverse internal array
            bool isValid();                                                 //check if vector field is in spec
            std::string ValidationReport();                                 //full report of validation results, run validation if needed
            bool DeduceField(const OVFParameter&, bool UseDefault = true);  //try to deduce a field from data already known, use defaults for insignificant data if needed
            std::string DeduceRecursively(const std::size_t& max_iter = 5); //try to deduce out all of the missing required fields
            
            //checking dimensions of internal data
            std::size_t pntCount() const noexcept;
            std::size_t pntDimension() const noexcept;

            //serialization using custom iterators
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
            class ConstVFieldIterator{ 
            private:
                using indexType = associatedType_t<pType::Uint>;
                //addressing bounds
                indexType pntCount{};
                indexType pntDimension{};
                //iterator position
                T* data {nullptr};
                const VField* parent {nullptr};
                explicit ConstVFieldIterator(VField* par, T* ref, const indexType& pcnt, const indexType& pdim): 
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
                long int operator-(const ConstVFieldIterator& ref) const noexcept 
                {if(parent != ref.parent) return 0u; return  (data - ref.data)/pntDimension;};
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
            class VFieldIterator{
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
                long int operator-(const VFieldIterator& ref) const noexcept 
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
    const float* VField::getData<float>() const;
    template<>
    const double* VField::getData<double>() const;

    //template for getting a copy of internal array, changes to it will be not regarded
    template<>
    float* VField::getDataCopy<float>() const;
    template<>
    double* VField::getDataCopy<double>() const;
    //and instantiations of class methods for iterators :'(
    template<> VField::VFieldIterator<float> VField::begin<float> ();
    template<> VField::VFieldIterator<double> VField::begin<double> ();
    template<> VField::ConstVFieldIterator<float> VField::begin<float> () const;
    template<> VField::ConstVFieldIterator<double> VField::begin<double> () const;
    template<> VField::VFieldIterator<float> VField::end<float> (); 
    template<> VField::VFieldIterator<double> VField::end<double> (); 
    template<> VField::ConstVFieldIterator<float> VField::end<float> () const; 
    template<> VField::ConstVFieldIterator<double> VField::end<double> () const; 
}
