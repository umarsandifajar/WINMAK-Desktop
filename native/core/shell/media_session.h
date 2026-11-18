#pragma once
#include "shell/task_model.h"
#include <windows.h>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace atrium {

struct LyricLine {
    int64_t timeMs = 0;
    std::wstring text;
};

struct MediaInfo {
    bool hasSession = false;
    bool playing = false;
    std::wstring title;
    std::wstring artist;
    std::wstring artKey;
    std::shared_ptr<const IconImage> art;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    uint64_t updatedTickMs = 0;
    std::vector<LyricLine> lyrics;
    bool lyricsLoading = false;
};

class MediaSessionMonitor {
public:
    MediaSessionMonitor(HWND notify, UINT message);
    ~MediaSessionMonitor();
    MediaSessionMonitor(const MediaSessionMonitor&) = delete;
    MediaSessionMonitor& operator=(const MediaSessionMonitor&) = delete;

    void SetActive(bool on);
    MediaInfo Latest();
    void TogglePlayPause();
    void Next();
    void Previous();
    void SeekRatio(double ratio);

private:
    void Run();
    HWND notify_;
    UINT message_;
    std::mutex lock_;
    std::condition_variable wake_;
    bool active_ = false, quit_ = false;
    int pendingCmd_ = 0;
    double pendingSeek_ = 0.0;
    MediaInfo latest_;
    std::thread thread_;
};

} // namespace atrium
