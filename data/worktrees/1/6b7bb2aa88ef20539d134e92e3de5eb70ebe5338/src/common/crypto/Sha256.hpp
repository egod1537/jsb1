#pragma once

#include <string>
#include <string_view>

namespace common::crypto {
std::string Sha256Hex(std::string_view data);
}
