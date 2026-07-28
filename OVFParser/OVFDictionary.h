#pragma once
// C++23 rewrite of OVFDictionary.h.
//
// Design:
//   * ParamTable is the single source of truth.
//   * Category arrays, names, and lookup functions are derived from ParamTable.
//   * Compile-time checks guarantee uniqueness and complete enum coverage.
//   * The compiler-signature enum probe is isolated in one helper and can later
//     be replaced by C++26 reflection without changing the public interface.
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "OVFHeader.h"

//namespace with utilities for pre-compile computation
namespace DictionaryHelpers{
  using Parameter      = VField::OVFParameter;
  using ParameterType  = VField::pType;
  using UnderlyingType = std::underlying_type_t<Parameter>;

  // -----------------------------------------------------------------------------
  // Pre-C++26 enum reflection backend
  // -----------------------------------------------------------------------------

  //some hacky comparison, finds compiler trying to cast instead of writing Enum type explicitly
  template <Parameter P>
    consteval bool IsDefined()
    {
#if defined(__clang__) || defined(__GNUC__)
      return !std::string_view{__PRETTY_FUNCTION__}.contains("(VField::OVFParameter)");
#elif defined(_MSC_VER)
      return !std::string_view{__FUNCSIG__}.contains("(enum VField::OVFParameter)");
#else
      []<bool supported = false>() {
        static_assert(supported,
            "OVF enum probing is unsupported by this compiler");
      }();
      return false;
#endif
    }

  template <Parameter First, std::size_t... I>
    consteval auto enumValuesImpl(std::index_sequence<I...>)
    {
      constexpr auto first = static_cast<UnderlyingType>(First);

      static_assert(
          (IsDefined<static_cast<Parameter>(
                                            first + static_cast<UnderlyingType>(I))>() && ...),
          "OVFParameter must be dense between the configured first and last values");

      return std::array{
        static_cast<Parameter>(first + static_cast<UnderlyingType>(I))...
      };
    }

  template <Parameter First, Parameter Last>
    consteval auto enumValues()
    {
      constexpr auto first = static_cast<UnderlyingType>(First);
      constexpr auto last  = static_cast<UnderlyingType>(Last);

      static_assert(first <= last,
          "The first OVFParameter must not follow the last one");

      constexpr auto count = static_cast<std::size_t>(last - first + 1);
      return enumValuesImpl<First>(std::make_index_sequence<count>{});
    }

  // -----------------------------------------------------------------------------
  // Generic constexpr range helpers
  // -----------------------------------------------------------------------------

  template <std::ranges::input_range R, class T>
    constexpr bool isElem(const T& value, const R& range)
    {
      return std::ranges::contains(range, value);
    }

  template <std::ranges::input_range R1, std::ranges::forward_range R2>
    constexpr bool isIntersecting(const R1& lhs, const R2& rhs)
    {
      return std::ranges::any_of(lhs, [&](const auto& value) {
          return std::ranges::contains(rhs, value);
          });
    }

  template <std::ranges::input_range Subset, std::ranges::forward_range Superset>
    constexpr bool isSubset(const Subset& subset, const Superset& superset)
    {
      return std::ranges::all_of(subset, [&](const auto& value) {
          return std::ranges::contains(superset, value);
          });
    }

  template <std::ranges::forward_range R>
    constexpr bool hasDuplicates(const R& range)
    {
      for (auto it = std::ranges::begin(range); it != std::ranges::end(range); ++it) {
        if (std::ranges::find(std::next(it), std::ranges::end(range), *it)
            != std::ranges::end(range)) {
          return true;
        }
      }
      return false;
    }

  template <std::ranges::input_range R1, std::ranges::forward_range R2>
    constexpr std::size_t countIntersect(const R1& lhs, const R2& rhs)
    {
      return static_cast<std::size_t>(std::ranges::count_if(lhs, [&](const auto& value) {
            return std::ranges::contains(rhs, value);
            }));
    }

  template <class... Arrays>
    constexpr auto makeUnion(const Arrays&... arrays)
    {
      constexpr std::size_t totalSize = (std::tuple_size_v<Arrays> + ... + 0U);
      std::array<Parameter, totalSize> result{};

      auto out = result.begin();
      ([&] {
       out = std::ranges::copy(arrays, out).out;
       }(), ...);

      return result;
    }

  template <std::size_t N>
    constexpr auto removeValue(const std::array<Parameter, N>& values,
        Parameter value)
    {
      static_assert(N > 0);

      std::array<Parameter, N - 1> result{};
      auto out = result.begin();
      bool removed = false;

      for (Parameter item : values) {
        if (!removed && item == value) {
          removed = true;
          continue;
        }

        if (out != result.end()) {
          *out++ = item;
        }
      }

      return result;
    }

  // -----------------------------------------------------------------------------
  // Parameter metadata
  // -----------------------------------------------------------------------------

  struct ParamDescriptor {
    Parameter parameter;
    ParameterType type;
    std::string_view description;

    friend constexpr bool operator==(const ParamDescriptor&, const ParamDescriptor&) = default;
  };

}

namespace VField {

  using DictionaryHelpers::ParamDescriptor;

  // Keep the range declaration explicit for the C++23 implementation.
  // C++26 reflection can replace DictionaryHelpers::enumValues() later.
  inline constexpr OVFParameter FirstParameter = OVFParameter::VersionString;
  inline constexpr OVFParameter LastParameter  = OVFParameter::Invalid;

  inline constexpr auto ParamUniverse { DictionaryHelpers::enumValues<FirstParameter, LastParameter>() };

  // Single source of truth for parameter classification and human-readable names.
  inline constexpr std::array ParamTable{
    ParamDescriptor{OVFParameter::Open,          pType::Other,  "Opening marker"},
      ParamDescriptor{OVFParameter::Close,         pType::Other,  "Closing marker"},
      ParamDescriptor{OVFParameter::Segcnt,        pType::Other,  "Segment count marker"},
      ParamDescriptor{OVFParameter::Mtype,         pType::Other,  "Mesh type"},
      ParamDescriptor{OVFParameter::Empty,         pType::Other,  "Empty line"},
      ParamDescriptor{OVFParameter::Comment,       pType::Other,  "Comment"},
      ParamDescriptor{OVFParameter::Unknown,       pType::Other,  "Unknown token"},
      ParamDescriptor{OVFParameter::Invalid,       pType::Other,  "Invalid token"},

      ParamDescriptor{OVFParameter::VersionString, pType::String, "Version string"},
      ParamDescriptor{OVFParameter::Title,         pType::String, "Data title"},
      ParamDescriptor{OVFParameter::Desc,          pType::String, "Description string"},
      ParamDescriptor{OVFParameter::Munit,         pType::String, "Grid mesh units"},
      ParamDescriptor{OVFParameter::Vunit,         pType::String, "Vector field value units"},
      ParamDescriptor{OVFParameter::Vlabels,       pType::String, "Vector field value labels"},
      ParamDescriptor{OVFParameter::Bound,         pType::String, "Bounding frame vertices"},

      ParamDescriptor{OVFParameter::Pcount,        pType::Uint,   "File point count"},
      ParamDescriptor{OVFParameter::Vdim,          pType::Uint,   "Vector field dimension"},
      ParamDescriptor{OVFParameter::Xnodes,        pType::Uint,   "Mesh x nodes"},
      ParamDescriptor{OVFParameter::Ynodes,        pType::Uint,   "Mesh y nodes"},
      ParamDescriptor{OVFParameter::Znodes,        pType::Uint,   "Mesh z nodes"},

      ParamDescriptor{OVFParameter::Vmult,         pType::Float,  "Vector field value multiplier"},
      ParamDescriptor{OVFParameter::Vmin,          pType::Float,  "Minimal vector field absolute value"},
      ParamDescriptor{OVFParameter::Vmax,          pType::Float,  "Maximal vector field absolute value"},
      ParamDescriptor{OVFParameter::Xmin,          pType::Float,  "Minimal mesh x value"},
      ParamDescriptor{OVFParameter::Xmax,          pType::Float,  "Maximal mesh x value"},
      ParamDescriptor{OVFParameter::Ymin,          pType::Float,  "Minimal mesh y value"},
      ParamDescriptor{OVFParameter::Ymax,          pType::Float,  "Maximal mesh y value"},
      ParamDescriptor{OVFParameter::Zmin,          pType::Float,  "Minimal mesh z value"},
      ParamDescriptor{OVFParameter::Zmax,          pType::Float,  "Maximal mesh z value"},
      ParamDescriptor{OVFParameter::Xbase,         pType::Float,  "Mesh initial x value"},
      ParamDescriptor{OVFParameter::Ybase,         pType::Float,  "Mesh initial y value"},
      ParamDescriptor{OVFParameter::Zbase,         pType::Float,  "Mesh initial z value"},
      ParamDescriptor{OVFParameter::Xstep,         pType::Float,  "Mesh x step"},
      ParamDescriptor{OVFParameter::Ystep,         pType::Float,  "Mesh y step"},
      ParamDescriptor{OVFParameter::Zstep,         pType::Float,  "Mesh z step"},
  };
}

namespace DictionaryHelpers {

  template <VField::pType Type>
    consteval auto parametersOfType()
    {
      constexpr auto count = static_cast<std::size_t>(
          std::ranges::count(VField::ParamTable, Type, &ParamDescriptor::type));

      std::array<VField::OVFParameter, count> result{};
      auto out = result.begin();

      for (const auto& descriptor : VField::ParamTable) {
        if (descriptor.type == Type) {
          *out++ = descriptor.parameter;
        }
      }

      return result;
    }

  consteval bool tableParametersAreUnique()
  {
    for (auto it = VField::ParamTable.begin(); it != VField::ParamTable.end(); ++it) {
      if (std::ranges::find(std::next(it), VField::ParamTable.end(),
            it->parameter, &ParamDescriptor::parameter)
          != VField::ParamTable.end()) {
        return false;
      }
    }
    return true;
  }

  consteval bool tableCoversUniverse()
  {
    return VField::ParamTable.size() == VField::ParamUniverse.size()
      && std::ranges::all_of(VField::ParamUniverse, [](VField::OVFParameter parameter) {
          return std::ranges::contains(
              VField::ParamTable, parameter, &ParamDescriptor::parameter);
          });
  }

} // namespace DictionaryHelpers


namespace VField {

  inline constexpr auto FPParamList     = DictionaryHelpers::parametersOfType<pType::Float>();
  inline constexpr auto UINTParamList   = DictionaryHelpers::parametersOfType<pType::Uint>();
  inline constexpr auto StringParamList = DictionaryHelpers::parametersOfType<pType::String>();
  inline constexpr auto OtherParamList  = DictionaryHelpers::parametersOfType<pType::Other>();

  static_assert(DictionaryHelpers::tableParametersAreUnique(),
      "ParamTable contains a duplicate OVFParameter");
  static_assert(DictionaryHelpers::tableCoversUniverse(),
      "ParamTable must describe every OVFParameter exactly once");

  // Compatibility information formerly supplied by DictionaryHelpers::Helper.
  struct ParamInfo {
    static constexpr auto firstParam = FirstParameter;
    static constexpr auto lastParam  = LastParameter;
    static constexpr auto count      = ParamUniverse.size();

    static constexpr auto begin() noexcept { return ParamUniverse.begin(); }
    static constexpr auto end() noexcept { return ParamUniverse.end(); }
  };

  constexpr pType paramIndex(OVFParameter parameter)
  {
    const auto descriptor = std::ranges::find(
        ParamTable, parameter, &ParamDescriptor::parameter);

    return descriptor != ParamTable.end()
      ? descriptor->type
      : pType::Other;
  }

  constexpr std::string_view ParameterName(OVFParameter parameter)
  {
    const auto descriptor = std::ranges::find(
        ParamTable, parameter, &ParamDescriptor::parameter);

    return descriptor != ParamTable.end()
      ? descriptor->description
      : std::string_view{"Undefined token"};
  }

  constexpr std::span<const OVFParameter> parametersOfType(pType type)
  {
    switch (type) {
      case pType::Float:
        return FPParamList;
      case pType::Uint:
        return UINTParamList;
      case pType::String:
        return StringParamList;
      case pType::Other:
        return OtherParamList;
    }

    std::unreachable();
  }

} // namespace VField
