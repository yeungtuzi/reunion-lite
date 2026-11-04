#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rl {

// Text conversion implemented by the platform layer.
// ANSI == the system codepage used by YR (GBK / CP936 on Chinese Windows).
std::string DecodeAnsiToUtf8(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> EncodeUtf8ToAnsi(const std::string& utf8);

} // namespace rl
