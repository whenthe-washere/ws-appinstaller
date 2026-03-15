// whenthe's app installer.cpp : Defines the entry point for the application.

#include "whenthe's app installer.h"
#include "framework.h"
#include <algorithm> // added for string transform
#include <chrono>
#include <commctrl.h> // For modern controls
#include <commdlg.h>
#include <ctime>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <string>
#include <urlmon.h>
#include <vector>
#include <windows.h>
#include <windowsx.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")

#define MAX_LOADSTRING 1000
#define CARD_HEIGHT 120
#define CARD_WIDTH 350
#define CARD_MARGIN 20
#define CARD_BUTTON_ID 4001
#define ID_APP_LOGO 6001
#define ID_INSTALL_LOGO 6002
#define DRAG_TIMER_ID 6003
#define ID_DIRECT_INSTALL_BUTTON 6004
#define ID_LONGPRESS_TIMER 6005
// Sidebar and search toggle IDs
#define ID_SIDEBAR 9000
#define ID_SIDEBAR_LIST 9001
#define ID_SIDEBAR_ABOUT_BTN 9002
#define ID_SEARCH_TOGGLE_BTN 7002
#define ID_SIDEBAR_TOGGLE_BTN 9003
// Sidebar search edit
#define ID_SIDEBAR_SEARCH_EDIT 9004
// Add IDs for details pane controls
#define ID_DETAILS_ICON 8001
#define ID_DETAILS_TITLE 8002
#define ID_DETAILS_DESC 8003
#define ID_DETAILS_BTN 8004
// Easter Egg IDs
#define ID_EE_NOTICE 8100
#define ID_EE_TEXT1 8101
#define ID_EE_TEXT2 8102
#define ID_EE_REST 8103

// Add this struct at the top (after includes)
struct CardData {
  wchar_t title[64];
  wchar_t description[256];
  wchar_t url[256];
};

// Selected app info for details pane
static CardData g_selected = {L"", L"", L""};
static HICON g_selectedIcon = nullptr;

HFONT hFont = nullptr; // Declare hFont as a global variable
static HBRUSH hWhiteBrush =
    CreateSolidBrush(RGB(30, 30, 60)); // dark background
static HBRUSH hMainBgBrush = CreateSolidBrush(RGB(20, 20, 40)); // main bg
HICON hCardIcons[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

HINSTANCE hInst = nullptr; // Move this here, before any function uses it

// Add UI globals
static HWND hProgressBar = nullptr;
static HWND hSearchEdit = nullptr;
static const int ID_SEARCH_EDIT = 7001;
static HWND hSearchToggleBtn = nullptr;
// Sidebar HWNDs
static HWND hSidebar = nullptr;
static HWND hSidebarList = nullptr;
static HWND hSidebarAbout = nullptr;
static HWND hSidebarToggleBtn = nullptr;
static HWND hSidebarSearchEdit = nullptr;
// Main title HWND
static HWND hMainTitle = nullptr;
// Right panel HWND
static HWND hRightPanel = nullptr;
// Details pane HWNDs (promoted to globals)
static HWND hDetailsIcon = nullptr;
static HWND hDetailsTitle = nullptr;
static HWND hDetailsDesc = nullptr;
static HWND hDetailsInstallBtn = nullptr;
// Sidebar width and visibility
static const int SIDEBAR_W = 260;
static bool g_sidebarVisible = false;
// Track when layout is invoked due to a window resize
static bool g_isResizingEvent = false;
// Long-press tracking variables
static bool g_isLongPressing = false;
static int g_longPressIndex = -1;

// Easter Egg Globals
static HWND hEENotice = nullptr;
static HWND hEEText1 = nullptr;
static HWND hEEText2 = nullptr;
static HWND hEERest = nullptr;
static COLORREF eeColor1 = RGB(220, 220, 220);
static COLORREF eeColor2 = RGB(220, 220, 220);
static int eeStage = 0; // 0: Start, 1: T1, 2: T1->T2, 3: T1->T2->T1, 4: WIN

void StartSpaceShooter(HINSTANCE hInstance);

// Content scrolling
static int g_scrollOffsetY = 0; // current scroll offset for main card area
static int g_contentHeight = 0; // total virtual content height (cards area)

// Forward declaration for controls referenced before their definitions
extern HWND hStatusLabel;

// Helper to detect whether a URL likely points to a downloadable file
static bool IsDownloadableUrl(const std::wstring &url) {
  if (url.empty())
    return false;
  // find last dot after last slash
  size_t lastSlash = url.find_last_of(L"/\\");
  size_t lastDot = url.find_last_of(L'.');
  if (lastDot == std::wstring::npos ||
      (lastSlash != std::wstring::npos && lastDot < lastSlash))
    return false;
  std::wstring ext = url.substr(lastDot + 1);
  for (auto &ch : ext)
    ch = (wchar_t)towlower(ch);
  const std::wstring known[] = {L"zip", L"exe", L"msi", L"msix", L"7z",
                                L"rar", L"tar", L"gz",  L"deb",  L"rpm"};
  for (auto &k : known)
    if (ext == k)
      return true;
  return false;
}

// Helper to update layout when window size or sidebar visibility changes
// Force a full redraw of the window and its children
static void RefreshUI(HWND hWnd) {
  RedrawWindow(hWnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
  if (hSidebar) {
    RedrawWindow(hSidebar, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
  }
}

// Layout the InfoBar contents dynamically based on description text height
static void LayoutInfoBar(HWND hWnd, int rightX, int rightY,
                          int rightPanelWidth, int rightPanelHeight) {
  int iconX = rightX + 20;
  int iconY = rightY + 20;
  int titleX = rightX + 60;
  int titleY = rightY + 20;
  int descX = rightX + 20;
  int descY = rightY + 60;
  int descW = rightPanelWidth - 40;

  int descH = 120;
  if (hDetailsDesc) {
    wchar_t buf[4096];
    GetWindowTextW(hDetailsDesc, buf, 4096);
    HDC hdc = GetDC(hWnd);
    HFONT old = (HFONT)SelectObject(hdc, hFont);
    RECT rcCalc{0, 0, descW, 0};
    DrawTextW(hdc, buf, -1, &rcCalc, DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hdc, old);
    ReleaseDC(hWnd, hdc);
    int h = rcCalc.bottom - rcCalc.top;
    if (h > descH)
      descH = h;
    int maxDescH = max(0, rightPanelHeight - (descY - rightY) - 200);
    if (maxDescH > 0)
      descH = min(descH, maxDescH);
  }

  if (hDetailsIcon)
    MoveWindow(hDetailsIcon, iconX, iconY, 32, 32, TRUE);
  if (hDetailsTitle)
    MoveWindow(hDetailsTitle, titleX, titleY, rightPanelWidth - 80, 24, TRUE);
  if (hDetailsDesc)
    MoveWindow(hDetailsDesc, descX, descY, descW, descH, TRUE);

  int btnY = descY + descH + 10;
  if (hDetailsInstallBtn)
    MoveWindow(hDetailsInstallBtn, descX, btnY, rightPanelWidth - 40, 36, TRUE);
  int statusY = btnY + 50;
  if (hStatusLabel)
    MoveWindow(hStatusLabel, descX, statusY, rightPanelWidth - 60, 20, TRUE);
  if (hProgressBar)
    MoveWindow(hProgressBar, descX, statusY + 25, rightPanelWidth - 60, 18,
               TRUE);
}

// Layout Easter Egg controls
static void LayoutEasterEgg(HWND hWnd, int rightX, int rightY,
                            int rightPanelWidth) {
  if (!hEENotice || !IsWindowVisible(hEENotice))
    return;

  int descX = rightX + 20;
  int curY = rightY + 60; // Start where DetailsDesc starts

  int noticeH = 100;
  MoveWindow(hEENotice, descX, curY, rightPanelWidth - 40, noticeH, TRUE);
  curY += noticeH;

  // Interactive Text Line
  int text1W = 230; // Sufficient width for the first line
  int text2W = 100; // Sufficient width for the second line
  MoveWindow(hEEText1, descX, curY, text1W, 24, TRUE);
  curY += 24; // Move to next line
  MoveWindow(hEEText2, descX, curY, text2W, 24, TRUE);
  curY += 24;

  // Rest of text "----------------..."
  MoveWindow(hEERest, descX, curY, rightPanelWidth - 40, 100, TRUE);
}

static void UpdateLayout(HWND hWnd) {
  RECT rc;
  GetClientRect(hWnd, &rc);
  const int padding = 20;
  const int rightPanelWidth = 300;
  const int sidebarMargin = 10; // Margin for floating effect

  // Calculate content area based on sidebar visibility
  int contentLeft = padding;
  int contentWidth = rc.right - padding;

  if (g_sidebarVisible) {
    // When sidebar is visible, content starts after sidebar
    contentLeft = SIDEBAR_W + padding;
    contentWidth = rc.right - SIDEBAR_W - padding * 2 - rightPanelWidth -
                   sidebarMargin * 2;
  }

  // Title and sidebar toggle button positioning
  if (g_sidebarVisible) {
    // When sidebar is visible, place toggle button inside the AppBar (sidebar)
    // client area
    if (hSidebarToggleBtn) {
      HWND btnParent = GetParent(hSidebarToggleBtn);
      if (btnParent != hSidebar) {
        SetParent(hSidebarToggleBtn, hSidebar);
      }
      MoveWindow(hSidebarToggleBtn, 16, 12, 28, 28, TRUE);
      SetWindowPos(hSidebarToggleBtn, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE);
    }
    if (hMainTitle)
      MoveWindow(hMainTitle, contentLeft, 10, max(300, contentWidth - padding),
                 40, TRUE);
  } else {
    // When sidebar is hidden, place toggle button at the main window's left
    // edge
    if (hSidebarToggleBtn) {
      HWND btnParent = GetParent(hSidebarToggleBtn);
      if (btnParent != hWnd) {
        SetParent(hSidebarToggleBtn, hWnd);
      }
      MoveWindow(hSidebarToggleBtn, padding, 12, 28, 28, TRUE);
      SetWindowPos(hSidebarToggleBtn, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE);
    }
    if (hMainTitle)
      MoveWindow(hMainTitle, padding + 36, 10, max(300, contentWidth - padding),
                 40, TRUE);
  }

  // Right panel (InfoBar) - float inside window with margins
  int rightX = rc.right - rightPanelWidth - sidebarMargin;
  int rightY = sidebarMargin;
  int rightHeight = rc.bottom - (sidebarMargin * 2);
  if (hRightPanel)
    MoveWindow(hRightPanel, rightX, rightY, rightPanelWidth, rightHeight, TRUE);

  // (Top-level search positioning removed)

  // Dynamic InfoBar layout
  LayoutInfoBar(hWnd, rightX, rightY, rightPanelWidth, rightHeight);
  LayoutEasterEgg(hWnd, rightX, rightY, rightPanelWidth);

  // AppBar (Left Sidebar) - float inside window with margins
  if (hSidebar) {
    MoveWindow(hSidebar, sidebarMargin, sidebarMargin,
               SIDEBAR_W - sidebarMargin, rc.bottom - (sidebarMargin * 2),
               TRUE);

    // Layout sidebar children relative to the sidebar itself
    RECT rcSb{};
    GetClientRect(hSidebar, &rcSb);
    int sidebarHeight = rcSb.bottom;
    // Place About button at bottom
    int aboutY = max(0, sidebarHeight - 44);
    if (hSidebarAbout)
      MoveWindow(hSidebarAbout, 16, aboutY, SIDEBAR_W - 32 - sidebarMargin, 28,
                 TRUE);

    // Sidebar search edit right above the About button
    int gap = 8; // space between search and about button
    int searchH = 28;
    int searchY = aboutY - gap - searchH;
    if (hSidebarSearchEdit)
      MoveWindow(hSidebarSearchEdit, 16, max(12, searchY),
                 SIDEBAR_W - 32 - sidebarMargin, searchH, TRUE);

    // Sidebar list fills from top to just above the search box
    int listTop = 50;
    int listHeight = max(0, (searchY - 16) - listTop);
    if (hSidebarList)
      MoveWindow(hSidebarList, 16, listTop, SIDEBAR_W - 32 - sidebarMargin,
                 listHeight, TRUE);
  }

  // Cards layout - positioned in content area with adjusted width
  int availableWidth = rightX - contentLeft - padding - sidebarMargin;
  if (availableWidth < CARD_WIDTH)
    availableWidth = CARD_WIDTH;
  int perCol = availableWidth / (CARD_WIDTH + CARD_MARGIN);
  if (perCol < 1)
    perCol = 1;
  if (perCol > 3)
    perCol = 3;

  const int numCards = 6;
  int lastBottom = 60;
  for (int i = 0; i < numCards; ++i) {
    int col = i % perCol;
    int row = i / perCol;
    int x = contentLeft + col * (CARD_WIDTH + CARD_MARGIN);
    int y = 60 + row * (CARD_HEIGHT + CARD_MARGIN);
    HWND hCard = GetDlgItem(hWnd, 2001 + i);
    if (hCard) {
      // apply vertical scroll offset
      MoveWindow(hCard, x, y - g_scrollOffsetY, CARD_WIDTH, CARD_HEIGHT, TRUE);
    }
    int cardBottom = y + CARD_HEIGHT;
    if (cardBottom > lastBottom)
      lastBottom = cardBottom;
  }
  // compute content height for scrolling (bottom of last row + padding)
  g_contentHeight = lastBottom + CARD_MARGIN;

  // configure scrollbar visibility and range
  int viewportHeight = rc.bottom - rc.top;
  BOOL needScroll = g_contentHeight > viewportHeight;
  ShowScrollBar(hWnd, SB_VERT, needScroll);
  SCROLLINFO si{};
  si.cbSize = sizeof(si);
  si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
  si.nMin = 0;
  si.nMax = g_contentHeight;
  si.nPage = viewportHeight;
  si.nPos = g_scrollOffsetY;
  SetScrollInfo(hWnd, SB_VERT, &si, TRUE);

  // After layout adjustments, force a redraw to avoid visual artifacts
  RefreshUI(hWnd);
}

// Add this global variable above (after hInst declaration)
static WNDPROC OldIconDragProc = nullptr;
static WNDPROC OldSidebarProc = nullptr;
static WNDPROC OldSidebarListProc = nullptr;

// Sidebar subclass proc to paint background, layout children, and forward
// notifications
LRESULT CALLBACK SidebarProc(HWND hwnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {
  switch (msg) {
  case WM_COMMAND:
    // Forward child notifications to the main window so existing handlers work
    return SendMessage(GetParent(hwnd), WM_COMMAND, wParam, lParam);
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Create rounded rectangle background
    HBRUSH hBrushSidebar = CreateSolidBrush(RGB(30, 30, 60));
    SelectObject(hdc, hBrushSidebar);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 20, 20);
    DeleteObject(hBrushSidebar);

    // Draw border
    HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(40, 40, 70));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 20, 20);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hBorderPen);

    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_CTLCOLORSTATIC: {
    SetBkColor((HDC)wParam, RGB(30, 30, 60));
    SetTextColor((HDC)wParam, RGB(220, 220, 220));
    static HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 60));
    return (INT_PTR)hBrush;
  }
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, RGB(30, 30, 60));
    SetTextColor(hdc, RGB(220, 220, 220));
    static HBRUSH hListBrush = CreateSolidBrush(RGB(30, 30, 60));
    return (INT_PTR)hListBrush;
  }
  default:
    return CallWindowProc(OldSidebarProc, hwnd, msg, wParam, lParam);
  }
}

// New window procedure for the drag icon control that draws an outline.
LRESULT CALLBACK IconDragProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Let the default proc paint the icon/control first.
    CallWindowProc(OldIconDragProc, hwnd, msg, wParam, lParam);

    // Draw a visible red border around the whole control.
    RECT rect;
    GetClientRect(hwnd, &rect);
    HPEN hPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0)); // Black, thicker border
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hPen);

    EndPaint(hwnd, &ps);
    return 0;
  }
  default:
    return CallWindowProc(OldIconDragProc, hwnd, msg, wParam, lParam);
  }
}

std::wstring GetDownloadsPath() {
  PWSTR path = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path))) {
    result = path;
    CoTaskMemFree(path);
  }
  return result;
}

std::wstring GetFileNameFromUrl(const std::wstring &url) {
  size_t pos = url.find_last_of(L"/\\");
  if (pos != std::wstring::npos && pos + 1 < url.length())
    return url.substr(pos + 1);
  return L"downloaded_file";
}

// Define your repo info and current version
const wchar_t *GITHUB_TAGS_URL =
    L"https://api.github.com/repos/whenthe-washere/whenthes-app-installer/tags";
const wchar_t *CURRENT_VERSION = L"2.00.5";

bool IsNewerVersionAvailable(const std::wstring &latestTag) {
  // Simple string comparison, can be improved for semantic versioning
  return latestTag != CURRENT_VERSION;
}

std::wstring GetLatestTagFromJson(const std::wstring &json) {
  // Very basic: look for "name":"vX.Y.Z"
  size_t pos = json.find(L"\"name\":\"");
  if (pos == std::wstring::npos)
    return L"";
  pos += 8;
  size_t end = json.find(L"\"", pos);
  if (end == std::wstring::npos)
    return L"";
  return json.substr(pos, end - pos);
}

void CheckForUpdates(HWND hWnd) {
  // Download tags JSON to a temp file
  wchar_t tempPath[MAX_PATH];
  GetTempPathW(MAX_PATH, tempPath);
  std::wstring tempFile = std::wstring(tempPath) + L"tags.json";
  HRESULT hr = URLDownloadToFileW(nullptr, GITHUB_TAGS_URL, tempFile.c_str(), 0,
                                  nullptr);

  if (SUCCEEDED(hr)) {
    // Read file content
    std::wifstream file(tempFile);
    std::wstringstream buffer;
    buffer << file.rdbuf();
    std::wstring json = buffer.str();

    std::wstring latestTag = GetLatestTagFromJson(json);
    if (!latestTag.empty() && IsNewerVersionAvailable(latestTag)) {
      std::wstring msg =
          L"Update available: " + latestTag +
          L"\nCheck WAI Releases in GitHub(repository: ws-appinstaller)";
      MessageBoxW(hWnd, msg.c_str(), L"Update Available", MB_ICONINFORMATION);
    }
    DeleteFileW(tempFile.c_str());
  }
}

// Global Variables:
WCHAR szTitle[MAX_LOADSTRING];       // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING]; // the main window class name
HWND hStatusLabel = nullptr;

// Forward declarations of functions included in this code module:
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CardProc(HWND hWnd, UINT message, WPARAM wParam,
                          LPARAM lParam);
void DownloadAndInstall(HWND hWnd, LPCWSTR url, LPCWSTR localPath);
void HideDragInstallUI(HWND hWnd);
void CALLBACK LongPressTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                                 DWORD dwTime);
LRESULT CALLBACK SidebarListProc(HWND hWnd, UINT message, WPARAM wParam,
                                 LPARAM lParam);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // Initialize common controls (progress bar)
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);

  // Initialize global strings
  LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
  LoadStringW(hInstance, IDC_WHENTHESAPPINSTALLER, szWindowClass,
              MAX_LOADSTRING);
  MyRegisterClass(hInstance);

  // Register custom card class
  WNDCLASSW wc = {0};
  wc.lpfnWndProc = CardProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = L"CardClass";
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 0);
  RegisterClassW(&wc);

  // Perform application initialization:
  if (!InitInstance(hInstance, nCmdShow)) {
    return FALSE;
  }

  HACCEL hAccelTable =
      LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WHENTHESAPPINSTALLER));

  MSG msg;

  // Main message loop:
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance) {
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WHENTHESAPPINSTALLER));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = nullptr; // remove menu bar (white topbar)
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

  return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
  hInst = hInstance;

  // Start with sidebar hidden, so base window width is 1200
  HWND hWnd =
      CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                    0, 1200, 700, nullptr, nullptr, hInstance, nullptr);

  if (!hWnd)
    return FALSE;

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  // Check for updates after window is shown
  CheckForUpdates(hWnd);

  // Create a larger, bold font for the title
  HFONT hTitleFont =
      CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  // Sidebar background panel (overlay)
  hSidebar = CreateWindowW(
      L"STATIC", nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
      SIDEBAR_W, 700, hWnd, (HMENU)ID_SIDEBAR, hInstance, nullptr);
  // Subclass sidebar for custom paint and forwarding
  OldSidebarProc =
      (WNDPROC)SetWindowLongPtr(hSidebar, GWLP_WNDPROC, (LONG_PTR)SidebarProc);

  // Sidebar product list and About button as children of the sidebar window
  hSidebarList = CreateWindowW(L"LISTBOX", nullptr,
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                               16, 50, SIDEBAR_W - 32, 560, hSidebar,
                               (HMENU)ID_SIDEBAR_LIST, hInstance, nullptr);
  // Subclass the list for long-press detection
  OldSidebarListProc = (WNDPROC)SetWindowLongPtr(hSidebarList, GWLP_WNDPROC,
                                                 (LONG_PTR)SidebarListProc);

  // Sidebar search edit (inside AppBar)
  hSidebarSearchEdit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      16, 12, SIDEBAR_W - 32, 28, hSidebar, (HMENU)ID_SIDEBAR_SEARCH_EDIT,
      hInstance, nullptr);

  // About button at bottom of sidebar
  RECT rcClient;
  GetClientRect(hWnd, &rcClient);
  hSidebarAbout =
      CreateWindowW(L"BUTTON", L"About whenthe's app installer",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16,
                    rcClient.bottom - 44, SIDEBAR_W - 32, 28, hSidebar,
                    (HMENU)ID_SIDEBAR_ABOUT_BTN, hInstance, nullptr);

  // Main title and sidebar toggle next to it
  hSidebarToggleBtn = CreateWindowW(
      L"BUTTON", L"◨ ", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 20, 12, 28, 28,
      hWnd, (HMENU)ID_SIDEBAR_TOGGLE_BTN, hInstance, nullptr);

  // (Top-level search removed; search exists in AppBar only)

  hMainTitle =
      CreateWindowW(L"STATIC", L"Welcome to WAI(whenthe's app installer)!",
                    WS_CHILD | WS_VISIBLE, 60, 10, 700, 40, hWnd, (HMENU)5001,
                    hInstance, nullptr);
  SendMessage(hMainTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

  const wchar_t *titles[6] = {L"CenterOS After Install Setup",
                              L"visualOS HoverNet - PY Variant",
                              L"Bugfinders",
                              L"visualOS HoverNet - IE Variant",
                              L"visualOS 1.2 OBT1",
                              L"visualOS HoverNet"};
  const wchar_t *descriptions[6] = {
      L"The new OOBE for CenterOS V4.",
      L"The Python variant of HoverNet, featuring PyQt6 and a new custom title "
      L"bar.",
      L"A game about finding actual system bugs and ancient/very old software. Mostly revolves around Windows. You might be able to find something like this in the About section...",
      L"The IE variant of HoverNet, featuring an UI similiar to Internet "
      L"Explorer.",
      L"visualOS 1.2 Open Beta - Test 1\nThe 1st Open Beta test of visualOS "
      L"1.2.",
      L"A completely renewed browser, made to continue WB's legacy: HoverNet."};
  const wchar_t *urls[6] = {
      L"https://github.com/whenthe-washere/CenterOS-AfterInstallSetup/releases/"
      L"download/preview3/ais-preview3.zip",
      L"https://github.com/whenthe-washere/visualos-hovernet/releases/download/"
      L"ver2.0.0-py/ver2-pyvariant.zip",
      L"",
      L"https://github.com/whenthe-washere/dev-releases/releases/download/"
      L"ver2.0.0-ie/vOS-HoverNet.zip",
      L"https://sites.google.com/view/ws-centerosinst/ready-to-switch",
      L""};

  // Load icons for cards from resources
  hCardIcons[0] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(AIS_ICON),
                                    IMAGE_ICON, 32, 32, 0);
  hCardIcons[1] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(HNPY_ICON),
                                    IMAGE_ICON, 32, 32, 0);
  hCardIcons[2] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(BUGFINDERS_ICON),
                                    IMAGE_ICON, 32, 32, 0);
  hCardIcons[3] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(HNDEV_ICON),
                                    IMAGE_ICON, 32, 32, 0);
  hCardIcons[4] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(VISUALOS_ICON1),
                                    IMAGE_ICON, 32, 32, 0);
  hCardIcons[5] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(HN_ICON),
                                    IMAGE_ICON, 32, 32, 0);

  const int numCards = 6;

  // Populate sidebar list with titles (copy)
  for (int i = 0; i < numCards; ++i) {
    SendMessageW(hSidebarList, LB_ADDSTRING, 0, (LPARAM)titles[i]);
  }

  // Create cards (initial position will be set by UpdateLayout)
  for (int i = 0; i < numCards; ++i) {
    HWND hCard =
        CreateWindowW(L"CardClass", nullptr, WS_CHILD | WS_VISIBLE, 0, 0,
                      CARD_WIDTH, CARD_HEIGHT, hWnd,
                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(2001 + i)),
                      hInstance, nullptr);

    CardData *data = new CardData();
    wcsncpy_s(data->title, titles[i], _TRUNCATE);
    wcsncpy_s(data->description, descriptions[i], _TRUNCATE);
    wcsncpy_s(data->url, urls[i], _TRUNCATE);
    SetWindowLongPtr(hCard, GWLP_USERDATA, (LONG_PTR)data);

    HWND hButton = GetDlgItem(hCard, CARD_BUTTON_ID);
    if (hButton) {
      if (wcslen(data->url) == 0) {
        SetWindowTextW(hButton, L"Not available");
        EnableWindow(hButton, FALSE);
      } else {
        // Decide label based on whether URL looks like a downloadable asset
        if (IsDownloadableUrl(data->url)) {
          SetWindowTextW(hButton, L"Download and Install");
        } else {
          SetWindowTextW(hButton, L"Open Website");
        }
        EnableWindow(hButton, TRUE);
      }
    }
  }

  // Right panel with rounded corners
  hRightPanel =
      CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE |
                        SS_OWNERDRAW, // Add SS_OWNERDRAW for custom painting
                    900, 0, 300, 700, hWnd, (HMENU)3001, hInstance, nullptr);

  // Details: Icon
  hDetailsIcon =
      CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON, 920, 20, 1,
                    32, hWnd, (HMENU)ID_DETAILS_ICON, hInstance, nullptr);
  // Details: Title
  hDetailsTitle =
      CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 920, 20, 220, 24,
                    hWnd, (HMENU)ID_DETAILS_TITLE, hInstance, nullptr);
  // Details: Description
  hDetailsDesc =
      CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 920, 60, 260, 120,
                    hWnd, (HMENU)ID_DETAILS_DESC, hInstance, nullptr);

  // Easter Egg Controls (Hidden initially)
  hEENotice = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0,
                            hWnd, (HMENU)ID_EE_NOTICE, hInstance, nullptr);
  hEEText1 = CreateWindowW(L"STATIC", L"whenthe's app installer by WS",
                           WS_CHILD | SS_NOTIFY | SS_LEFT, 0, 0, 0, 0, hWnd,
                           (HMENU)ID_EE_TEXT1, hInstance, nullptr);
  hEEText2 = CreateWindowW(L"STATIC", L"Version 2.00.5",
                           WS_CHILD | SS_NOTIFY | SS_LEFT, 0, 0, 0, 0, hWnd,
                           (HMENU)ID_EE_TEXT2, hInstance, nullptr);
  hEERest = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0, hWnd,
                          (HMENU)ID_EE_REST, hInstance, nullptr);

  // Fonts
  hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT hTitleFont2 =
      CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  SendMessage(hDetailsTitle, WM_SETFONT, (WPARAM)hTitleFont2, TRUE);
  SendMessage(hDetailsDesc, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hDetailsInstallBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hSidebarList, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hSidebarAbout, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hSidebarToggleBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hSidebarSearchEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

  // Apply font to Easter Egg controls
  SendMessage(hEENotice, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hEEText1, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hEEText2, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hEERest, WM_SETFONT, (WPARAM)hFont, TRUE);

  // Cue banner for AppBar search
  SendMessageW(hSidebarSearchEdit, 0x1501 /* EM_SETCUEBANNER */, FALSE,
               (LPARAM)L"");

  // Status and progress under details
  hStatusLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 920, 240,
                               240, 20, hWnd, (HMENU)2002, hInstance, nullptr);
  SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
  ShowWindow(hStatusLabel, SW_HIDE);

  hProgressBar = CreateWindowExW(
      0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 920,
      265, 240, 18, hWnd, (HMENU)2003, hInstance, nullptr);
  SendMessage(hProgressBar, PBM_SETMARQUEE, FALSE, 0);
  ShowWindow(hProgressBar, SW_HIDE);

  // Ensure sidebar overlays other content and starts hidden
  SetWindowPos(hSidebar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  ShowWindow(hSidebar, g_sidebarVisible ? SW_SHOW : SW_HIDE);
  ShowWindow(hSidebarList, g_sidebarVisible ? SW_SHOW : SW_HIDE);
  ShowWindow(hSidebarAbout, g_sidebarVisible ? SW_SHOW : SW_HIDE);

  // Initial responsive layout
  UpdateLayout(hWnd);

  return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  switch (message) {
  case WM_SIZE:
    g_isResizingEvent = true;
    // clamp scroll offset on resize
    {
      RECT rc;
      GetClientRect(hWnd, &rc);
      int viewportHeight = rc.bottom - rc.top;
      int maxOffset = max(0, g_contentHeight - viewportHeight);
      if (g_scrollOffsetY > maxOffset)
        g_scrollOffsetY = maxOffset;
    }
    UpdateLayout(hWnd);
    g_isResizingEvent = false;
    break;
  case WM_COMMAND: {
    int wmId = LOWORD(wParam);
    int notify = HIWORD(wParam);
    if (wmId == ID_SIDEBAR_TOGGLE_BTN && notify == BN_CLICKED) {
      g_sidebarVisible = !g_sidebarVisible;

      // Show/hide sidebar and its children only (do not resize the window)
      ShowWindow(hSidebar, g_sidebarVisible ? SW_SHOW : SW_HIDE);
      ShowWindow(hSidebarList, g_sidebarVisible ? SW_SHOW : SW_HIDE);
      ShowWindow(hSidebarAbout, g_sidebarVisible ? SW_SHOW : SW_HIDE);
      // Keep AppBar on top of content
      if (g_sidebarVisible) {
        SetWindowPos(hSidebar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      }

      // Update layout
      UpdateLayout(hWnd);
      return 0;
    }
    if (wmId == ID_SIDEBAR_ABOUT_BTN && notify == BN_CLICKED) {
      // Show About content in the InfoBar instead of a dialog
      const wchar_t *aboutTitle = L"About WAI";
      const wchar_t *aboutDesc =
          L"whenthe's app installer"
          L"v2.00.5\n-------------------------------------------\n"
          L"Release Notes:\n"
          L"Fixed border colors for AppBar and InfoBar";

      HICON hAppIcon =
          LoadIcon(hInst, MAKEINTRESOURCE(IDI_WHENTHESAPPINSTALLER));
      g_selected = {0};
      wcsncpy_s(g_selected.title, aboutTitle, _TRUNCATE);
      g_selected.url[0] = L'\0';
      g_selectedIcon = hAppIcon;
      if (hDetailsIcon)
        SendMessage(hDetailsIcon, STM_SETICON, (WPARAM)g_selectedIcon, 0);
      if (hDetailsTitle)
        SetWindowTextW(hDetailsTitle, aboutTitle);
      if (hDetailsDesc)
        SetWindowTextW(hDetailsDesc, aboutDesc);

      // Easter Egg Setup: Hide standard Desc, Show Easter Egg controls
      ShowWindow(hDetailsDesc, SW_HIDE);
      ShowWindow(hEENotice, SW_SHOW);
      ShowWindow(hEEText1, SW_SHOW);
      ShowWindow(hEEText2, SW_SHOW);
      ShowWindow(hEERest, SW_SHOW);

      SetWindowTextW(
          hEENotice,
          L"Notice:\n"
          L"WAI will use a repository named 'WS-DownloadManagement' for all "
          L"downloads in the near future(expected to be v2.01.0).\n\n");
      // Text1 and Text2 are static: "whenthe's app installer " and "v2.00.5"
      SetWindowTextW(hEERest, L"\n-------------------------------------------\n"
                              L"Release Notes:\n"
                              L"Fixed border colors for AppBar and InfoBar");

      eeStage = 0; // Reset stage

      UpdateLayout(hWnd);
      if (hDetailsInstallBtn) {
        EnableWindow(hDetailsInstallBtn, FALSE);
        SetWindowTextW(hDetailsInstallBtn, L"Not available");
      }
      return 0;
    }
    // Check for Easter Egg interactions
    if (wmId == ID_EE_TEXT1 && notify == STN_CLICKED) {
      // Change color
      eeColor1 = RGB(rand() % 256, rand() % 256, rand() % 256);
      InvalidateRect(hEEText1, NULL, TRUE);
      UpdateWindow(hEEText1);

      // Sequence Logic: T1 -> T2 -> T1 -> T2
      if (eeStage == 0)
        eeStage = 1;
      else if (eeStage == 2)
        eeStage = 3;
      else
        eeStage = 1; // Restart loop if clicked out of order (or just clicked T1
                     // again)
      return 0;
    }
    if (wmId == ID_EE_TEXT2 && notify == STN_CLICKED) {
      // Change color
      eeColor2 = RGB(rand() % 256, rand() % 256, rand() % 256);
      InvalidateRect(hEEText2, NULL, TRUE);
      UpdateWindow(hEEText2);

      if (eeStage == 1)
        eeStage = 2;
      else if (eeStage == 3) {
        eeStage = 4;
        StartSpaceShooter(hInst);
        eeStage = 0;
      } else {
        eeStage = 0;
      }
      return 0;
    }
    // Search toggle
    // (Top-level search toggle removed)
    // Search text change from AppBar search filters cards and list
    if (notify == EN_CHANGE && wmId == ID_SIDEBAR_SEARCH_EDIT) {
      wchar_t query[256] = {0};
      GetWindowTextW(hSidebarSearchEdit, query, 256);
      std::wstring q = query;
      for (auto &ch : q)
        ch = (wchar_t)towlower(ch);
      const int numCards = 6;
      // Filter cards
      for (int i = 0; i < numCards; ++i) {
        HWND hCard = GetDlgItem(hWnd, 2001 + i);
        if (!hCard)
          continue;
        CardData *data = (CardData *)GetWindowLongPtr(hCard, GWLP_USERDATA);
        if (!data)
          continue;
        std::wstring title = data->title;
        std::wstring desc = data->description;
        for (auto &ch : title)
          ch = (wchar_t)towlower(ch);
        for (auto &ch : desc)
          ch = (wchar_t)towlower(ch);
        bool match = q.empty() || title.find(q) != std::wstring::npos ||
                     desc.find(q) != std::wstring::npos;
        ShowWindow(hCard, match ? SW_SHOW : SW_HIDE);
      }
      // Filter sidebar list by rebuilding (include About)
      SendMessageW(hSidebarList, LB_RESETCONTENT, 0, 0);
      const wchar_t *titlesLocal[7] = {L"CenterOS After Install Setup",
                                       L"visualOS HoverNet - PY Variant",
                                       L"Bugfinders",
                                       L"visualOS HoverNet - IE Variant",
                                       L"visualOS 1.2 OBT1",
                                       L"visualOS HoverNet",
                                       L"About WAI(No value)"};
      for (int i = 0; i < 7; ++i) {
        std::wstring t = titlesLocal[i];
        for (auto &ch : t)
          ch = (wchar_t)towlower(ch);
        if (q.empty() || t.find(q) != std::wstring::npos) {
          SendMessageW(hSidebarList, LB_ADDSTRING, 0, (LPARAM)titlesLocal[i]);
        }
      }
      RefreshUI(hWnd);
      return 0;
    }
    // Sidebar list selection updates details pane
    if (wmId == ID_SIDEBAR_LIST && notify == LBN_SELCHANGE) {
      int sel = (int)SendMessageW(hSidebarList, LB_GETCURSEL, 0, 0);
      if (sel != LB_ERR) {
        wchar_t buffer[128] = {0};
        SendMessageW(hSidebarList, LB_GETTEXT, sel, (LPARAM)buffer);
        // Find matching card by title
        for (int i = 0; i < 6; ++i) {
          HWND hCard = GetDlgItem(hWnd, 2001 + i);
          if (!hCard)
            continue;
          CardData *d = (CardData *)GetWindowLongPtr(hCard, GWLP_USERDATA);
          if (d && wcscmp(d->title, buffer) == 0) {
            g_selected = *d;
            int cardIndex = i;
            if (cardIndex >= 0 && cardIndex < 6 && hCardIcons[cardIndex]) {
              g_selectedIcon = hCardIcons[cardIndex];
              SendMessage(hDetailsIcon, STM_SETICON, (WPARAM)g_selectedIcon, 0);
            }
            SetWindowTextW(hDetailsTitle, g_selected.title);
            SetWindowTextW(hDetailsDesc, g_selected.description);

            // Ensure standard description is shown and EE hidden
            ShowWindow(hDetailsDesc, SW_SHOW);
            eeStage = 0;
            ShowWindow(hEENotice, SW_HIDE);
            ShowWindow(hEEText1, SW_HIDE);
            ShowWindow(hEEText2, SW_HIDE);
            ShowWindow(hEERest, SW_HIDE);

            EnableWindow(hDetailsInstallBtn, wcslen(g_selected.url) > 0);
            UpdateLayout(hWnd);
            break;
          }
        }
      }
      return 0;
    }
    // Details button click
    if (wmId == ID_DETAILS_BTN && notify == BN_CLICKED) {
      if (wcslen(g_selected.url) > 0) {
        // If URL is a downloadable asset, perform download; otherwise open in
        // default browser
        if (IsDownloadableUrl(g_selected.url)) {
          std::wstring downloadsPath = GetDownloadsPath();
          std::wstring fileName = GetFileNameFromUrl(g_selected.url);
          std::wstring fullPath = downloadsPath + L"\\" + fileName;
          DownloadAndInstall(hWnd, g_selected.url, fullPath.c_str());
        } else {
          ShellExecuteW(nullptr, L"open", g_selected.url, nullptr, nullptr,
                        SW_SHOWNORMAL);
        }
      } else {
        SetWindowTextW(hStatusLabel, L"No link.");
        ShowWindow(hStatusLabel, SW_SHOW);
      }
      return 0;
    }
    // Parse the menu selections (menu removed)
    switch (wmId) {
    case IDM_EXIT:
      DestroyWindow(hWnd);
      break;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  } break;
  case WM_MOUSEWHEEL: {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    int step = 60; // pixels per wheel notch
    g_scrollOffsetY -= (delta / WHEEL_DELTA) * step;
    // clamp
    {
      RECT rc;
      GetClientRect(hWnd, &rc);
      int viewportHeight = rc.bottom - rc.top;
      int maxOffset = max(0, g_contentHeight - viewportHeight);
      if (g_scrollOffsetY < 0)
        g_scrollOffsetY = 0;
      if (g_scrollOffsetY > maxOffset)
        g_scrollOffsetY = maxOffset;
    }
    UpdateLayout(hWnd);
    return 0;
  }
  case WM_VSCROLL: {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hWnd, SB_VERT, &si);
    int pos = si.nPos;
    switch (LOWORD(wParam)) {
    case SB_LINEUP:
      pos -= 30;
      break;
    case SB_LINEDOWN:
      pos += 30;
      break;
    case SB_PAGEUP:
      pos -= (int)si.nPage;
      break;
    case SB_PAGEDOWN:
      pos += (int)si.nPage;
      break;
    case SB_THUMBTRACK:
      pos = si.nTrackPos;
      break;
    default:
      break;
    }
    // clamp and apply
    {
      RECT rc;
      GetClientRect(hWnd, &rc);
      int viewportHeight = rc.bottom - rc.top;
      int maxOffset = max(0, g_contentHeight - viewportHeight);
      if (pos < 0)
        pos = 0;
      if (pos > maxOffset)
        pos = maxOffset;
    }
    g_scrollOffsetY = pos;
    UpdateLayout(hWnd);
    return 0;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, hMainBgBrush);

    EndPaint(hWnd, &ps);
  } break;
  case WM_ERASEBKGND:
    return 1;
  case WM_CTLCOLORSTATIC: {
    HWND hStatic = (HWND)lParam;
    int ctrlId = GetDlgCtrlID(hStatic);
    if (ctrlId == 3001) {
      SetBkColor((HDC)wParam, RGB(30, 30, 60));
      SetTextColor((HDC)wParam, RGB(220, 220, 220));
      static HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 60));
      return (INT_PTR)hBrush;
    }
    if (ctrlId == ID_SIDEBAR) {
      SetBkColor((HDC)wParam, RGB(30, 30, 60));
      SetTextColor((HDC)wParam, RGB(220, 220, 220));
      static HBRUSH hBrushSidebar = CreateSolidBrush(RGB(30, 30, 60));
      return (INT_PTR)hBrushSidebar;
    }
    // Main title on main background: make background transparent so it blends
    if (ctrlId == 5001) {
      SetBkMode((HDC)wParam, TRANSPARENT);
      SetTextColor((HDC)wParam, RGB(220, 220, 220));
      return (INT_PTR)GetStockObject(HOLLOW_BRUSH);
    }
    // Details area (icon + labels) should match InfoBar background color
    if (ctrlId == ID_DETAILS_TITLE || ctrlId == ID_DETAILS_DESC ||
        ctrlId == ID_DETAILS_ICON) {
      SetBkColor((HDC)wParam, RGB(30, 30, 60));
      SetTextColor((HDC)wParam, RGB(220, 220, 220));
      static HBRUSH hRightBrush = CreateSolidBrush(RGB(30, 30, 60));
      return (INT_PTR)hRightBrush;
    }
    // Easter Egg text colors
    if (ctrlId == ID_EE_TEXT1) {
      SetBkColor((HDC)wParam, RGB(30, 30, 60));
      SetTextColor((HDC)wParam, eeColor1);
      static HBRUSH hBrushEE = CreateSolidBrush(RGB(30, 30, 60));
      return (INT_PTR)hBrushEE;
    }
    if (ctrlId == ID_EE_TEXT2) {
      SetBkColor((HDC)wParam, RGB(30, 30, 60));
      SetTextColor((HDC)wParam, eeColor2);
      static HBRUSH hBrushEE = CreateSolidBrush(RGB(30, 30, 60));
      return (INT_PTR)hBrushEE;
    }
    if (ctrlId == ID_EE_NOTICE || ctrlId == ID_EE_REST) {
      SetBkColor((HDC)wParam, RGB(30, 30, 60));
      SetTextColor((HDC)wParam, RGB(220, 220, 220));
      static HBRUSH hBrushEE = CreateSolidBrush(RGB(30, 30, 60));
      return (INT_PTR)hBrushEE;
    }
    SetBkColor((HDC)wParam, RGB(30, 30, 60));
    SetTextColor((HDC)wParam, RGB(220, 220, 220));
    return (INT_PTR)hMainBgBrush;
  }
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, RGB(30, 30, 60));
    SetTextColor(hdc, RGB(220, 220, 220));
    static HBRUSH hListBrush = CreateSolidBrush(RGB(30, 30, 60));
    return (INT_PTR)hListBrush;
  }
  case WM_CTLCOLOREDIT: {
    HDC hdcEdit = (HDC)wParam;
    SetBkColor(hdcEdit, RGB(30, 30, 60));
    SetTextColor(hdcEdit, RGB(220, 220, 220));
    static HBRUSH hEditBrush = CreateSolidBrush(RGB(30, 30, 60));
    return (INT_PTR)hEditBrush;
  }
  case WM_SYSCOLORCHANGE:
    DrawMenuBar(hWnd);
    break;
  case WM_DESTROY:
    for (int i = 0; i < 6; ++i) {
      if (hCardIcons[i])
        DestroyIcon(hCardIcons[i]);
    }
    if (g_selectedIcon)
      DestroyIcon(g_selectedIcon);
    PostQuitMessage(0);
    break;
  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
    if (pDIS->CtlID == 3001) // Right panel
    {
      // Create rounded rectangle background
      HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 60));
      SelectObject(pDIS->hDC, hBrush);
      RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top,
                pDIS->rcItem.right, pDIS->rcItem.bottom, 20, 20);
      DeleteObject(hBrush);

      // Draw border
      HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(40, 40, 70));
      HPEN hOldPen = (HPEN)SelectObject(pDIS->hDC, hBorderPen);
      HBRUSH hOldBrush =
          (HBRUSH)SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH));
      RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top,
                pDIS->rcItem.right, pDIS->rcItem.bottom, 20, 20);
      SelectObject(pDIS->hDC, hOldPen);
      SelectObject(pDIS->hDC, hOldBrush);
      DeleteObject(hBorderPen);

      return TRUE;
    }
    return FALSE;
  }
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  UNREFERENCED_PARAMETER(lParam);
  switch (message) {
  case WM_INITDIALOG:
    return (INT_PTR)TRUE;

  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
      EndDialog(hDlg, LOWORD(wParam));
      return (INT_PTR)TRUE;
    }
    break;
  }
  return (INT_PTR)FALSE;
}

void DownloadAndInstall(HWND hWnd, LPCWSTR url, LPCWSTR localPath) {
  SetWindowTextW(hStatusLabel, L"Downloading...  (App may be unresponsive, but "
                               L"the download progress will still continue.)");
  ShowWindow(hStatusLabel, SW_SHOW);
  if (hProgressBar) {
    ShowWindow(hProgressBar, SW_SHOW);
    SendMessage(hProgressBar, PBM_SETMARQUEE, TRUE, 0);
  }
  UpdateWindow(hStatusLabel);

  HRESULT hr = URLDownloadToFileW(nullptr, url, localPath, 0, nullptr);
  if (SUCCEEDED(hr)) {
    SetWindowTextW(hStatusLabel, L"Downloaded successfully.");
    if (hProgressBar) {
      SendMessage(hProgressBar, PBM_SETMARQUEE, FALSE, 0);
      ShowWindow(hProgressBar, SW_HIDE);
    }
    ShellExecuteW(nullptr, L"open", localPath, nullptr, nullptr, SW_SHOWNORMAL);
  } else {
    SetWindowTextW(hStatusLabel, L"Failed to download - Try again later");
    if (hProgressBar) {
      SendMessage(hProgressBar, PBM_SETMARQUEE, FALSE, 0);
      ShowWindow(hProgressBar, SW_HIDE);
    }
  }
}

void HideDragInstallUI(HWND hWnd) {
  if (hStatusLabel) {
    ShowWindow(hStatusLabel, SW_HIDE);
    SetWindowTextW(hStatusLabel, L"");
  }
  if (hProgressBar) {
    SendMessage(hProgressBar, PBM_SETMARQUEE, FALSE, 0);
    ShowWindow(hProgressBar, SW_HIDE);
  }
}

// Timer callback for long-press detection
void CALLBACK LongPressTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                                 DWORD dwTime) {
  if (idEvent == ID_LONGPRESS_TIMER && g_isLongPressing &&
      g_longPressIndex >= 0) {
    // Long press detected - trigger download
    g_isLongPressing = false;
    KillTimer(hWnd, ID_LONGPRESS_TIMER);

    // Get the selected item text
    wchar_t buffer[128] = {0};
    SendMessageW(hSidebarList, LB_GETTEXT, g_longPressIndex, (LPARAM)buffer);

    // Find matching card data and trigger download
    for (int i = 0; i < 6; ++i) {
      HWND hCard = GetDlgItem(hWnd, 2001 + i);
      if (!hCard)
        continue;
      CardData *d = (CardData *)GetWindowLongPtr(hCard, GWLP_USERDATA);
      if (d && wcscmp(d->title, buffer) == 0) {
        if (wcslen(d->url) > 0) {
          std::wstring downloadsPath = GetDownloadsPath();
          std::wstring fileName = GetFileNameFromUrl(d->url);
          std::wstring fullPath = downloadsPath + L"\\" + fileName;
          std::wstring msg = L"Do you want to download the following app: \"" +
                             std::wstring(d->title) + L"\"?";
          int result = MessageBoxW(hWnd, msg.c_str(), L"Long-press Shortcut",
                                   MB_ICONINFORMATION | MB_YESNO);
          if (result == IDYES) {
            DownloadAndInstall(hWnd, d->url, fullPath.c_str());
          }
        } else {
          SetWindowTextW(hStatusLabel, L"No download link available.");
          ShowWindow(hStatusLabel, SW_SHOW);
        }
        break;
      }
    }
  }
}

// Sidebar list subclass proc to handle long-press detection
LRESULT CALLBACK SidebarListProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
  switch (msg) {
  case WM_LBUTTONDOWN: {
    // Start long-press timer (500ms delay)
    g_isLongPressing = true;
    g_longPressIndex = (int)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
    SetTimer(GetParent(GetParent(hwnd)), ID_LONGPRESS_TIMER, 500,
             LongPressTimerProc);
  } break;
  case WM_LBUTTONUP: {
    // Cancel long-press if mouse is released early
    if (g_isLongPressing) {
      g_isLongPressing = false;
      KillTimer(GetParent(GetParent(hwnd)), ID_LONGPRESS_TIMER);
    }
  } break;
  case WM_MOUSEMOVE: {
    // Cancel long-press if mouse moves too far
    if (g_isLongPressing) {
      g_isLongPressing = false;
      KillTimer(GetParent(GetParent(hwnd)), ID_LONGPRESS_TIMER);
    }
  } break;
  default:
    return CallWindowProc(OldSidebarListProc, hwnd, msg, wParam, lParam);
  }
  return CallWindowProc(OldSidebarListProc, hwnd, msg, wParam, lParam);
}

// Replace the existing CardProc function with this version:
LRESULT CALLBACK CardProc(HWND hWnd, UINT message, WPARAM wParam,
                          LPARAM lParam) {
  CardData *data = (CardData *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
  static bool btnPressed = false;

  switch (message) {
  case WM_ERASEBKGND:
    return 1;

  case WM_CREATE:
    // No longer creating a physical button
    break;

  case WM_LBUTTONDOWN: {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT btnRect = {CARD_WIDTH - 170, CARD_HEIGHT - 40, CARD_WIDTH - 10,
                    CARD_HEIGHT - 10};

    if (PtInRect(&btnRect, pt)) {
      // Clicked on button area
      btnPressed = true;
      InvalidateRect(hWnd, NULL, FALSE);
    } else {
      // Card selection (InfoBar update)
      if (data) {
        g_selected = *data;
        int cardIndex = GetDlgCtrlID(hWnd) - 2001;
        if (cardIndex >= 0 && cardIndex < 6 && hCardIcons[cardIndex]) {
          g_selectedIcon = hCardIcons[cardIndex];
          SendMessage(hDetailsIcon, STM_SETICON, (WPARAM)g_selectedIcon, 0);
        }
        SetWindowTextW(hDetailsTitle, g_selected.title);
        SetWindowTextW(hDetailsDesc, g_selected.description);

        // Ensure standard description is shown and EE hidden
        ShowWindow(hDetailsDesc, SW_SHOW);
        ShowWindow(hEENotice, SW_HIDE);
        ShowWindow(hEEText1, SW_HIDE);
        ShowWindow(hEEText2, SW_HIDE);
        ShowWindow(hEERest, SW_HIDE);
        eeStage = 0;

        EnableWindow(hDetailsInstallBtn, wcslen(g_selected.url) > 0);
      }
      SetFocus(hWnd);
    }
    return 0;
  }

  case WM_LBUTTONUP: {
    if (btnPressed) {
      btnPressed = false;
      InvalidateRect(hWnd, NULL, FALSE);

      // Handle button click
      if (data && wcslen(data->url) > 0) {
        if (IsDownloadableUrl(data->url)) {
          std::wstring downloadsPath = GetDownloadsPath();
          std::wstring fileName = GetFileNameFromUrl(data->url);
          std::wstring fullPath = downloadsPath + L"\\" + fileName;
          std::wstring msg = L"Do you want to download and install " +
                             std::wstring(data->title);
          int result =
              MessageBoxW(GetParent(hWnd), msg.c_str(), L"Confirm Download",
                          MB_ICONINFORMATION | MB_YESNO);
          if (result == IDYES) {
            DownloadAndInstall(GetParent(hWnd), data->url, fullPath.c_str());
          }
        } else {
          ShellExecuteW(nullptr, L"open", data->url, nullptr, nullptr,
                        SW_SHOWNORMAL);
        }
      }
    }
    return 0;
  }

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    // Fill card background with rounded corners
    RECT cardRect;
    GetClientRect(hWnd, &cardRect);
    HBRUSH hCardBgBrush = CreateSolidBrush(RGB(30, 30, 60));
    SelectObject(hdc, hCardBgBrush);
    // Create rounded rectangle for card background
    RoundRect(hdc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom,
              20, 20);
    DeleteObject(hCardBgBrush);

    // Draw card border with rounded corners
    HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(40, 40, 70));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom,
              20, 20);

    // Draw icon
    int cardIndex = GetDlgCtrlID(hWnd) - 2001;
    if (cardIndex >= 0 && cardIndex < 6 && hCardIcons[cardIndex]) {
      DrawIconEx(hdc, 20, 20, hCardIcons[cardIndex], 32, 32, 0, nullptr,
                 DI_NORMAL);
    }

    // Draw title
    SetBkMode(hdc, TRANSPARENT);
    HFONT hTitleFont =
        CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hTitleFont);
    SetTextColor(hdc, RGB(220, 220, 220));
    if (data) {
      TextOutW(hdc, 70, 20, data->title, lstrlenW(data->title));
    }
    SelectObject(hdc, oldFont);
    DeleteObject(hTitleFont);

    // Draw custom button
    RECT btnRect = {CARD_WIDTH - 170, CARD_HEIGHT - 40, CARD_WIDTH - 10,
                    CARD_HEIGHT - 10};

    // Button border with transparent background
    HPEN hBtnPen =
        CreatePen(PS_SOLID, 2, RGB(100, 180, 255)); // Bright blue border
    SelectObject(hdc, hBtnPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, btnRect.left, btnRect.top, btnRect.right, btnRect.bottom, 6,
              6);

    // Button text
    HFONT hBtnFont =
        CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    oldFont = (HFONT)SelectObject(hdc, hBtnFont);
    SetTextColor(hdc, RGB(100, 180, 255));

    const wchar_t *btnText = L"Download and Install";
    if (data) {
      if (wcslen(data->url) == 0) {
        btnText = L"Not available";
        SetTextColor(hdc, RGB(128, 128, 128));
      } else if (!IsDownloadableUrl(data->url)) {
        btnText = L"Open Website";
      }
    }

    DrawTextW(hdc, btnText, -1, &btnRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Cleanup
    SelectObject(hdc, oldFont);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hBtnFont);
    DeleteObject(hBtnPen);
    DeleteObject(hBorderPen);

    EndPaint(hWnd, &ps);
    return 0;
  }

  case WM_DESTROY:
    if (data)
      delete data;
    break;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// BUGSHOT! Easter Egg(Click the whenthe's app installer(T1) text and the
// version number(T2) in the About section in this order: T1 > T2 > T1 > T2)

#define GAME_WIDTH 600
#define GAME_HEIGHT 600
#define ID_GAME_TIMER 9999

struct GameEntity {
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f; // velocity for inertia
  float w = 0.0f;
  float h = 0.0f;
  bool active = false;
  int shootTimer = 0; // Timer for enemy shooting
};

struct GameState {
  GameEntity player{};
  std::vector<GameEntity> bullets;
  std::vector<GameEntity> enemyBullets; // Enemy bullets!
  std::vector<GameEntity> enemies;
  int ammoCount = 25;
  int killStreak = 0;
  bool gameOver = false;
  float playerAccel = 0.0f; // acceleration for smooth movement
  float playerMaxSpeed = 0.0f;
  float playerFriction = 0.0f;
  int currentLevel = 1;
};

static GameState *g_gameState = nullptr;

LRESULT CALLBACK SpaceShooterProc(HWND hWnd, UINT message, WPARAM wParam,
                                  LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
    SetTimer(hWnd, ID_GAME_TIMER, 16, NULL); // ~60 FPS
    return 0;

  case WM_TIMER:
    if (g_gameState && !g_gameState->gameOver) {
      // Space Invaders-style discrete movement!
      static int moveDelay = 0;
      static bool leftWasPressed = false;
      static bool rightWasPressed = false;

      const int MOVE_STEP = 8; // How many pixels to jump
      const int MOVE_COOLDOWN =
          4; // Frames between moves (creates that chunky feel)

      bool leftPressed = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
      bool rightPressed = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

      // Detect key press (not hold) for discrete movement
      bool leftJustPressed = leftPressed && !leftWasPressed;
      bool rightJustPressed = rightPressed && !rightWasPressed;

      if (moveDelay > 0) {
        moveDelay--;
      }

      // Discrete movement with cooldown
      if (moveDelay == 0) {
        if (leftPressed) {
          g_gameState->player.x -= MOVE_STEP;
          moveDelay = MOVE_COOLDOWN;
        } else if (rightPressed) {
          g_gameState->player.x += MOVE_STEP;
          moveDelay = MOVE_COOLDOWN;
        }
      }

      leftWasPressed = leftPressed;
      rightWasPressed = rightPressed;

      // Clamp Player position (no bounce, just hard stop like Space Invaders)
      if (g_gameState->player.x < 0) {
        g_gameState->player.x = 0;
      }
      if (g_gameState->player.x > GAME_WIDTH - g_gameState->player.w) {
        g_gameState->player.x = GAME_WIDTH - g_gameState->player.w;
      }

      // Shooting with slower fire rate (more 90s arcade feel)
      static int shootCooldown = 0;
      if (shootCooldown > 0)
        shootCooldown--;
      if ((GetAsyncKeyState(VK_SPACE) & 0x8000) && shootCooldown <= 0 &&
          g_gameState->ammoCount > 0) {
        GameEntity b;
        b.x = g_gameState->player.x + g_gameState->player.w / 2 - 2;
        b.y = g_gameState->player.y;
        b.vx = 0;
        b.vy = -10.0f; // bullet speed
        b.w = 4;
        b.h = 12;
        b.active = true;
        g_gameState->bullets.push_back(b);
        g_gameState->ammoCount--; // Consume bullet
        shootCooldown = 20;       // slower fire rate
      }

      // Update player Bullets
      for (auto &b : g_gameState->bullets) {
        if (b.active) {
          b.y += b.vy;
        }
        if (b.y < -20) {
          b.active = false;
          g_gameState->killStreak = 0; // Reset streak on miss
        }
      }

      // Update Enemy Bullets
      for (auto &b : g_gameState->enemyBullets) {
        if (b.active) {
          b.y += b.vy;
        }
        if (b.y > GAME_HEIGHT + 20)
          b.active = false;

        // Check collision with player
        if (b.active &&
            b.x < g_gameState->player.x + g_gameState->player.w - 3 &&
            b.x + b.w > g_gameState->player.x + 3 &&
            b.y < g_gameState->player.y + g_gameState->player.h - 3 &&
            b.y + b.h > g_gameState->player.y + 3) {
          g_gameState->gameOver = true;
          b.active = false;
        }
      }

      // Space Invaders-style enemy formation spawning
      static int enemySpawnTimer = 0;
      static int enemiesSpawned = 0;
      static bool formationComplete = false;
      static int currentLevelState = -1; // To track level changes

      // Level initialization / transition
      if (currentLevelState != g_gameState->currentLevel) {
        currentLevelState = g_gameState->currentLevel;
        enemiesSpawned = 0;
        formationComplete = false;
        enemySpawnTimer = 0;
        g_gameState->bullets.clear();
        g_gameState->enemyBullets.clear();
        // Note: g_gameState->enemies should be empty here naturally if we
        // cleared the level
      }

      // Check for level completion (all enemies spawned AND all enemies dead)
      if (formationComplete && g_gameState->enemies.empty()) {
        g_gameState->currentLevel++;
        g_gameState->ammoCount +=
            25 + (g_gameState->currentLevel * 5); // Level up bonus
        if (g_gameState->ammoCount > 256)
          g_gameState->ammoCount = 256; // Cap ammo
        // Variables will be reset in the next frame due to currentLevelState
        // check
      }

      // Spawn enemies in rows (Space Invaders formation)
      // Scaling difficulty:
      // Level 1: 4 rows (32 enemies), size 24
      // Level 2: 5 rows (40 enemies), size 22
      // Level 3: 6 rows (48 enemies), size 20
      // ...
      int rowsToSpawn = 4 + (g_gameState->currentLevel - 1);
      if (rowsToSpawn > 8)
        rowsToSpawn = 8; // Cap at 8 rows

      float enemySize = 24.0f - (g_gameState->currentLevel - 1) * 2.0f;
      if (enemySize < 12.0f)
        enemySize = 12.0f; // Cap minimum size

      int enemiesPerRow = 8;
      int totalEnemies = rowsToSpawn * enemiesPerRow;

      if (!formationComplete && enemySpawnTimer == 0) {
        int row = enemiesSpawned / enemiesPerRow;
        int col = enemiesSpawned % enemiesPerRow;

        // Center the formation based on enemy size and count
        float spacing = enemySize * 2.5f; // Spacing relative to size
        float rowSpacing = enemySize * 1.6f;
        float startX = (GAME_WIDTH - (enemiesPerRow * spacing)) / 2.0f +
                       (spacing - enemySize) / 2.0f;
        float startY = 80.0f;

        if (enemiesSpawned < totalEnemies) {
          GameEntity e;
          e.x = startX + col * spacing;
          e.y = startY + row * rowSpacing;
          e.vx = 0; // Movement controlled globally
          e.vy = 0;
          e.w = enemySize;
          e.h = enemySize;
          e.active = true;
          // Random shoot timer
          e.shootTimer = 300 + (rand() % 121);
          // Higher levels shoot slightly faster
          e.shootTimer =
              max(30, e.shootTimer - (g_gameState->currentLevel * 10));

          g_gameState->enemies.push_back(e);
          enemiesSpawned++;
          enemySpawnTimer = 2; // Small delay between spawns
        } else {
          formationComplete = true;
        }
      }

      if (enemySpawnTimer > 0)
        enemySpawnTimer--;

      // Space Invaders formation movement
      static int moveTimer = 0;
      static float formationDirection = 1.0f; // 1 = right, -1 = left
      static float formationSpeed =
          3.0f + (g_gameState->currentLevel * 0.5f); // Faster at higher levels
      static bool shouldDropDown = false;

      moveTimer++;
      // Move frequency increases with level (harder!)
      int moveInterval = max(5, 30 - (g_gameState->currentLevel * 2));

      if (moveTimer > moveInterval) {
        moveTimer = 0;

        // Check if any enemy hit the edge
        bool hitEdge = false;
        for (auto &e : g_gameState->enemies) {
          if (!e.active)
            continue;
          if ((formationDirection > 0 && e.x + e.w >= GAME_WIDTH - 20) ||
              (formationDirection < 0 && e.x <= 20)) {
            hitEdge = true;
            break;
          }
        }

        if (hitEdge) {
          shouldDropDown = true;
          formationDirection *= -1; // Reverse direction
        }

        // Move all enemies as a formation
        for (auto &e : g_gameState->enemies) {
          if (!e.active)
            continue;

          if (shouldDropDown) {
            e.y += 20; // Drop down
          } else {
            e.x += formationSpeed * formationDirection; // Move horizontally
          }
        }

        shouldDropDown = false;
      }

      // Update Enemies & Collision
      for (auto &e : g_gameState->enemies) {
        if (!e.active)
          continue;

        // Enemy shooting logic
        e.shootTimer--;
        if (e.shootTimer <= 0) {
          // Check if this enemy is in the front row (can shoot)
          bool canShoot = true;
          for (auto &other : g_gameState->enemies) {
            if (!other.active)
              continue;
            // If there's another enemy directly below, this one can't shoot
            if (fabs(other.x - e.x) < (enemySize * 0.8f) && other.y > e.y) {
              canShoot = false;
              break;
            }
          }

          if (canShoot) {
            // Shoot!
            GameEntity bullet;
            bullet.x = e.x + e.w / 2 - 2;
            bullet.y = e.y + e.h;
            bullet.vx = 0;
            bullet.vy =
                4.0f + (g_gameState->currentLevel * 0.5f); // Faster bullets
            bullet.w = 4;
            bullet.h = 10;
            bullet.active = true;
            g_gameState->enemyBullets.push_back(bullet);
          }

          // Reset timer for next shot
          int baseTime = max(60, 300 - (g_gameState->currentLevel * 15));
          e.shootTimer = baseTime + (rand() % 120);
        }

        // Check if enemies reached the player line (game over!)
        if (e.y + e.h >= g_gameState->player.y - 10) {
          g_gameState->gameOver = true;
        }

        if (e.y > GAME_HEIGHT + 20)
          e.active = false;

        // Collision with player
        if (e.x < g_gameState->player.x + g_gameState->player.w - 5 &&
            e.x + e.w > g_gameState->player.x + 5 &&
            e.y < g_gameState->player.y + g_gameState->player.h - 5 &&
            e.y + e.h > g_gameState->player.y + 5) {
          g_gameState->gameOver = true;
        }

        // Collision with bullets
        for (auto &b : g_gameState->bullets) {
          if (b.active && e.active && b.x < e.x + e.w - 3 &&
              b.x + b.w > e.x + 3 && b.y < e.y + e.h - 3 &&
              b.y + b.h > e.y + 3) {
            e.active = false;
            b.active = false;

            // Streak and ammo logic
            g_gameState->killStreak++;
            int bonus = (g_gameState->killStreak >= 2)
                            ? (g_gameState->killStreak - 1)
                            : 0;
            g_gameState->ammoCount += 5 + bonus;
            if (g_gameState->ammoCount > 256)
              g_gameState->ammoCount = 256; // Cap ammo
          }
        }
      }

      // Clean up
      g_gameState->bullets.erase(
          std::remove_if(g_gameState->bullets.begin(),
                         g_gameState->bullets.end(),
                         [](const GameEntity &e) { return !e.active; }),
          g_gameState->bullets.end());
      g_gameState->enemyBullets.erase(
          std::remove_if(g_gameState->enemyBullets.begin(),
                         g_gameState->enemyBullets.end(),
                         [](const GameEntity &e) { return !e.active; }),
          g_gameState->enemyBullets.end());
      g_gameState->enemies.erase(
          std::remove_if(g_gameState->enemies.begin(),
                         g_gameState->enemies.end(),
                         [](const GameEntity &e) { return !e.active; }),
          g_gameState->enemies.end());

      InvalidateRect(hWnd, NULL, FALSE);
    }
    return 0;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    // Create a memory DC for double buffering (reduces flicker - 90s
    // technique!)
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap =
        CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Main game area background (dark space)
    HBRUSH bgBrush = CreateSolidBrush(RGB(10, 10, 30));
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    // Draw styled border (matching app's sidebar style)
    const int borderWidth = 12;

    // Outer rounded border with app's color scheme
    HBRUSH borderBrush = CreateSolidBrush(RGB(30, 30, 60));
    SelectObject(memDC, borderBrush);
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(40, 40, 70));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);

    // Draw rounded rectangle border
    RoundRect(memDC, 0, 0, clientRect.right, clientRect.bottom, 20, 20);
    RoundRect(memDC, borderWidth - 2, borderWidth - 2,
              clientRect.right - borderWidth + 2,
              clientRect.bottom - borderWidth + 2, 15, 15);

    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);
    DeleteObject(borderBrush);

    // Inner game area
    RECT gameArea = {borderWidth, borderWidth, clientRect.right - borderWidth,
                     clientRect.bottom - borderWidth};

    if (g_gameState) {
      // Player (Green triangle spaceship - 90s style!)
      HBRUSH pBrush = CreateSolidBrush(RGB(50, 255, 50));
      SelectObject(memDC, pBrush);
      HPEN pPen = CreatePen(PS_SOLID, 2, RGB(100, 255, 100));
      SelectObject(memDC, pPen);

      POINT triangle[3];
      triangle[0] = {(LONG)(g_gameState->player.x + g_gameState->player.w / 2),
                     (LONG)g_gameState->player.y};
      triangle[1] = {(LONG)g_gameState->player.x,
                     (LONG)(g_gameState->player.y + g_gameState->player.h)};
      triangle[2] = {(LONG)(g_gameState->player.x + g_gameState->player.w),
                     (LONG)(g_gameState->player.y + g_gameState->player.h)};
      Polygon(memDC, triangle, 3);

      DeleteObject(pBrush);
      DeleteObject(pPen);

      // Bullets (Bright cyan/yellow - classic arcade)
      HBRUSH bBrush = CreateSolidBrush(RGB(255, 255, 100));
      SelectObject(memDC, bBrush);
      for (const auto &b : g_gameState->bullets) {
        RECT bRect = {(LONG)b.x, (LONG)b.y, (LONG)(b.x + b.w),
                      (LONG)(b.y + b.h)};
        RoundRect(memDC, bRect.left, bRect.top, bRect.right, bRect.bottom, 4,
                  4);
      }
      DeleteObject(bBrush);

      // Enemy Bullets (Red/Orange - dangerous!)
      HBRUSH ebBrush = CreateSolidBrush(RGB(255, 100, 50));
      SelectObject(memDC, ebBrush);
      for (const auto &b : g_gameState->enemyBullets) {
        RECT bRect = {(LONG)b.x, (LONG)b.y, (LONG)(b.x + b.w),
                      (LONG)(b.y + b.h)};
        RoundRect(memDC, bRect.left, bRect.top, bRect.right, bRect.bottom, 4,
                  4);
      }
      DeleteObject(ebBrush);

      // Enemies (Red with pixel-art style outline)
      for (const auto &e : g_gameState->enemies) {
        // Inner fill
        HBRUSH eBrush = CreateSolidBrush(RGB(255, 50, 50));
        SelectObject(memDC, eBrush);
        HPEN ePen = CreatePen(PS_SOLID, 2, RGB(255, 100, 100));
        SelectObject(memDC, ePen);

        RECT eRect = {(LONG)e.x, (LONG)e.y, (LONG)(e.x + e.w),
                      (LONG)(e.y + e.h)};
        RoundRect(memDC, eRect.left, eRect.top, eRect.right, eRect.bottom, 6,
                  6);

        DeleteObject(eBrush);
        DeleteObject(ePen);
      }

      // Score & Level display (retro font style)
      SetBkMode(memDC, TRANSPARENT);
      HFONT scoreFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                                    DEFAULT_PITCH | FF_MODERN, L"Courier New");
      HFONT oldFont = (HFONT)SelectObject(memDC, scoreFont);

      // Score shadow (90s effect)
      SetTextColor(memDC, RGB(0, 0, 0));
      std::wstring scoreText =
          L"BULLETS: " + std::to_wstring(g_gameState->ammoCount) +
          L"   LEVEL: " + std::to_wstring(g_gameState->currentLevel);
      TextOutW(memDC, 22, 22, scoreText.c_str(), scoreText.length());

      // Score text
      SetTextColor(memDC, RGB(100, 255, 255));
      TextOutW(memDC, 20, 20, scoreText.c_str(), scoreText.length());

      if (g_gameState->gameOver) {
        // Game Over with retro styling
        HFONT bigFont = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                                    DEFAULT_PITCH | FF_MODERN, L"Courier New");
        SelectObject(memDC, bigFont);

        std::wstring overText = L"GAME OVER";
        // Shadow
        SetTextColor(memDC, RGB(0, 0, 0));
        TextOutW(memDC, GAME_WIDTH / 2 - 132, GAME_HEIGHT / 2 - 22,
                 overText.c_str(), overText.length());
        // Main text
        SetTextColor(memDC, RGB(255, 100, 100));
        TextOutW(memDC, GAME_WIDTH / 2 - 130, GAME_HEIGHT / 2 - 20,
                 overText.c_str(), overText.length());

        DeleteObject(bigFont);
      }

      SelectObject(memDC, oldFont);
      DeleteObject(scoreFont);
    }

    // Blit the memory DC to screen
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0,
           SRCCOPY);

    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hWnd, &ps);
    return 0;
  }

  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE && g_gameState && g_gameState->gameOver) {
      DestroyWindow(hWnd);
    }
    return 0;

  case WM_DESTROY:
    KillTimer(hWnd, ID_GAME_TIMER);
    if (g_gameState) {
      delete g_gameState;
      g_gameState = nullptr;
    }
    return 0;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

void StartSpaceShooter(HINSTANCE hInstance) {
  if (g_gameState)
    return; // already running

  WNDCLASSW wc = {0};
  wc.lpfnWndProc = SpaceShooterProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = L"SpaceShooterWnd";
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);

  g_gameState = new GameState();
  g_gameState->player.x = GAME_WIDTH / 2 - 15;
  g_gameState->player.y = GAME_HEIGHT - 70;
  g_gameState->player.w = 30;
  g_gameState->player.h = 30;
  g_gameState->player.vx = 0;
  g_gameState->player.vy = 0;
  g_gameState->player.active = true;
  g_gameState->currentLevel = 1; // Initialize level

  // 90s-style movement parameters
  g_gameState->playerAccel = 0.6f;     // acceleration per frame
  g_gameState->playerMaxSpeed = 6.0f;  // max velocity
  g_gameState->playerFriction = 0.85f; // friction coefficient (momentum)

  g_gameState->ammoCount = 30; // Level 1 start
  g_gameState->killStreak = 0;
  g_gameState->gameOver = false;

  HWND hGame = CreateWindowW(
      L"SpaceShooterWnd", L"whenthe's app installer - BUGSHOT!",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, GAME_WIDTH + 16, GAME_HEIGHT + 39, nullptr,
      nullptr, hInstance, nullptr);
}