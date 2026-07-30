#pragma once
// C++23 metaprogramming utilities for OVF parameter metadata.
//
// Provides compile-time access to the parameter sets defined by the OVF
// standard, together with helpers for classifying and organising them.
//
// Design:
//   * SourceParamTable is the single human-maintained source of truth.
//   * ParamTable is generated in OVFParameter ordinal order for O(1) lookup.
//   * Category arrays, names, tokens, and lookup functions are derived from
//     ParamTable.
//   * Compile-time checks guarantee uniqueness and complete enum coverage.
//   * The compiler-signature enum probe is isolated in one helper and can later
//     be replaced by standard reflection without changing the public interface.
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cassert>
#include <variant>
#include <optional>

#include "OVFHeader.h"

namespace VField {

  /**
   * @brief Version signatures accepted at the beginning of an OVF file.
   *
   * Matching is case-insensitive and accepts the historical OVF 1 aliases
   * documented by OOMMF.
   */
  inline const std::map<OVFVersion, std::regex> VersionPatterns{
    {OVFVersion::OVF0,
      std::regex("^#\\s*OOMMF\\s*:\\s*(.+?)\\s+v0.0\\s*$",
        std::regex_constants::icase | std::regex_constants::ECMAScript)},
    {OVFVersion::OVF1,
      std::regex("^#\\s*OOMMF\\s*:\\s*(.+?)\\s+v((?:1.0)|(?:0.99)|(?:0.0a0))\\s*$",
        std::regex_constants::icase | std::regex_constants::ECMAScript)},
    {OVFVersion::OVF2,
      std::regex("^#\\s*OOMMF\\s+OVF\\s+2.0\\s*$",
        std::regex_constants::icase | std::regex_constants::ECMAScript)}
  };

  /**
   * @brief Identify an OVF revision from its file signature.
   *
   * @param signature First line of an OVF file.
   * @return Matching revision, or OVFVersion::Unknown when unsupported.
   */
  [[nodiscard]]
  inline OVFVersion matchVersionString(const std::string& signature)
  {
    for(const auto& [version, pattern]: VersionPatterns)
      if(std::regex_match(signature, pattern))
        return version;
    return OVFVersion::Unknown;
  }

  /**
   * @brief Return the canonical file signature for an OVF revision.
   * @throws std::invalid_argument If version is OVFVersion::Unknown.
   */
  [[nodiscard]]
  inline std::string_view canonicalVersionString(OVFVersion version)
  {
    switch(version)
    {
      case OVFVersion::OVF0: return "# OOMMF: irregular mesh v0.0";
      case OVFVersion::OVF1: return "# OOMMF: rectangular mesh v1.0";
      case OVFVersion::OVF2: return "# OOMMF OVF 2.0";
      case OVFVersion::Unknown:
        throw std::invalid_argument("Unknown OVF revision has no file signature");
    }
    std::unreachable();
  }

  /**
   * @name Binary scalar representation requirements
   *
   * OVF binary data stores IEEE 754 values in fixed four- and eight-byte
   * representations. Reject platforms whose native float or double cannot be
   * serialized without conversion to those standard representations.
   * @{
   */
  static_assert(std::numeric_limits<double>::is_iec559,
      "The system's double is not IEC 559 compatible");
  static_assert(std::numeric_limits<float>::is_iec559,
      "The system's float is not IEC 559 compatible");
  static_assert(sizeof(float) == 4,
      "The system's float has the wrong number of bytes");
  static_assert(sizeof(double) == 8,
      "The system's double has the wrong number of bytes");
  static_assert(sizeof(1.0) == sizeof(double),
      "Double literals have an unexpected representation");
  static_assert(sizeof(1.0f) == sizeof(float),
      "Float literals have an unexpected representation");
  /** @} */

  /**
   * @brief OVF binary data sentinel for a scalar type.
   *
   * Binary data blocks begin with this value so readers can validate scalar
   * width and byte order before consuming the field data. Only the float and
   * double specializations are meaningful OVF sentinels.
   *
   * @tparam T Scalar representation used by the binary data block.
   */
  template<typename T>
    inline constexpr T TestVal{};

  template<>
    inline constexpr float TestVal<float> = 1234567.0f;

  template<>
    inline constexpr double TestVal<double> = 123456789012345.0;

} // namespace VField

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

  using TokenResolver_t = std::string_view (*)(VField::OVFVersion);
  /**
    * @brief Description of one recognised OVF header parameter.
    *
    * A token is either absent, fixed for every OVF version, or resolved from
    * the requested OVF version by a constexpr function.
    */
  struct ParamDescriptor {
    using Token_t = std::variant<std::monostate, std::string_view, TokenResolver_t>;

    Parameter parameter;
    ParameterType type;
    std::string_view description;
    Token_t token{};

    friend constexpr bool operator==(const ParamDescriptor&, const ParamDescriptor&) = default;
  };

  //special resolved token for value units renamed between ovf1 and ovf2
  //ovf0 doesn't require anything, and all metadata is options, so might as well be OVF1 syntax
  constexpr auto valueUnitToken(VField::OVFVersion version) noexcept -> std::string_view 
  { return version == VField::OVFVersion::OVF2 ? "valueunits" : "valueunit"; }

  inline constexpr std::array SourceParamTable{
      ParamDescriptor{Parameter::Open,          ParameterType::Other,  "Opening marker", "Begin"},
      ParamDescriptor{Parameter::Close,         ParameterType::Other,  "Closing marker", "End"},
      ParamDescriptor{Parameter::Segcnt,        ParameterType::Other,  "Segment count marker", "Segment count"},
      ParamDescriptor{Parameter::Mtype,         ParameterType::Other,  "Mesh type", "Meshtype"},
      ParamDescriptor{Parameter::Empty,         ParameterType::Other,  "Empty line"},
      ParamDescriptor{Parameter::Comment,       ParameterType::Other,  "Comment"},
      ParamDescriptor{Parameter::Unknown,       ParameterType::Other,  "Unknown token"},
      ParamDescriptor{Parameter::Invalid,       ParameterType::Other,  "Invalid token"},

      ParamDescriptor{Parameter::VersionString, ParameterType::String, "Version string"},
      ParamDescriptor{Parameter::Title,         ParameterType::String, "Data title", "Title"},
      ParamDescriptor{Parameter::Desc,          ParameterType::String, "Description string", "Desc"},
      ParamDescriptor{Parameter::Munit,         ParameterType::String, "Grid mesh units", "meshunit"},
      ParamDescriptor{Parameter::Vunit,         ParameterType::String, "Vector field value units", valueUnitToken},
      ParamDescriptor{Parameter::Vlabels,       ParameterType::String, "Vector field value labels", "valuelabels"},
      ParamDescriptor{Parameter::Bound,         ParameterType::String, "Bounding frame vertices", "boundary"},

      ParamDescriptor{Parameter::Pcount,        ParameterType::Uint,   "File point count", "pointcount"},
      ParamDescriptor{Parameter::Vdim,          ParameterType::Uint,   "Vector field dimension", "valuedim"},
      ParamDescriptor{Parameter::Xnodes,        ParameterType::Uint,   "Mesh x nodes", "xnodes"},
      ParamDescriptor{Parameter::Ynodes,        ParameterType::Uint,   "Mesh y nodes", "ynodes"},
      ParamDescriptor{Parameter::Znodes,        ParameterType::Uint,   "Mesh z nodes", "znodes"},

      ParamDescriptor{Parameter::Vmult,         ParameterType::Float,  "Vector field value multiplier", "valuemultiplier"},
      ParamDescriptor{Parameter::Vmin,          ParameterType::Float,  "Minimal vector field absolute value", "ValueRangeMinMag"},
      ParamDescriptor{Parameter::Vmax,          ParameterType::Float,  "Maximal vector field absolute value", "ValueRangeMaxMag"},
      ParamDescriptor{Parameter::Xmin,          ParameterType::Float,  "Minimal mesh x value", "xmin"},
      ParamDescriptor{Parameter::Xmax,          ParameterType::Float,  "Maximal mesh x value", "xmax"},
      ParamDescriptor{Parameter::Ymin,          ParameterType::Float,  "Minimal mesh y value", "ymin"},
      ParamDescriptor{Parameter::Ymax,          ParameterType::Float,  "Maximal mesh y value", "ymax"},
      ParamDescriptor{Parameter::Zmin,          ParameterType::Float,  "Minimal mesh z value", "zmin"},
      ParamDescriptor{Parameter::Zmax,          ParameterType::Float,  "Maximal mesh z value", "zmax"},
      ParamDescriptor{Parameter::Xbase,         ParameterType::Float,  "Mesh initial x value", "xbase"},
      ParamDescriptor{Parameter::Ybase,         ParameterType::Float,  "Mesh initial y value", "ybase"},
      ParamDescriptor{Parameter::Zbase,         ParameterType::Float,  "Mesh initial z value", "zbase"},
      ParamDescriptor{Parameter::Xstep,         ParameterType::Float,  "Mesh x step", "xstepsize"},
      ParamDescriptor{Parameter::Ystep,         ParameterType::Float,  "Mesh y step", "ystepsize"},
      ParamDescriptor{Parameter::Zstep,         ParameterType::Float,  "Mesh z step", "zstepsize"},
   };

  consteval bool sourceParametersAreUnique()
   {
      for (auto it{SourceParamTable.begin()}; it != SourceParamTable.end(); ++it) 
      {
         if (std::ranges::find(
                std::next(it),
                SourceParamTable.end(),
                it->parameter,
                &ParamDescriptor::parameter
             ) != SourceParamTable.end()) 
            return false;
      }
      return true;
   }

   template <std::size_t N>
   consteval auto orderTable( const std::array<Parameter, N>& universe )
   {
      std::array<ParamDescriptor, N> result{};

      for (std::size_t i{}; i < N; ++i) 
      {
         const auto source{std::ranges::find(
            SourceParamTable,
            universe[i],
            &ParamDescriptor::parameter
         )};

         if (source != SourceParamTable.end()) {
            result[i] = *source;
         }
      }

      return result;
   }

   template <ParameterType Type, const auto& Table>
   consteval auto parametersOfType()
   {
      constexpr auto count{static_cast<std::size_t>(std::ranges::count(
         Table,
         Type,
         &ParamDescriptor::type
      ))};

      std::array<VField::OVFParameter, count> result{};
      auto out{result.begin()};

      for (const auto& descriptor : Table) {
         if (descriptor.type == Type) {
            *out++ = descriptor.parameter;
         }
      }

      return result;
   }
}

namespace VField {

  using DictionaryHelpers::ParamDescriptor;

  // Keep the range declaration explicit for the C++23 implementation.
  // C++26 reflection can replace DictionaryHelpers::enumValues() later.
  inline constexpr auto FirstParameter { OVFParameter::VersionString };
  inline constexpr auto LastParameter  { OVFParameter::Invalid };
  //these values are validated already by the fact that they addressed through the enum namespace
  //test the compiler compatibility for static checks anyway
  inline constexpr bool _LastIsMax { 
    static_cast<DictionaryHelpers::UnderlyingType>(LastParameter) == std::numeric_limits<DictionaryHelpers::UnderlyingType>::max()};
  inline constexpr bool _FirstIsMin { 
    static_cast<DictionaryHelpers::UnderlyingType>(FirstParameter) == std::numeric_limits<DictionaryHelpers::UnderlyingType>::min()};

  template<bool atMin = _FirstIsMin>
  consteval bool _noParamBeforeFirst()
  {
    if constexpr(atMin)
    {
      return true;
    }
    else
    {
      constexpr auto before { static_cast<OVFParameter>(static_cast<DictionaryHelpers::UnderlyingType>(FirstParameter)-1) };
      return !DictionaryHelpers::IsDefined<before>();
    }
  }
  template<bool atMax = _LastIsMax>
  consteval bool _noParamAfterLast()
  {
    if constexpr(atMax)
    {
      return true;
    }
    else
    {
      constexpr auto after { static_cast<OVFParameter>(static_cast<DictionaryHelpers::UnderlyingType>(LastParameter)+1) };
      return !DictionaryHelpers::IsDefined<after>();
    }
  }

  static_assert( 
      DictionaryHelpers::IsDefined<FirstParameter>() &&
      DictionaryHelpers::IsDefined<LastParameter>() &&
      _noParamAfterLast()&&_noParamBeforeFirst(),
      "The Compiler didn't appreciate the hack, please fix, or wait for reflection!");

  inline constexpr auto ParamUniverse { DictionaryHelpers::enumValues<FirstParameter, LastParameter>() };

  /**
   * @brief OVF parameter metadata ordered exactly like ParamUniverse.
   *
   * The ordinal ordering permits O(1) descriptor lookup by subtracting
   * FirstParameter from the parameter's underlying integer value.
   */
  inline constexpr auto ParamTable{ DictionaryHelpers::orderTable(ParamUniverse) };

  static_assert(
      DictionaryHelpers::sourceParametersAreUnique(),
      "SourceParamTable contains a duplicate OVFParameter");

  static_assert(
      DictionaryHelpers::SourceParamTable.size() == ParamUniverse.size()&&
      std::ranges::equal(
        ParamTable,
        ParamUniverse,
        {},
        &ParamDescriptor::parameter,
        std::identity{}),
      "ParamTable must repeat ParamUniverse in the same order");

  /**
   * @brief Return the metadata descriptor for an OVF parameter.
   *
   * @pre parameter is a declared OVFParameter enumerator.
   * @param parameter Parameter to look up.
   * @return Constant reference to its descriptor.
   *
   * @terminate If parameter is not a declared enumerator.
   */
  [[nodiscard]]
    constexpr const auto& parameterDescriptor(OVFParameter parameter) noexcept
    {
      const auto beg { static_cast<DictionaryHelpers::UnderlyingType> (FirstParameter) };
      const auto end { static_cast<DictionaryHelpers::UnderlyingType> (LastParameter) };
      const auto parmRep { static_cast<DictionaryHelpers::UnderlyingType> (parameter) };

      //OOB check for poor programming, would need to static cast to OVFParameter enum dangerously to trigger
      //earlier static assert insures that every named enum member is in the table
      assert( end >= parmRep && parmRep >= beg );
      //in release this will throw and terminate immediately instead
      return ParamTable.at( static_cast<std::size_t>(parmRep - beg) );
    }

  /**
   * @brief Return the storage category of an OVF parameter.
   *
   * @param parameter Parameter to classify.
   * @return Parameter storage category
   */
  [[nodiscard]]
    constexpr auto paramType(OVFParameter parameter) noexcept
    { return parameterDescriptor(parameter).type; }

  /**
   * @brief Return a human-readable description of an OVF parameter.
   *
   * @param parameter Parameter to describe.
   * @return Static human-readable description
   */
  [[nodiscard]]
    constexpr auto paramName(OVFParameter parameter) noexcept
    { return parameterDescriptor(parameter).description; }

  /**
   * @brief Return the serialized header token for a parameter and OVF version.
   *
   * Fixed tokens are returned directly. Version-dependent tokens are resolved
   * using the supplied OVF version. Service parameters without a serialized
   * token return std::nullopt.
   *
   * @param parameter Parameter whose file token is requested.
   * @param version OVF version for resolving version-dependent spellings.
   * @return Token view, or std::nullopt when the parameter has no file token.
   */
  [[nodiscard]]
    constexpr auto paramToken(OVFParameter parameter, OVFVersion version) noexcept -> std::optional<std::string_view> 
    {
      return std::visit(
          [version](const auto& token) -> std::optional<std::string_view> {
          using TokenType = std::remove_cvref_t<decltype(token)>;
          if constexpr (std::is_same_v<TokenType, std::monostate>) {
          return std::nullopt;
          }
          else if constexpr (std::is_same_v<TokenType, std::string_view>) {
          return token;
          }
          else
          return token(version);
          },
          parameterDescriptor(parameter).token
          );
    }
  template<pType Type>
    inline constexpr auto PTypeList{ DictionaryHelpers::parametersOfType<Type, ParamTable>() };

  inline constexpr auto& FPParamList{ PTypeList<pType::Float> };
  inline constexpr auto& UINTParamList{ PTypeList<pType::Uint> };
  inline constexpr auto& StringParamList{ PTypeList<pType::String> };
  inline constexpr auto& OtherParamList{ PTypeList<pType::Other> };

} // namespace VField
