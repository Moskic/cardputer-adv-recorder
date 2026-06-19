#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"
#include "recorder/media/file_naming.h"

namespace cardputer_recorder {

void RecorderApp::deleteSelected()
{
    const String path = "/" + files_[selected_];
    if (!storage_.remove(path.c_str())) {
        setError("Could not delete recording.");
        return;
    }
    message_ = "Deleted " + files_[selected_];
    scanFiles();
    forceRedraw_ = true;
}
void RecorderApp::scanFiles()
{
    files_.clear();
    File directory = storage_.open("/", FILE_READ);
    if (!directory || !directory.isDirectory()) {
        setError("Could not read SD directory.");
        return;
    }
    File entry = directory.openNextFile();
    while (entry) {
        String name = entry.name();
        String normalizedName = name;
        normalizedName.toLowerCase();
        if (!entry.isDirectory() &&
            normalizedName.endsWith(".wav") &&
            entry.size() >= kPcmWavHeaderSize) {
            if (name.startsWith("/")) {
                name.remove(0, 1);
            }
            files_.push_back(name);
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    if (files_.empty()) {
        selected_ = 0;
    } else if (selected_ >= static_cast<int>(files_.size())) {
        selected_ = files_.size() - 1;
    }
}
bool RecorderApp::chooseRecordingPath(char* path, std::size_t capacity)
{
    for (std::uint32_t index = 1; index <= 9999; ++index) {
        if (!makeRecordingPath(index, path, capacity)) {
            return false;
        }
        if (!storage_.exists(path)) {
            return true;
        }
    }
    return false;
}
String RecorderApp::storageUsageText() const
{
    if (!storage_.isMounted()) {
        return "SD unavailable";
    }
    const std::uint64_t capacity = storage_.capacityBytes();
    const std::uint64_t used = storage_.usedBytes();
    if (capacity == 0 || used > capacity) {
        return "SD size unknown";
    }
    return "FREE " + formatByteCount(capacity - used) + " / " +
           formatByteCount(capacity);
}
String RecorderApp::selectedRecordingDetail()
{
    if (files_.empty() || selected_ < 0 ||
        selected_ >= static_cast<int>(files_.size())) {
        return storageUsageText();
    }

    const String path = "/" + files_[selected_];
    const std::uint32_t size = storage_.fileSize(path.c_str());
    String detail = formatByteCount(size);

    File file = storage_.open(path.c_str(), FILE_READ);
    WavReader detailReader;
    if (detailReader.begin(file)) {
        const WavInfo& wav = detailReader.info();
        const std::uint32_t bytesPerSecond =
            wav.spec.sampleRate * wav.spec.channels *
            (wav.spec.bitsPerSample / 8);
        if (bytesPerSecond > 0) {
            const unsigned long durationMs =
                static_cast<unsigned long>(
                    static_cast<std::uint64_t>(wav.dataSize) * 1000ULL /
                    bytesPerSecond);
            detail += "  " + formatTime(durationMs);
        }
        detailReader.end();
    } else if (file) {
        file.close();
    }
    return detail + "  " + storageUsageText();
}

}  // namespace cardputer_recorder
