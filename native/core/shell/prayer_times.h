#pragma once
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <array>
#include <vector>

namespace atrium {

struct PrayerSchedule {
    std::wstring city = L"Bekasi";
    std::array<int, 5> minutes{}; // Subuh, Dzuhur, Ashar, Maghrib, Isya (menit dari 00:00)
    int nextIndex = 0;            // 0..4
    bool isTomorrow = false;
    int minsUntilNext = 0;
    float progress = 0.0f;        // 0.0 .. 1.0 dari sholat sebelumnya ke berikutnya
};

inline std::wstring FormatPrayerTime(int totalMins) {
    int m = ((totalMins % 1440) + 1440) % 1440;
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", m / 60, m % 60);
    return buf;
}

inline PrayerSchedule ComputePrayerSchedule(const SYSTEMTIME& localNow, int dayOffset = 0,
                                            double lat = -6.2383, double lon = 106.9756, double tz = 7.0) {
    // Hitung Julian Date untuk tanggal target (localNow + dayOffset hari)
    FILETIME ft{};
    SystemTimeToFileTime(&localNow, &ft);
    ULARGE_INTEGER uli{};
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart += static_cast<LONGLONG>(dayOffset) * 864000000000LL;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);

    int y = st.wYear, m = st.wMonth, d = st.wDay;
    if (m <= 2) { y -= 1; m += 12; }
    int A = y / 100;
    int B = 2 - A + (A / 4);
    double jd = std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + d + B - 1524.5;

    constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
    constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;

    auto sunPos = [&](double j) {
        double D = j - 2451545.0;
        double g = std::fmod(357.529 + 0.98560028 * D, 360.0);
        double q = std::fmod(280.459 + 0.98564736 * D, 360.0);
        double L = std::fmod(q + 1.915 * std::sin(g * kDeg2Rad) + 0.020 * std::sin(2.0 * g * kDeg2Rad), 360.0);
        double e = 23.439 - 0.00000036 * D;
        double RA = std::atan2(std::cos(e * kDeg2Rad) * std::sin(L * kDeg2Rad), std::cos(L * kDeg2Rad)) * kRad2Deg / 15.0;
        RA = std::fmod(RA + 24.0, 24.0);
        double decl = std::asin(std::sin(e * kDeg2Rad) * std::sin(L * kDeg2Rad)) * kRad2Deg;
        double eqT = q / 15.0 - RA;
        if (eqT > 12.0) eqT -= 24.0;
        if (eqT < -12.0) eqT += 24.0;
        return std::make_pair(decl, eqT);
    };

    auto [decl, eqT] = sunPos(jd + 0.5 - tz / 24.0);
    double transit = 12.0 + tz - (lon / 15.0) - eqT;

    auto hourAngle = [&](double angleDeg) {
        double num = -std::sin(angleDeg * kDeg2Rad) - std::sin(lat * kDeg2Rad) * std::sin(decl * kDeg2Rad);
        double den = std::cos(lat * kDeg2Rad) * std::cos(decl * kDeg2Rad);
        double val = num / den;
        if (val < -1.0) val = -1.0;
        if (val > 1.0) val = 1.0;
        return std::acos(val) * kRad2Deg / 15.0;
    };

    // Standar Kemenag RI: Subuh 20 deg, Isya 18 deg, Ashar shadow factor 1 (Syafi'i), +2 menit ihtiyat
    double fajr = transit - hourAngle(20.0) + (2.0 / 60.0);
    double dhuhr = transit + (2.0 / 60.0);
    double asrAngle = -std::atan(1.0 / (1.0 + std::tan(std::abs(lat - decl) * kDeg2Rad))) * kRad2Deg;
    double asr = transit + hourAngle(asrAngle) + (2.0 / 60.0);
    double maghrib = transit + hourAngle(0.8333) + (2.0 / 60.0);
    double isha = transit + hourAngle(18.0) + (2.0 / 60.0);

    PrayerSchedule out;
    out.minutes = {
        int(std::round(fajr * 60.0)),
        int(std::round(dhuhr * 60.0)),
        int(std::round(asr * 60.0)),
        int(std::round(maghrib * 60.0)),
        int(std::round(isha * 60.0))
    };

    int nowMins = localNow.wHour * 60 + localNow.wMinute;
    out.nextIndex = -1;
    for (int i = 0; i < 5; ++i) {
        if (nowMins < out.minutes[i]) {
            out.nextIndex = i;
            out.isTomorrow = false;
            out.minsUntilNext = out.minutes[i] - nowMins;
            int prevMins = (i == 0) ? (out.minutes[4] - 1440) : out.minutes[i - 1];
            int span = out.minutes[i] - prevMins;
            out.progress = span > 0 ? float(nowMins - prevMins) / float(span) : 0.0f;
            break;
        }
    }
    if (out.nextIndex == -1) {
        // Setelah Isya: target berikutnya adalah Subuh esok hari
        out.nextIndex = 0;
        out.isTomorrow = true;
        int tomorrowFajr = out.minutes[0] + 1440;
        out.minsUntilNext = tomorrowFajr - nowMins;
        int span = tomorrowFajr - out.minutes[4];
        out.progress = span > 0 ? float(nowMins - out.minutes[4]) / float(span) : 0.0f;
    }
    if (out.progress < 0.0f) out.progress = 0.0f;
    if (out.progress > 1.0f) out.progress = 1.0f;
    return out;
}

} // namespace atrium
