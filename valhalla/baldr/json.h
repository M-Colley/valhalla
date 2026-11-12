#ifndef VALHALLA_BALDR_JSON_H_
#define VALHALLA_BALDR_JSON_H_

#include <boost/variant/static_visitor.hpp>
#include <boost/variant/variant.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace valhalla {
namespace baldr {
namespace json {

// forward declare a little bit
class Jmap;
using MapPtr = std::shared_ptr<Jmap>;
class Jarray;
using ArrayPtr = std::shared_ptr<Jarray>;

// Fixed precision serializer - outputs a fixed number of decimal places
struct fixed_t {
  long double value;
  size_t precision;
  // and be able to spit out text
  friend std::ostream& operator<<(std::ostream& stream, const fixed_t&);
};

// Floating point serializer - variable number of decimal places (no trailing zeros)
struct float_t {
  long double value;
  friend std::ostream& operator<<(std::ostream& stream, const float_t&);
};

struct RawJSON {
  std::string data;
};

// a variant of all the possible values to go with keys in json
using Value = boost::variant<std::string,
                             uint64_t,
                             int64_t,
                             fixed_t,
                             float_t,
                             bool,
                             std::nullptr_t,
                             MapPtr,
                             ArrayPtr,
                             RawJSON>;

// the map value type in json
class Jmap : public std::unordered_map<std::string, Value> {
public:
  // just specialize unordered_map
  using std::unordered_map<std::string, Value>::unordered_map;
  // and be able to spit out text
  friend std::ostream& operator<<(std::ostream&, const Jmap&);
};

// the array value type in json
class Jarray : public std::vector<Value> {
public:
  // just specialize vector
  using std::vector<Value>::vector;

protected:
  // and be able to spit out text
  friend std::ostream& operator<<(std::ostream&, const Jarray&);
};

// how we serialize the different primitives to string
class OstreamVisitor : public boost::static_visitor<std::ostream&> {
public:
  OstreamVisitor(std::ostream& o) : ostream_(o) {
  }

  std::ostream& operator()(const std::string& value) const {
    ostream_ << '"';
    size_t start = 0;
    for (size_t i = 0; i < value.size(); ++i) {
      const auto c = value[i];
      const auto byte = static_cast<unsigned char>(c);
      if (byte >= 0x20 && byte != 0x7f && c != '\\' && c != '"' && c != '/')
        continue;
      if (i > start)
        ostream_.write(value.data() + start, static_cast<std::streamsize>(i - start));
      switch (c) {
        case '\\':
          ostream_ << "\\\\";
          break;
        case '"':
          ostream_ << "\\\"";
          break;
        case '/':
          ostream_ << "\\/";
          break;
        case '\b':
          ostream_ << "\\b";
          break;
        case '\f':
          ostream_ << "\\f";
          break;
        case '\n':
          ostream_ << "\\n";
          break;
        case '\r':
          ostream_ << "\\r";
          break;
        case '\t':
          ostream_ << "\\t";
          break;
        default:
          constexpr char digits[] = "0123456789ABCDEF";
          ostream_ << "\\u00" << digits[byte >> 4] << digits[byte & 0xf];
          break;
      }
      start = i + 1;
    }
    if (start < value.size())
      ostream_.write(value.data() + start, static_cast<std::streamsize>(value.size() - start));
    return ostream_ << '"';
  }
  std::ostream& operator()(uint64_t value) const {
    return ostream_ << value;
  }
  std::ostream& operator()(int64_t value) const {
    return ostream_ << value;
  }
  std::ostream& operator()(fixed_t value) const {
    return ostream_ << value;
  }
  std::ostream& operator()(float_t value) const {
    return ostream_ << value;
  }
  std::ostream& operator()(bool value) const {
    return ostream_ << (value ? "true" : "false");
  }
  std::ostream& operator()(std::nullptr_t) const {
    return ostream_ << "null";
  }

  std::ostream& operator()(const RawJSON& value) const {
    return ostream_ << value.data;
  }

  template <class Nullable> std::ostream& operator()(const Nullable& value) const {
    if (value) {
      return ostream_ << *value;
    } else {
      return this->operator()(nullptr);
    }
  }

private:
  std::ostream& ostream_;
};

inline std::ostream& operator<<(std::ostream& stream, const fixed_t& fp) {
  if (std::isfinite(fp.value)) {
    stream << std::setprecision(fp.precision) << std::fixed << fp.value;
  } else {
    stream << std::setprecision(fp.precision) << std::fixed << '"' << fp.value << '"';
  }
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const float_t& fp) {
  // precision defaults to 6 according to lib stdc++
  if (std::isfinite(fp.value)) {
    stream << std::setprecision(6) << std::defaultfloat << fp.value;
  } else {
    stream << std::setprecision(6) << std::defaultfloat << '"' << fp.value << '"';
  }
  return stream;
}

template <typename Visitable>
inline void applyOutputVisitor(std::ostream& stream, Visitable& visitable) {
  // Cannot use boost::apply_visitor with C++14 due to
  // ambiguity introduced in boost_1_58 and fixed in the latest
  // https://lists.boost.org/boost-bugs/2015/05/41067.php
  OstreamVisitor visitor(stream);
  visitable.apply_visitor(visitor);
}

inline std::ostream& operator<<(std::ostream& stream, const Jmap& json) {
  stream << '{';
  bool separator = false;
  OstreamVisitor key_visitor(stream);
  for (const auto& key_value : json) {
    if (separator) {
      stream << ',';
    }
    separator = true;
    key_visitor(key_value.first);
    stream << ':';
    applyOutputVisitor(stream, key_value.second);
  }
  stream << '}';
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const Jarray& json) {
  stream << '[';
  bool separator = false;
  for (const auto& element : json) {
    if (separator) {
      stream << ',';
    }
    separator = true;
    applyOutputVisitor(stream, element);
  }
  stream << ']';
  return stream;
}

inline MapPtr map(std::initializer_list<Jmap::value_type> list) {
  return std::make_shared<Jmap>(list);
}

inline ArrayPtr array(std::initializer_list<Jarray::value_type> list) {
  return std::make_shared<Jarray>(list);
}

} // namespace json
} // namespace baldr
} // namespace valhalla

#endif // VALHALLA_BALDR_JSON_H_
