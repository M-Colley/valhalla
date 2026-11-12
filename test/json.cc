#include "baldr/json.h"
#include "baldr/rapidjson_utils.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {

template <typename T> void CheckDoubleIntegerBounds() {
  rapidjson::Document doc;
  const double upper_bound = std::ldexp(1.0, std::numeric_limits<T>::digits);
  const double lower_bound = static_cast<double>(std::numeric_limits<T>::lowest());
  for (const double value :
       {upper_bound, std::numeric_limits<double>::max(),
        std::nextafter(lower_bound - 1.0, -std::numeric_limits<double>::infinity()),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    doc.SetDouble(value);
    EXPECT_FALSE(rapidjson::get_optional<T>(doc, "")) << value;
    EXPECT_EQ(rapidjson::get<T>(doc, "", T{7}), T{7}) << value;
    EXPECT_THROW(rapidjson::get<T>(doc, ""), std::runtime_error) << value;
  }

  for (const double value : {lower_bound, std::nextafter(upper_bound, 0.0), -0.75, 42.75}) {
    doc.SetDouble(value);
    const auto result = rapidjson::get_optional<T>(doc, "");
    ASSERT_TRUE(result) << value;
    EXPECT_EQ(*result, static_cast<T>(value)) << value;
  }
}

TEST(JSON, DoubleIntegerBounds) {
  CheckDoubleIntegerBounds<int32_t>();
  CheckDoubleIntegerBounds<uint32_t>();
  CheckDoubleIntegerBounds<int64_t>();
  CheckDoubleIntegerBounds<uint64_t>();
}

TEST(JSON, ArithmeticCoercions) {
  rapidjson::Document doc;
  doc.Parse(R"({"fraction":42.75,"false":0.0,"true":-0.5,"string":"123","bool":true,
                "integer":-42,"large_integer":18446744073709551615,"overflow":1e100})");
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(rapidjson::get<int>(doc, "/fraction"), 42);
  EXPECT_EQ(rapidjson::get<float>(doc, "/fraction"), 42.75f);
  EXPECT_EQ(rapidjson::get<double>(doc, "/fraction"), 42.75);
  EXPECT_FALSE(rapidjson::get<bool>(doc, "/false"));
  EXPECT_TRUE(rapidjson::get<bool>(doc, "/true"));
  EXPECT_EQ(rapidjson::get<unsigned>(doc, "/string"), 123u);
  EXPECT_EQ(rapidjson::get<int>(doc, "/bool"), 1);
  EXPECT_EQ(rapidjson::get<int>(doc, "/integer"), -42);
  EXPECT_EQ(rapidjson::get<uint64_t>(doc, "/large_integer"), std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(rapidjson::get_optional<unsigned>(doc, "/overflow"));
  EXPECT_THROW(rapidjson::get<unsigned>(doc, "/overflow"), std::runtime_error);

  doc.SetDouble(static_cast<double>(std::numeric_limits<int32_t>::max()) + 0.75);
  EXPECT_EQ(rapidjson::get<int32_t>(doc, ""), std::numeric_limits<int32_t>::max());
  doc.SetDouble(static_cast<double>(std::numeric_limits<int32_t>::lowest()) - 0.75);
  EXPECT_EQ(rapidjson::get<int32_t>(doc, ""), std::numeric_limits<int32_t>::lowest());
  doc.SetDouble(static_cast<double>(std::numeric_limits<uint32_t>::max()) + 0.75);
  EXPECT_EQ(rapidjson::get<uint32_t>(doc, ""), std::numeric_limits<uint32_t>::max());
}

TEST(JSON, FloatBounds) {
  rapidjson::Document doc;
  doc.Parse(R"({"overflow":1e100,"underflow":-1e100})");
  ASSERT_FALSE(doc.HasParseError());
  for (const char* key : {"/overflow", "/underflow"}) {
    EXPECT_FALSE(rapidjson::get_optional<float>(doc, key));
    EXPECT_EQ(rapidjson::get<float>(doc, key, 7.0f), 7.0f);
    EXPECT_THROW(rapidjson::get<float>(doc, key), std::runtime_error);
  }

  const double maximum = std::numeric_limits<float>::max();
  for (const double value : {maximum, -maximum, 0.0, std::numeric_limits<double>::min()}) {
    doc.SetDouble(value);
    EXPECT_EQ(rapidjson::get<float>(doc, ""), static_cast<float>(value));
  }
  for (const double value : {std::nextafter(maximum, std::numeric_limits<double>::infinity()),
                             std::nextafter(-maximum, -std::numeric_limits<double>::infinity())}) {
    doc.SetDouble(value);
    EXPECT_FALSE(rapidjson::get_optional<float>(doc, ""));
  }
}

template <typename T> void CheckNonFiniteSerialization() {
  for (const auto value : {std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::infinity(),
                           -std::numeric_limits<T>::infinity()}) {
    rapidjson::writer_wrapper_t array;
    array.start_array();
    array(value);
    array(4.25);
    array.end_array();
    rapidjson::Document parsed;
    parsed.Parse(array.get_buffer());
    ASSERT_FALSE(parsed.HasParseError()) << array.get_buffer();
    ASSERT_TRUE(parsed.IsArray());
    ASSERT_EQ(parsed.Size(), 2);
    EXPECT_TRUE(parsed[0u].IsNull());
    EXPECT_DOUBLE_EQ(parsed[1u].GetDouble(), 4.25);

    rapidjson::writer_wrapper_t object;
    object.start_object();
    object("value", value);
    object("finite", 4.25);
    object.end_object();
    parsed.Parse(object.get_buffer());
    ASSERT_FALSE(parsed.HasParseError()) << object.get_buffer();
    EXPECT_TRUE(parsed["value"].IsNull());
    EXPECT_DOUBLE_EQ(parsed["finite"].GetDouble(), 4.25);
  }
}

TEST(JSON, NonFiniteSerialization) {
  CheckNonFiniteSerialization<float>();
  CheckNonFiniteSerialization<double>();
}

TEST(JSON, StringViews) {
  const char text[] = {'a', '\0', 'b', 'c'};
  const std::string_view view(text, 3);
  rapidjson::writer_wrapper_t object;
  object.start_object();
  object("empty", std::string_view{});
  object(std::string_view{}, std::string_view{});
  object(view, view);
  object.end_object();
  rapidjson::Document parsed;
  parsed.Parse(object.get_buffer());
  ASSERT_FALSE(parsed.HasParseError());
  EXPECT_STREQ(parsed["empty"].GetString(), "");
  EXPECT_STREQ(parsed[""].GetString(), "");
  const rapidjson::Value key(view.data(), view.size());
  ASSERT_TRUE(parsed.HasMember(key));
  EXPECT_EQ(std::string_view(parsed[key].GetString(), parsed[key].GetStringLength()), view);

  rapidjson::writer_wrapper_t array;
  array.start_array();
  array(std::string_view{});
  array(view);
  array.end_array();
  parsed.Parse(array.get_buffer());
  ASSERT_FALSE(parsed.HasParseError());
  ASSERT_EQ(parsed.Size(), 2);
  EXPECT_STREQ(parsed[0u].GetString(), "");
  EXPECT_EQ(std::string_view(parsed[1u].GetString(), parsed[1u].GetStringLength()), view);
}

template <typename T> void CheckIntegerSerialization() {
  rapidjson::writer_wrapper_t array;
  array.start_array();
  array(std::numeric_limits<T>::lowest());
  array(std::numeric_limits<T>::max());
  array.end_array();
  rapidjson::Document parsed;
  parsed.Parse(array.get_buffer());
  ASSERT_FALSE(parsed.HasParseError());
  ASSERT_EQ(parsed.Size(), 2);
  if constexpr (std::is_signed_v<T>) {
    EXPECT_EQ(parsed[0u].GetInt64(), std::numeric_limits<T>::lowest());
    EXPECT_EQ(parsed[1u].GetInt64(), std::numeric_limits<T>::max());
  } else {
    EXPECT_EQ(parsed[0u].GetUint64(), std::numeric_limits<T>::lowest());
    EXPECT_EQ(parsed[1u].GetUint64(), std::numeric_limits<T>::max());
  }
}

TEST(JSON, IntegerTypes) {
  CheckIntegerSerialization<long>();
  CheckIntegerSerialization<unsigned long>();
  CheckIntegerSerialization<long long>();
  CheckIntegerSerialization<unsigned long long>();
}

TEST(JSON, Serialize) {
  using namespace std;
  using namespace valhalla::baldr;
  stringstream result;
  result << *json::map(
      {{"hint_data",
        json::map(
            {{"locations",
              json::array({string("_____38_SADaFQQAKwEAABEAAAAAAAAAdgAAAFfLwga4tW0C4P6W-wAARAA"),
                           string("fzhIAP____8wFAQA1AAAAC8BAAAAAAAAAAAAAP____9Uu20CGAiX-wAAAAA")})},
             {"checksum", static_cast<uint64_t>(2875622111)}})},
       {"route_name", json::array({string("West 26th Street"), string("Madison Avenue")})},
       {"via_indices", json::array({uint64_t(0), uint64_t(9)})},
       {"found_alternative", bool(false)},
       {"route_summary", json::map({{"end_point", string("West 29th Street")},
                                    {"start_point", string("West 26th Street")},
                                    {"total_time", uint64_t(145)},
                                    {"total_distance", uint64_t(878)}})},
       {"via_points",
        json::array({json::array({json::fixed_t{40.744377, 3}, json::fixed_t{-73.990433, 3}}),
                     json::array({json::fixed_t{40.745811, 3}, json::fixed_t{-73.988075, 3}})})},
       {"route_instructions",
        json::array(
            {json::array({string("10"), string("West 26th Street"), uint64_t(216), uint64_t(0),
                          uint64_t(52), string("215m"), string("SE"), uint64_t(118)}),
             json::array({string("1"), string("East 26th Street"), uint64_t(153), uint64_t(2),
                          uint64_t(29), string("153m"), string("SE"), uint64_t(120)}),
             json::array({string("7"), string("Madison Avenue"), uint64_t(237), uint64_t(3),
                          uint64_t(25), string("236m"), string("NE"), uint64_t(29)}),
             json::array({string("7"), string("East 29th Street"), uint64_t(155), uint64_t(6),
                          uint64_t(29), string("154m"), string("NW"), uint64_t(299)}),
             json::array({string("1"), string("West 29th Street"), uint64_t(118), uint64_t(7),
                          uint64_t(21), string("117m"), string("NW"), uint64_t(299)}),
             json::array({string("15"), string(""), uint64_t(0), uint64_t(8), uint64_t(0),
                          string("0m"), string("N"), uint64_t(0)})})},
       {"route_geometry", string("ozyulA~p_clCfc@ywApTar@li@ybBqe@c[ue@e[ue@i[ci@dcB}^rkA")},
       {"status_message", string("Found route between points")},
       {"status", uint64_t(0)},
       {"escaped_string", string("\"\t\r\n\\\a")}});

  stringstream answer;
  answer << "{\"escaped_string\":\"\\\"\\t\\r\\n\\\\\\u0007\",\"hint_data\":{\"checksum\":"
            "2875622111,\"locations\":[\"_____38_SADaFQQAKwEAABEAAAAAAAAAdgAAAFfLwga4tW0C4P6W-"
            "wAARAA\",\"fzhIAP____8wFAQA1AAAAC8BAAAAAAAAAAAAAP____9Uu20CGAiX-wAAAAA\"]},\"route_"
            "name\":[\"West 26th Street\",\"Madison "
            "Avenue\"],\"found_alternative\":false,\"route_summary\":{\"total_distance\":878,"
            "\"total_time\":145,\"start_point\":\"West 26th Street\",\"end_point\":\"West 29th "
            "Street\"},\"via_points\":[[40.744,-73.990],[40.746,-73.988]],\"route_instructions\":[["
            "\"10\",\"West 26th Street\",216,0,52,\"215m\",\"SE\",118],[\"1\",\"East 26th "
            "Street\",153,2,29,\"153m\",\"SE\",120],[\"7\",\"Madison "
            "Avenue\",237,3,25,\"236m\",\"NE\",29],[\"7\",\"East 29th "
            "Street\",155,6,29,\"154m\",\"NW\",299],[\"1\",\"West 29th "
            "Street\",118,7,21,\"117m\",\"NW\",299],[\"15\",\"\",0,8,0,\"0m\",\"N\",0]],\"route_"
            "geometry\":\"ozyulA~p_clCfc@ywApTar@li@ybBqe@c[ue@e[ue@i[ci@dcB}^rkA\",\"status_"
            "message\":\"Found route between points\",\"via_indices\":[0,9],\"status\":0}";

  rapidjson::Document res, ans;

  res.Parse(result.str());
  EXPECT_FALSE(res.HasParseError());

  ans.Parse(answer.str());
  EXPECT_FALSE(ans.HasParseError());

  EXPECT_EQ(res, ans) << "Wrong json";
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
