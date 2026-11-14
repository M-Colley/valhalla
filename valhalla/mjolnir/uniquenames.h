#ifndef VALHALLA_MJOLNIR_UNIQUENAMES_H
#define VALHALLA_MJOLNIR_UNIQUENAMES_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace valhalla {
namespace mjolnir {

using NamesMap = std::unordered_map<std::string, uint32_t>;

/**
 * Class to hold a list of unique names and indexes to them.
 */
class UniqueNames {
public:
  /**
   * Constructor.
   */
  UniqueNames() {
    // Insert dummy so index 0 is never used
    index("");
  }

  UniqueNames(const UniqueNames& other) : names_(other.names_) {
    indexes_.resize(other.indexes_.size());
    for (const auto& entry : names_) {
      indexes_[entry.second] = &entry.first;
    }
  }

  UniqueNames(UniqueNames&& other) noexcept
      : names_(std::move(other.names_)), indexes_(std::move(other.indexes_)) {
    other.Clear();
  }

  UniqueNames& operator=(UniqueNames other) noexcept {
    names_.swap(other.names_);
    indexes_.swap(other.indexes_);
    return *this;
  }

  /**
   * Get an index for the specified name. If the name is not already used
   * it is added to the name map.
   * @param  name  Name.
   * @return  Returns an index into the unique list of names.
   */
  uint32_t index(const std::string& name) {
    // Clear and moves leave an empty pool. Reserve zero before adding a real name.
    if (indexes_.empty() && !name.empty()) {
      index("");
    }
    const auto next_index = static_cast<uint32_t>(indexes_.size());
    auto [it, inserted] = names_.try_emplace(name, next_index);
    if (!inserted) {
      return it->second;
    }
    try {
      // References to hash-map entries survive rehashing; iterators do not.
      indexes_.push_back(&it->first);
    } catch (...) {
      names_.erase(it);
      throw;
    }
    return next_index;
  }

  /**
   * Get a name given an index. Returns an empty string if the index is out of range.
   * @param  index  Index into the unique name list.
   * @return  Returns the name
   */
  const std::string& name(const uint32_t index) const {
    if (index < indexes_.size()) {
      return *indexes_[index];
    }
    static const std::string empty;
    return empty;
  }

  /**
   * Clear the names and indexes.
   */
  void Clear() {
    names_.clear();
    indexes_.clear();
  }

  /**
   * Get the size - number of names. Since a blank name is added as the first unique name this
   * returns the size of the map - 1.
   * @return  Returns the number of unique names.
   */
  size_t Size() const {
    return names_.empty() ? 0 : names_.size() - 1;
  }

protected:
  // Map of names to indexes
  NamesMap names_;

  // Pointers into this pool's map, rebuilt on copies and stable across rehashes.
  std::vector<const std::string*> indexes_;
};

} // namespace mjolnir
} // namespace valhalla

#endif // VALHALLA_MJOLNIR_UNIQUENAMES_H
