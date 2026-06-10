#include "recorder/media/file_naming.h"

#include <cstdio>

namespace cardputer_recorder {

bool makeRecordingPath(std::uint32_t index, char* destination,
                       std::size_t capacity)
{
    if (destination == nullptr || capacity == 0 || index > 9999) {
        return false;
    }
    const int written = std::snprintf(
        destination, capacity, "/REC%04lu.WAV",
        static_cast<unsigned long>(index));
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

}  // namespace cardputer_recorder
