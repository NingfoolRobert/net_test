/**
 * @file io_cfg.h
 * @brief IO configuration
 * @author bfning
 * @version 0.1
 */

#pragma once

#include <cstdint>
namespace net {
enum io_media_enum : uint8_t
{
  IO_MEDIA_BSD,
  IO_MEDIA_EFVI,
  IO_MEDIA_DIRECT,
  IO_MEDIA_EXANIC,
  IO_MEDIA_SHM,
  IO_MEDIA_MAX
};

enum io_family_enum : int8_t
{
  IO_FAMILY_IPV4,
  IO_FAMILY_IPV6,
  IO_FAMILY_UNIX,
  IO_FAMILY_MAX
};
/**
 * @brief IO type enumeration
 */
enum io_protocol_enum : int8_t
{
  PROTOCOL_TCP = 6,
  PROTOCOL_UDP = 17,
  IO_TYPE_MAX = 19
};

struct io_cfg_s {
  io_media_enum media;
  io_family_enum family;
  io_protocol_enum type;
};

using io_cfg_t = io_cfg_s;
}  // namespace net