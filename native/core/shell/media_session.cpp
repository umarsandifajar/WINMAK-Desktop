#include "shell/media_session.h"
#include <wincodec.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

namespace atrium {
namespace {

std::wstring UrlEncode(const std::wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return L"";
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, utf8.data(), len - 1, nullptr, nullptr);
    std::wstring out;
    wchar_t hex[8];
    for (unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(wchar_t(c));
        } else if (c == ' ') {
            out += L"%20";
        } else {
            swprintf_s(hex, L"%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::wstring CleanTrackTitle(std::wstring t) {
    for (wchar_t open : { L'(', L'[' }) {
        size_t pos = t.find(open);
        if (pos != std::wstring::npos && pos > 2) t = t.substr(0, pos);
    }
    while (!t.empty() && iswspace(t.back())) t.pop_back();
    return t;
}

std::vector<LyricLine> ParseLrcText(const std::wstring& raw, bool synced) {
    std::vector<LyricLine> lines;
    std::wstringstream ss(raw);
    std::wstring line;
    int64_t fallbackTime = 0;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;
        if (synced && line.size() > 7 && line[0] == L'[' && iswdigit(line[1])) {
            size_t close = line.find(L']');
            if (close != std::wstring::npos) {
                std::wstring ts = line.substr(1, close - 1);
                size_t colon = ts.find(L':');
                if (colon != std::wstring::npos) {
                    int mins = _wtoi(ts.substr(0, colon).c_str());
                    double secs = _wtof(ts.substr(colon + 1).c_str());
                    std::wstring lyric = line.substr(close + 1);
                    while (!lyric.empty() && iswspace(lyric.front())) lyric.erase(lyric.begin());
                    if (!lyric.empty()) {
                        lines.push_back({ static_cast<int64_t>((mins * 60.0 + secs) * 1000.0), lyric });
                    }
                }
            }
        } else if (!synced) {
            lines.push_back({ fallbackTime, line });
            fallbackTime += 3500;
        }
    }
    std::sort(lines.begin(), lines.end(), [](const LyricLine& a, const LyricLine& b) { return a.timeMs < b.timeMs; });
    return lines;
}

std::wstring HttpGetLrcLib(const std::wstring& path) {
    HINTERNET hSession = WinHttpOpen(L"AtriumDesktop/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return L"";
    WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 4000);
    HINTERNET hConnect = WinHttpConnect(hSession, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return L""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    std::string body;
    if (hRequest && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                std::vector<char> chunk(avail);
                DWORD read = 0;
                if (WinHttpReadData(hRequest, chunk.data(), avail, &read) && read > 0)
                    body.append(chunk.data(), read);
            }
        }
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (body.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return L"";
    std::wstring wjson(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, wjson.data(), wlen - 1);
    return wjson;
}

std::vector<LyricLine> ExtractLyricsFromArray(const std::wstring& wjson) {
    if (wjson.empty()) return {};
    try {
        JsonArray arr{ nullptr };
        if (JsonArray::TryParse(wjson, arr)) {
            std::wstring plainFallback;
            for (uint32_t i = 0; i < arr.Size(); ++i) {
                auto obj = arr.GetObjectAt(i);
                std::wstring synced = std::wstring(obj.GetNamedString(L"syncedLyrics", L""));
                if (!synced.empty()) return ParseLrcText(synced, true);
                if (plainFallback.empty())
                    plainFallback = std::wstring(obj.GetNamedString(L"plainLyrics", L""));
            }
            if (!plainFallback.empty()) return ParseLrcText(plainFallback, false);
        }
    } catch (...) {}
    return {};
}

std::vector<LyricLine> FetchLyricsFromLrcLib(const std::wstring& title, const std::wstring& artist) {
    if (title.empty()) return {};
    std::wstring cleanTitle = CleanTrackTitle(title);
    std::wstring firstArtist = artist;
    size_t comma = firstArtist.find_first_of(L",&");
    if (comma != std::wstring::npos && comma > 1) firstArtist = firstArtist.substr(0, comma);

    if (!firstArtist.empty()) {
        std::wstring q1 = HttpGetLrcLib(L"/api/search?q=" + UrlEncode(cleanTitle + L" " + firstArtist));
        auto res1 = ExtractLyricsFromArray(q1);
        if (!res1.empty()) return res1;
    }
    std::wstring q2 = HttpGetLrcLib(L"/api/search?q=" + UrlEncode(cleanTitle));
    return ExtractLyricsFromArray(q2);
}

std::shared_ptr<IconImage> DecodeThumbnail(const IRandomAccessStreamReference& thumbRef) {
    if (!thumbRef) return nullptr;
    try {
        auto stream = thumbRef.OpenReadAsync().get();
        if (!stream) return nullptr;
        uint64_t sz = stream.Size();
        if (sz == 0 || sz > 6 * 1024 * 1024) return nullptr;

        DataReader reader(stream.GetInputStreamAt(0));
        uint32_t loaded = reader.LoadAsync(static_cast<uint32_t>(sz)).get();
        if (loaded == 0) return nullptr;

        std::vector<uint8_t> buf(loaded);
        reader.ReadBytes(buf);

        com_ptr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()))))
            return nullptr;

        com_ptr<IWICStream> wicStream;
        if (FAILED(wic->CreateStream(wicStream.put()))) return nullptr;
        if (FAILED(wicStream->InitializeFromMemory(buf.data(), static_cast<DWORD>(buf.size())))) return nullptr;

        com_ptr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromStream(wicStream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())))
            return nullptr;

        com_ptr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.put()))) return nullptr;

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0) return nullptr;

        com_ptr<IWICBitmapSource> src = frame;
        if (w > 192 || h > 192) {
            UINT nw = w >= h ? 192 : std::max(1u, UINT(192.0 * w / h));
            UINT nh = h >= w ? 192 : std::max(1u, UINT(192.0 * h / w));
            com_ptr<IWICBitmapScaler> scaler;
            if (SUCCEEDED(wic->CreateBitmapScaler(scaler.put())) &&
                SUCCEEDED(scaler->Initialize(frame.get(), nw, nh, WICBitmapInterpolationModeFant))) {
                src = scaler;
                w = nw; h = nh;
            }
        }

        com_ptr<IWICFormatConverter> conv;
        if (FAILED(wic->CreateFormatConverter(conv.put()))) return nullptr;
        if (FAILED(conv->Initialize(src.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            return nullptr;

        auto img = std::make_shared<IconImage>();
        img->width = static_cast<int>(w);
        img->height = static_cast<int>(h);
        img->bgra.resize(size_t(w) * size_t(h) * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(img->bgra.size()), img->bgra.data())))
            return nullptr;
        return img;
    } catch (...) {
        return nullptr;
    }
}

} // namespace

MediaSessionMonitor::MediaSessionMonitor(HWND notify, UINT message) : notify_(notify), message_(message) {
    thread_ = std::thread([this] { Run(); });
}

MediaSessionMonitor::~MediaSessionMonitor() {
    { std::lock_guard<std::mutex> g(lock_); quit_ = true; }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void MediaSessionMonitor::SetActive(bool on) {
    {
        std::lock_guard<std::mutex> g(lock_);
        if (active_ == on) return;
        active_ = on;
    }
    wake_.notify_all();
}

MediaInfo MediaSessionMonitor::Latest() {
    std::lock_guard<std::mutex> g(lock_);
    return latest_;
}

void MediaSessionMonitor::TogglePlayPause() { { std::lock_guard<std::mutex> g(lock_); pendingCmd_ = 1; } wake_.notify_all(); }
void MediaSessionMonitor::Next()            { { std::lock_guard<std::mutex> g(lock_); pendingCmd_ = 2; } wake_.notify_all(); }
void MediaSessionMonitor::Previous()        { { std::lock_guard<std::mutex> g(lock_); pendingCmd_ = 3; } wake_.notify_all(); }
void MediaSessionMonitor::SeekRatio(double r) {
    { std::lock_guard<std::mutex> g(lock_); pendingCmd_ = 4; pendingSeek_ = std::clamp(r, 0.0, 1.0); }
    wake_.notify_all();
}

void MediaSessionMonitor::Run() {
    init_apartment(apartment_type::multi_threaded);
    GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
    std::wstring lastTrackId;
    std::shared_ptr<const IconImage> cachedArt;
    std::wstring cachedArtKey;
    std::vector<LyricLine> cachedLyrics;
    uint64_t artSeq = 0;

    while (true) {
        int cmd = 0;
        double seekRatio = 0.0;
        {
            std::unique_lock<std::mutex> lk(lock_);
            wake_.wait_for(lk, std::chrono::milliseconds(950), [&] { return quit_ || (active_ && pendingCmd_ != 0); });
            if (quit_) break;
            if (!active_) continue;
            cmd = pendingCmd_;
            seekRatio = pendingSeek_;
            pendingCmd_ = 0;
        }

        try {
            if (!mgr) mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto session = mgr ? mgr.GetCurrentSession() : nullptr;

            if (session && cmd != 0) {
                if (cmd == 1) session.TryTogglePlayPauseAsync().get();
                else if (cmd == 2) session.TrySkipNextAsync().get();
                else if (cmd == 3) session.TrySkipPreviousAsync().get();
                else if (cmd == 4) {
                    auto tl = session.GetTimelineProperties();
                    int64_t dur = tl.EndTime().count() - tl.StartTime().count();
                    if (dur > 0) {
                        int64_t target = tl.StartTime().count() + static_cast<int64_t>(dur * seekRatio);
                        session.TryChangePlaybackPositionAsync(target).get();
                    }
                }
            }

            MediaInfo info;
            if (session) {
                info.hasSession = true;
                auto pb = session.GetPlaybackInfo();
                info.playing = (pb && pb.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

                auto props = session.TryGetMediaPropertiesAsync().get();
                if (props) {
                    info.title = props.Title().c_str();
                    info.artist = props.Artist().c_str();
                    std::wstring trackId = info.title + L"|" + info.artist;
                    if (trackId != lastTrackId) {
                        lastTrackId = trackId;
                        cachedArt = DecodeThumbnail(props.Thumbnail());
                        cachedArtKey = cachedArt ? (L"media_art_" + std::to_wstring(++artSeq)) : L"";
                        cachedLyrics = FetchLyricsFromLrcLib(info.title, info.artist);
                    }
                    info.art = cachedArt;
                    info.artKey = cachedArtKey;
                    info.lyrics = cachedLyrics;
                }

                auto tl = session.GetTimelineProperties();
                int64_t startMs = tl.StartTime().count() / 10000;
                int64_t endMs = tl.EndTime().count() / 10000;
                int64_t posMs = tl.Position().count() / 10000;
                info.durationMs = std::max<int64_t>(0, endMs - startMs);
                info.positionMs = std::clamp<int64_t>(posMs - startMs, 0, info.durationMs);
                info.updatedTickMs = GetTickCount64();
            } else {
                lastTrackId.clear();
                cachedArt.reset();
                cachedArtKey.clear();
                cachedLyrics.clear();
            }

            bool changed = false;
            {
                std::lock_guard<std::mutex> g(lock_);
                changed = (latest_.hasSession != info.hasSession ||
                           latest_.playing != info.playing ||
                           latest_.title != info.title ||
                           latest_.artist != info.artist ||
                           latest_.artKey != info.artKey ||
                           latest_.lyrics.size() != info.lyrics.size() ||
                           (info.playing && (latest_.positionMs / 1000) != (info.positionMs / 1000)));
                latest_ = std::move(info);
            }
            if (changed && notify_) PostMessageW(notify_, message_, 0, 0);
        } catch (...) {
            mgr = nullptr;
        }
    }
    uninit_apartment();
}

} // namespace atrium
