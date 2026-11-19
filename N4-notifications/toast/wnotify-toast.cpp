// wnotify-toast - show one notification as a *native* Windows toast, with
// herbe's process contract, so it can be the display of `notifyd --exec`:
//
//   exit 0  the user clicked the toast (or one of its buttons)  -> ActionInvoked
//   exit 2  dismissed, expired, or closed with WM_CLOSE           -> NotificationClosed
//   exit 1  failure
//
// Input, in this order of preference:
//   NOTIFYD_SUMMARY / NOTIFYD_BODY / NOTIFYD_APP / NOTIFYD_ICON / NOTIFYD_URGENCY
//   (set by notifyd --exec), else the arguments as lines like herbe:
//   wnotify-toast "title" "line" "line"
//
// Options: --aumid ID       AppUserModelID shown as the sender (default: Windows
//                           PowerShell's, which is registered on every machine)
//          --keep           after the popup times out, stay in the Action Center and
//                           keep waiting for a click there (exit 2 when it is removed)
//
// Why C++/WinRT and not a PowerShell script: Windows PowerShell 5.1 cannot
// subscribe to WinRT events ("cannot subscribe to Windows RT events"), so a
// script cannot tell a click (Activated) from a dismissal (Dismissed).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <string>
#include <vector>

using namespace winrt;
using namespace Windows::UI::Notifications;
using namespace Windows::Data::Xml::Dom;

static const wchar_t *DEFAULT_AUMID = L"{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\WindowsPowerShell\\v1.0\\powershell.exe";

enum : UINT { WM_TOAST_ACTIVATED = WM_APP + 1, WM_TOAST_DISMISSED, WM_TOAST_FAILED, WM_TOAST_GONE };
static HWND g_hwnd;

static std::wstring env(const wchar_t *name)
{
	DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
	if (!n)
		return {};
	std::wstring s(n, L'\0');
	GetEnvironmentVariableW(name, s.data(), n);
	s.resize(n - 1);
	return s;
}

static std::wstring xml_escape(const std::wstring &s)
{
	std::wstring o;
	for (wchar_t c : s) {
		switch (c) {
		case L'&': o += L"&amp;"; break;
		case L'<': o += L"&lt;"; break;
		case L'>': o += L"&gt;"; break;
		case L'"': o += L"&quot;"; break;
		default: o += c;
		}
	}
	return o;
}

/* "C:\x.png" -> "file:///C:/x.png"; URIs pass through; icon names are dropped
 * (toasts only load files/URIs). */
static std::wstring icon_uri(const std::wstring &icon)
{
	if (icon.empty())
		return {};
	if (icon.find(L"://") != std::wstring::npos)
		return icon;
	if (icon.size() > 2 && icon[1] == L':' && GetFileAttributesW(icon.c_str()) != INVALID_FILE_ATTRIBUTES) {
		std::wstring u = L"file:///" + icon;
		for (auto &c : u)
			if (c == L'\\')
				c = L'/';
		return u;
	}
	return {};
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_CLOSE: /* notifyd CloseNotification / taskkill */
	case WM_TOAST_ACTIVATED:
	case WM_TOAST_DISMISSED:
	case WM_TOAST_FAILED:
	case WM_TOAST_GONE:
		PostQuitMessage(m == WM_TOAST_ACTIVATED ? 0 : m == WM_TOAST_FAILED ? 1 : 2);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static ToastNotification g_toast{nullptr};
static ToastNotifier g_notifier{nullptr};
static std::wstring g_aumid;

static void withdraw_toast()
{
	try {
		if (g_notifier && g_toast) {
			g_notifier.Hide(g_toast);
			ToastNotificationManager::History().Remove(g_toast.Tag(), g_toast.Group(), g_aumid);
		}
	} catch (...) {
	}
}

/* Runs on a thread the console creates. For CTRL_CLOSE_EVENT (console window
 * closed, which is also what `taskkill /PID` without /F does to a console
 * program) Windows terminates the process as soon as the handler returns, so
 * posting a message is not enough: withdraw the toast here and exit. */
static BOOL WINAPI on_ctrl(DWORD)
{
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	} catch (...) {
	}
	withdraw_toast();
	ExitProcess(2);
}

int wmain(int argc, wchar_t **argv)
{
	std::wstring aumid = DEFAULT_AUMID;
	bool keep = false;
	std::vector<std::wstring> lines;
	for (int i = 1; i < argc; i++) {
		std::wstring a = argv[i];
		if (a == L"--aumid" && i + 1 < argc)
			aumid = argv[++i];
		else if (a == L"--keep")
			keep = true;
		else
			lines.push_back(a);
	}

	std::wstring summary = env(L"NOTIFYD_SUMMARY"), body = env(L"NOTIFYD_BODY");
	std::wstring app = env(L"NOTIFYD_APP"), icon = env(L"NOTIFYD_ICON"), urgency = env(L"NOTIFYD_URGENCY");
	bool from_env = !summary.empty() || !body.empty();
	if (!from_env) {
		if (lines.empty()) {
			fwprintf(stderr, L"usage: wnotify-toast [--aumid ID] [--keep] TITLE [LINE...]\n");
			return 1;
		}
		summary = lines[0];
		for (size_t i = 1; i < lines.size(); i++)
			body += (i > 1 ? L"\n" : L"") + lines[i];
	}

	init_apartment(apartment_type::single_threaded);

	WNDCLASSW wc{};
	wc.lpfnWndProc = wndproc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"wnotify-toast";
	RegisterClassW(&wc);
	/* hidden top-level window (not HWND_MESSAGE): EnumWindows must find it so
	 * notifyd's CloseNotification (WM_CLOSE to the process' windows) works */
	g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"wnotify-toast", WS_POPUP, 0, 0, 0, 0,
				 nullptr, nullptr, wc.hInstance, nullptr);
	SetConsoleCtrlHandler(on_ctrl, TRUE);

	/* <toast>: critical -> scenario="urgent" style (stays until acted on) */
	std::wstring x = L"<toast";
	if (urgency == L"2")
		x += L" scenario=\"reminder\"";
	x += L"><visual><binding template=\"ToastGeneric\">";
	x += L"<text>" + xml_escape(summary) + L"</text>";
	if (!body.empty())
		x += L"<text>" + xml_escape(body) + L"</text>";
	if (!app.empty())
		x += L"<text placement=\"attribution\">" + xml_escape(app) + L"</text>";
	std::wstring uri = icon_uri(icon);
	if (!uri.empty())
		x += L"<image placement=\"appLogoOverride\" src=\"" + xml_escape(uri) + L"\"/>";
	x += L"</binding></visual>";
	if (urgency == L"2") /* reminder scenario requires a button */
		x += L"<actions><action content=\"OK\" arguments=\"default\"/></actions>";
	x += L"</toast>";

	ToastNotification toast{nullptr};
	ToastNotifier notifier{nullptr};
	try {
		XmlDocument doc;
		doc.LoadXml(x);
		toast = ToastNotification(doc);
		std::wstring tag = env(L"NOTIFYD_ID");
		toast.Tag(tag.empty() ? std::to_wstring(GetCurrentProcessId()) : tag);
		toast.Group(L"wnotify");
		/* events arrive on worker threads: hop to the window thread */
		toast.Activated([](auto &&, auto &&) { PostMessageW(g_hwnd, WM_TOAST_ACTIVATED, 0, 0); });
		toast.Failed([](auto &&, auto &&) { PostMessageW(g_hwnd, WM_TOAST_FAILED, 0, 0); });
		toast.Dismissed([keep](auto &&, ToastDismissedEventArgs const &a) {
			if (a.Reason() == ToastDismissalReason::TimedOut && keep)
				return; /* popup gone, still in the Action Center: keep waiting */
			PostMessageW(g_hwnd, WM_TOAST_DISMISSED, 0, 0);
		});
		notifier = ToastNotificationManager::CreateToastNotifier(aumid);
		g_toast = toast;
		g_notifier = notifier;
		g_aumid = aumid;
		notifier.Show(toast);
	} catch (hresult_error const &e) {
		fwprintf(stderr, L"wnotify-toast: %s (0x%08x)\n", e.message().c_str(), (unsigned)e.code());
		return 1;
	}

	/* --keep: the toast lives in the Action Center; poll until it leaves it */
	if (keep)
		SetTimer(g_hwnd, 1, 2000, nullptr);

	MSG m;
	int code = 2;
	while (GetMessageW(&m, nullptr, 0, 0) > 0) {
		if (m.message == WM_TIMER && keep) {
			bool present = false;
			for (auto const &t : ToastNotificationManager::History().GetHistory(aumid))
				present |= t.Tag() == toast.Tag() && t.Group() == toast.Group();
			if (!present)
				PostMessageW(g_hwnd, WM_TOAST_GONE, 0, 0);
			continue;
		}
		DispatchMessageW(&m);
	}
	code = (int)m.wParam;

	/* done: never leave a stale toast in the Action Center (notifyd already
	 * records the notification; Windows' copy would outlive its state) */
	withdraw_toast();
	return code;
}
