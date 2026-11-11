#include "../memory/allocator.h"
#include "../container/bheap.h"
#include "../ipc/fixed_shm.h"
#include "../container/fstring.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

TEST(LegacyOwnership, OwnersRejectShallowCopies) {
  EXPECT_FALSE((std::is_copy_constructible_v<detail::bheap<int>>));
  EXPECT_FALSE((std::is_copy_assignable_v<detail::bheap<int>>));
  EXPECT_FALSE((std::is_copy_constructible_v<basic::Allocator<int, 4>>));
  EXPECT_FALSE((std::is_copy_assignable_v<basic::Allocator<int, 4>>));
}

TEST(LegacyOwnership, AllocatorStillReusesReturnedStorage) {
  basic::Allocator<int, 1> allocator;
  auto* first = allocator.allocate(42);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(*first, 42);
  EXPECT_EQ(allocator.allocate(0), nullptr);
  allocator.deallocate(first);
  auto* second = allocator.allocate(7);
  ASSERT_EQ(second, first);
  EXPECT_EQ(*second, 7);
  allocator.deallocate(second);
}

TEST(FstringAssignment, ShortCStringAndEmptyInput) {
  detail::fstring<64> value("old contents");
  const char input[] = {'x', '\0'};
  EXPECT_EQ(&(value = input), &value);
  EXPECT_STREQ(value.c_str(), "x");
  value = "";
  EXPECT_TRUE(value.empty());
  value = "kept";
  value = static_cast<const char*>(nullptr);
  EXPECT_STREQ(value.c_str(), "kept");
}

TEST(FstringAssignment, ShortStdStringAndTruncation) {
  detail::fstring<64> value;
  EXPECT_EQ(&(value = std::string("x")), &value);
  EXPECT_STREQ(value.c_str(), "x");
  value = std::string();
  EXPECT_TRUE(value.empty());
  detail::fstring<4> short_value;
  short_value = std::string("abcdef");
  EXPECT_STREQ(short_value.c_str(), "abc");
  short_value = "xyz123";
  EXPECT_STREQ(short_value.c_str(), "xyz");
}

TEST(FstringAssignment, OverlapAndTerminatorOnlyCapacity) {
  detail::fstring<16> value("abcdef");
  value = value.c_str();
  EXPECT_STREQ(value.c_str(), "abcdef");
  value = value.c_str() + 2;
  EXPECT_STREQ(value.c_str(), "cdef");
  detail::fstring<1> empty;
  empty = "x";
  EXPECT_TRUE(empty.empty());
  empty = std::string("x");
  EXPECT_TRUE(empty.empty());
}

namespace {
class LegacyShmSafety : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE(::mkdtemp(directory_), nullptr);
    path_ = std::string(directory_) + "/memory";
  }
  void TearDown() override {
    ::unlink(path_.c_str());
    ::rmdir(directory_);
  }
  void CreateFile(off_t size) {
    const int fd = ::open(path_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(::ftruncate(fd, size), 0);
    if (size > 0) EXPECT_EQ(::pwrite(fd, "A", 1, 0), 1);
    EXPECT_EQ(::close(fd), 0);
  }
  off_t FileSize() const {
    struct stat status{};
    EXPECT_EQ(::stat(path_.c_str(), &status), 0);
    return status.st_size;
  }
  char directory_[64] = "/tmp/basic-legacy-shm-XXXXXX";
  std::string path_;
};
}  // namespace

TEST_F(LegacyShmSafety, CreatesAndSizesNewFile) {
  detail::fixed_shm memory(path_.c_str(), 4096);
  ASSERT_TRUE(memory.open());
  EXPECT_EQ(FileSize(), 4096);
  ASSERT_NE(memory.map(), nullptr);
  ASSERT_EQ(memory.writeto(0, "hello", 5), 5);
  char result[5];
  ASSERT_EQ(memory.readfrom(0, result, sizeof(result)), 5);
  EXPECT_EQ(std::string(result, sizeof(result)), "hello");
}

TEST_F(LegacyShmSafety, RejectsExistingEmptyFileWithoutResizing) {
  CreateFile(0);
  detail::fixed_shm memory(path_.c_str(), 4096);
  ASSERT_FALSE(memory.open());
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(FileSize(), 0);
  EXPECT_EQ(memory.map(), nullptr);
  EXPECT_EQ(errno, EBADF);
  // A failed open must release its descriptor and allow a later retry.
  ASSERT_EQ(::truncate(path_.c_str(), 4096), 0);
  ASSERT_TRUE(memory.open());
  EXPECT_NE(memory.map(), nullptr);
}

TEST_F(LegacyShmSafety, RejectsShortFileWithoutDestroyingData) {
  CreateFile(1);
  detail::fixed_shm memory(path_.c_str(), 4096);
  ASSERT_FALSE(memory.open());
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(FileSize(), 1);
  const int fd = ::open(path_.c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  char byte = 0;
  EXPECT_EQ(::read(fd, &byte, 1), 1);
  EXPECT_EQ(byte, 'A');
  EXPECT_EQ(::close(fd), 0);
}

TEST_F(LegacyShmSafety, AllowsPrefixMappingOfLargerFile) {
  CreateFile(4096);
  detail::fixed_shm memory(path_.c_str(), 128);
  ASSERT_TRUE(memory.open());
  ASSERT_NE(memory.map(), nullptr);
  EXPECT_EQ(*memory.mem(0), 'A');
  EXPECT_EQ(FileSize(), 4096);
}

TEST_F(LegacyShmSafety, RechecksFileSizeBeforeMapping) {
  CreateFile(4096);
  detail::fixed_shm memory(path_.c_str(), 4096);
  ASSERT_TRUE(memory.open());
  ASSERT_EQ(::truncate(path_.c_str(), 0), 0);
  EXPECT_EQ(memory.map(), nullptr);
  EXPECT_EQ(errno, EINVAL);
}

TEST_F(LegacyShmSafety, AcceptsMaximumStoredPathLength) {
  const std::string prefix = std::string(directory_) + "/";
  path_ = prefix + std::string(255 - prefix.size(), 'a');
  detail::fixed_shm memory(path_.c_str(), 1);
  ASSERT_TRUE(memory.open());
  EXPECT_NE(memory.map(), nullptr);
}

TEST_F(LegacyShmSafety, RejectsOversizedPathsBeforeCreatingFiles) {
  const std::string prefix = std::string(directory_) + "/";
  for (std::size_t length : {256u, 300u}) {
    const auto path = prefix + std::string(length - prefix.size(), 'a');
    detail::fixed_shm memory(path.c_str(), 4096);
    EXPECT_FALSE(memory.open());
    EXPECT_EQ(errno, ENAMETOOLONG);
  }
}

TEST_F(LegacyShmSafety, MovesPreserveInvalidPathStatus) {
  const std::string path(300, 'a');
  detail::fixed_shm source(path.c_str(), 4096);
  detail::fixed_shm moved(std::move(source));
  EXPECT_FALSE(moved.open());
  EXPECT_EQ(errno, ENAMETOOLONG);
  detail::fixed_shm assigned(path_.c_str(), 4096);
  assigned = std::move(moved);
  EXPECT_FALSE(assigned.open());
  EXPECT_EQ(errno, ENAMETOOLONG);
  EXPECT_NE(::access(path_.c_str(), F_OK), 0);
}

TEST_F(LegacyShmSafety, RejectsInvalidSizesWithoutCreatingFiles) {
  detail::fixed_shm empty(path_.c_str(), 0);
  EXPECT_FALSE(empty.open());
  EXPECT_EQ(errno, EINVAL);
  detail::fixed_shm huge(path_.c_str(), std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(huge.open());
  EXPECT_EQ(errno, EOVERFLOW);
  EXPECT_NE(::access(path_.c_str(), F_OK), 0);
}

TEST_F(LegacyShmSafety, RejectsWritesOutsideTheMapping) {
  detail::fixed_shm memory(path_.c_str(), 4096);
  ASSERT_TRUE(memory.open());
  ASSERT_NE(memory.map(), nullptr);
  ASSERT_EQ(memory.writeto(4095, "A", 1), 1);
  EXPECT_EQ(memory.writeto(4096, nullptr, 0), 0);
  EXPECT_EQ(memory.writeto(4096, "B", 1), -1);
  EXPECT_EQ(memory.writeto(4097, "B", 1), -1);
  EXPECT_EQ(memory.writeto(std::numeric_limits<uint64_t>::max(), "B", 1), -1);
  EXPECT_EQ(memory.writeto(4095, nullptr, 1), -1);
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(*memory.mem(4095), 'A');
}

TEST_F(LegacyShmSafety, MoveAssignmentReleasesThePreviousMappingAndDescriptor) {
  const auto descriptor_count = [] {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                         std::filesystem::directory_iterator{});
  };
  const auto baseline = descriptor_count();
  {
    detail::fixed_shm target(path_.c_str(), 4096), source(path_.c_str(), 4096);
    ASSERT_TRUE(target.open());
    ASSERT_TRUE(source.open());
    char* old_mapping = target.map();
    ASSERT_NE(old_mapping, nullptr);
    char* source_mapping = source.map();
    ASSERT_NE(source_mapping, nullptr);
    ASSERT_EQ(source.writeto(0, "A", 1), 1);
    target = std::move(source);
    unsigned char residency = 0;
    EXPECT_EQ(::mincore(old_mapping, 1, &residency), -1);
    EXPECT_EQ(errno, ENOMEM);
    EXPECT_EQ(descriptor_count(), baseline + 1);
    EXPECT_EQ(target.mem(0), source_mapping);
    EXPECT_EQ(*target.mem(0), 'A');
  }
  EXPECT_EQ(descriptor_count(), baseline);
}
