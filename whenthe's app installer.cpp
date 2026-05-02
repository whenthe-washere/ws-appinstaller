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
#include <thread>
#include <urlmon.h>
#include <vector>
#include <windows.h>
#include <windowsx.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "msimg32.lib")
#include <combaseapi.h>
#include <shlguid.h>
#include <shobjidl.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

#define MAX_LOADSTRING 1000
#define CARD_HEIGHT 205
#define CARD_WIDTH 200
#define CARD_MARGIN 20
#define CARD_BUTTON_ID 4001
#define ID_APP_LOGO 6001
#define ID_INSTALL_LOGO 6002
#define DRAG_TIMER_ID 6003
#define ID_DIRECT_INSTALL_BUTTON 6004
#define ID_LONGPRESS_TIMER 6005
// Add IDs for details pane controls
#define ID_ABOUT_BTN 9500
#define ID_ABOUT_CARD 9501
// Sidebar search edit removed
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
#define ID_QUEUE_BTN 8104
#define ID_DOWNLOAD_ALL_BTN 8105
#define ID_BACK_TO_APPS_BTN 8106

#define ID_SETTINGS_BTN 8107
#define ID_THEME_LIGHT 8108
#define ID_THEME_DARK 8109
#define ID_THEME_AMOLED 8110
#define ID_TEXT_SIZE_INC 8111
#define ID_TEXT_SIZE_DEC 8112
#define ID_SET_BROWSE 8113
#define ID_SET_PATH_EDIT 8114

enum ViewMode { VIEW_APPS, VIEW_QUEUE, VIEW_SETTINGS };
static ViewMode g_viewMode = VIEW_APPS;
static std::vector<int> g_downloadQueue; // Stores indices (0-5)
static HWND hQueueBtn = nullptr;
static HWND hDownloadAllBtn = nullptr;
static HWND hBackBtn = nullptr;
static HWND hSettingsBtn = nullptr;
static HWND hSetPathEdit = nullptr;
static HWND hSetBrowseBtn = nullptr;

enum Theme { THEME_LIGHT, THEME_DARK, THEME_AMOLED };
static Theme g_currentTheme = THEME_DARK;
static int g_textSizePercent = 100;
static std::wstring g_downloadLocation = L"";
static bool g_useSmartDownload = true;
static bool g_askEveryTime = false;

// Add this struct at the top (after includes)
struct CardData {
  wchar_t title[64];
  wchar_t description[256];
  wchar_t url[256];
  wchar_t version[32]; // Added version field
};

static bool g_isMenuOpen = false;

// Selected app info for details pane
static CardData g_selected = {L"", L"", L""};
static HICON g_selectedIcon = nullptr;
static int g_expandedIndex = -1;
static HWND hAboutBtn = nullptr;
static HWND hAboutCard = nullptr;
static bool g_showAbout = false;

struct InstallInfo {
  std::wstring filePath;
  std::wstring appName;
  std::wstring version;
};

static void CheckForAppUpdates(HWND hWnd);
static void PerformInstallation(HWND hWnd, struct InstallInfo *info);
void DownloadAndInstall(HWND hWnd, LPCWSTR url, LPCWSTR localPath,
                        LPCWSTR appName, LPCWSTR version);

static std::wstring g_latestVersions[6] = {L"", L"", L"", L"", L"", L""};

HFONT hFont = nullptr; // Declare hFont as a global variable
static HBRUSH hWhiteBrush =
    CreateSolidBrush(RGB(30, 30, 60)); // dark background
static HBRUSH hMainBgBrush = CreateSolidBrush(RGB(20, 20, 40)); // main bg

// whenthesspace.vercel.app palette colors
#define WS_LIGHT_BG      RGB(204, 204, 255)  // #CCCCFF periwinkle
#define WS_LIGHT_SURFACE RGB(250, 250, 255)  // Glass card surface
#define WS_LIGHT_TEXT    RGB(26, 26, 46)    // #1a1a2e dark navy
#define WS_PRIMARY       RGB(166, 166, 255)  // #A6A6FF primary accent
#define WS_PRIMARY_GLOW  RGB(100, 180, 255)  // Bright accent for borders
HICON hCardIcons[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// Per-card averaged icon color (sampled once after icons are loaded)
static COLORREF g_cardAvgColor[6] = {RGB(30, 30, 60), RGB(30, 30, 60),
                                     RGB(30, 30, 60), RGB(30, 30, 60),
                                     RGB(30, 30, 60), RGB(30, 30, 60)};

// Sample the average non-dark color from an HICON by rendering into a DIB
static COLORREF SampleIconAvgColor(HICON hIcon) {
  if (!hIcon)
    return RGB(30, 30, 60);
  const int SZ = 32;
  HDC hdcScreen = GetDC(nullptr);
  HDC hdcMem = CreateCompatibleDC(hdcScreen);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = SZ;
  bmi.bmiHeader.biHeight = -SZ;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  VOID *pBits = nullptr;
  HBITMAP hBmp =
      CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
  if (!hBmp) {
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return RGB(30, 30, 60);
  }
  HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
  RECT rc = {0, 0, SZ, SZ};
  FillRect(hdcMem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
  DrawIconEx(hdcMem, 0, 0, hIcon, SZ, SZ, 0, nullptr, DI_NORMAL);
  GdiFlush();
  DWORD *px = (DWORD *)pBits;
  long long rSum = 0, gSum = 0, bSum = 0, count = 0;
  for (int i = 0; i < SZ * SZ; i++) {
    BYTE b2 = (px[i] >> 0) & 0xFF;
    BYTE g2 = (px[i] >> 8) & 0xFF;
    BYTE r2 = (px[i] >> 16) & 0xFF;
    if (r2 + g2 + b2 > 60) {
      rSum += r2;
      gSum += g2;
      bSum += b2;
      count++;
    }
  }
  SelectObject(hdcMem, hOld);
  DeleteObject(hBmp);
  DeleteDC(hdcMem);
  ReleaseDC(nullptr, hdcScreen);
  if (count == 0)
    return RGB(30, 30, 60);
  return RGB((BYTE)(rSum / count), (BYTE)(gSum / count), (BYTE)(bSum / count));
}
// Card bg: 20% icon color mixed into dark base (30,30,60)
static COLORREF DarkenForCard(COLORREF c) {
  BYTE r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
  return RGB((BYTE)(30 + ((int)r - 30) * 20 / 100),
             (BYTE)(30 + ((int)g - 30) * 20 / 100),
             (BYTE)(60 + ((int)b - 60) * 20 / 100));
}
// Border: slightly lighter than card bg
static COLORREF LightenForBorder(COLORREF c) {
  return RGB((BYTE)min(255, (int)GetRValue(c) + 22),
             (BYTE)min(255, (int)GetGValue(c) + 22),
             (BYTE)min(255, (int)GetBValue(c) + 22));
}
// Text: white (220,220,220) with 12% icon tint
static COLORREF TintForText(COLORREF c) {
  BYTE r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
  return RGB((BYTE)(220 + ((int)r - 220) * 12 / 100),
             (BYTE)(220 + ((int)g - 220) * 12 / 100),
             (BYTE)(220 + ((int)b - 220) * 12 / 100));
}

// Theme-aware background color for the main window
// Using whenthesspace.vercel.app palette
static COLORREF GetMainBgColor() {
  switch (g_currentTheme) {
  case THEME_LIGHT:  return RGB(204, 204, 255);  // #CCCCFF periwinkle bg
  case THEME_AMOLED: return RGB(0, 0, 0);
  default:           return RGB(20, 20, 40);
  }
}

// Theme-aware background color for cards
// Using whenthesspace.vercel.app palette - glass surface style
static COLORREF GetCardBgColor(COLORREF avgColor) {
  switch (g_currentTheme) {
  case THEME_LIGHT:
    // White/lavender tinted surface instead of icon-based colors
    return RGB(250, 250, 255);  // Near-white with slight blue tint
  case THEME_AMOLED: return RGB(10, 10, 10);
  default:           return DarkenForCard(avgColor);
  }
}

// Theme-aware text color for cards (renamed to avoid Win32 GetTextColor clash)
// Using whenthesspace.vercel.app palette
static COLORREF GetCardTextColor(COLORREF avgColor) {
  switch (g_currentTheme) {
  case THEME_LIGHT:  return RGB(26, 26, 46);  // #1a1a2e dark navy text
  default:           return TintForText(avgColor);
  }
}

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

// Add DPI scaling support
static int GetDPI(HWND hWnd) {
  HDC hdc = GetDC(hWnd);
  int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
  ReleaseDC(hWnd, hdc);
  return dpi;
}

static int Scale(int val, int dpi) { return MulDiv(val, dpi, 96) * g_textSizePercent / 100; }

// Softcoded downloadable extensions
static std::vector<std::wstring> g_downloadableExtensions = {
    L"zip", L"exe", L"msi", L"msix", L"7z",
    L"rar", L"tar", L"gz",  L"deb",  L"rpm"};

// Content scrolling
static int g_scrollOffsetY = 0; // current scroll offset for main card area
static int g_contentHeight = 0; // total virtual content height (cards area)

// Forward declaration for controls referenced before their definitions
extern HWND hStatusLabel;

// Helper to detect whether a URL likely points to a downloadable file
static bool IsDownloadableUrl(const std::wstring &url) {
  if (url.empty())
    return false;
  size_t lastDot = url.find_last_of(L'.');
  if (lastDot == std::wstring::npos)
    return false;
  std::wstring ext = url.substr(lastDot + 1);
  for (auto &ch : ext)
    ch = (wchar_t)towlower(ch);
  for (const auto &k : g_downloadableExtensions)
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
  int dpi = GetDPI(hWnd);
  int iconX = rightX + Scale(20, dpi);
  int iconY = rightY + Scale(20, dpi);
  int titleX = rightX + Scale(60, dpi);
  int titleY = rightY + Scale(20, dpi);
  int descX = rightX + Scale(20, dpi);
  int descY = rightY + Scale(60, dpi);
  // If EE text lines are visible (about view), push desc below them
  if (hEEText1 && IsWindowVisible(hEEText1))
    descY = rightY + Scale(60 + 24 + 24 + 6, dpi); // Text1 + Text2 + gap
  int descW = rightPanelWidth - Scale(40, dpi);

  int descH = Scale(120, dpi);
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
    int maxDescH =
        max(0, rightPanelHeight - (descY - rightY) - Scale(200, dpi));
    if (maxDescH > 0)
      descH = min(descH, maxDescH);
  }

  if (hDetailsIcon)
    MoveWindow(hDetailsIcon, iconX, iconY, Scale(32, dpi), Scale(32, dpi),
               TRUE);
  if (hDetailsTitle)
    MoveWindow(hDetailsTitle, titleX, titleY, rightPanelWidth - Scale(80, dpi),
               Scale(24, dpi), TRUE);
  if (hDetailsDesc)
    MoveWindow(hDetailsDesc, descX, descY, descW, descH, TRUE);

  int bottomMargin = Scale(20, dpi); // Margin from bottom
  int progressH = Scale(18, dpi);
  int statusH = Scale(20, dpi);
  int btnH = Scale(36, dpi);

  // Bottom-up: progress bar, status label, button
  int progressY = rightY + rightPanelHeight - bottomMargin - progressH;
  int statusY = progressY - statusH - Scale(5, dpi);
  int btnY = statusY - btnH - Scale(10, dpi);

  if (hDetailsInstallBtn)
    MoveWindow(hDetailsInstallBtn, descX, btnY,
               rightPanelWidth - Scale(40, dpi), btnH, TRUE);
  if (hStatusLabel)
    MoveWindow(hStatusLabel, descX, statusY, rightPanelWidth - Scale(60, dpi),
               statusH, TRUE);
  if (hProgressBar)
    MoveWindow(hProgressBar, descX, progressY, rightPanelWidth - Scale(60, dpi),
               progressH, TRUE);
}

// Layout Easter Egg controls
static void LayoutEasterEgg(HWND hWnd, int rightX, int rightY,
                            int rightPanelWidth) {
  if (!hEEText1 || !IsWindowVisible(hEEText1))
    return;

  int dpi = GetDPI(hWnd);
  int descX = rightX + Scale(20, dpi);
  int curY = rightY + Scale(60, dpi); // Start where DetailsDesc starts

  // Interactive Text Lines first
  int text1W = Scale(230, dpi);
  int text2W = Scale(100, dpi);
  MoveWindow(hEEText1, descX, curY, text1W, Scale(24, dpi), TRUE);
  curY += Scale(24, dpi);
  MoveWindow(hEEText2, descX, curY, text2W, Scale(24, dpi), TRUE);
  curY += Scale(24 + 6, dpi); // small gap before aboutDesc

  // aboutDesc (release notes) below the two text lines
  int noticeH = 100;
  MoveWindow(hEENotice, descX, curY, rightPanelWidth - 40, noticeH, TRUE);
  curY += noticeH;

  // Rest of text "----------------..."
  MoveWindow(hEERest, descX, curY, rightPanelWidth - 40, 100, TRUE);
}

static void UpdateLayout(HWND hWnd) {
  RECT rc;
  GetClientRect(hWnd, &rc);
  int dpi = GetDPI(hWnd);
  const int padding = Scale(20, dpi);

  int sCardWidth = Scale(CARD_WIDTH, dpi);
  int sCardHeight = Scale(CARD_HEIGHT, dpi);
  int sCardMargin = Scale(CARD_MARGIN, dpi);

  // Calculate content area
  int contentLeft = padding;
  int contentWidth = rc.right - padding * 2;

  if (hMainTitle)
    MoveWindow(hMainTitle, padding, Scale(10, dpi),
               max(Scale(300, dpi), contentWidth), Scale(40, dpi), TRUE);

  // Settings button at bottom-right
  if (hSettingsBtn) {
    int btnSize = Scale(40, dpi);
    MoveWindow(hSettingsBtn, rc.right - btnSize - Scale(20, dpi),
               rc.bottom - btnSize - Scale(20, dpi), btnSize, btnSize, TRUE);
  }

  // Queue button at bottom-right (left of settings)
  if (hQueueBtn) {
    int btnSize = Scale(40, dpi);
    int setBtnSize = Scale(40, dpi);
    MoveWindow(hQueueBtn, rc.right - setBtnSize - Scale(20, dpi) - btnSize - Scale(10, dpi),
               rc.bottom - btnSize - Scale(20, dpi), btnSize, btnSize, TRUE);
  }

  if (hDownloadAllBtn) {
    int btnW = Scale(150, dpi);
    int btnH = Scale(40, dpi);
    MoveWindow(hDownloadAllBtn, rc.right - btnW - Scale(20, dpi),
               Scale(10, dpi), btnW, btnH, TRUE);
    ShowWindow(hDownloadAllBtn, g_viewMode == VIEW_QUEUE ? SW_SHOW : SW_HIDE);
  }

  if (hBackBtn) {
    int btnW = Scale(40, dpi);
    int btnH = Scale(40, dpi);
    MoveWindow(hBackBtn, Scale(20, dpi), Scale(10, dpi), btnW, btnH, TRUE);
    ShowWindow(hBackBtn, (g_viewMode == VIEW_QUEUE || g_viewMode == VIEW_SETTINGS) ? SW_SHOW : SW_HIDE);
  }

  if (hMainTitle) {
    ShowWindow(hMainTitle, g_viewMode == VIEW_APPS ? SW_SHOW : SW_HIDE);
    if (g_viewMode == VIEW_APPS)
      MoveWindow(hMainTitle, padding, Scale(10, dpi),
                 max(Scale(300, dpi), contentWidth), Scale(40, dpi), TRUE);
  }

  if (hSetPathEdit) {
    ShowWindow(hSetPathEdit, g_viewMode == VIEW_SETTINGS ? SW_SHOW : SW_HIDE);
    if (g_viewMode == VIEW_SETTINGS) {
      int editY = Scale(260, dpi) - g_scrollOffsetY;
      MoveWindow(hSetPathEdit, Scale(28, dpi), editY, rc.right - Scale(130, dpi), Scale(26, dpi), TRUE);
    }
  }
  if (hSetBrowseBtn) {
    ShowWindow(hSetBrowseBtn, g_viewMode == VIEW_SETTINGS ? SW_SHOW : SW_HIDE);
    if (g_viewMode == VIEW_SETTINGS) {
      int editY = Scale(260, dpi) - g_scrollOffsetY;
      MoveWindow(hSetBrowseBtn, rc.right - Scale(98, dpi), editY, Scale(88, dpi), Scale(26, dpi), TRUE);
    }
  }
  int availableWidth = contentWidth;
  if (availableWidth < sCardWidth)
    availableWidth = sCardWidth;
  int perCol = availableWidth / (sCardWidth + sCardMargin);
  if (perCol < 1)
    perCol = 1;
  if (perCol > 5)
    perCol = 5;

  const int numCards = 6;
  int lastBottom = Scale(60, dpi);
  for (int i = 0; i < numCards; ++i) {
    int col = i % perCol;
    int row = i / perCol;
    int x = contentLeft + col * (sCardWidth + sCardMargin);
    int y = Scale(60, dpi) + row * (sCardHeight + sCardMargin);
    HWND hCard = GetDlgItem(hWnd, 2001 + i);
    if (hCard) {
      if (g_viewMode == VIEW_APPS) {
        ShowWindow(hCard, SW_SHOW);
        MoveWindow(hCard, x, y - g_scrollOffsetY, sCardWidth, sCardHeight,
                   TRUE);
      } else {
        ShowWindow(hCard, SW_HIDE);
      }
    }
    int cardBottom = y + sCardHeight;
    if (cardBottom > lastBottom)
      lastBottom = cardBottom;
  }
  if (g_viewMode == VIEW_QUEUE) {
    g_contentHeight =
        Scale(100, dpi) + (int)g_downloadQueue.size() * Scale(60, dpi);
  } else if (g_viewMode == VIEW_SETTINGS) {
    g_contentHeight = Scale(600, dpi); // Fixed content height for settings
  } else {
    g_contentHeight = lastBottom + sCardMargin;
  }

  if (g_showAbout && hAboutCard) {
    CardData *data = (CardData *)GetWindowLongPtr(hAboutCard, GWLP_USERDATA);
    int aw = Scale(450, dpi);
    int ah = Scale(300, dpi);
    if (data) {
      HDC hdc = GetDC(hAboutCard);
      HFONT hDescFont =
          CreateFontW(Scale(16, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      HFONT oldFont = (HFONT)SelectObject(hdc, hDescFont);
      RECT rcText = {0, 0, aw - Scale(40, dpi), 0};
      DrawTextW(hdc, data->description, -1, &rcText,
                DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
      ah = rcText.bottom +
           Scale(160, dpi); // Title + padding + description + bottom buffer
      SelectObject(hdc, oldFont);
      DeleteObject(hDescFont);
      ReleaseDC(hAboutCard, hdc);
    }

    MoveWindow(hAboutCard, (rc.right - aw) / 2, (rc.bottom - ah) / 2, aw, ah,
               TRUE);
    BringWindowToTop(hAboutCard);
    SetWindowPos(hAboutCard, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  }

  // configure scrollbar
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
void DownloadAndInstall(HWND hWnd, LPCWSTR url, LPCWSTR localPath,
                        LPCWSTR appName, LPCWSTR version);
void HideDragInstallUI(HWND hWnd);
void CALLBACK LongPressTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                                 DWORD dwTime);
LRESULT CALLBACK SidebarListProc(HWND hWnd, UINT message, WPARAM wParam,
                                 LPARAM lParam);
LRESULT CALLBACK ToastProc(HWND hWnd, UINT message, WPARAM wParam,
                           LPARAM lParam);
void ShowToast(HWND hOwner, LPCWSTR message, COLORREF color);

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

  // Register toast class
  WNDCLASSW twc = {0};
  twc.lpfnWndProc = ToastProc;
  twc.hInstance = hInstance;
  twc.lpszClassName = L"ToastClass";
  twc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
  RegisterClassW(&twc);

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

  HDC hdcSystem = GetDC(NULL);
  int dpi = GetDeviceCaps(hdcSystem, LOGPIXELSX);
  ReleaseDC(NULL, hdcSystem);

  // Start with sidebar hidden, so base window width is 1200
  HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, 0, Scale(1200, dpi), Scale(700, dpi),
                            nullptr, nullptr, hInstance, nullptr);

  if (!hWnd)
    return FALSE;

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  g_downloadLocation = GetDownloadsPath();
  
  // Start with sidebar hidden, so base window width is 1200

  // Create a larger, bold font for the title
  HFONT hTitleFont =
      CreateFontW(Scale(32, dpi), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  // Sidebar removed

  int btnSize = Scale(40, dpi);
  int iconBtnSize = Scale(40, dpi);
  hSettingsBtn = CreateWindowW(
      L"BUTTON", L"\x2699", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, iconBtnSize,
      iconBtnSize, hWnd, (HMENU)ID_SETTINGS_BTN, hInstance, nullptr);

  hAboutCard =
      CreateWindowW(L"CardClass", L"AboutCard", WS_CHILD | WS_CLIPSIBLINGS, 0,
                    0, Scale(400, dpi), Scale(300, dpi), hWnd,
                    (HMENU)ID_ABOUT_CARD, hInstance, nullptr);
  // SetWindowLongPtr(hAboutCard, GWLP_USERDATA, (LONG_PTR)&aboutData); // Removed variable reference

  hMainTitle = CreateWindowW(
      L"STATIC", L"Build. Improve. Develop. - WAI v2.10.8",
      WS_CHILD | WS_VISIBLE, 60, 10, 700, 40, hWnd, (HMENU)5001, hInstance,
      nullptr);
  SendMessage(hMainTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

  hQueueBtn = CreateWindowW(
      L"BUTTON", L"Q", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, iconBtnSize,
      iconBtnSize, hWnd, (HMENU)ID_QUEUE_BTN, hInstance, nullptr);

  hDownloadAllBtn =
      CreateWindowW(L"BUTTON", L"Download All", WS_CHILD | BS_OWNERDRAW, 0, 0,
                    Scale(150, dpi), Scale(40, dpi), hWnd,
                    (HMENU)ID_DOWNLOAD_ALL_BTN, hInstance, nullptr);

  hBackBtn = CreateWindowW(L"BUTTON", L"\x2190", WS_CHILD | BS_OWNERDRAW,
                           0, 0, Scale(100, dpi), Scale(40, dpi), hWnd,
                           (HMENU)ID_BACK_TO_APPS_BTN, hInstance, nullptr);

  hSetPathEdit = CreateWindowW(L"EDIT", g_downloadLocation.c_str(),
                               WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0,
                               hWnd, (HMENU)ID_SET_PATH_EDIT, hInstance, nullptr);
  hSetBrowseBtn = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD, 0, 0, 0, 0,
                                hWnd, (HMENU)ID_SET_BROWSE, hInstance, nullptr);

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
      L"A game about finding actual system bugs and ancient/very old software. "
      L"Mostly revolves around Windows. You might be able to find something "
      L"like this in the About section...",
      L"The IE variant of HoverNet, featuring an UI similiar to Internet "
      L"Explorer.",
      L"visualOS 1.2 Open Beta - Test 1\nThe 1st Open Beta test of visualOS "
      L"1.2.",
      L"A completely renewed browser, made to continue WB's legacy: HoverNet."};
  const wchar_t *versions[6] = {L"v2.0.0", L"v2.0.1", L"v1.0.0",
                                L"v2.0.0", L"v1.2.0", L"v2.1.0"};
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
  hCardIcons[4] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(VISUALOS_ICON),
                                    IMAGE_ICON, 32, 32, 0);
  hCardIcons[5] = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(HN_ICON),
                                    IMAGE_ICON, 32, 32, 0);

  // Sample and cache average color from each icon
  for (int i = 0; i < 6; i++)
    g_cardAvgColor[i] = SampleIconAvgColor(hCardIcons[i]);

  const int numCards = 6;

  // populate sidebar removed

  // Create cards (initial position will be set by UpdateLayout)
  for (int i = 0; i < numCards; ++i) {
    HWND hCard = CreateWindowW(
        L"CardClass", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
        CARD_WIDTH, CARD_HEIGHT, hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(2001 + i)), hInstance,
        nullptr);

    CardData *data = new CardData();
    wcsncpy_s(data->title, titles[i], _TRUNCATE);
    wcsncpy_s(data->description, descriptions[i], _TRUNCATE);
    wcsncpy_s(data->url, urls[i], _TRUNCATE);
    wcsncpy_s(data->version, versions[i], _TRUNCATE);
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

  // Easter Egg Controls (Hidden initially)
  hEENotice = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0,
                            hWnd, (HMENU)ID_EE_NOTICE, hInstance, nullptr);
  hEEText1 = CreateWindowW(L"STATIC", L"whenthe's app installer by WS",
                           WS_CHILD | SS_NOTIFY | SS_LEFT, 0, 0, 0, 0, hWnd,
                           (HMENU)ID_EE_TEXT1, hInstance, nullptr);
  hEEText2 =
      CreateWindowW(L"STATIC", L"v2.00.6", WS_CHILD | SS_NOTIFY | SS_LEFT, 0, 0,
                    0, 0, hWnd, (HMENU)ID_EE_TEXT2, hInstance, nullptr);
  hEERest = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0, hWnd,
                          (HMENU)ID_EE_REST, hInstance, nullptr);

  // Fonts
  hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  SendMessage(hSettingsBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hAboutCard, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hSetPathEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hSetBrowseBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

  // Apply font to Easter Egg controls
  SendMessage(hEENotice, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hEEText1, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hEEText2, WM_SETFONT, (WPARAM)hFont, TRUE);
  SendMessage(hEERest, WM_SETFONT, (WPARAM)hFont, TRUE);

  // Initial responsive layout
  UpdateLayout(hWnd);

  return TRUE;
}

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
    
    if (wmId == ID_SET_BROWSE) {
        wchar_t path[MAX_PATH];
        BROWSEINFOW bi = { 0 };
        bi.lpszTitle = L"Select Download Folder";
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl != 0) {
            SHGetPathFromIDListW(pidl, path);
            g_downloadLocation = path;
            SetWindowTextW(hSetPathEdit, path);
            CoTaskMemFree(pidl);
        }
        return 0;
    }
    if (wmId == ID_SETTINGS_BTN) {
        g_viewMode = VIEW_SETTINGS;
        g_scrollOffsetY = 0;
        UpdateLayout(hWnd);
        return 0;
    }

    if (wmId == ID_QUEUE_BTN) {
      g_viewMode = VIEW_QUEUE;
      g_scrollOffsetY = 0;
      UpdateLayout(hWnd);
      return 0;
    } else if (wmId == ID_BACK_TO_APPS_BTN) {
      g_viewMode = VIEW_APPS;
      g_scrollOffsetY = 0;
      UpdateLayout(hWnd);
      return 0;
    } else if (wmId == ID_DOWNLOAD_ALL_BTN) {
      for (int idx : g_downloadQueue) {
        HWND hCard = GetDlgItem(hWnd, 2001 + idx);
        CardData *data = (CardData *)GetWindowLongPtr(hCard, GWLP_USERDATA);
        if (data) {
          std::wstring downloadsPath = GetDownloadsPath();
          std::wstring fileName = GetFileNameFromUrl(data->url);
          std::wstring fullPath = downloadsPath + L"\\" + fileName;
          DownloadAndInstall(hWnd, data->url, fullPath.c_str(), data->title,
                             data->version);
        }
      }
      g_downloadQueue.clear();
      UpdateLayout(hWnd);
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
  case WM_LBUTTONDOWN: {
    if (g_viewMode == VIEW_QUEUE) {
      int dpi = GetDPI(hWnd);
      int startY = Scale(80, dpi) - g_scrollOffsetY;
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      RECT rc;
      GetClientRect(hWnd, &rc);
      for (size_t i = 0; i < g_downloadQueue.size(); i++) {
        RECT xRect = {rc.right - Scale(70, dpi), startY,
                      rc.right - Scale(20, dpi), startY + Scale(50, dpi)};
        if (PtInRect(&xRect, pt)) {
          g_downloadQueue.erase(g_downloadQueue.begin() + i);
          UpdateLayout(hWnd);
          return 0;
        }
        startY += Scale(60, dpi);
      }
    }
    if (g_viewMode == VIEW_SETTINGS) {
        int dpi = GetDPI(hWnd);
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int lm = Scale(28, dpi);
        int btnW = Scale(110, dpi), btnH = Scale(36, dpi), btnGap = Scale(10, dpi);
        // Theme buttons Y: 16 + 46 + 14 + 22 = 98
        int themeY = Scale(98, dpi);
        RECT rL = {lm + 0*(btnW+btnGap), themeY, lm + 0*(btnW+btnGap) + btnW, themeY + btnH};
        RECT rD = {lm + 1*(btnW+btnGap), themeY, lm + 1*(btnW+btnGap) + btnW, themeY + btnH};
        RECT rA = {lm + 2*(btnW+btnGap), themeY, lm + 2*(btnW+btnGap) + btnW, themeY + btnH};
        // +/- Y: 98 + 36 + 18 = 152
        int szY = Scale(152, dpi);
        int badgeW = Scale(50, dpi);
        RECT rP = {lm + Scale(110,dpi) + badgeW + Scale(8,dpi), szY,
                   lm + Scale(110,dpi) + badgeW + Scale(8,dpi) + Scale(30,dpi), szY + Scale(30,dpi)};
        RECT rM = {rP.right + Scale(6,dpi), szY, rP.right + Scale(6,dpi) + Scale(30,dpi), szY + Scale(30,dpi)};
        // Toggle Y: 152 + 30 + 18 + 14 + 22 + 24 + 34 = 294 (+1 for centering) = 295
        int dlY  = Scale(295, dpi);
        RECT rS  = {lm, dlY,              lm + Scale(20,dpi), dlY + Scale(20,dpi)};
        RECT rAsk= {lm, dlY + Scale(36,dpi), lm + Scale(20,dpi), dlY + Scale(36,dpi) + Scale(20,dpi)};

        if (PtInRect(&rL, pt)) g_currentTheme = THEME_LIGHT;
        else if (PtInRect(&rD, pt)) g_currentTheme = THEME_DARK;
        else if (PtInRect(&rA, pt)) g_currentTheme = THEME_AMOLED;
        else if (PtInRect(&rP, pt)) g_textSizePercent = min(200, g_textSizePercent + 10);
        else if (PtInRect(&rM, pt)) g_textSizePercent = max(50, g_textSizePercent - 10);
        else if (PtInRect(&rS, pt)) g_useSmartDownload = !g_useSmartDownload;
        else if (PtInRect(&rAsk, pt)) g_askEveryTime = !g_askEveryTime;
        
        DeleteObject(hMainBgBrush);
        hMainBgBrush = CreateSolidBrush(GetMainBgColor());
        UpdateLayout(hWnd);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    if (g_showAbout) {
      g_showAbout = false;
      g_expandedIndex = -1;
      ShowWindow(hAboutCard, SW_HIDE);
      InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    }
    if (g_expandedIndex != -1) {
      g_expandedIndex = -1;
      InvalidateRect(hWnd, NULL, TRUE);
      // Force all cards to reset
      for (int i = 0; i < 6; i++) {
        HWND hCard = GetDlgItem(hWnd, 2001 + i);
        if (hCard)
          SetWindowPos(hCard, NULL, 0, 0, CARD_WIDTH, CARD_HEIGHT,
                       SWP_NOMOVE | SWP_NOZORDER);
      }
    }
  } break;
  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
    int dpi = GetDPI(hWnd);
    if (pDIS->CtlID == ID_SETTINGS_BTN || pDIS->CtlID == ID_QUEUE_BTN || pDIS->CtlID == ID_BACK_TO_APPS_BTN) {
      COLORREF bgColor = GetMainBgColor();
      HBRUSH hTempBg = CreateSolidBrush(bgColor);
      FillRect(pDIS->hDC, &pDIS->rcItem, hTempBg);
      DeleteObject(hTempBg);

      COLORREF btnBg = g_currentTheme == THEME_LIGHT ? RGB(230, 230, 250) : RGB(30, 30, 60);
      COLORREF btnAccent = g_currentTheme == THEME_LIGHT ? WS_PRIMARY : RGB(100, 180, 255);
      HBRUSH hBg = CreateSolidBrush(btnBg);
      HPEN hPen = CreatePen(PS_SOLID, 2, btnAccent);
      SelectObject(pDIS->hDC, hBg);
      SelectObject(pDIS->hDC, hPen);
      Ellipse(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top,
              pDIS->rcItem.right, pDIS->rcItem.bottom);
      SetBkMode(pDIS->hDC, TRANSPARENT);
      SetTextColor(pDIS->hDC, btnAccent);
      HFONT old = (HFONT)SelectObject(pDIS->hDC, hFont);
      wchar_t label[2] = {0};
      if (pDIS->CtlID == ID_SETTINGS_BTN) label[0] = L'\x2699';
      else if (pDIS->CtlID == ID_QUEUE_BTN) label[0] = L'Q';
      else label[0] = L'\x2190'; // Left arrow for back button
      DrawTextW(pDIS->hDC, label, 1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(pDIS->hDC, old);
      DeleteObject(hBg);
      DeleteObject(hPen);
      return TRUE;
    } else if (pDIS->CtlID == ID_DOWNLOAD_ALL_BTN) {
      COLORREF accent = g_currentTheme == THEME_LIGHT ? WS_PRIMARY : RGB(100, 180, 255);
      HBRUSH hBg = CreateSolidBrush(accent);
      FillRect(pDIS->hDC, &pDIS->rcItem, hBg);
      SetBkMode(pDIS->hDC, TRANSPARENT);
      SetTextColor(pDIS->hDC, g_currentTheme == THEME_LIGHT ? RGB(26, 26, 46) : RGB(20, 20, 40));
      HFONT old = (HFONT)SelectObject(pDIS->hDC, hFont);
      wchar_t txt[64];
      GetWindowTextW(pDIS->hwndItem, txt, 64);
      DrawTextW(pDIS->hDC, txt, -1, &pDIS->rcItem,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(pDIS->hDC, old);
      DeleteObject(hBg);
      return TRUE;
    }
    return FALSE;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);
    // Use theme-aware background color
    COLORREF bgColor = GetMainBgColor();
    HBRUSH hBgBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rc, hBgBrush);
    DeleteObject(hBgBrush);

    if (g_viewMode == VIEW_SETTINGS) {
        int dpi = GetDPI(hWnd);
        COLORREF txtCol     = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT    : RGB(220, 220, 220);
        COLORREF dimCol     = (g_currentTheme == THEME_LIGHT) ? RGB(100, 100, 140) : RGB(130, 130, 160);
        COLORREF accent     = (g_currentTheme == THEME_LIGHT) ? WS_PRIMARY       : RGB(100, 180, 255);
        COLORREF cardCol    = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_SURFACE :
                              (g_currentTheme == THEME_AMOLED) ? RGB(18, 18, 18) : RGB(32, 32, 60);
        COLORREF divCol     = (g_currentTheme == THEME_LIGHT) ? RGB(180, 180, 220) : RGB(50, 50, 80);

        SetBkMode(hdc, TRANSPARENT);

        // Helper: draw a rounded rectangle
        auto RoundedRect = [&](RECT r, int rx, COLORREF fill, COLORREF border, int bw) {
            HBRUSH hBr = CreateSolidBrush(fill);
            HPEN   hPn = CreatePen(PS_SOLID, bw, border);
            HBRUSH ob  = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op  = (HPEN)SelectObject(hdc, hPn);
            RoundRect(hdc, r.left, r.top, r.right, r.bottom, rx, rx);
            SelectObject(hdc, ob); SelectObject(hdc, op);
            DeleteObject(hBr); DeleteObject(hPn);
        };

        // Helper: draw a rounded checkbox
        auto DrawToggle = [&](int x, int y, bool on) {
            int sz = Scale(20, dpi);
            RECT box = {x, y, x + sz, y + sz};
            COLORREF fill   = on ? accent : GetMainBgColor();
            COLORREF border = on ? accent : divCol;
            RoundedRect(box, Scale(5, dpi), fill, border, 2);
            if (on) {
                // Draw white checkmark
                HPEN hCk = CreatePen(PS_SOLID, Scale(2, dpi), RGB(255, 255, 255));
                HPEN op  = (HPEN)SelectObject(hdc, hCk);
                int mx = x + sz/2, my = y + sz/2;
                MoveToEx(hdc, x + Scale(4,dpi), my,              NULL);
                LineTo  (hdc, mx - Scale(1,dpi), y + sz - Scale(5,dpi));
                LineTo  (hdc, x + sz - Scale(4,dpi), y + Scale(5,dpi));
                SelectObject(hdc, op);
                DeleteObject(hCk);
            }
        };

        // Section label helper
        HFONT hTitleFont   = CreateFontW(Scale(26, dpi), 0,0,0, FW_BOLD,   FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
        HFONT hSectionFont = CreateFontW(Scale(19, dpi), 0,0,0, FW_SEMIBOLD,FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
        HFONT hBodyFont    = CreateFontW(Scale(15, dpi), 0,0,0, FW_NORMAL,  FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
        HFONT hSmallFont   = CreateFontW(Scale(13, dpi), 0,0,0, FW_NORMAL,  FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");

        int lm = Scale(28, dpi); // left margin
        int y  = Scale(16, dpi) - g_scrollOffsetY;

        // ── Title ──────────────────────────────────────────────
        SelectObject(hdc, hTitleFont);
        SetTextColor(hdc, txtCol);
        TextOutW(hdc, Scale(70, dpi), y, L"Settings", 8);
        y += Scale(46, dpi);

        // ── Divider ────────────────────────────────────────────
        HPEN hDiv = CreatePen(PS_SOLID, 1, divCol);
        HPEN hOldP = (HPEN)SelectObject(hdc, hDiv);
        MoveToEx(hdc, lm, y, NULL); LineTo(hdc, rc.right - lm, y);
        SelectObject(hdc, hOldP); DeleteObject(hDiv);
        y += Scale(14, dpi);

        // ── THEME section ──────────────────────────────────────
        SelectObject(hdc, hSectionFont);
        SetTextColor(hdc, dimCol);
        TextOutW(hdc, lm, y, L"APPEARANCE", 10);
        y += Scale(22, dpi);

        // Theme pill-buttons
        const wchar_t* themeLabels[] = {L"Light", L"Dark", L"AMOLED Dark"};
        Theme themeVals[] = {THEME_LIGHT, THEME_DARK, THEME_AMOLED};
        int btnW = Scale(110, dpi), btnH = Scale(36, dpi), btnGap = Scale(10, dpi);
        for (int i = 0; i < 3; i++) {
            bool active = (g_currentTheme == themeVals[i]);
            RECT br = {lm + i*(btnW+btnGap), y, lm + i*(btnW+btnGap) + btnW, y + btnH};
            COLORREF fill   = active ? accent : cardCol;
            COLORREF border = active ? accent : divCol;
            RoundedRect(br, Scale(8,dpi), fill, border, active ? 2 : 1);
            SelectObject(hdc, hBodyFont);
            SetTextColor(hdc, active ? RGB(15,25,50) : txtCol);
            DrawTextW(hdc, themeLabels[i], -1, &br, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        y += btnH + Scale(18, dpi);

        // ── Text Size ──────────────────────────────────────────
        SelectObject(hdc, hBodyFont);
        SetTextColor(hdc, txtCol);
        TextOutW(hdc, lm, y + Scale(4, dpi), L"Text Size", 9);

        // badge showing current %
        std::wstring pct = std::to_wstring(g_textSizePercent) + L"%";
        int badgeW = Scale(50, dpi), badgeH = Scale(30, dpi);
        RECT badge = {lm + Scale(110, dpi), y, lm + Scale(110, dpi) + badgeW, y + badgeH};
        RoundedRect(badge, Scale(6,dpi), cardCol, divCol, 1);
        SetTextColor(hdc, accent);
        DrawTextW(hdc, pct.c_str(), -1, &badge, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        // +/- buttons
        int sbW = Scale(30, dpi), sbH = Scale(30, dpi);
        RECT rPlus  = {badge.right + Scale(8,dpi), y, badge.right + Scale(8,dpi) + sbW, y + sbH};
        RECT rMinus = {rPlus.right  + Scale(6,dpi), y, rPlus.right + Scale(6,dpi) + sbW, y + sbH};
        RoundedRect(rPlus,  Scale(6,dpi), cardCol, accent, 1);
        RoundedRect(rMinus, Scale(6,dpi), cardCol, accent, 1);
        SetTextColor(hdc, accent);
        DrawTextW(hdc, L"+", -1, &rPlus,  DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        DrawTextW(hdc, L"\x2212", -1, &rMinus, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        y += sbH + Scale(18, dpi);

        // ── Divider ────────────────────────────────────────────
        hDiv = CreatePen(PS_SOLID, 1, divCol);
        hOldP = (HPEN)SelectObject(hdc, hDiv);
        MoveToEx(hdc, lm, y, NULL); LineTo(hdc, rc.right - lm, y);
        SelectObject(hdc, hOldP); DeleteObject(hDiv);
        y += Scale(14, dpi);

        // ── DOWNLOAD section ───────────────────────────────────
        SelectObject(hdc, hSectionFont);
        SetTextColor(hdc, dimCol);
        TextOutW(hdc, lm, y, L"DOWNLOAD", 8);
        y += Scale(22, dpi);

        // "Download Location" label — edit box and Browse btn are native controls positioned by UpdateLayout
        SelectObject(hdc, hBodyFont);
        SetTextColor(hdc, txtCol);
        TextOutW(hdc, lm, y, L"Download Location", 17);
        y += Scale(24, dpi); // past the label text
        // edit control sits here (placed by UpdateLayout at this y ≈ 260)
        y += Scale(34, dpi); // past the edit control height + gap

        // Smart download toggle
        int toggleY = y + (Scale(24,dpi) - Scale(22,dpi))/2;
        DrawToggle(lm, toggleY, g_useSmartDownload);
        SelectObject(hdc, hBodyFont);
        SetTextColor(hdc, txtCol);
        TextOutW(hdc, lm + Scale(28, dpi), y, L"Smart install (extract ZIPs, add shortcuts)", 43);
        y += Scale(36, dpi);

        // Ask-every-time toggle
        toggleY = y + (Scale(24,dpi) - Scale(22,dpi))/2;
        DrawToggle(lm, toggleY, g_askEveryTime);
        TextOutW(hdc, lm + Scale(28, dpi), y, L"Ask for download location every time", 36);
        y += Scale(36, dpi);

        // ── Divider ────────────────────────────────────────────
        hDiv = CreatePen(PS_SOLID, 1, divCol);
        hOldP = (HPEN)SelectObject(hdc, hDiv);
        MoveToEx(hdc, lm, y, NULL); LineTo(hdc, rc.right - lm, y);
        SelectObject(hdc, hOldP); DeleteObject(hDiv);
        y += Scale(14, dpi);

        // ── ABOUT section ──────────────────────────────────────
        SelectObject(hdc, hSectionFont);
        SetTextColor(hdc, dimCol);
        TextOutW(hdc, lm, y, L"ABOUT", 5);
        y += Scale(22, dpi);

        // Calculate text height for dynamic sizing
        SelectObject(hdc, hSmallFont);
        SetTextColor(hdc, dimCol);
        RECT aNoteCalc = {lm + Scale(14,dpi), 0, rc.right - lm - Scale(14,dpi), 1000};
        int noteHeight = DrawTextW(hdc, L"Redesigned the main page's user interface - removed AppBar and InfoBar - added a new settings page - added DPI scaling - "
            L"added light and AMOLED dark themes - added download path settings - added smart installing - added queueing/multi-downloading - "
            L"the app is now useable during downloads", -1, &aNoteCalc, DT_LEFT|DT_WORDBREAK|DT_CALCRECT);
        
        // Dynamic card height based on text content
        int cardHeight = Scale(58,dpi) + noteHeight + Scale(16,dpi); // 58 for title+version, noteHeight for text, 16 for padding
        RECT aCard = {lm, y, rc.right - lm, y + cardHeight};
        RoundedRect(aCard, Scale(10,dpi), cardCol, divCol, 1);
        
        SelectObject(hdc, hTitleFont);
        SetTextColor(hdc, txtCol);
        RECT aInner = {aCard.left+Scale(14,dpi), aCard.top+Scale(12,dpi),
                       aCard.right-Scale(14,dpi), aCard.top+Scale(38,dpi)};
        DrawTextW(hdc, L"whenthe's app installer", -1, &aInner, DT_LEFT|DT_SINGLELINE);
        
        SelectObject(hdc, hSectionFont);
        SetTextColor(hdc, accent);
        RECT aVer = {aCard.left+Scale(14,dpi), aCard.top+Scale(38,dpi),
                     aCard.right-Scale(14,dpi), aCard.top+Scale(58,dpi)};
        DrawTextW(hdc, L"Version 2, Build 1.0, Revision 8 \x2022 whenthe's app installer made by (C) whenthe's space.", -1, &aVer, DT_LEFT|DT_SINGLELINE);
        
        SelectObject(hdc, hBodyFont);
        SetTextColor(hdc, dimCol);
        RECT aNote = {aCard.left+Scale(14,dpi), aCard.top+Scale(58,dpi),
                      aCard.right-Scale(14,dpi), aCard.bottom-Scale(8,dpi)};
        DrawTextW(hdc, L"Redesigned the main page's user interface - removed AppBar and InfoBar - added a new settings page - added DPI scaling - "
            L"added light and AMOLED dark themes - added download path settings - added smart installing - added queueing/multi-downloading - "
            L"the app is now useable during downloads - added custom download & install notifications, these are WIP at the moment", -1, &aNote, DT_LEFT|DT_WORDBREAK);

        DeleteObject(hTitleFont);
        DeleteObject(hSectionFont);
        DeleteObject(hBodyFont);
        DeleteObject(hSmallFont);
    }
 else if (g_expandedIndex != -1) {
      // Darken background
      HDC memDC = CreateCompatibleDC(hdc);
      HBITMAP hBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
      SelectObject(memDC, hBmp);
      FillRect(memDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
      BLENDFUNCTION bf = {AC_SRC_OVER, 0, 120, 0};
      AlphaBlend(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, rc.right,
                 rc.bottom, bf);
      DeleteObject(hBmp);
      DeleteDC(memDC);
    }

    if (g_viewMode == VIEW_QUEUE) {
      // Draw Queue View
      int dpi = GetDPI(hWnd);
      COLORREF textColor = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : RGB(220, 220, 220);
      COLORREF rowBgColor = (g_currentTheme == THEME_LIGHT) ? RGB(230, 230, 250) : RGB(40, 40, 70);
      SetTextColor(hdc, textColor);
      SetBkMode(hdc, TRANSPARENT);
      HFONT hTitleFont =
          CreateFontW(Scale(28, dpi), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      HFONT oldFont = (HFONT)SelectObject(hdc, hTitleFont);
      TextOutW(hdc, Scale(70, dpi), Scale(15, dpi) - g_scrollOffsetY, L"Download Queue", 14);

      HFONT hItemFont =
          CreateFontW(Scale(18, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      SelectObject(hdc, hItemFont);

      int startY = Scale(80, dpi) - g_scrollOffsetY;
      for (size_t i = 0; i < g_downloadQueue.size(); i++) {
        int idx = g_downloadQueue[i];
        HWND hCard = GetDlgItem(hWnd, 2001 + idx);
        CardData *data = (CardData *)GetWindowLongPtr(hCard, GWLP_USERDATA);
        if (data) {
          RECT rc;
          GetClientRect(hWnd, &rc);
          RECT rowRect = {Scale(20, dpi), startY, rc.right - Scale(20, dpi),
                          startY + Scale(50, dpi)};
          HBRUSH hRowBrush = CreateSolidBrush(rowBgColor);
          FillRect(hdc, &rowRect, hRowBrush);
          DeleteObject(hRowBrush);

          std::wstring itemText =
              std::wstring(data->title) + L" (" + data->version + L")";
          TextOutW(hdc, Scale(35, dpi), startY + Scale(12, dpi),
                   itemText.c_str(), (int)itemText.length());

          // Draw X button (simplified visual for now)
          SetTextColor(hdc, RGB(255, 100, 100));
          TextOutW(hdc, rc.right - Scale(60, dpi), startY + Scale(12, dpi),
                   L"X", 1);
          SetTextColor(hdc, textColor);
        }
        startY += Scale(60, dpi);
      }
      SelectObject(hdc, oldFont);
      DeleteObject(hTitleFont);
      DeleteObject(hItemFont);
    }

    EndPaint(hWnd, &ps);
  } break;
  case WM_ERASEBKGND:
    return 1;
  case WM_CTLCOLORSTATIC: {
    HWND hStatic = (HWND)lParam;
    int ctrlId = GetDlgCtrlID(hStatic);

    // Theme-aware colors
    COLORREF bgColor = GetMainBgColor();
    COLORREF textColor = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : RGB(220, 220, 220);

    // Main title on main background: make background transparent so it blends
    if (ctrlId == 5001) {
      SetBkMode((HDC)wParam, TRANSPARENT);
      SetTextColor((HDC)wParam, textColor);
      return (INT_PTR)GetStockObject(HOLLOW_BRUSH);
    }
    // Easter Egg text colors
    if (ctrlId == ID_EE_TEXT1) {
      SetBkColor((HDC)wParam, (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_BG : RGB(30, 30, 60));
      SetTextColor((HDC)wParam, (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : eeColor1);
      static HBRUSH hBrushEE = CreateSolidBrush(RGB(30, 30, 60));
      static HBRUSH hBrushEELight = CreateSolidBrush(WS_LIGHT_BG);
      return (INT_PTR)((g_currentTheme == THEME_LIGHT) ? hBrushEELight : hBrushEE);
    }
    if (ctrlId == ID_EE_TEXT2) {
      SetBkColor((HDC)wParam, (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_BG : RGB(30, 30, 60));
      SetTextColor((HDC)wParam, (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : eeColor2);
      static HBRUSH hBrushEE = CreateSolidBrush(RGB(30, 30, 60));
      static HBRUSH hBrushEELight = CreateSolidBrush(WS_LIGHT_BG);
      return (INT_PTR)((g_currentTheme == THEME_LIGHT) ? hBrushEELight : hBrushEE);
    }
    if (ctrlId == ID_EE_NOTICE || ctrlId == ID_EE_REST) {
      SetBkColor((HDC)wParam, (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_BG : RGB(30, 30, 60));
      SetTextColor((HDC)wParam, textColor);
      static HBRUSH hBrushEE = CreateSolidBrush(RGB(30, 30, 60));
      static HBRUSH hBrushEELight = CreateSolidBrush(WS_LIGHT_BG);
      return (INT_PTR)((g_currentTheme == THEME_LIGHT) ? hBrushEELight : hBrushEE);
    }
    SetBkColor((HDC)wParam, bgColor);
    SetTextColor((HDC)wParam, textColor);
    // Return appropriate brush for theme
    static HBRUSH hLightBrush = CreateSolidBrush(WS_LIGHT_BG);
    return (INT_PTR)((g_currentTheme == THEME_LIGHT) ? hLightBrush : hMainBgBrush);
  }
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    COLORREF bgColor = GetMainBgColor();
    COLORREF textColor = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : RGB(220, 220, 220);
    SetBkColor(hdc, bgColor);
    SetTextColor(hdc, textColor);
    static HBRUSH hListBrush = CreateSolidBrush(RGB(30, 30, 60));
    static HBRUSH hListBrushLight = CreateSolidBrush(WS_LIGHT_BG);
    return (INT_PTR)((g_currentTheme == THEME_LIGHT) ? hListBrushLight : hListBrush);
  }
  case WM_CTLCOLOREDIT: {
    HDC hdcEdit = (HDC)wParam;
    COLORREF bgColor = (g_currentTheme == THEME_LIGHT) ? RGB(240, 240, 250) : RGB(30, 30, 60);
    COLORREF textColor = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : RGB(220, 220, 220);
    SetBkColor(hdcEdit, bgColor);
    SetTextColor(hdcEdit, textColor);
    static HBRUSH hEditBrush = CreateSolidBrush(RGB(30, 30, 60));
    static HBRUSH hEditBrushLight = CreateSolidBrush(RGB(240, 240, 250));
    return (INT_PTR)((g_currentTheme == THEME_LIGHT) ? hEditBrushLight : hEditBrush);
  }
  case WM_SYSCOLORCHANGE:
    DrawMenuBar(hWnd);
    break;
  case WM_DESTROY:
    for (int i = 0; i < 6; ++i) {
      if (hCardIcons[i])
        DestroyIcon(hCardIcons[i]);
    }
    PostQuitMessage(0);
    break;
  case WM_USER + 101: {
    InstallInfo *info = (InstallInfo *)lParam;
    PerformInstallation(hWnd, info);
    return 0;
  }
  case WM_USER + 102: {
    ShowToast(hWnd, L"Download Failed!", RGB(255, 50, 50));
    return 0;
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

static HWND g_hActiveToast = nullptr;

LRESULT CALLBACK ToastProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  static COLORREF toastColor = RGB(100, 180, 255);
  switch (msg) {
  case WM_CREATE: {
    LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
    toastColor = (COLORREF)(uintptr_t)pcs->lpCreateParams;
    return 0;
  }
  case WM_TIMER:
    DestroyWindow(hWnd);
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT rc;
    GetClientRect(hWnd, &rc);
    COLORREF bgColor = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_SURFACE : RGB(30, 30, 60);
    COLORREF textColor = (g_currentTheme == THEME_LIGHT) ? WS_LIGHT_TEXT : RGB(220, 220, 220);
    HBRUSH hBg = CreateSolidBrush(bgColor);
    HPEN hPen = CreatePen(PS_SOLID, 2, toastColor);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, hBg);
    HPEN oldP = (HPEN)SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, 15, 15);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    HFONT hTFont =
        CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, hTFont);
    wchar_t text[256];
    GetWindowTextW(hWnd, text, 256);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);
    DeleteObject(hTFont);
    SelectObject(hdc, oldB);
    SelectObject(hdc, oldP);
    DeleteObject(hBg);
    DeleteObject(hPen);
    EndPaint(hWnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    if (hWnd == g_hActiveToast)
      g_hActiveToast = nullptr;
    return 0;
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

static std::wstring FetchURL(LPCWSTR url) {
  std::wstring result;
  HINTERNET hInternet =
      InternetOpenW(L"WAI", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
  if (hInternet) {
    HINTERNET hConnect =
        InternetOpenUrlW(hInternet, url, NULL, 0,
                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);
    if (hConnect) {
      char buffer[1024];
      DWORD bytesRead;
      while (
          InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) &&
          bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::string s(buffer);
        result += std::wstring(s.begin(), s.end());
      }
      InternetCloseHandle(hConnect);
    }
    InternetCloseHandle(hInternet);
  }
  return result;
}

static std::wstring GetLatestGitHubTag(const std::wstring &url) {
  if (url.find(L"github.com") == std::wstring::npos)
    return L"";

  // Extract repo path: https://github.com/user/repo/...
  size_t start = url.find(L"github.com/") + 11;
  size_t end = url.find(L"/", start);
  if (end == std::wstring::npos)
    return L"";
  end = url.find(L"/", end + 1);
  if (end == std::wstring::npos)
    end = url.length();

  std::wstring repo = url.substr(start, end - start);
  std::wstring apiURL =
      L"https://api.github.com/repos/" + repo + L"/releases/latest";

  std::wstring json = FetchURL(apiURL.c_str());
  size_t tagPos = json.find(L"\"tag_name\":\"");
  if (tagPos != std::wstring::npos) {
    tagPos += 12;
    size_t tagEnd = json.find(L"\"", tagPos);
    return json.substr(tagPos, tagEnd - tagPos);
  }
  return L"";
}

static void CheckForAppUpdates(HWND hWnd) {
  // This would ideally be run in a thread
  std::thread([hWnd]() {
    for (int i = 0; i < 6; i++) {
      HWND hCard = GetDlgItem(hWnd, 2001 + i);
      if (!hCard)
        continue;
      CardData *data = (CardData *)GetWindowLongPtr(hCard, GWLP_USERDATA);
      if (data && wcslen(data->url) > 0) {
        std::wstring latest = GetLatestGitHubTag(data->url);
        if (!latest.empty()) {
          g_latestVersions[i] = latest;
          InvalidateRect(hCard, NULL, TRUE);
        }
      }
    }
  }).detach();
}

static std::wstring FindExecutable(const std::wstring &directory) {
  std::wstring result = L"";
  WIN32_FIND_DATAW ffd;
  std::wstring searchPath = directory + L"\\*";
  HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
  if (hFind == INVALID_HANDLE_VALUE)
    return L"";

  do {
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (wcscmp(ffd.cFileName, L".") != 0 &&
          wcscmp(ffd.cFileName, L"..") != 0) {
        std::wstring subExec =
            FindExecutable(directory + L"\\" + ffd.cFileName);
        if (!subExec.empty()) {
          result = subExec;
          break;
        }
      }
    } else {
      std::wstring fileName = ffd.cFileName;
      if (fileName.length() > 4 &&
          _wcsicmp(fileName.substr(fileName.length() - 4).c_str(), L".exe") ==
              0) {
        result = directory + L"\\" + fileName;
        break;
      }
    }
  } while (FindNextFileW(hFind, &ffd) != 0);

  FindClose(hFind);
  return result;
}

static HRESULT CreateShortcut(LPCWSTR lpszPathObj, LPCWSTR lpszPathLink,
                              LPCWSTR lpszDesc) {
  HRESULT hres;
  IShellLink *psl;
  hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLink, (LPVOID *)&psl);
  if (SUCCEEDED(hres)) {
    psl->SetPath(lpszPathObj);
    psl->SetDescription(lpszDesc);
    IPersistFile *ppf;
    hres = psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf);
    if (SUCCEEDED(hres)) {
      hres = ppf->Save(lpszPathLink, TRUE);
      ppf->Release();
    }
    psl->Release();
  }
  return hres;
}

static std::wstring GetWSApplicationsPath() {
  wchar_t path[MAX_PATH];
  GetEnvironmentVariableW(L"ProgramFiles", path, MAX_PATH);
  std::wstring wsPath = path;
  wsPath += L"\\WS Applications";
  CreateDirectoryW(wsPath.c_str(), NULL);
  return wsPath;
}

static void PerformInstallation(HWND hWnd, InstallInfo *info) {
  std::wstring baseDir = GetWSApplicationsPath();
  std::wstring appDir = baseDir + L"\\" + info->appName;
  CreateDirectoryW(appDir.c_str(), NULL);

  if (!g_useSmartDownload) {
    ShowToast(hWnd, (L"Download Complete: " + info->appName).c_str(), RGB(100, 255, 100));
    delete info;
    return;
  }

  bool isZip = (info->filePath.find(L".zip") != std::wstring::npos);
  bool isMsi = (info->filePath.find(L".msi") != std::wstring::npos);

  if (isZip) {
    std::wstring cmd = L"powershell -command \"Expand-Archive -Path '";
    cmd += info->filePath;
    cmd += L"' -DestinationPath '";
    cmd += appDir;
    cmd += L"' -Force\"";

    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
      WaitForSingleObject(pi.hProcess, INFINITE);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }

    // Try to find an .exe to shortcut using Win32 FindFirstFile
    std::wstring exePath = FindExecutable(appDir);
    if (!exePath.empty()) {
      wchar_t desktop[MAX_PATH];
      SHGetSpecialFolderPathW(NULL, desktop, CSIDL_DESKTOPDIRECTORY, FALSE);
      std::wstring lnkPath =
          std::wstring(desktop) + L"\\" + info->appName + L".lnk";
      CreateShortcut(exePath.c_str(), lnkPath.c_str(), info->appName.c_str());
    }
  }

  // Save version to file
  std::wstring verFile = appDir + L"\\version.txt";
  HANDLE hFile = CreateFileW(verFile.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    DWORD written;
    WriteFile(hFile, info->version.c_str(),
              (DWORD)(info->version.length() * sizeof(wchar_t)), &written,
              NULL);
    CloseHandle(hFile);
  }

  ShowToast(
      hWnd,
      (L"Installed: " + info->appName + L" (" + info->version + L")").c_str(),
      RGB(50, 255, 50));
  delete info;
}

void ShowToast(HWND hOwner, LPCWSTR message, COLORREF color) {
  if (g_hActiveToast && IsWindow(g_hActiveToast))
    DestroyWindow(g_hActiveToast);
  int w = 350, h = 60;
  RECT rc;
  GetWindowRect(hOwner, &rc);
  int x = rc.right - w - 40, y = rc.bottom - h - 40;
  g_hActiveToast = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"ToastClass", message,
      WS_POPUP | WS_VISIBLE, x, y, w, h, hOwner, nullptr,
      GetModuleHandle(nullptr), (LPVOID)(uintptr_t)color);
  if (g_hActiveToast) {
    SetLayeredWindowAttributes(g_hActiveToast, 0, 230, LWA_ALPHA);
    SetTimer(g_hActiveToast, 1, 4000, nullptr);
  }
}

void DownloadAndInstall(HWND hWnd, LPCWSTR url, LPCWSTR localPath,
                        LPCWSTR appName, LPCWSTR version) {
  wchar_t currentPath[MAX_PATH];
  GetWindowTextW(hSetPathEdit, currentPath, MAX_PATH);
  g_downloadLocation = currentPath;

  std::wstring finalPath = localPath;
  
  if (g_askEveryTime) {
      wchar_t szFile[MAX_PATH] = { 0 };
      wcsncpy_s(szFile, GetFileNameFromUrl(url).c_str(), _TRUNCATE);
      OPENFILENAMEW ofn = { 0 };
      ofn.lStructSize = sizeof(ofn);
      ofn.hwndOwner = hWnd;
      ofn.lpstrFile = szFile;
      ofn.nMaxFile = sizeof(szFile);
      ofn.lpstrFilter = L"All Files\0*.*\0";
      ofn.nFilterIndex = 1;
      ofn.lpstrInitialDir = g_downloadLocation.c_str();
      ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
      if (GetSaveFileNameW(&ofn)) {
          finalPath = szFile;
      } else {
          return; // Cancelled
      }
  } else if (!g_downloadLocation.empty()) {
      finalPath = g_downloadLocation + L"\\" + GetFileNameFromUrl(url);
  }

  std::wstring startedMsg = L"Download Started: ";
  startedMsg += appName;
  ShowToast(hWnd, startedMsg.c_str(), RGB(100, 180, 255));

  InstallInfo *info = new InstallInfo();
  info->filePath = finalPath;
  info->appName = appName;
  info->version = version;

  std::wstring u = url;
  std::wstring lp = finalPath;
  std::thread([hWnd, u, lp, info]() {
    HRESULT hr = URLDownloadToFileW(nullptr, u.c_str(), lp.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
      PostMessage(hWnd, WM_USER + 101, 0, (LPARAM)info);
    } else {
      delete info;
      PostMessage(hWnd, WM_USER + 102, 0, 0);
    }
  }).detach();
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
        if (wcslen(d->url) == 0) {
        } else if (!IsDownloadableUrl(d->url)) {
          // It's a redirect/website link - open in browser
          ShellExecuteW(nullptr, L"open", d->url, nullptr, nullptr,
                        SW_SHOWNORMAL);
        } else {
          std::wstring downloadsPath = GetDownloadsPath();
          std::wstring fileName = GetFileNameFromUrl(d->url);
          std::wstring fullPath = downloadsPath + L"\\" + fileName;
          std::wstring msg = L"Do you want to download the following app: \"" +
                             std::wstring(d->title) + L"\"?";
          int result = MessageBoxW(hWnd, msg.c_str(), L"Long-press Shortcut",
                                   MB_ICONINFORMATION | MB_YESNO);
          if (result == IDYES) {
            DownloadAndInstall(hWnd, d->url, fullPath.c_str(), d->title,
                               d->version);
          }
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
  int myId = GetDlgCtrlID(hWnd);
  int myIndex = (myId == ID_ABOUT_CARD) ? 999 : (myId - 2001);

  switch (message) {
  case WM_ERASEBKGND:
    return 1;

  case WM_MOUSEMOVE: {
    if (g_showAbout && myIndex != 999)
      return 0; // Disable hover for other cards when About is shown
    if (myIndex == 999)
      return 0; // Don't expand About card further
    if (g_expandedIndex != myIndex) {
      g_expandedIndex = myIndex;
      BringWindowToTop(hWnd);
      int dpi = GetDPI(hWnd);
      SetWindowPos(hWnd, HWND_TOP, 0, 0, Scale(CARD_WIDTH + 75, dpi),
                   Scale(CARD_HEIGHT + 45, dpi), SWP_NOMOVE);
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, HOVER_DEFAULT};
      TrackMouseEvent(&tme);
      InvalidateRect(GetParent(hWnd), NULL, TRUE);
      UpdateWindow(GetParent(hWnd));
      InvalidateRect(hWnd, NULL, TRUE);
    }
    return 0;
  }
  case WM_MOUSELEAVE: {
    if (g_isMenuOpen)
      return 0;
    if (g_showAbout && myIndex != 999)
      return 0;
    if (myIndex == 999)
      return 0;
    g_expandedIndex = -1;
    SetWindowPos(hWnd, NULL, 0, 0, CARD_WIDTH, CARD_HEIGHT,
                 SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(GetParent(hWnd), NULL, TRUE);
    UpdateWindow(GetParent(hWnd));
    InvalidateRect(hWnd, NULL, TRUE);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    if (myIndex == 999) {
      // Clicking ON the about card shouldn't close it, only clicking OFF
      // should.
      return 0;
    }
    int dpi = GetDPI(hWnd);
    if (g_expandedIndex == myIndex) {
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      int curW = Scale(CARD_WIDTH + 75, dpi);
      int curH = Scale(CARD_HEIGHT + 45, dpi);

      // Add to Queue button rect
      RECT btnRect = {curW - Scale(175, dpi), curH - Scale(50, dpi),
                      curW - Scale(45, dpi), curH - Scale(15, dpi)};
      // Dropdown button rect
      RECT ddRect = {curW - Scale(40, dpi), curH - Scale(50, dpi),
                     curW - Scale(15, dpi), curH - Scale(15, dpi)};

      if (PtInRect(&btnRect, pt)) {
        bool already = false;
        for (int idx : g_downloadQueue)
          if (idx == (myId - 2001))
            already = true;
        if (!already) {
          g_downloadQueue.push_back(myId - 2001);
          ShowToast(GetParent(hWnd),
                    (std::wstring(data->title) + L" added to queue").c_str(),
                    RGB(100, 255, 100));
        } else {
          ShowToast(GetParent(hWnd), L"Already in queue", RGB(255, 200, 50));
        }
        return 0;
      } else if (PtInRect(&ddRect, pt)) {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1, L"Download Now");
        POINT screenPt = pt;
        ClientToScreen(hWnd, &screenPt);
        g_isMenuOpen = true;
        int sel = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RETURNCMD,
                                 screenPt.x, screenPt.y, 0, hWnd, NULL);
        g_isMenuOpen = false;
        DestroyMenu(hMenu);
        if (sel == 1) {
          std::wstring downloadsPath = GetDownloadsPath();
          std::wstring fileName = GetFileNameFromUrl(data->url);
          std::wstring fullPath = downloadsPath + L"\\" + fileName;
          DownloadAndInstall(GetParent(hWnd), data->url, fullPath.c_str(),
                             data->title, data->version);
        }
        return 0;
      }
    } else {
      g_expandedIndex = myIndex;
      BringWindowToTop(hWnd);
      SetWindowPos(hWnd, HWND_TOP, 0, 0, Scale(CARD_WIDTH + 75, dpi),
                   Scale(CARD_HEIGHT + 45, dpi), SWP_NOMOVE);
      InvalidateRect(GetParent(hWnd), NULL, TRUE);
      UpdateWindow(GetParent(hWnd));
      InvalidateRect(hWnd, NULL, TRUE);
    }
    SetFocus(hWnd);
    return 0;
  }

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    int dpi = GetDPI(hWnd);
    bool expanded = (g_expandedIndex == myIndex);
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int curW = clientRect.right;
    int curH = clientRect.bottom;

    COLORREF avgColor = (myIndex >= 0 && myIndex < 6)
                            ? g_cardAvgColor[myIndex]
                            : RGB(30, 30, 60);
    COLORREF bgColor = GetCardBgColor(avgColor);
    COLORREF borderColor;
    if (g_currentTheme == THEME_LIGHT)
        borderColor = RGB(180, 180, 220);  // Subtle lavender border for light mode
    else if (g_currentTheme == THEME_AMOLED)
        borderColor = RGB(40, 40, 40);
    else
        borderColor = LightenForBorder(bgColor);
    COLORREF textColor = GetCardTextColor(avgColor);

    RECT cardRect = {0, 0, curW, curH};
    HBRUSH hCardBgBrush = CreateSolidBrush(bgColor);
    SelectObject(hdc, hCardBgBrush);
    RoundRect(hdc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom,
              Scale(20, dpi), Scale(20, dpi));
    DeleteObject(hCardBgBrush);

    HPEN hBorderPen = CreatePen(PS_SOLID, expanded ? 2 : 1,
                                expanded ? RGB(100, 180, 255) : borderColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom,
              Scale(20, dpi), Scale(20, dpi));

    // Draw icon
    HICON hIconToDraw =
        (myIndex >= 0 && myIndex < 6)
            ? hCardIcons[myIndex]
            : LoadIcon(GetModuleHandle(NULL),
                       MAKEINTRESOURCE(IDI_WHENTHESAPPINSTALLER));
    if (hIconToDraw) {
      DrawIconEx(hdc, (curW - Scale(32, dpi)) / 2, Scale(15, dpi), hIconToDraw,
                 Scale(32, dpi), Scale(32, dpi), 0, nullptr, DI_NORMAL);
    }

    // Draw title
    SetBkMode(hdc, TRANSPARENT);
    HFONT hTitleFont =
        CreateFontW(Scale(22, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hTitleFont);
    SetTextColor(hdc, textColor);
    RECT titleRect = {Scale(10, dpi), Scale(55, dpi), curW - Scale(10, dpi),
                      Scale(110, dpi)};
    DrawTextW(hdc, data->title, -1, &titleRect,
              DT_CENTER | DT_WORDBREAK | DT_TOP);
    SelectObject(hdc, oldFont);
    DeleteObject(hTitleFont);

    if (expanded) {
      // Draw Version (newly added)
      HFONT hVerFont =
          CreateFontW(Scale(14, dpi), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      oldFont = (HFONT)SelectObject(hdc, hVerFont);
      COLORREF dimTextColor = (g_currentTheme == THEME_LIGHT) ? RGB(100, 100, 130) : RGB(150, 150, 150);
      SetTextColor(hdc, dimTextColor);
      RECT verRect = {Scale(20, dpi), Scale(85, dpi), curW - Scale(20, dpi),
                      Scale(105, dpi)};
      DrawTextW(hdc, data->version, -1, &verRect, DT_LEFT | DT_SINGLELINE);

      // Draw Update Indicator if available
      if (myIndex >= 0 && myIndex < 6 && !g_latestVersions[myIndex].empty() &&
          g_latestVersions[myIndex] != data->version) {
        SetTextColor(hdc, RGB(255, 100, 100));
        RECT upRect = {Scale(100, dpi), Scale(85, dpi), curW - Scale(20, dpi),
                       Scale(105, dpi)};
        DrawTextW(hdc,
                  (L"Update Available: " + g_latestVersions[myIndex]).c_str(),
                  -1, &upRect, DT_LEFT | DT_SINGLELINE);
        SetTextColor(hdc, RGB(150, 150, 150)); // reset
      }

      SelectObject(hdc, oldFont);
      DeleteObject(hVerFont);

      // Draw Description (shifted down slightly to make room for version)
      HFONT hDescFont =
          CreateFontW(Scale(16, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      oldFont = (HFONT)SelectObject(hdc, hDescFont);
      COLORREF descTextColor = (g_currentTheme == THEME_LIGHT) ? RGB(80, 80, 110) : RGB(200, 200, 200);
      SetTextColor(hdc, descTextColor);
      RECT descRect = {Scale(20, dpi), Scale(115, dpi), curW - Scale(20, dpi),
                       curH - Scale(60, dpi)};
      DrawTextW(hdc, data->description, -1, &descRect,
                DT_LEFT | DT_WORDBREAK | DT_TOP);
      SelectObject(hdc, oldFont);
      DeleteObject(hDescFont);

      if (myIndex != 999) {
        // Draw Add to Queue Button
        RECT btnRect = {curW - Scale(175, dpi), curH - Scale(50, dpi),
                        curW - Scale(45, dpi), curH - Scale(15, dpi)};
        COLORREF btnColor = (g_currentTheme == THEME_LIGHT) ? WS_PRIMARY : RGB(100, 180, 255);
        HPEN hBtnPen = CreatePen(PS_SOLID, 2, btnColor);
        SelectObject(hdc, hBtnPen);
        RoundRect(hdc, btnRect.left, btnRect.top, btnRect.right, btnRect.bottom,
                  Scale(8, dpi), Scale(8, dpi));

        SetTextColor(hdc, btnColor);
        HFONT hBtnFont = CreateFontW(
            Scale(15, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        oldFont = (HFONT)SelectObject(hdc, hBtnFont);

        DrawTextW(hdc, L"Add to Queue", -1, &btnRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Draw Dropdown arrow button
        RECT ddRect = {curW - Scale(40, dpi), curH - Scale(50, dpi),
                       curW - Scale(15, dpi), curH - Scale(15, dpi)};
        RoundRect(hdc, ddRect.left, ddRect.top, ddRect.right, ddRect.bottom,
                  Scale(8, dpi), Scale(8, dpi));
        DrawTextW(hdc, L"\x25BC", -1, &ddRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, oldFont);
        DeleteObject(hBtnFont);
        DeleteObject(hBtnPen);
      }
    } else if (g_expandedIndex != -1) {
      // Darken this card if another card is expanded
      HDC memDC = CreateCompatibleDC(hdc);
      HBITMAP hBmp = CreateCompatibleBitmap(hdc, curW, curH);
      SelectObject(memDC, hBmp);
      FillRect(memDC, &cardRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
      BLENDFUNCTION bf = {AC_SRC_OVER, 0, 120, 0};
      AlphaBlend(hdc, 0, 0, curW, curH, memDC, 0, 0, curW, curH, bf);
      DeleteObject(hBmp);
      DeleteDC(memDC);
    }

    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
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