#include "../file_util.h"

#include <gtest/gtest.h>

#include <utility>

TEST(FileUtil, ParentDirectoryUsesTheLastPathComponent) {
  const std::pair<const char*, const char*> cases[] = {
      {nullptr, ""},
      {"", ""},
      {"file", ""},
      {".", ""},
      {"..", ""},
      {"a/file", "a"},
      {"a/b/file", "a/b"},
      {"/file", "/"},
      {"/a/b/file", "/a/b"},
      {"/", "/"},
      {"///", "/"},
      {"a/", "a"},
      {"/a/b/", "/a/b"},
      {"/a/b///", "/a/b"},
      {"a///file", "a"},
      {"a//b/file", "a//b"},
  };
  for (const auto& entry : cases) {
    SCOPED_TRACE(entry.first ? entry.first : "(null)");
    EXPECT_EQ(detail::path_dir_name(entry.first), entry.second);
  }
}
