/* antares-gog-signin.exe (Windows): GOG's own sign-in page in a window (WP5.15b, D29 case 3).
 *
 * The Windows twin of main_gtk.c, on WebView2 (the Edge engine Windows 10 and 11 carry). Same
 * contract with the launcher: when GOG sends the page to https://embed.gog.com/on_login_success?
 * ...&code=..., the one-time code goes to standard output and the program exits 0; closing the
 * window exits 1; no web engine (the WebView2 runtime missing, or its loader) exits 2, and the
 * wizard then offers GOG's website. The session is InPrivate in a temporary profile folder that is
 * deleted afterwards, so nothing outlives the window, and no script is injected into the page: the
 * redirect is seen through WebView2's NavigationStarting event. WebView2Loader.dll (Microsoft's,
 * BSD licence) sits beside this program and is loaded at run time. */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

#include <shellapi.h>

#include <cwchar>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "WebView2.h"

namespace {
    constexpr wchar_t GOG_AUTH_URL[] = L"https://auth.gog.com/auth?client_id=46899977096215655&redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient"
                                       L"&response_type=code&layout=client2";
    constexpr wchar_t WINDOW_CLASS[] = L"AntaresGogSignin";

    int s_exit_code = 1;
    ICoreWebView2Environment* s_environment = nullptr;

    // A COM callback from a lambda: WebView2's handlers are one-method interfaces.
    template <class I, const IID* ID, class... A>
    class c_handler final : public I {
      public:
        explicit c_handler(std::function<HRESULT(A...)> call) : call_(std::move(call)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, *ID)) {
                *out = static_cast<I*>(this);
                AddRef();
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            return static_cast<ULONG>(InterlockedIncrement(&refs_));
        }
        ULONG STDMETHODCALLTYPE Release() override {
            const LONG left = InterlockedDecrement(&refs_);
            if (left == 0) delete this;
            return static_cast<ULONG>(left);
        }
        HRESULT STDMETHODCALLTYPE Invoke(A... arguments) override {
            return call_(arguments...);
        }

      private:
        LONG refs_ = 1;
        std::function<HRESULT(A...)> call_;
    };

    using t_environment_done = c_handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>;
    using t_controller_done = c_handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>;
    using t_navigation = c_handler<ICoreWebView2NavigationStartingEventHandler, &IID_ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>;
    using t_source = c_handler<ICoreWebView2SourceChangedEventHandler, &IID_ICoreWebView2SourceChangedEventHandler, ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*>;
    using t_new_window = c_handler<ICoreWebView2NewWindowRequestedEventHandler, &IID_ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>;
    using t_close = c_handler<ICoreWebView2WindowCloseRequestedEventHandler, &IID_ICoreWebView2WindowCloseRequestedEventHandler, ICoreWebView2*, IUnknown*>;

    // One window and its web view: the sign-in page, or a page's own window (Google's and the other
    // "continue with" sign-ins open one).
    struct s_view {
        HWND window = nullptr;
        ICoreWebView2Controller* controller = nullptr;
        ICoreWebView2* web = nullptr;
        bool main = false;
    };
    std::vector<s_view*> s_views;

    s_view* View_Of_(HWND window) {
        for (s_view* view : s_views)
            if (view->window == window) return view;
        return nullptr;
    }

    int Hex_(wchar_t c) {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    }

    // The code from GOG's success address, or "" when `url` is any other page. Only https on
    // embed.gog.com itself (not a look-alike host), the exact path, and a non-empty code.
    std::string Code_From_(const std::wstring& url) {
        const std::wstring scheme = L"https://";
        if (url.size() < scheme.size() || _wcsnicmp(url.c_str(), scheme.c_str(), scheme.size()) != 0) return "";
        const size_t host_end = url.find_first_of(L"/?#", scheme.size());
        std::wstring host = url.substr(scheme.size(), host_end == std::wstring::npos ? std::wstring::npos : host_end - scheme.size());
        if (host.size() > 4 && _wcsicmp(host.c_str() + host.size() - 4, L":443") == 0) host.resize(host.size() - 4);
        if (_wcsicmp(host.c_str(), L"embed.gog.com") != 0 || host_end == std::wstring::npos) return "";
        const size_t query = url.find(L'?', host_end);
        const std::wstring path = url.substr(host_end, (query == std::wstring::npos ? url.size() : query) - host_end);
        if (path != L"/on_login_success" || query == std::wstring::npos) return "";
        std::wstring parameters = url.substr(query + 1);
        const size_t hash = parameters.find(L'#');
        if (hash != std::wstring::npos) parameters.resize(hash);
        size_t at = 0;
        while (at <= parameters.size()) {
            size_t end = parameters.find(L'&', at);
            if (end == std::wstring::npos) end = parameters.size();
            const std::wstring pair = parameters.substr(at, end - at);
            if (pair.rfind(L"code=", 0) == 0) {
                std::string code;
                for (size_t i = 5; i < pair.size(); ++i) {
                    wchar_t c = pair[i];
                    if (c == L'%' && i + 2 < pair.size() && Hex_(pair[i + 1]) >= 0 && Hex_(pair[i + 2]) >= 0) {
                        c = static_cast<wchar_t>(Hex_(pair[i + 1]) * 16 + Hex_(pair[i + 2]));
                        i += 2;
                    } else if (c == L'+') {
                        c = L' ';
                    }
                    if (c > 127) return ""; // GOG's codes are plain ASCII
                    code += static_cast<char>(c);
                }
                return code;
            }
            at = end + 1;
        }
        return "";
    }

    void Write_Code_(const std::string& code) {
        const std::string line = code + "\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
    }

    void Fit_(s_view* view) {
        if (view->controller == nullptr) return;
        RECT bounds;
        GetClientRect(view->window, &bounds);
        view->controller->put_Bounds(bounds);
    }

    HWND New_Window_(int width, int height) {
        const UINT dpi = GetDpiForSystem();
        return CreateWindowExW(0, WINDOW_CLASS, L"Sign in to GOG", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(width, static_cast<int>(dpi), 96), MulDiv(height, static_cast<int>(dpi), 96), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    void Watch_(s_view* view);

    // A controller for `view`'s window, InPrivate where the runtime offers it (WebView2 101 on).
    HRESULT Create_Controller_(s_view* view, std::function<void(ICoreWebView2Controller*)> done) {
        auto* handler = new t_controller_done([view, done](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (FAILED(result) || controller == nullptr) {
                if (view->main) {
                    s_exit_code = 2;
                    PostQuitMessage(0);
                } else {
                    DestroyWindow(view->window);
                }
                return S_OK;
            }
            controller->AddRef();
            view->controller = controller;
            controller->get_CoreWebView2(&view->web);
            Fit_(view);
            Watch_(view);
            done(controller);
            return S_OK;
        });
        ICoreWebView2Environment10* environment10 = nullptr;
        HRESULT result = E_NOINTERFACE;
        if (SUCCEEDED(s_environment->QueryInterface(IID_ICoreWebView2Environment10, reinterpret_cast<void**>(&environment10)))) {
            ICoreWebView2ControllerOptions* options = nullptr;
            if (SUCCEEDED(environment10->CreateCoreWebView2ControllerOptions(&options))) {
                options->put_IsInPrivateModeEnabled(TRUE);
                result = environment10->CreateCoreWebView2ControllerWithOptions(view->window, options, handler);
                options->Release();
            }
            environment10->Release();
        }
        if (FAILED(result)) result = s_environment->CreateCoreWebView2Controller(view->window, handler);
        handler->Release();
        return result;
    }

    void Watch_(s_view* view) {
        EventRegistrationToken token;
        auto* navigation = new t_navigation([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* arguments) -> HRESULT {
            LPWSTR uri = nullptr;
            if (FAILED(arguments->get_Uri(&uri)) || uri == nullptr) return S_OK;
            std::string code = Code_From_(uri);
            CoTaskMemFree(uri);
            if (code.empty()) return S_OK; // every other page loads as usual
            arguments->put_Cancel(TRUE);   // the success page itself is never loaded
            Write_Code_(code);
            code.assign(code.size(), '\0');
            s_exit_code = 0;
            PostQuitMessage(0);
            return S_OK;
        });
        view->web->add_NavigationStarting(navigation, &token);
        navigation->Release();

        // The title shows which site the page is on, so the player can see it is GOG's.
        auto* source = new t_source([view](ICoreWebView2* web, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
            LPWSTR uri = nullptr;
            if (FAILED(web->get_Source(&uri)) || uri == nullptr) return S_OK;
            std::wstring host = uri;
            CoTaskMemFree(uri);
            const size_t start = host.find(L"://");
            host = start == std::wstring::npos ? L"" : host.substr(start + 3);
            host = host.substr(0, host.find_first_of(L"/?#"));
            SetWindowTextW(view->window, (L"Sign in to GOG - " + host).c_str());
            return S_OK;
        });
        view->web->add_SourceChanged(source, &token);
        source->Release();

        auto* new_window = new t_new_window([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* arguments) -> HRESULT {
            ICoreWebView2Deferral* deferral = nullptr;
            arguments->GetDeferral(&deferral);
            arguments->AddRef();
            auto* popup = new s_view();
            popup->window = New_Window_(520, 700);
            s_views.push_back(popup);
            const HRESULT result = Create_Controller_(popup, [arguments, deferral, popup](ICoreWebView2Controller*) {
                arguments->put_NewWindow(popup->web); // the page's window, with its opener kept
                arguments->put_Handled(TRUE);
                deferral->Complete();
                deferral->Release();
                arguments->Release();
                ShowWindow(popup->window, SW_SHOW);
            });
            if (FAILED(result)) {
                deferral->Complete();
                deferral->Release();
                arguments->Release();
                DestroyWindow(popup->window);
            }
            return S_OK;
        });
        view->web->add_NewWindowRequested(new_window, &token);
        new_window->Release();

        auto* close = new t_close([view](ICoreWebView2*, IUnknown*) -> HRESULT {
            if (!view->main) DestroyWindow(view->window);
            return S_OK;
        });
        view->web->add_WindowCloseRequested(close, &token);
        close->Release();
    }

    LRESULT CALLBACK Window_Proc_(HWND window, UINT message, WPARAM w, LPARAM l) {
        s_view* view = View_Of_(window);
        switch (message) {
            case WM_SIZE:
                if (view != nullptr) Fit_(view);
                return 0;
            case WM_DESTROY:
                if (view != nullptr) {
                    if (view->controller != nullptr) {
                        view->controller->Close();
                        view->controller->Release();
                    }
                    if (view->web != nullptr) view->web->Release();
                    const bool main = view->main;
                    for (size_t i = 0; i < s_views.size(); ++i)
                        if (s_views[i] == view) s_views.erase(s_views.begin() + static_cast<long>(i));
                    delete view;
                    if (main) PostQuitMessage(0); // closed before signing in: exit 1
                }
                return 0;
            default: return DefWindowProcW(window, message, w, l);
        }
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool test_page = argv != nullptr && argc > 2 && wcscmp(argv[1], L"--page") == 0; // tests only
    const std::wstring start_url = test_page ? argv[2] : GOG_AUTH_URL;

    // WebView2Loader.dll from this program's own folder, never from the search path.
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::filesystem::path folder = std::filesystem::path(self).parent_path();
    HMODULE loader = LoadLibraryExW((folder / L"WebView2Loader.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    using t_create = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    const auto create = loader != nullptr ? reinterpret_cast<t_create>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (create == nullptr) return 2;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = Window_Proc_;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&window_class);

    // A profile folder of our own in %TEMP%, removed when the window goes.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path profile = std::filesystem::path(temp) / (L"antares-gog-signin-" + std::to_wstring(GetCurrentProcessId()));

    auto* main_view = new s_view();
    main_view->main = true;
    main_view->window = New_Window_(520, 760);
    s_views.push_back(main_view);

    auto* environment_done = new t_environment_done([main_view, start_url, show](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
        if (FAILED(result) || environment == nullptr) { // no WebView2 runtime
            s_exit_code = 2;
            PostQuitMessage(0);
            return S_OK;
        }
        environment->AddRef();
        s_environment = environment;
        if (FAILED(Create_Controller_(main_view, [main_view, start_url, show](ICoreWebView2Controller*) {
                main_view->web->Navigate(start_url.c_str());
                ShowWindow(main_view->window, show);
                SetForegroundWindow(main_view->window);
            }))) {
            s_exit_code = 2;
            PostQuitMessage(0);
        }
        return S_OK;
    });
    const HRESULT started = create(nullptr, profile.c_str(), nullptr, environment_done);
    environment_done->Release();
    if (FAILED(started)) s_exit_code = 2;
    else {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    while (!s_views.empty()) DestroyWindow(s_views.back()->window);
    if (s_environment != nullptr) s_environment->Release();
    if (argv != nullptr) LocalFree(argv);
    // The browser processes let go of the profile folder shortly after the last view closes.
    for (int attempt = 0; attempt < 30; ++attempt) {
        std::error_code error;
        std::filesystem::remove_all(profile, error);
        if (!std::filesystem::exists(profile, error)) break;
        Sleep(100);
    }
    CoUninitialize();
    return s_exit_code;
}
