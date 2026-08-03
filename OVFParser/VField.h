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
     * @tparam T Array element type.
     */
    template<typename T>
    class DataDeleter
    {
        std::move_only_function<void(T*) noexcept> deleter_;

      public:
        /** @brief Construct a deleter that uses `delete[]`. */
        DataDeleter() noexcept = default;

        /**
         * @brief Preserve an arbitrary array deleter for later invocation.
         * @tparam Deleter Nothrow callable type accepting a `T*`.
         * @param ref Deleter to store; move-only deleters are supported.
         */
        template<typename Deleter>
          requires std::is_nothrow_invocable_v<std::remove_reference_t<Deleter>&, T*>
        explicit DataDeleter(Deleter&& ref):
          deleter_(std::forward<Deleter>(ref)) {}

        /**
         * @brief Move a preserved deleter.
         * @param other Deleter to move from.
         */
        DataDeleter(DataDeleter&& other) noexcept = default;
        /**
         * @brief Replace this deleter by moving another preserved deleter.
         * @param other Deleter to move from.
         * @return This deleter.
         */
        DataDeleter& operator=(DataDeleter&& other) noexcept = default;
        /**
         * @brief Copying is disabled because preserved deleters may be move-only.
         * @param other Deleter that would otherwise be copied.
         */
        DataDeleter(const DataDeleter& other) = delete;
        /**
         * @brief Copy assignment is disabled.
         * @param other Deleter that would otherwise be copied.
         * @return This deleter.
         */
        DataDeleter& operator=(const DataDeleter& other) = delete;

        /**
         * @brief Destroy an array with the preserved deleter.
         * @param pointer Array to destroy; may be null.
         */
        void operator()(T* pointer) noexcept
        {
            if(deleter_)
                deleter_(pointer);
            else
                delete[] pointer;
        }
    };

    /**
     * @brief Owning VField array pointer that retains its original deleter.
     * @tparam T Stored scalar type (`float` or `double`).
     */
    template<typename T>
    using OwnedData = std::unique_ptr<T[], DataDeleter<T>>;

    /** @brief Scalar types supported by VField storage and views. */
    template<typename T>
    concept FieldScalar = std::same_as<T, float> || std::same_as<T, double>;

    /**
     * @brief Vector-field data and its associated OVF metadata.
     *
     * A field stores one contiguous array of either `float` or `double` values.
     * The associated header describes how that flat array is interpreted as points
     * or as a rectangular grid. Views never own the data and remain valid only
     * until the field is destroyed, assigned, converted, cleared, or given new
     * data.
     */
    class OVFPARSER_EXPORT VField
    {
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            std::unique_ptr<StorageArray> storage_{};
            OVFHeader header_{};

            void adoptFloatData(OwnedData<float>, std::size_t);
            void adoptDoubleData(OwnedData<double>, std::size_t);

        public:
            /** @brief Construct an empty OVF 2 field. */
            VField();
            /**
             * @brief Construct an empty field with a version string.
             * @param version OVF version string stored in Header.
             */
            explicit VField(const std::string& version): VField()
            { header_.set(OVFParameter::VersionString, version); }
            /**
             * @brief Construct an empty field for an OVF version.
             * @param version Version used to initialize Header.
             */
            explicit VField(OVFVersion version): VField()
            { header_ = OVFHeader{version}; }
            /**
             * @brief Construct an empty field by copying metadata.
             * @param head Header to copy.
             */
            explicit VField(const OVFHeader& head) : VField()
            { header_ = head; }
            /**
             * @brief Construct a field by copying a flat data array.
             * @tparam T `float` or `double`.
             * @param head Header to copy.
             * @param size Number of scalar values at @p ref.
             * @param ref Source array; a null pointer leaves the field empty.
             */
            template<FieldScalar T>
            explicit VField(const OVFHeader& head, std::size_t size, const T* ref) : VField()
            { header_ = head; if(ref!=nullptr) setData(ref, size); }
            /** @brief Destroy the field and its owned data. */
            ~VField();
            /**
             * @brief Deep-copy another field, including its data.
             * @param other Field to copy.
             */
            VField(const VField& other);
            /**
             * @brief Deep-copy another field, including its data.
             * @param other Field to copy.
             * @return This field.
             */
            VField& operator=(const VField& other);
            /**
             * @brief Move a field without copying its data.
             * @param other Field to move from.
             */
            VField(VField&& other) noexcept;
            /**
             * @brief Replace this field by moving another field.
             * @param other Field to move from.
             * @return This field.
             */
            VField& operator=(VField&& other) noexcept;

            /**
             * @brief Compare stored values while ignoring Header metadata.
             * @param other Field whose values are compared.
             * @return `true` when both arrays contain equivalent values.
             */
            bool isSameDataAs(const VField& other) const noexcept;
            /**
             * @brief Compare both Header metadata and stored values.
             * @param other Field to compare.
             * @return `true` when metadata and values are equivalent.
             */
            bool operator==(const VField& other) const noexcept;
            
            /** @return Mutable metadata describing the stored field data. */
            [[nodiscard]] OVFHeader& header() noexcept { return header_; }
            /** @return Immutable metadata describing the stored field data. */
            [[nodiscard]] const OVFHeader& header() const noexcept { return header_; }

            /** @return Number of scalar values in the flat data array. */
            std::size_t scalarCount() const noexcept;
            /** @return `true` when a non-empty data array is stored. */
            bool isDataPresent() const noexcept; 
            /** @brief Delete the stored array and make the field empty. */
            void clearData() noexcept;
            /**
             * @brief Allocate a value-initialized array and adopt it.
             * @tparam T `float` or `double`.
             * @param size Number of scalar values to allocate.
             */
            template<FieldScalar T>
            void initData(std::size_t size)
            { adoptData(std::make_unique<T[]>(size), size); }
            
            /**
             * @brief Adopt an array and preserve its original deleter.
             * @tparam T `float` or `double`.
             * @tparam Deleter Callable array-deleter type.
             * @param owner Array whose ownership is transferred to this field.
             * @param size Number of scalar values in the array.
             *
             * Existing data is destroyed. A null pointer or zero size produces
             * an empty field. The original deleter is used when the data is
             * cleared, destroyed, or released and subsequently destroyed.
             */
            template<FieldScalar T, typename Deleter>
            void adoptData(std::unique_ptr<T[], Deleter> owner, std::size_t size)
            {
                auto erasedDeleter = [&]() -> DataDeleter<T>
                {
                    using DeleterType = std::remove_cvref_t<Deleter>;
                    if constexpr(std::same_as<DeleterType, std::default_delete<T[]>>)
                        return {};
                    else
                        return DataDeleter<T>{std::move(owner.get_deleter())};
                }();
                OwnedData<T> erasedOwner{owner.release(), std::move(erasedDeleter)};
                if constexpr(std::is_same_v<T, float>)
                    adoptFloatData(std::move(erasedOwner), size);
                else
                    adoptDoubleData(std::move(erasedOwner), size);
            }
            /**
             * @brief Replace the field data with a copy of a flat array.
             * @tparam T `float` or `double`.
             * @param values Source array.
             * @param size Number of scalar values to copy.
             * @pre @p values addresses at least @p size elements, unless size is zero.
             * @throws std::bad_alloc if allocation fails.
             */
            template<FieldScalar T>
            void setData(const T* values, std::size_t size);
            /**
             * @brief Copy data from a contiguous, sized range.
             *
             * Float and double ranges preserve their scalar type. Other element
             * types convertible to double are copied and stored as double. The
             * source range retains ownership of its data.
             *
             * @tparam Range Contiguous, sized range with elements convertible
             * to `double`.
             * @param values Range to copy.
             * @throws std::bad_alloc if allocation fails.
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
             * Float and double containers are moved into field-owned lifetime
             * storage, retaining their allocator. Containers such as vector
             * whose move preserves their backing allocation incur no data copy.
             * Other convertible element types are copied and converted to
             * double. Views and borrowed ranges use the copying overload instead.
             *
             * @tparam Range Owning, movable, contiguous, sized range.
             * @param values Rvalue container to consume.
             * @throws std::bad_alloc if allocation fails.
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
            /**
             * @brief Access the mutable flat array without copying.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Data pointer, or `nullptr` for an empty field.
             * @throws std::bad_variant_access if non-empty data has another type.
             */
            template <FieldScalar T>
            T* data();
            /**
             * @brief Access the immutable flat array without copying.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Data pointer, or `nullptr` for an empty field.
             * @throws std::bad_variant_access if non-empty data has another type.
             */
            template <FieldScalar T>
            const T* data() const;
            /**
             * @brief Transfer ownership of the flat array to the caller.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Owned array retaining its original deleter, or a null
             * owner when this field is empty.
             * @throws std::bad_variant_access if non-empty data has another type.
             *
             * On success this field becomes empty; Header is unchanged.
             */
            template <FieldScalar T>
            [[nodiscard]] OwnedData<T> releaseData();
            /**
             * @brief Copy the flat array, converting scalar type if necessary.
             * @tparam T Destination type (`float` or `double`).
             * @return Independent vector; empty when this field has no data.
             * @throws std::bad_alloc if allocation fails.
             */
            template <FieldScalar T>
            [[nodiscard]] std::vector<T> dataCopy() const;
            /**
             * @brief Convert the stored array in place.
             * @tparam T Destination type (`float` or `double`).
             *
             * This is a no-op for empty fields and fields already storing T.
             * Existing views and pointers are invalidated when conversion occurs.
             * @throws std::bad_alloc if allocation fails.
             */
            template <FieldScalar T>
            void convert();
            /**
             * @return Number of logical field points, or zero when the array
             * cannot be grouped into points from the available metadata.
             */
            std::size_t pointCount() const noexcept;
            /**
             * @return Number of scalar values per logical point, including
             * coordinates for irregular meshes, or zero if indeterminate.
             */
            std::size_t pointDimension() const noexcept;
            /** @brief Runtime scalar representation of the flat array. */
            enum class ScalarType {
              None,    ///< No data is stored.
              Float32, ///< Data is stored as `float`.
              Float64  ///< Data is stored as `double`.
            };
            /** @return Runtime scalar type of the stored array. */
            [[nodiscard]]
              ScalarType scalarType() const noexcept;
            /**
             * @tparam T `float` or `double`.
             * @return `true` exactly when the field stores T.
             */
            template<FieldScalar T>
              [[nodiscard]]
              bool stores() const noexcept;
            /**
             * @return Bytes per stored scalar value, or zero for an empty field.
             * @note Despite its historical name, this is not the total array size.
             */
            std::size_t scalarSizeBytes() const noexcept;
            /** @return Total number of bytes described by the stored array. */
            std::size_t dataSizeBytes() const noexcept;

            /**
             * @return `true` when complete Header metadata determines an expected
             * scalar count equal to the stored array size.
             */
            bool isAddressable() const noexcept;
            /**
             * @return `true` when the available metadata is sufficient to group
             * every stored scalar into equally sized logical points.
             */
            bool isWeaklyAddressable() const noexcept;
            /**
             * @return `true` when gridView() can derive a rectangular grid with
             * consistent, nonzero X, Y, and Z node counts.
             */
            bool isGridAddressable() const noexcept;
            /**
             * @brief Validate Header and its agreement with stored data.
             * @return Successful result, or a diagnostic report identifying
             * invalid parameters and data/header inconsistencies.
             */
            [[nodiscard]] ValidationResult validate() const;
            /**
             * @brief Attempt to deduce one Header parameter from known state.
             * @param parameter Parameter to replace when deduction succeeds.
             * @param useDefault Permit a standard default when deduction from
             * existing state fails.
             * @return `true` if the parameter was set or deliberately cleared.
             */
            bool deduceField(const OVFParameter& parameter, bool useDefault = true);
            /**
             * @brief Repeatedly deduce parameters reported by Header validation.
             * @param maxIterations Maximum number of deduction passes.
             * @return Human-readable report of each pass and stopping condition.
             */
            std::string deduceRecursively(const std::size_t& maxIterations = 5);
            /** @brief Remove optional Header parameters supported by strip. */
            void strip() noexcept;

            /**
             * @brief Return a mutable one-dimensional view of the flat array.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Empty span for an empty field.
             * @throws std::logic_error if non-empty data has another type.
             */
            template<FieldScalar T>
              std::span<T> rawView()
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling rawView(), or convert the field.");

                return std::span<T>{data<T>(), scalarCount()};
              }
            /**
             * @brief Return an immutable one-dimensional view of the flat array.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Empty span for an empty field.
             * @throws std::logic_error if non-empty data has another type.
             */
            template<FieldScalar T>
              std::span<const T> rawView() const
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling rawView(), or convert the field.");

                return std::span<const T>{data<T>(), scalarCount()};
              }
            /**
             * @brief Convert this field if needed and return a mutable raw view.
             * @tparam T Requested type (`float` or `double`).
             * @return One-dimensional view of the converted storage.
             * @note This operation is unavailable on const fields because it may
             * replace the stored array.
             */
            template<FieldScalar T>
              std::span<T> rawViewAs()
              {
                convert<T>();
                return rawView<T>();
              }
            /**
             * @brief Const conversion-on-access is disabled because it mutates data.
             * @tparam T Requested type (`float` or `double`).
             */
            template<FieldScalar T>
              std::span<const T> rawViewAs() const = delete;

            /**
             * @brief Rank-two point view type with `[point, component]` indexing.
             * @tparam T Element type, optionally const-qualified.
             */
            template<typename T>
              using vecspan = md::mdspan<T, md::dextents<std::size_t, 2>, md::layout_right>;
            /**
             * @brief Return a mutable `[point, component]` view.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Rank-two non-owning view of the field data.
             * @throws std::logic_error if the type differs or point dimensions
             * cannot be derived from Header.
             */
            template<FieldScalar T>
              vecspan<T> pointView()
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling pointView(), or convert the field.");
                if( !isWeaklyAddressable() )
                  throw std::logic_error("The metadata in the Header doesn't permit addressing the array as points.");

                const auto vecLen = pointDimension();
                return vecspan<T>{data<T>(), pointCount(), vecLen};
              }
            /**
             * @brief Return an immutable `[point, component]` view.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Rank-two non-owning view of the field data.
             * @throws std::logic_error if the type differs or point dimensions
             * cannot be derived from Header.
             */
            template<FieldScalar T>
              vecspan<const T> pointView() const
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling pointView(), or convert the field.");
                if( !isWeaklyAddressable() )
                  throw std::logic_error("The metadata in the Header doesn't permit addressing the array as points.");

                const auto vecLen = pointDimension();
                return vecspan<const T>{data<T>(), pointCount(), vecLen};
              }
            /**
             * @brief Convert this field if needed and return a mutable point view.
             * @tparam T Requested type (`float` or `double`).
             * @return Rank-two view of the converted storage.
             * @note Unavailable on const fields because conversion may mutate data.
             */
            template<FieldScalar T>
              vecspan<T> pointViewAs()
              {
                convert<T>();
                return pointView<T>();
              }
            /**
             * @brief Const conversion-on-access is disabled because it mutates data.
             * @tparam T Requested type (`float` or `double`).
             */
            template<FieldScalar T>
              vecspan<const T> pointViewAs() const = delete;
            /**
             * @brief Rank-four rectangular-grid view type.
             *
             * Indices are ordered `[z, y, x, component]`.
             * @tparam T Element type, optionally const-qualified.
             */
            template<typename T>
              using gridspan = md::mdspan<T, md::dextents<std::size_t, 4>>;
            /**
             * @brief Return a mutable `[z, y, x, component]` grid view.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Rank-four non-owning view of the field data.
             * @throws std::logic_error if the type differs or
             * isGridAddressable() is false.
             */
            template<FieldScalar T>
              gridspan<T> gridView()
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling gridView(), or convert the field.");
                if( !isGridAddressable() )
                  throw std::logic_error("A grid view requires rectangular field data with consistent node counts.");

                return gridspan<T>{data<T>(),
                    header_.requireAs<std::size_t>(OVFParameter::Znodes),
                    header_.requireAs<std::size_t>(OVFParameter::Ynodes),
                    header_.requireAs<std::size_t>(OVFParameter::Xnodes),
                    pointDimension()};
              }
            /**
             * @brief Return an immutable `[z, y, x, component]` grid view.
             * @tparam T Requested stored type (`float` or `double`).
             * @return Rank-four non-owning view of the field data.
             * @throws std::logic_error if the type differs or
             * isGridAddressable() is false.
             */
            template<FieldScalar T>
              gridspan<const T> gridView() const
              {
                if( isDataPresent() && !stores<T>() )
                  throw std::logic_error("Trying to access wrong stored field array; please check stores<T>() before calling gridView(), or convert the field.");
                if( !isGridAddressable() )
                  throw std::logic_error("A grid view requires rectangular field data with consistent node counts.");

                return gridspan<const T>{data<T>(),
                    header_.requireAs<std::size_t>(OVFParameter::Znodes),
                    header_.requireAs<std::size_t>(OVFParameter::Ynodes),
                    header_.requireAs<std::size_t>(OVFParameter::Xnodes),
                    pointDimension()};
              }
            /**
             * @brief Convert this field if needed and return a mutable grid view.
             * @tparam T Requested type (`float` or `double`).
             * @return Rank-four view of the converted storage.
             * @note Unavailable on const fields because conversion may mutate data.
             */
            template<FieldScalar T>
              gridspan<T> gridViewAs()
              {
                convert<T>();
                return gridView<T>();
              }
            /**
             * @brief Const conversion-on-access is disabled because it mutates data.
             * @tparam T Requested type (`float` or `double`).
             */
            template<FieldScalar T>
              gridspan<const T> gridViewAs() const = delete;
    };

    /** @cond */
    //available specializations
    //templates for getting the internal array
    extern template
    OVFPARSER_EXPORT float* VField::data<float>();
    extern template
    OVFPARSER_EXPORT double* VField::data<double>();
    extern template
    OVFPARSER_EXPORT const float* VField::data<float>() const;
    extern template
    OVFPARSER_EXPORT const double* VField::data<double>() const;
    extern template
    OVFPARSER_EXPORT OwnedData<float> VField::releaseData<float>();
    extern template
    OVFPARSER_EXPORT OwnedData<double> VField::releaseData<double>();

    //template for getting a copy of internal array, changes to it will be not regarded
    extern template OVFPARSER_EXPORT std::vector<float>  VField::dataCopy<float>  () const;
    extern template OVFPARSER_EXPORT std::vector<double> VField::dataCopy<double> () const;
    //instantiation of conversions
    extern template OVFPARSER_EXPORT void VField::convert<float>();
    extern template OVFPARSER_EXPORT void VField::convert<double>();
    //instantiation of copying data setters
    extern template OVFPARSER_EXPORT void VField::setData<float>(const float*, std::size_t);
    extern template OVFPARSER_EXPORT void VField::setData<double>(const double*, std::size_t);
    //instantiation of store check
    extern template OVFPARSER_EXPORT bool VField::stores<float>() const noexcept;
    extern template OVFPARSER_EXPORT bool VField::stores<double>() const noexcept;
    /** @endcond */
}
