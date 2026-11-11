#include "../config_parser.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
template <typename Parser>
void CheckDoubleFormatting() {
  Parser parser;
  for (double value : {0.0, -0.0, 1.25, -2.5, 1e100,
                       std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::lowest(),
                       std::numeric_limits<double>::denorm_min()}) {
    std::ostringstream expected;
    expected.imbue(std::locale::classic());
    expected << std::fixed << std::setprecision(6) << value;
    EXPECT_EQ(parser.dump(detail::ConfigNode(value)), expected.str());
  }
}

template <typename Parser>
void CheckPrecisionBounds() {
  Parser parser;
  parser.options().set_option("fmt_double_precision", 0);
  EXPECT_EQ(parser.dump(detail::ConfigNode(1.25)), "1");
  parser.options().set_option("fmt_double_precision", 1024);
  EXPECT_EQ(parser.dump(detail::ConfigNode(1.25)),
            "1.25" + std::string(1022, '0'));
  for (int precision : {-1, 1025, std::numeric_limits<int>::max()}) {
    parser.options().set_option("fmt_double_precision", precision);
    EXPECT_THROW(parser.dump(detail::ConfigNode(1.25)), std::invalid_argument);
  }
}
}  // namespace

TEST(ConfigDoubleSerialization, IniPreservesFixedFormatForLargeValues) {
  CheckDoubleFormatting<detail::IniParser>();
}
TEST(ConfigDoubleSerialization, ConfPreservesFixedFormatForLargeValues) {
  CheckDoubleFormatting<detail::ConfParser>();
}
TEST(ConfigDoubleSerialization, IniBoundsPrecisionBeforeFormatting) {
  CheckPrecisionBounds<detail::IniParser>();
}
TEST(ConfigDoubleSerialization, ConfBoundsPrecisionBeforeFormatting) {
  CheckPrecisionBounds<detail::ConfParser>();
}
