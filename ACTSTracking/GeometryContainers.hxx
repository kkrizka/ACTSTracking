// Copied from GeometryContainers.hpp from the Acts project
#pragma once

#include <Acts/Geometry/GeometryIdentifier.hpp>

#include <boost/container/flat_set.hpp>

namespace ACTSTracking
{
  // extract the geometry identifier from a variety of types
  struct GeometryIdGetter {
    // explicit geometry identifier are just forwarded
    constexpr Acts::GeometryIdentifier operator()(
						  Acts::GeometryIdentifier geometryId) const {
      return geometryId;
    }
    // encoded geometry ids are converted back to geometry identifiers.
    constexpr Acts::GeometryIdentifier operator()(
						  Acts::GeometryIdentifier::Value encoded) const {
      return Acts::GeometryIdentifier(encoded);
    }
    // support elements in map-like structures.
    template <typename T>
    constexpr Acts::GeometryIdentifier operator()(
						  const std::pair<Acts::GeometryIdentifier, T>& mapItem) const {
      return mapItem.first;
    }
    // support elements that implement `.geometryId()`.
    template <typename T>
    inline auto operator()(const T& thing) const
      -> decltype(thing.geometryId(), Acts::GeometryIdentifier()) {
      return thing.geometryId();
    }
  };

  namespace detail {
    struct CompareGeometryId {
      // indicate that comparisons between keys and full objects are allowed.
      using is_transparent = void;
      // compare two elements using the automatic key extraction.
      template <typename Left, typename Right>
      constexpr bool operator()(Left&& lhs, Right&& rhs) const {
	return GeometryIdGetter()(lhs) < GeometryIdGetter()(rhs);
      }
    };
  }

  /// Store elements that know their detector geometry id, e.g. simulation hits.
  ///
  /// @tparam T type to be stored, must be compatible with `CompareGeometryId`
  ///
  /// The container stores an arbitrary number of elements for any geometry
  /// id. Elements can be retrieved via the geometry id; elements can be selected
  /// for a specific geometry id or for a larger range, e.g. a volume or a layer
  /// within the geometry hierachy using the helper functions below. Elements can
  /// also be accessed by index that uniquely identifies each element regardless
  /// of geometry id.
  template <typename T>
  using GeometryIdMultiset =
    boost::container::flat_multiset<T, detail::CompareGeometryId>;

  /// Store elements indexed by an geometry id.
  ///
  /// @tparam T type to be stored
  ///
  /// The behaviour is the same as for the `GeometryIdMultiset` except that the
  /// stored elements do not know their geometry id themself. When iterating
  /// the iterator elements behave as for the `std::map`, i.e.
  ///
  ///     for (const auto& entry: elements) {
  ///         auto id = entry.first; // geometry id
  ///         const auto& el = entry.second; // stored element
  ///     }
  ///
  template <typename T>
  using GeometryIdMultimap =
    GeometryIdMultiset<std::pair<Acts::GeometryIdentifier, T>>;

  /// The accessor for the GeometryIdMultiset container
  ///
  /// It wraps up a few lookup methods to be used in the Combinatorial Kalman
  /// Filter
  template <typename T>
  struct GeometryIdMultisetAccessor {
    using Container = GeometryIdMultiset<T>;
    using Key = Acts::GeometryIdentifier;
    using Value = typename GeometryIdMultiset<T>::value_type;
    using Iterator = typename GeometryIdMultiset<T>::const_iterator;

    // pointer to the container
    const Container* container = nullptr;

    // count the number of elements with requested geoId
    size_t count(const Acts::GeometryIdentifier& geoId) const {
      assert(container != nullptr);
      return container->count(geoId);
    }

    // get the range of elements with requested geoId
    std::pair<Iterator, Iterator> range(
					const Acts::GeometryIdentifier& geoId) const {
      assert(container != nullptr);
      return container->equal_range(geoId);
    }

    // get the element using the iterator
    const Value& at(const Iterator& it) const { return *it; }
  };
}
