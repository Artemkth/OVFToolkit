//header file for the main vector field storage container, template class VField
#pragma once
#include"OVFHeader.h"
#include"OVFToolkitConfig.h"
#include"ovfparser_export.h"
#include<memory>

#include<span>
#include <stdexcept>

#if OVFTOOLKIT_HAS_STD_MDSPAN
#include <mdspan>
namespace VField {
namespace md = std;
}
#else
#include <kokkos/mdspan/mdspan.hpp>

namespace VField {
namespace md = Kokkos;
}

#endif
namespace VField{
    class OVFPARSER_EXPORT VField
    {
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            std::unique_ptr<StorageArray> data{};

        public:
            //constructors and other general utility
            //*every* can throw iff out of memory (std::bad_alloc)
            VField();
            explicit VField(const associatedType_t<pType::String>& version): VField()
            { Header.set(OVFParameter::VersionString, version); }
            explicit VField(OVFVersion version): VField()
            { Header = OVFHeader{version}; }
            explicit VField(const OVFHeader& head) : VField()
            { Header = head; }
            //constructors for fully populating the internals
            template<typename T>
            explicit VField(const OVFHeader& head, std::size_t size, const T* ref) : VField()
            { Header = head; if(ref!=nullptr) setData(ref, size); }
            ~VField();
            //copy and move c-tors
            VField(const VField&);
            VField& operator=(const VField&);
            //I would like to move it move it lol
            VField(VField&& ref) noexcept;
            VField& operator=(VField&& ref) noexcept;

            //comparison operations
            bool isSameDataAs(const VField&) const noexcept;
            bool operator==(const VField&) const noexcept;
            
            //Header storing all the metadata
            OVFHeader Header{};

            //Access to internal data array
            //and number of data points
            std::size_t curDataPoints() const noexcept;
            //is data present
            bool isDataPresent() const noexcept; 
            //Delete the stored array and reset the field to an empty state.
            void clearData() noexcept;
            //initialize it empty
            template<typename T>
            inline void initData( std::size_t );
            
            //data access methods
            //setting data to whatever, clears previous data 
            template<typename T>
            void insertData(std::unique_ptr<T[]>, std::size_t) noexcept;
            //same but with a copy, indicated by pointer being constant
            //throw when out of memory
            template<typename T>
            void setData(const T*, std::size_t);
            //setting specific elements, bool indicates success
            template<typename T>
              [[deprecated]]
            bool setPoint(std::size_t, const T&);
            //get data, throw if trying to get wrong type
            template <typename T>
            T* getData();
            template <typename T>
            const T* getData() const;
            //Transfer ownership of the stored array to the caller. Throws when
            //T does not match the stored scalar type; an empty field returns null.
            template <typename T>
            [[nodiscard]] std::unique_ptr<T[]> releaseData();
            //get a copy, perform a conversion if needed
            template <typename T>
            T* getDataCopy() const;
            //convert to specified type 
            template <typename T>
            void convert();
            //checking dimensions of internal data
            std::size_t pntCount() const noexcept;
            std::size_t pntDimension() const noexcept;
            //type deduction
            enum class ScalarType {
              None,
              Float32,
              Float64
            };
            [[nodiscard]]
              ScalarType scalarType() const noexcept;
            template<typename T>
              [[nodiscard]]
              bool stores() const noexcept;
            //number of bytes of current internally stored data
            //return 0 if for some reason cannot be calculated
            std::size_t curDataInternalSize() const noexcept;

            //interfaces for validation and deduction
            //defined and realized in OVFGrammar.cpp!
            bool isAddressable() const noexcept;                                     //validate if there is enough information to traverse internal array
            bool isWeaklyAddressable() const noexcept;                               //validate if there is *just* enough information to traverse internal array
            [[nodiscard]] ValidationResult validate() const;                //validate the header and stored field data
            bool DeduceField(const OVFParameter&, bool UseDefault = true);  //try to deduce a field from data already known, use defaults for insignificant data if needed
            std::string DeduceRecursively(const std::size_t& max_iter = 5); //try to deduce out all of the missing required fields
            void Strip() noexcept;                                          //remove optional parameters

            //data views for disseminating the data
            //raw view outputting raw sequence of values
            //will return on non-empty data and correct type requested
            template< typename T>
              std::span<T> rawView()
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling rawView(), or convert the field.");

                return std::span<T>{getData<T>(), curDataPoints()};
              }
            template< typename T>
              std::span<const T> rawView() const
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling rawView(), or convert the field.");

                return std::span<const T>{getData<T>(), curDataPoints()};
              }

            //point-vise view, output sequence of vectors from vector field
            //best view one can get for unstructured datasets
            //but also compatible with structured grids
            //dynamic rank 2 span
            template<typename T>
              using vecspan = md::mdspan<T, md::dextents<std::size_t, 2>, md::layout_right>;
            template<typename T>
              vecspan<T> pntView()
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling pntView(), or convert the field.");
                if( !isWeaklyAddressable() )
                  throw std::logic_error("The metadata in the Header doesn't permit addressing the array as points.");

                const auto vecLen = pntDimension();
                return vecspan<T>{getData<T>(), pntCount(), vecLen};
              }
            template<typename T>
              vecspan<const T> pntView() const
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling pntView(), or convert the field.");
                if( !isWeaklyAddressable() )
                  throw std::logic_error("The metadata in the Header doesn't permit addressing the array as points.");

                const auto vecLen = pntDimension();
                return vecspan<const T>{getData<T>(), pntCount(), vecLen};
              }
            //grid view for structured 3d data
            //dynamic rank 4 span
            template<typename T>
              using gridspan = md::mdspan<T, md::dextents<std::size_t, 4>>;
            template<typename T>
              gridspan<T> gridView()
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling gridView(), or convert the field.");
                if( !isWeaklyAddressable() || Header.getMeshType() != OVFHeader::MeshType::rectangular ||
                    !Header.isSet(OVFParameter::Xnodes) || !Header.isSet(OVFParameter::Ynodes) ||
                    !Header.isSet(OVFParameter::Znodes) ||
                    pntCount() != Header.getUint(OVFParameter::Xnodes) *
                                  Header.getUint(OVFParameter::Ynodes) *
                                  Header.getUint(OVFParameter::Znodes) )
                  throw std::logic_error("A grid view requires rectangular field data with consistent node counts.");

                return gridspan<T>{getData<T>(),
                    Header.getUint(OVFParameter::Znodes),
                    Header.getUint(OVFParameter::Ynodes),
                    Header.getUint(OVFParameter::Xnodes),
                    pntDimension()};
              }
            template<typename T>
              gridspan<const T> gridView() const
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling gridView(), or convert the field.");
                if( !isWeaklyAddressable() || Header.getMeshType() != OVFHeader::MeshType::rectangular ||
                    !Header.isSet(OVFParameter::Xnodes) || !Header.isSet(OVFParameter::Ynodes) ||
                    !Header.isSet(OVFParameter::Znodes) ||
                    pntCount() != Header.getUint(OVFParameter::Xnodes) *
                                  Header.getUint(OVFParameter::Ynodes) *
                                  Header.getUint(OVFParameter::Znodes) )
                  throw std::logic_error("A grid view requires rectangular field data with consistent node counts.");

                return gridspan<const T>{getData<T>(),
                    Header.getUint(OVFParameter::Znodes),
                    Header.getUint(OVFParameter::Ynodes),
                    Header.getUint(OVFParameter::Xnodes),
                    pntDimension()};
              }
    };
    
    //available specializations
    //templates for getting the internal array
    extern template
    OVFPARSER_EXPORT float* VField::getData<float>();
    extern template
    OVFPARSER_EXPORT double* VField::getData<double>();
    extern template
    OVFPARSER_EXPORT const float* VField::getData<float>() const;
    extern template
    OVFPARSER_EXPORT const double* VField::getData<double>() const;
    extern template
    OVFPARSER_EXPORT std::unique_ptr<float[]> VField::releaseData<float>();
    extern template
    OVFPARSER_EXPORT std::unique_ptr<double[]> VField::releaseData<double>();

    //template for getting a copy of internal array, changes to it will be not regarded
    extern template OVFPARSER_EXPORT float*  VField::getDataCopy<float>  () const;
    extern template OVFPARSER_EXPORT double* VField::getDataCopy<double> () const;
    //instantiation of empty data setter
    template<> inline OVFPARSER_EXPORT void VField::initData<float>(std::size_t size)
    { insertData(std::make_unique<float[]>(size), size); }
    template<> inline OVFPARSER_EXPORT void VField::initData<double>(std::size_t size)
    { insertData(std::make_unique<double[]>(size), size); }
    //instantiation of conversions
    extern template OVFPARSER_EXPORT void VField::convert<float>();
    extern template OVFPARSER_EXPORT void VField::convert<double>();
    //instantiation of data setters
    extern template OVFPARSER_EXPORT void VField::insertData<float>(std::unique_ptr<float[]>, std::size_t) noexcept;
    extern template OVFPARSER_EXPORT void VField::insertData<double>(std::unique_ptr<double[]>, std::size_t) noexcept;
    extern template OVFPARSER_EXPORT void VField::setData<float>(const float*, std::size_t);
    extern template OVFPARSER_EXPORT void VField::setData<double>(const double*, std::size_t);
    //instantiation of store check
    extern template OVFPARSER_EXPORT bool VField::stores<float>() const noexcept;
    extern template OVFPARSER_EXPORT bool VField::stores<double>() const noexcept;
}
