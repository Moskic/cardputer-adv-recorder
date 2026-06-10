#pragma once

#include <cstddef>
#include <cstdint>

namespace cardputer_recorder {

bool makeRecordingPath(std::uint32_t index, char* destination,
                       std::size_t capacity);

}  // namespace cardputer_recorder
