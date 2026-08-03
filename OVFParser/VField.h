//header file for the main vector field storage container, template class VField
#pragma once
#include"OVFHeader.h"
#include"OVFToolkitConfig.h"
#include"ovfparser_export.h"
#include<algorithm>
#include<concepts>
#include<memory>
#include<functional>
#include<ranges>
#include<type_traits>
#include<utility>
#include<vector>

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
    /**
     * @brief Type-erased array deleter used by VField ownership transfers.
     *
     * Preserves the original deleter when data allocated by another API is
     * adopted and later released again.
     */
    template<typename T>
    class DataDeleter
    {
        struct DeleterBase
        {
            virtual ~DeleterBase() = default;
            virtual void destroy(T*) noexcept = 0;
        };

        template<typename Deleter>
        struct DeleterModel final : DeleterBase
        {
            Deleter deleter;

            template<typename U>
            explicit DeleterModel(U&& ref): deleter(std::forward<U>(ref)) {}

            void destroy(T* pointer) noexcept override
            { std::invoke(deleter, pointer); }
        };

        std::unique_ptr<DeleterBase> deleter{};

      public:
        DataDeleter() noexcept = default;

        template<typename Deleter>
          requires std::is_invocable_v<std::remove_reference_t<Deleter>&, T*>
        explicit DataDeleter(Deleter&& ref)
        {
            using StoredDeleter = std::remove_cvref_t<Deleter>;
            if constexpr(!std::is_same_v<StoredDeleter, std::default_delete<T[]>>)
                deleter = std::make_unique<DeleterModel<StoredDeleter>>(
                    std::forward<Deleter>(ref));
        }

        DataDeleter(DataDeleter&&) noexcept = default;
        DataDeleter& operator=(DataDeleter&&) noexcept = default;
        DataDeleter(const DataDeleter&) = delete;
        DataDeleter& operator=(const DataDeleter&) = delete;

        void operator()(T* pointer) noexcept
        {
            if(deleter)
                deleter->destroy(pointer);
            else
                std::default_delete<T[]>{}(pointer);
        }
    };

    template<typename T>
    using OwnedData = std::unique_ptr<T[], DataDeleter<T>>;

    class OVFPARSER_EXPORT VField
    {
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            std::unique_ptr<StorageArray> data{};

            void adoptFloatData(OwnedData<float>, std::size_t);
            void adoptDoubleData(OwnedData<double>, std::size_t);

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
            //Adopt an owned array, preserving its deleter, and clear previous data.
            template<typename T, typename Deleter>
            void adoptData(std::unique_ptr<T[], Deleter> owner, std::size_t size)
            {
                static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                    "VField only stores float or double arrays");

                DataDeleter<T> erasedDeleter{std::move(owner.get_deleter())};
                OwnedData<T> erasedOwner{owner.release(), std::move(erasedDeleter)};
                if constexpr(std::is_same_v<T, float>)
                    adoptFloatData(std::move(erasedOwner), size);
                else
                    adoptDoubleData(std::move(erasedOwner), size);
            }
            //same but with a copy, indicated by pointer being constant
            //throw when out of memory
            template<typename T>
            void setData(const T*, std::size_t);
            /**
             * @brief Copy data from a contiguous, sized range.
             *
             * Float and double ranges preserve their scalar type. Other element
             * types convertible to double are copied and stored as double. The
             * source range retains ownership of its data.
             */
            template<std::ranges::contiguous_range Range>
              requires std::ranges::sized_range<Range> &&
                std::convertible_to<std::ranges::range_value_t<Range>, double>
            void setData(const Range& values)
            {
                using Source = std::remove_cv_t<std::ranges::range_value_t<Range>>;
                const auto size = static_cast<std::size_t>(std::ranges::size(values));

                if constexpr(std::is_same_v<Source, float> || std::is_same_v<Source, double>)
                    setData(std::ranges::data(values), size);
                else
                {
                    auto buffer = std::make_unique<double[]>(size);
                    std::ranges::transform(values, buffer.get(),
                        [](const auto& value) { return static_cast<double>(value); });
                    adoptData(std::move(buffer), size);
                }
            }
            /**
             * @brief Consume an owning contiguous container.
             *
             * Float and double storage is retained without copying and remains
             * owned by its original container and allocator. Other convertible
             * element types are copied and converted to double. Views and
             * borrowed ranges use the copying overload instead.
             */
            template<std::ranges::contiguous_range Range>
              requires std::ranges::sized_range<Range> &&
                (!std::is_lvalue_reference_v<Range>) &&
                (!std::ranges::view<std::remove_cvref_t<Range>>) &&
                (!std::ranges::borrowed_range<std::remove_cvref_t<Range>>) &&
                std::constructible_from<std::remove_cvref_t<Range>, Range> &&
                std::convertible_to<std::ranges::range_value_t<Range>, double>
            void setData(Range&& values)
            {
                using Container = std::remove_cvref_t<Range>;
                using Source = std::remove_cv_t<std::ranges::range_value_t<Range>>;
                const auto size = static_cast<std::size_t>(std::ranges::size(values));

                if constexpr(std::is_same_v<Source, float> || std::is_same_v<Source, double>)
                {
                    auto storage = std::make_unique<Container>(std::forward<Range>(values));
                    auto* pointer = std::ranges::data(*storage);
                    auto deleter = [storage = std::move(storage)](Source*) mutable noexcept
                    { storage.reset(); };
                    std::unique_ptr<Source[], decltype(deleter)> owner{
                        pointer, std::move(deleter)};
                    adoptData(std::move(owner), size);
                }
                else
                {
                    auto buffer = std::make_unique<double[]>(size);
                    std::ranges::transform(values, buffer.get(),
                        [](const auto& value) { return static_cast<double>(value); });
                    adoptData(std::move(buffer), size);
                }
            }
            //get data, throw if trying to get wrong type
            template <typename T>
            T* getData();
            template <typename T>
            const T* getData() const;
            //Transfer ownership of the stored array to the caller. Throws when
            //T does not match the stored scalar type; an empty field returns null.
            template <typename T>
            [[nodiscard]] OwnedData<T> releaseData();
            //Get an independently owned copy, converting scalar type if needed.
            template <typename T>
            [[nodiscard]] std::vector<T> getDataCopy() const;
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
            //Convert this field when necessary, then return the requested view.
            template<typename T>
              std::span<T> rawViewAs()
              {
                convert<T>();
                return rawView<T>();
              }
            template<typename T>
              std::span<const T> rawViewAs() const = delete;

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
            template<typename T>
              vecspan<T> pntViewAs()
              {
                convert<T>();
                return pntView<T>();
              }
            template<typename T>
              vecspan<const T> pntViewAs() const = delete;
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
            template<typename T>
              gridspan<T> gridViewAs()
              {
                convert<T>();
                return gridView<T>();
              }
            template<typename T>
              gridspan<const T> gridViewAs() const = delete;
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
    OVFPARSER_EXPORT OwnedData<float> VField::releaseData<float>();
    extern template
    OVFPARSER_EXPORT OwnedData<double> VField::releaseData<double>();

    //template for getting a copy of internal array, changes to it will be not regarded
    extern template OVFPARSER_EXPORT std::vector<float>  VField::getDataCopy<float>  () const;
    extern template OVFPARSER_EXPORT std::vector<double> VField::getDataCopy<double> () const;
    //instantiation of empty data setter
    template<> inline OVFPARSER_EXPORT void VField::initData<float>(std::size_t size)
    { adoptData(std::make_unique<float[]>(size), size); }
    template<> inline OVFPARSER_EXPORT void VField::initData<double>(std::size_t size)
    { adoptData(std::make_unique<double[]>(size), size); }
    //instantiation of conversions
    extern template OVFPARSER_EXPORT void VField::convert<float>();
    extern template OVFPARSER_EXPORT void VField::convert<double>();
    //instantiation of copying data setters
    extern template OVFPARSER_EXPORT void VField::setData<float>(const float*, std::size_t);
    extern template OVFPARSER_EXPORT void VField::setData<double>(const double*, std::size_t);
    //instantiation of store check
    extern template OVFPARSER_EXPORT bool VField::stores<float>() const noexcept;
    extern template OVFPARSER_EXPORT bool VField::stores<double>() const noexcept;
}
