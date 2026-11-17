// Manual integration probe. No CTest registration: --click changes foreground/menu state.
// Compile the production adapter unchanged; omit task-model artwork/shortcut loading only.
#include "../core/shell/tray_model.cpp"
#include <iostream>

namespace atrium {
std::wstring AppExecutable(const std::wstring& path) { return path; }
bool LoadAtriumIcon(const std::wstring&, int, IconImage&) { return false; }
bool LoadShellImage(const std::wstring&, int, IconImage&, bool) { return false; }
}
namespace {
constexpr UINT kResult = WM_APP + 42;
int completion = -1;
HWND fixtureView = nullptr;
bool fixtureRespond = true;
std::vector<std::pair<UINT, UINT>> fixtureEvents;
LRESULT CALLBACK ProbeProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == kResult && w) completion = int(w);
    if (m == WM_TIMER && w == 99) { EndMenu(); return 0; }
    if (fixtureView && (m == WM_APP + 43 || m == WM_APP + 44)) {
        const bool v4 = m == WM_APP + 44;
        const UINT id = v4 ? HIWORD(l) : UINT(w), event = v4 ? LOWORD(l) : UINT(l);
        fixtureEvents.push_back({id, event});
        if (!fixtureRespond) return 0; // valid background-only action; no window/menu response
        if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT)
            ShowWindow(fixtureView, SW_SHOWNOACTIVATE);
        if (event == UINT(v4 ? WM_CONTEXTMENU : WM_RBUTTONUP)) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Atrium test menu (closes automatically)");
            SetTimer(h, 99, 450, nullptr);
            POINT at{}; GetCursorPos(&at);
            TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, h, nullptr);
            KillTimer(h, 99);
            DestroyMenu(menu);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void Pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
}
int wmain(int argc, wchar_t** argv)
{
    using namespace atrium;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    winrt::com_ptr<IUIAutomation> uia;
    winrt::check_hresult(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(uia.put())));
    if (argc == 2 && std::wstring(argv[1]) == L"--watch") {
        // Read-only production worker exercise, including bounded startup retries.
        TrayModel model(nullptr, 0);
        model.Refresh();
        const auto until = GetTickCount64() + 16500;
        while (GetTickCount64() < until) { Pump(); Sleep(20); }
        std::wcout << L"worker loaded=" << model.Loaded() << L" available=" << model.Available()
            << L" icons=" << model.Icons().size() << L'\n';
        return model.Loaded() ? 0 : 1;
    }
    if (argc == 2 && (std::wstring(argv[1]) == L"--fixture" || std::wstring(argv[1]) == L"--fixture-clicks")) {
        const bool interactions = std::wstring(argv[1]) == L"--fixture-clicks";
        WNDCLASSW wc{};
        wc.lpfnWndProc = ProbeProc; wc.lpszClassName = L"AtriumTrayFixture"; wc.hInstance = GetModuleHandleW(nullptr);
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        struct Fixture {
            NOTIFYICONDATAW uid{}, guid{};
            ~Fixture() { Shell_NotifyIconW(NIM_DELETE, &uid); Shell_NotifyIconW(NIM_DELETE, &guid); }
        } fixture;
        auto& uid = fixture.uid; auto& guid = fixture.guid;
        uid.cbSize = sizeof(uid); uid.hWnd = window; uid.uID = 4242;
        uid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        uid.hIcon = LoadIconW(nullptr, IDI_APPLICATION); uid.uCallbackMessage = WM_APP + 43;
        wcscpy_s(uid.szTip, L"tray_live_probe - fixture UID");
        guid = uid; guid.uID = 4243; guid.uFlags |= NIF_GUID;
        CLSIDFromString(L"{DCA21D39-29A7-4E50-82A9-D8CA9B6A29F9}", &guid.guidItem);
        wcscpy_s(guid.szTip, L"tray_live_probe - fixture GUID");
        if (interactions) guid.uCallbackMessage = WM_APP + 44;
        if (!Shell_NotifyIconW(NIM_ADD, &uid) || !Shell_NotifyIconW(NIM_ADD, &guid)) return 1;
        for (const auto& nid : {uid, guid}) {
            NOTIFYICONIDENTIFIER identifier{sizeof(identifier), nid.hWnd, nid.uID, nid.guidItem}; RECT bounds{};
            std::wcout << L"fixture getrect id=" << nid.uID << L" hr=" << std::hex
                << Shell_NotifyIconGetRect(&identifier, &bounds) << std::dec << L'\n';
        }
        if (interactions) {
            guid.uVersion = NOTIFYICON_VERSION_4;
            if (!Shell_NotifyIconW(NIM_SETVERSION, &guid)) return 1;
        }
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
        if (interactions) std::wcout << L"fixture materialize=" << ShowOverflow(uia.get()) << L'\n';
        auto expect = [&](size_t expected) {
            size_t count = 0;
            for (int i = 0; i < 20; ++i) {
                Pump(); Sleep(50);
                bool available = false, ordered = false;
                auto buttons = FindButtons(uia.get(), available);
                std::vector<Record> records;
                const auto match = MatchButtons(buttons, records, ordered);
                count = 0;
                for (int r : match) if (r >= 0 && _wcsicmp(records[size_t(r)].exe.c_str(), path) == 0) ++count;
                if (count == expected) break;
                if (i == 19) {
                    std::wcout << L"fixture path=" << path << L" records=" << records.size() << L" buttons=" << buttons.size() << L'\n';
                    for (size_t b = 0; b < buttons.size(); ++b)
                        if (buttons[b].tip.find(L"fixture") != std::wstring::npos)
                            std::wcout << L"fixture button tip=" << buttons[b].tip << L" exe="
                                << (match[b] >= 0 ? records[size_t(match[b])].exe : L"?") << L'\n';
                    for (const auto& record : records)
                        if (record.tip.find(L"fixture") != std::wstring::npos || record.exe.find(L"tray_live_probe") != std::wstring::npos)
                            std::wcout << L"fixture record tip=" << record.tip << L" exe=" << record.exe << L'\n';
                }
            }
            std::wcout << L"fixture expected=" << expected << L" actual=" << count << L'\n';
            return count == expected;
        };
        bool ok = expect(2);
        if (interactions && ok) {
            fixtureView = CreateWindowW(wc.lpszClassName, L"Atrium tray interaction test", WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, 360, 180, nullptr, nullptr, wc.hInstance, nullptr);
            // Explicit manual test: materialize the native popup, then use the production worker.
            const bool prepared = fixtureView && ShowOverflow(uia.get());
            std::wcout << L"fixture popup prepared=" << prepared << L'\n';
            ok = prepared;
            if (prepared) {
                TrayModel model(window, kResult);
                for (UINT id : {4242u, 4243u}) for (bool right : {false, true}) {
                    ShowWindow(fixtureView, SW_HIDE);
                    model.Refresh();
                    ULONGLONG deadline = GetTickCount64() + 5000;
                    std::vector<TrayIcon> icons;
                    do { Pump(); Sleep(20); icons = model.Icons(); }
                    while (!model.Loaded() && GetTickCount64() < deadline);
                    const TrayIcon* chosen = nullptr;
                    for (const auto& icon : icons)
                        if (_wcsicmp(icon.exe.c_str(), path) == 0 &&
                            icon.tip == (id == 4243 ? guid.szTip : uid.szTip)) {
                            if (chosen) { chosen = nullptr; break; }
                            chosen = &icon;
                        }
                    completion = -1; fixtureEvents.clear();
                    if (chosen) model.Click(*chosen, right);
                    deadline = GetTickCount64() + 5000;
                    while (chosen && completion < 0 && GetTickCount64() < deadline) { Pump(); Sleep(10); }
                    const UINT expected = right ? (id == 4243 ? WM_CONTEXTMENU : WM_RBUTTONUP) :
                        id == 4243 ? NIN_SELECT : WM_LBUTTONUP;
                    size_t callbacks = 0;
                    bool wrong = false;
                    for (const auto& [recipient, event] : fixtureEvents) {
                        if (event == expected || (!right && id == 4243 && event == NIN_KEYSELECT)) {
                            ++callbacks; wrong |= recipient != id;
                        }
                    }
                    const bool passed = completion == 1 && callbacks == 1 && !wrong;
                    std::wcout << L"fixture id=" << id << L" action=" << (right ? L"right" : L"left")
                        << L" callbacks=" << callbacks << L" completion=" << completion << L" passed=" << passed << L'\n';
                    ok = passed && ok;
                }
            }
            {
                TrayModel model(window, kResult);
                model.Refresh();
                ULONGLONG deadline = GetTickCount64() + 5000;
                while (!model.Loaded() && GetTickCount64() < deadline) { Pump(); Sleep(10); }
                fixtureRespond = false; completion = -1; fixtureEvents.clear();
                bool clicked = false;
                for (const auto& icon : model.Icons()) if (icon.tip == guid.szTip && _wcsicmp(icon.exe.c_str(), path) == 0) {
                    model.Click(icon, false); clicked = true; break;
                }
                deadline = GetTickCount64() + 5000;
                while (clicked && completion < 0 && GetTickCount64() < deadline) { Pump(); Sleep(10); }
                const bool quiet = completion == 4 && !fixtureEvents.empty();
                std::wcout << L"fixture background-only completion=" << completion << L" passed=" << quiet << L'\n';
                ok = quiet && ok; fixtureRespond = true;
            }
            DestroyWindow(fixtureView); fixtureView = nullptr;
        }
        Shell_NotifyIconW(NIM_DELETE, &uid);
        ok = expect(1) && ok;
        Shell_NotifyIconW(NIM_DELETE, &guid);
        ok = expect(0) && ok;
        DestroyWindow(window);
        return ok ? 0 : 1;
    }
    bool available = false, ordered = false;
    auto buttons = FindButtons(uia.get(), available);
    const auto buttonCount = buttons.size();
    std::vector<Record> records;
    const auto begin = GetTickCount64();
    const auto match = MatchButtons(buttons, records, ordered);
    std::wcout << L"buttons=" << buttonCount << L" reconciled=" << buttons.size() << L" records=" << records.size()
               << L" matchMs=" << GetTickCount64() - begin << L" taskbarVisible=" << IsWindowVisible(FindWindowW(L"Shell_TrayWnd", nullptr)) << L'\n';
    for (size_t i = 0; i < buttons.size(); ++i)
        std::wcout << i << L" source=" << (buttons[i].element ? L"uia" : L"record") << L" match=" << tray::MethodName(buttons[i].method)
                   << L" identity=" << buttons[i].recordId << L" exe=" << (match[i] >= 0 ? records[size_t(match[i])].exe : L"?") << L'\n';
    if (argc == 1) return 0; // read-only by default
    if (argc == 2 && std::wstring(argv[1]) == L"--tree") {
        for (HWND host : {FindWindowW(L"Shell_TrayWnd", nullptr), OverflowWindow()}) {
            if (HWND bridge = FindWindowExW(host, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)) host = bridge;
            winrt::com_ptr<IUIAutomationElement> root;
            winrt::com_ptr<IUIAutomationCondition> all;
            winrt::com_ptr<IUIAutomationElementArray> elements;
            uia->CreateTrueCondition(all.put());
            if (!host || FAILED(uia->ElementFromHandle(host, root.put())) || !root ||
                FAILED(root->FindAll(TreeScope_Descendants, all.get(), elements.put())) || !elements) continue;
            int count = 0; elements->get_Length(&count);
            for (int i = 0; i < count; ++i) {
                winrt::com_ptr<IUIAutomationElement> e;
                elements->GetElement(i, e.put());
                if (!e) continue;
                BSTR cls = nullptr, id = nullptr;
                BOOL offscreen = TRUE; RECT r{};
                e->get_CurrentClassName(&cls); e->get_CurrentAutomationId(&id);
                e->get_CurrentIsOffscreen(&offscreen); e->get_CurrentBoundingRectangle(&r);
                std::wcout << L"class=" << (cls ? cls : L"") << L" id=" << (id ? id : L"") << L" offscreen=" << offscreen
                           << L" rect=" << r.left << L"," << r.top << L"," << r.right << L"," << r.bottom << L'\n';
                SysFreeString(cls); SysFreeString(id);
            }
        }
        return 0;
    }
    if (argc != 4 || (std::wstring(argv[1]) != L"--click" && std::wstring(argv[1]) != L"--visible-click") ||
        (std::wstring(argv[3]) != L"left" && std::wstring(argv[3]) != L"right" && std::wstring(argv[3]) != L"double")) {
        std::wcerr << L"usage: tray_live_probe [--click FULL_EXE_PATH left|right|double]\n";
        return 2;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProbeProc; wc.lpszClassName = L"AtriumTrayProbe"; wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    int result = 2;
    {
        TrayModel model(window, kResult);
        model.Refresh();
        ULONGLONG deadline = GetTickCount64() + 10000;
        while (!model.Loaded() && GetTickCount64() < deadline) { Pump(); Sleep(10); }
        const auto icons = model.Icons();
        const TrayIcon* chosen = nullptr;
        for (const auto& icon : icons) if (_wcsicmp(icon.exe.c_str(), argv[2]) == 0) {
            if (chosen) { std::wcerr << L"Ambiguous executable; refusing to choose an icon\n"; return 2; }
            chosen = &icon;
        }
        if (!chosen) std::wcerr << L"No live icon; no application was launched\n";
        else {
            if (std::wstring(argv[1]) == L"--visible-click") {
                std::wcout << L"prepared=" << ShowOverflow(uia.get()) << L'\n';
            }
            if (std::wstring(argv[3]) == L"double") model.DoubleClick(*chosen);
            else model.Click(*chosen, std::wstring(argv[3]) == L"right");
            deadline = GetTickCount64() + 10000;
            while (completion < 0 && GetTickCount64() < deadline) { Pump(); Sleep(10); }
            std::wcout << L"completion=" << completion << L" (1=observed,2=system-popup,3=needs-user,4=accepted-unconfirmed,5=explicit-popup)\n";
            result = completion == 1 ? 0 : 1;
        }
    }
    DestroyWindow(window);
    return result;
}
