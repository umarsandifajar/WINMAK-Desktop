$wfPath = "D:\dev\atrium-desktop\native\core\ui\widget_faces.cpp"
$wfCpp = Get-Content -Path $wfPath -Raw

$startIdx = $wfCpp.IndexOf("void PaintPrayer(")
$endMarker = "// The material's face under the content"
$endIdx = $wfCpp.IndexOf($endMarker)

if ($startIdx -ge 0 -and $endIdx -gt $startIdx) {
    $newPaintPrayer = @'
void PaintPrayer(Canvas& c, WidgetSize size, const WidgetContent& content, const WidgetWorld& world, const Pal& p, float W, float H, std::vector<WidgetHit>& hits)
{
    const D2D1_COLOR_F text = p.text, sub = p.sub, faint = p.track;
    const D2D1_COLOR_F accent = c.theme.accent;
    const float pad = 18.0f;

    SYSTEMTIME st = world.now;
    if (st.wYear == 0) GetLocalTime(&st);

    const PrayerSchedule todaySched = ComputePrayerSchedule(st, 0);
    const PrayerSchedule viewSched = ComputePrayerSchedule(st, content.prayerDayOffset);

    static const wchar_t* kPrayerNames[5] = { L"Subuh", L"Dzuhur", L"Ashar", L"Maghrib", L"Isya" };
    static const wchar_t* kDays[7] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
    static const wchar_t* kMonths[12] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };

    // 1. Header Bar (menyatu penuh dengan transparent glass)
    if (size == WidgetSize::Small) {
        c.Text(todaySched.city, D2D1::RectF(pad, 14, W - pad, 30), 13.0f, text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    } else {
        c.Text(L"Prayer \u2022 " + todaySched.city, D2D1::RectF(pad, 14, W * 0.56f, 32), 13.5f, text, DWRITE_FONT_WEIGHT_SEMI_BOLD);

        wchar_t dateBuf[48];
        swprintf_s(dateBuf, L"%s %d %s %d", kDays[st.wDayOfWeek % 7], st.wDay, kMonths[(st.wMonth - 1) % 12], st.wYear);
        wchar_t timeBuf[16];
        swprintf_s(timeBuf, L"%02d:%02d", st.wHour, st.wMinute);

        c.Text(dateBuf, D2D1::RectF(W * 0.45f, 12, W - pad, 25), 10.5f, sub, DWRITE_FONT_WEIGHT_NORMAL, 2);
        c.Text(timeBuf, D2D1::RectF(W * 0.55f, 24, W - pad, 42), 15.0f, text, DWRITE_FONT_WEIGHT_BOLD, 2);
    }

    // 2. Bagian Next Prayer (tanpa kotak gelap)
    const wchar_t* nextLabel = todaySched.isTomorrow ? L"Next, tomorrow" : L"Next prayer";
    std::wstring nextTimeStr = FormatPrayerTime(todaySched.minutes[todaySched.nextIndex]);

    int remH = todaySched.minsUntilNext / 60;
    int remM = todaySched.minsUntilNext % 60;
    wchar_t remBuf[32];
    if (remH > 0) swprintf_s(remBuf, L"in %d h %d min", remH, remM);
    else swprintf_s(remBuf, L"in %d min", remM);

    if (size == WidgetSize::Small) {
        c.Text(nextLabel, D2D1::RectF(pad, 36, W - pad, 50), 11.0f, sub, DWRITE_FONT_WEIGHT_NORMAL);
        c.Text(kPrayerNames[todaySched.nextIndex], D2D1::RectF(pad, 50, W - pad, 80), 24.0f, text, DWRITE_FONT_WEIGHT_BOLD);
        c.Text(nextTimeStr, D2D1::RectF(pad, 84, W - pad, 102), 14.0f, text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        c.Text(remBuf, D2D1::RectF(pad, 104, W - pad, 120), 11.5f, accent, DWRITE_FONT_WEIGHT_SEMI_BOLD);

        float barY = H - 18.0f;
        c.Fill(D2D1::RectF(pad, barY - 2.0f, W - pad, barY + 2.0f), 2.0f, faint);
        float fillW = std::max(8.0f, (W - 2.0f * pad) * todaySched.progress);
        c.Fill(D2D1::RectF(pad, barY - 2.0f, pad + fillW, barY + 2.0f), 2.0f, accent);
        return;
    }

    // Medium & Large Hero Section
    float heroTop = 44.0f;
    c.Text(nextLabel, D2D1::RectF(pad, heroTop, W * 0.6f, heroTop + 15), 11.0f, sub, DWRITE_FONT_WEIGHT_NORMAL);
    c.Text(kPrayerNames[todaySched.nextIndex], D2D1::RectF(pad, heroTop + 15, W * 0.6f, heroTop + 45), 25.0f, text, DWRITE_FONT_WEIGHT_BOLD);
    c.Text(nextTimeStr, D2D1::RectF(pad, heroTop + 47, pad + 80, heroTop + 65), 14.5f, text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.Text(remBuf, D2D1::RectF(W * 0.45f, heroTop + 48, W - pad, heroTop + 65), 12.0f, accent, DWRITE_FONT_WEIGHT_SEMI_BOLD, 2);

    float barY = heroTop + 74.0f;
    c.Fill(D2D1::RectF(pad, barY - 2.0f, W - pad, barY + 2.0f), 2.0f, faint);
    float fillW = std::max(10.0f, (W - 2.0f * pad) * todaySched.progress);
    c.Fill(D2D1::RectF(pad, barY - 2.0f, pad + fillW, barY + 2.0f), 2.0f, accent);

    // Medium (2x1): Jadwal 5 waktu berjajar rapi
    if (size == WidgetSize::Medium) {
        float rowTop = 130.0f;
        c.Line(pad, rowTop - 6.0f, W - pad, rowTop - 6.0f, faint, 1.0f);
        float colW = (W - 2.0f * pad) / 5.0f;
        for (int i = 0; i < 5; ++i) {
            float x0 = pad + i * colW;
            bool isNext = (i == todaySched.nextIndex);
            c.Text(kPrayerNames[i], D2D1::RectF(x0, rowTop, x0 + colW, rowTop + 14), 10.5f, isNext ? accent : sub, DWRITE_FONT_WEIGHT_SEMI_BOLD, 1);
            c.Text(FormatPrayerTime(todaySched.minutes[i]), D2D1::RectF(x0, rowTop + 15, x0 + colW, rowTop + 30), 11.5f, isNext ? text : sub, DWRITE_FONT_WEIGHT_BOLD, 1);
        }
        return;
    }

    // 3. Large (2x2): Kurva Matahari & Jadwal Lengkap (tanpa kotak gelap & tanpa blok Dzikir)
    float sectionTop = 136.0f;
    c.Line(pad, sectionTop, W - pad, sectionTop, faint, 1.0f);

    D2D1_RECT_F prevHit = D2D1::RectF(pad, sectionTop + 8.0f, pad + 32.0f, sectionTop + 34.0f);
    D2D1_RECT_F nextHit = D2D1::RectF(W - pad - 32.0f, sectionTop + 8.0f, W - pad, sectionTop + 34.0f);
    hits.push_back({ prevHit, L"prayer_prev" });
    hits.push_back({ nextHit, L"prayer_next" });

    c.Text(L"\u2039", prevHit, 18.0f, text, DWRITE_FONT_WEIGHT_BOLD, 1);
    c.Text(L"\u203A", nextHit, 18.0f, text, DWRITE_FONT_WEIGHT_BOLD, 1);

    std::wstring dayTitle = L"Hari ini";
    if (content.prayerDayOffset == -1) dayTitle = L"Kemarin";
    else if (content.prayerDayOffset == 1) dayTitle = L"Besok";
    else if (content.prayerDayOffset != 0) {
        wchar_t offBuf[24];
        swprintf_s(offBuf, L"%+d Hari", content.prayerDayOffset);
        dayTitle = offBuf;
    }
    c.Text(dayTitle, D2D1::RectF(pad + 36.0f, sectionTop + 12.0f, W - pad - 36.0f, sectionTop + 30.0f),
           13.0f, text, DWRITE_FONT_WEIGHT_SEMI_BOLD, 1);

    float colW = (W - 2.0f * pad) / 5.0f;
    float baseLineY = H - 56.0f;
    float peakY = sectionTop + 48.0f;
    const float kElev[5] = { 0.05f, 0.95f, 0.72f, 0.32f, 0.05f };
    D2D1_POINT_2F pts[5];
    for (int i = 0; i < 5; ++i) {
        pts[i].x = pad + colW * (float(i) + 0.5f);
        pts[i].y = baseLineY - kElev[i] * (baseLineY - peakY);
    }

    c.Line(pad, baseLineY, W - pad, baseLineY, faint, 1.0f);

    if (auto* d2d = c.D2D()) {
        winrt::com_ptr<ID2D1Factory> factory;
        d2d->GetFactory(factory.put());
        winrt::com_ptr<ID2D1PathGeometry> path;
        if (factory && SUCCEEDED(factory->CreatePathGeometry(path.put()))) {
            winrt::com_ptr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(path->Open(sink.put()))) {
                sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
                for (int i = 0; i < 4; ++i) {
                    float midX = (pts[i].x + pts[i + 1].x) * 0.5f;
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(midX, pts[i].y),
                        D2D1::Point2F(midX, pts[i + 1].y),
                        pts[i + 1]
                    ));
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();

                winrt::com_ptr<ID2D1SolidColorBrush> curveBrush;
                if (SUCCEEDED(d2d->CreateSolidColorBrush(accent, curveBrush.put()))) {
                    d2d->DrawGeometry(path.get(), curveBrush.get(), 2.4f);
                }
            }
        }
    }

    int activeIdx = (todaySched.nextIndex + 4) % 5;
    for (int i = 0; i < 5; ++i) {
        bool isCurrent = (content.prayerDayOffset == 0 && i == activeIdx);
        if (isCurrent) {
            D2D1_COLOR_F haloCol = accent;
            haloCol.a = 0.28f;
            c.Circle(pts[i].x, pts[i].y, 8.5f, haloCol);
            c.Circle(pts[i].x, pts[i].y, 4.5f, accent);
        } else {
            c.Circle(pts[i].x, pts[i].y, 3.8f, sub);
        }

        float x0 = pad + colW * float(i);
        c.Text(kPrayerNames[i], D2D1::RectF(x0, baseLineY + 10.0f, x0 + colW, baseLineY + 24.0f),
               11.0f, isCurrent ? text : sub, DWRITE_FONT_WEIGHT_SEMI_BOLD, 1);
        c.Text(FormatPrayerTime(viewSched.minutes[i]), D2D1::RectF(x0, baseLineY + 26.0f, x0 + colW, baseLineY + 40.0f),
               10.5f, isCurrent ? text : sub, isCurrent ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, 1);
    }
}

