#include <Windows.h>
#include <WindowsX.h>
#include <map>
#include <string>
#include "Gyazo.h"
#include "Screenshot.h"
#include "Util.h"
#include "resource.h"

#include "KeyHook.h"
#pragma comment(lib, "KeyHook.lib")

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// デバッグとして保存先をDesktopにする
// ファイル名は{日付時間}.png
#define DEBUG_LOCAL_SAVE
#define WM_TASKTRAY (WM_APP + 1)
#define WM_ACTIVEWINDOW_SS (WM_USER + 1)
#define WM_DESKTOP_SS (WM_USER + 2)
#define ID_TASKTRAY 1
#define S_TASKTRAY_TIPS L"Gyazo"
#define MUTEX_NAME L"GyazoIM"
#define IF_KEY_PRESS(lp) ((lp & (1 << 31)) == 0)

#define DLG_KEYCONFIG_PROC_WINDOW_TITLE L"キー設定"
#define DLG_KEYCONFIG_ASK L"キーを入力してください"
#define DLG_KEYCONFIG_ASK_BUTTON_TITLE L"入力"
#define DLG_KEYCONFIG_DEFAULT_BUTTON_TITLE L"設定"
#define DLG_MONITOR_GAMMA_WINDOW_TITLE_FORMAT L"%sのガンマ調節"

#define SCREENSHOT_FILEPATH L"screenshot.png"
#define CURSOR_FONT L"Tahoma"

HINSTANCE g_hInstance = NULL;
TCHAR szWindowClass[] = L"GyazoIM";
TCHAR szTitle[] = L"GyazoIM";
HWND g_hWnd = NULL;
HWND g_hSelectedArea = NULL;
HWND oldHWND = NULL;
BOOL bStartCapture = FALSE;
HHOOK g_hook = NULL;
HWND g_hKeyConfigDlg = NULL;
HHOOK g_hKeyConfigHook = NULL;

// キーボードショートカット用構造体
KEYINFO g_activeSSKeyInfo = {0};
KEYINFO g_desktopSSKeyInfo = {0};

// クライアントからスクリーン座標に変換
POINT mousePressedPt = {0};		// マウスクリックした場所(始点)
POINT mouseReleasedPt = {0};	// マウスを離した場所(終点)
RECT mouseSelectedArea = {0};	// マウスによって選択されている領域

// ドラッグ中状態管理
//BOOL bDrag = FALSE; 

LPCTSTR layer1WindowClass = L"GyazoIM_Layer1";
LPCTSTR layer2WindowClass = L"GyazoIM_Layer2";

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// ===========================================
//	設定ファイル操作系関数
// ===========================================
#define CONFIG_FILENAME L"config.ini"
void LoadConfig()
{
  LPTSTR lpConfigPath = NULL;

  __try{
    lpConfigPath = ::GetConfigPath(CONFIG_FILENAME);

    // setup default key config
    ::QuickSetKeyInfo(&::g_activeSSKeyInfo, KEY_NOT_SET, VK_F9);
    ::QuickSetKeyInfo(&::g_desktopSSKeyInfo, VK_CONTROL, VK_F9);

    // load keyconfig
    ::GetPrivateProfileKeyInfo(L"KeyBind", L"g_activeSSKeyInfo", &::g_activeSSKeyInfo, lpConfigPath);
    ::GetPrivateProfileKeyInfo(L"KeyBind", L"g_desktopSSKeyInfo", &::g_desktopSSKeyInfo, lpConfigPath);
  }__finally{
    if(lpConfigPath)
      ::GlobalFree(lpConfigPath);
  }
}

void SaveConfig()
{
  LPTSTR lpConfigPath = NULL;

  __try{
    lpConfigPath = ::GetConfigPath(CONFIG_FILENAME);

    // save keyconfig
    ::WritePrivateProfileKeyInfo(L"KeyBind", L"g_activeSSKeyInfo", &::g_activeSSKeyInfo, lpConfigPath);
    ::WritePrivateProfileKeyInfo(L"KeyBind", L"g_desktopSSKeyInfo", &::g_desktopSSKeyInfo, lpConfigPath);
  }__finally{
    if(lpConfigPath)
      ::GlobalFree(lpConfigPath);
  }
}

// ===========================================
//	スクリーンショット撮影簡略化系
// ===========================================
// Desktopの指定した範囲をキャプチャしてアップロード
UINT g_uploadTarget = IDM_OUTPUT_PREVIEW;
void ScreenShotAndUpload(HWND forErrorMessage, LPCTSTR path, RECT *rect)
{
  try{
    ::Screenshot::ScreenshotDesktop(path, rect);
    ::MessageBeep(MB_ICONASTERISK); // 撮影音をこの時点で出す

    // 送信先は右クリメニューで設定した場所
    // デフォルトはローカルプレビュー
    if( g_uploadTarget == IDM_OUTPUT_GYAZO ){
      // gyazoで画像データを送信する
      Gyazo *g = new Gyazo();
      g->UploadFileAndOpenURL(forErrorMessage, path);
      delete g;
    }else{
      ::ExecuteFile(g_hWnd, path);
    }
  }catch(exception e){
    ::ErrorMessageBox(L"%s", e);
  }

  ::MessageBeep(MB_ICONASTERISK); // 投稿音をこの時点で出す
}

void ScreenShotAndUpload_ActiveWindow(HWND forErrorMessage, LPCTSTR path)
{
  RECT rect;
  ::GetWindowRect(::GetForegroundWindow(), &rect);
  ::ScreenShotAndUpload(forErrorMessage, path, &rect);
}

void ScreenShotAndUpload_Desktop(HWND forErrorMessage, LPCTSTR path)
{
  RECT rect;
  ::GetWindowRect(::GetDesktopWindow(), &rect);
  ScreenShotAndUpload(forErrorMessage, path, &rect);
}

// ===========================================
//	GUIイベント系(インスペクトモード)
// ===========================================
// インスペクトモード開始
BOOL StartInspect()
{
  if( !::StartMouseEventProxy(::g_hWnd, ::g_hInstance) ){
    ::ShowLastError();
    return FALSE;
  }
  bStartCapture = TRUE;
  return TRUE;
}

// インスペクトモード無効
BOOL StopInspect()
{
  if( !::StopMouseEventProxy() ){
    ::ShowLastError();
    return FALSE;
  }

  bStartCapture = FALSE;
  return TRUE;
}

// ===========================================
//	GUIイベント系(キャプチャモード)
// ===========================================
void OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags)
{
  if(::bStartCapture) { // Inspectモードの間違い
    if( HWND h = ::WindowFromCursorPos() ){
      if(oldHWND != h){
        ::NoticeRedraw(h);
        ::NoticeRedraw(oldHWND);
        oldHWND = NULL;
      }

      ::SetForegroundWindow(h);
      ::HighlightWindow(h);
      oldHWND = h;
    }
  }
}

void OnLButtonDown(HWND hWnd, BOOL isDoubleClick, int x, int y, UINT keyFlags)
{
  if( bStartCapture ){
    if( HWND h = WindowFromCursorPos() ){
      ::NoticeRedraw(oldHWND);
      ::NoticeRedraw(h);
      StopInspect();

      // 選択領域を削除するコードをここに入れる
      RECT rect;
      ::GetWindowRect(h, &rect);
      ::ScreenShotAndUpload(hWnd, L"inspect.png", &rect);
    }
  }
}

// インスペクト中に右クリックで、中止処理
void OnRButtonDown(HWND hWnd, BOOL isDoubleClick, int x, int y, UINT keyFlags)
{
  ::NoticeRedraw(hWnd);
  ::NoticeRedraw(oldHWND);
  ::StopInspect();
}

void OnDestroy(HWND hWnd)
{
  ::StopInspect();
  ::TasktrayDeleteIcon(hWnd, ID_TASKTRAY);
  ::PostQuitMessage(0);
}


HDC g_hMemDC = NULL;
BOOL bNormalize = FALSE;
BOOL bStick = FALSE;
#define CAPTURE_MIN_WIDTH 10
#define CAPTURE_MIN_HEIGHT 10
RECT selected = {0};
POINT mousePressed = {0};
POINT mousePressedR = {0};
RECT lastSelected = {0};
BOOL bCapture = FALSE;
BOOL bDrag = FALSE;

// キャプチャモード開始
BOOL StartCapture()
{
  RECT rect;
  ::GetWindowRect(::GetDesktopWindow(), &rect);
  rect.right = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
  rect.bottom = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

  // 画面がちらつくので、最初は0,0でウインドウ作成しといて
  // 準備できたらごりっと拡大
  HWND hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_COMPOSITED | WS_EX_TOOLWINDOW,
    ::layer1WindowClass, layer1WindowClass, WS_POPUP,
    //0,0,0,0, NULL, NULL, g_hInstance, NULL);
    rect.left,rect.top,rect.right - rect.left,rect.bottom - rect.top, NULL, NULL, g_hInstance, NULL);
  ::SetLayeredWindowAttributes(hWnd, RGB(255,0,0), 100, LWA_COLORKEY | LWA_ALPHA);
  if(hWnd == NULL){
    ::ShowLastError();
    return FALSE;
  }
  ShowWindow(hWnd, SW_SHOW);
  UpdateWindow(hWnd);
  ::SetWindowTopMost(hWnd);

  // 選択領域
  HWND hWnd2 = ::CreateWindowEx(WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
    layer2WindowClass, layer2WindowClass, WS_POPUP,
    rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
    NULL, NULL, g_hInstance, NULL);
  if(hWnd == NULL){
    ::ShowLastError();
    return FALSE;
  }
  ShowWindow(hWnd2, SW_SHOW);
  UpdateWindow(hWnd2);
  ::SetWindowTopMost(hWnd2);

  // kokokokokoko

  return FALSE;
}

BOOL StopCapture()
{
  return FALSE;
}

// 描画するだけノプロシージャとして使ってる
LRESULT CALLBACK SelectedAreaWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message){
  case WM_ERASEBKGND:
    return FALSE;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// 実際にマウスイベントとかを処理する透明なレイヤー
LRESULT CALLBACK LayerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  PAINTSTRUCT ps;
  HDC hdc;

  static RECT windowRect;
  static HBRUSH transparentBrush = NULL;
  static HBRUSH colorBrush = NULL;
  static HBRUSH colorBrush2 = NULL;
  static HBITMAP hOldBitmap = NULL;
  static HBITMAP hBitmap = NULL;
  static HFONT hFont;

  switch (message)
  {
  case WM_CREATE:
    ::GetClientRect(hWnd, &windowRect);
    transparentBrush = ::CreateSolidBrush(RGB(255,0,0));
    colorBrush = ::CreateSolidBrush(RGB(100,100,100));
    colorBrush2 = ::CreateSolidBrush(RGB(0,0,255));
    hFont = ::QuickCreateFont(18, L"Tahoma");

    // メモリデバイスコンテキストの作成
    {
      g_hMemDC = ::CreateCompatibleDC(::GetDC(hWnd));
      hBitmap = ::CreateCompatibleBitmap(::GetDC(hWnd), windowRect.right, windowRect.bottom);
      hOldBitmap = (HBITMAP)::SelectObject(g_hMemDC, hBitmap);
    }
    return TRUE;
  case WM_COMMAND:
    return DefWindowProc(hWnd, message, wParam, lParam);

  case WM_KEYDOWN:
    if(wParam == VK_CONTROL){
      // 正規化モードON
      bNormalize = TRUE;
      return TRUE;
    }

    if(wParam == VK_SHIFT){
      bStick = TRUE;
      return TRUE;
    }

    // なんかキーおされたら中断します
    bCapture = FALSE;
    ::InvalidateRect(hWnd, NULL, FALSE);
    ::DestroyWindow(hWnd);
    break;

  case WM_KEYUP:
    if(wParam == VK_CONTROL){
      bNormalize = FALSE;
      return TRUE;
    }

    if(wParam == VK_SHIFT){
      bStick = FALSE;
      return TRUE;
    }

    // なんかキーおされたら中断します
    bCapture = FALSE;
    ::InvalidateRect(hWnd, NULL, FALSE);
    ::DestroyWindow(hWnd);
    break;

  case WM_ERASEBKGND:
    return FALSE;

  case WM_PAINT:
    //::FillRect(::g_hMemDC, &windowRect, colorBrush2);
    ::FillRect(::g_hMemDC, &windowRect, transparentBrush);

    if(bCapture){
      RECT rect = selected;
      trace(L"selected: %d,%d\n", selected.bottom - selected.top,
        selected.right - selected.left);

      RectangleNormalize(&rect);

      // 描画可能な最小単位よりも大きかったときだけ撮影可能
      int w = abs(rect.right - rect.left);
      int h = abs(rect.bottom - rect.top);

      if(CAPTURE_MIN_WIDTH < w && CAPTURE_MIN_HEIGHT < h){
        ::FillRect(::g_hMemDC, &rect, colorBrush);

        // ウインドウのサイズを描画
        HFONT hOldFont = SelectFont(g_hMemDC, hFont);

        // マウスカーソルの右下に座標を描画する
        POINT pt;
        ::GetCursorPos(&pt);

        //int parentMode = ::SetBkMode(::g_hMemDC, TRANSPARENT);
        ::SelectObject(::g_hMemDC, ::CreateSolidBrush(RGB(255,255,255)));
        ::TextFormatOut(::g_hMemDC, pt.x + 10, pt.y, L"%d,%d",
          rect.right - rect.left,
          rect.bottom - rect.top);

        // fontを戻す
        SelectFont(g_hMemDC, hOldFont);
      }else{
        //::ErrorMessageBox(L"test");
        ::FillRect(::g_hMemDC, &selected, colorBrush2);
      }
    }

    hdc = BeginPaint(hWnd, &ps);
    ::BitBlt(hdc, 0, 0, windowRect.right, windowRect.bottom, ::g_hMemDC, 0, 0, SRCCOPY);
    EndPaint(hWnd, &ps);
    return TRUE;
  case WM_DESTROY:
    ::SelectObject(::g_hMemDC, hOldBitmap);
    SafeDeleteObject(::g_hMemDC);
    SafeDeleteObject(transparentBrush);
    SafeDeleteObject(colorBrush);
    SafeDeleteObject(colorBrush2);
    SafeDeleteObject(hFont);
    //PostQuitMessage(0);
    return TRUE;
  case WM_LBUTTONDOWN:
    if(!bCapture)
    {
      POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      mousePressed = point;
      bCapture = TRUE;
    }
    return TRUE;
  case WM_LBUTTONUP:
    if(bCapture){
      POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ::InvalidateRect(hWnd, NULL, FALSE);

      // 描画可能な最小単位よりも大きかったときだけ撮影可能
      int w = abs(selected.right - selected.left);
      int h = abs(selected.bottom - selected.top);

      // 最小サイズよりもでかかった場合
      if(CAPTURE_MIN_WIDTH < w && CAPTURE_MIN_HEIGHT < h){
        ::InvalidateRect(hWnd, NULL, FALSE);
        ::MessageBeep(MB_ICONASTERISK);

        // 選択枠を消します
        bCapture = FALSE;
        ::NoticeRedraw(hWnd);

        // ノーマライズしたselectedが対象
        // 撮影します
        RECT tmp = selected;
        ::RectangleNormalize(&tmp);
        ::ScreenShotAndUpload(hWnd, L"capture.png", &tmp);

        ::DestroyWindow(hWnd);
        return TRUE;
      }

      ::DestroyWindow(hWnd);
    }
    bCapture = FALSE;
    return TRUE;
  case WM_RBUTTONDOWN:
    bDrag = TRUE;
    ::mousePressedR.x = GET_X_LPARAM(lParam);
    ::mousePressedR.y = GET_Y_LPARAM(lParam);
    lastSelected = selected;
    break;
  case WM_RBUTTONUP:
    bDrag = FALSE;
    // 原点を書き換える
    ::mousePressed.x = ::selected.left;
    ::mousePressed.y = ::selected.top;
    break;

  case WM_MOUSEWHEEL: // 選択領域の拡大・縮小
    {
      int v =  GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 20; // マウスホイールの移動量

      // すべての方向に均等に拡大する
      selected.left += -v;
      selected.top += -v;
      selected.right += v;
      selected.bottom += v;

      // マウス原点もかえないと他と整合性がとれんくなる
      ::mousePressed.x = selected.left;
      ::mousePressed.y = selected.top;

      ::InvalidateRect(hWnd, NULL, FALSE);
    }
    break;

  case WM_MOUSEMOVE:
    if(bDrag){
      POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

      int w = lastSelected.right - lastSelected.left;
      int h = lastSelected.bottom - lastSelected.top;

      // マウス押したポイントから現在の位置への移動量分、left/topを移動
      int x = (point.x - mousePressedR.x);
      int y = (point.y - mousePressedR.y);

      selected.left = lastSelected.left + point.x - mousePressedR.x;
      selected.top = lastSelected.top + point.y - mousePressedR.y;
      selected.right = selected.left + w;
      selected.bottom = selected.top + h;

    }else if(bCapture){
      POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

      selected.left = mousePressed.x;
      selected.top = mousePressed.y;
      selected.right = point.x;
      selected.bottom = point.y;

      // ウインドウ縦横サイズの正規化
      if(bNormalize){
        // 横のサイズと同じだけの縦のサイズにする
        if(selected.left < selected.right){
          if(selected.top < selected.bottom){
            selected.bottom = selected.top + (selected.right - selected.left);
          }else{
            selected.bottom = selected.top - (selected.right - selected.left);
          }
        }else{
          if(selected.top < selected.bottom){
            selected.bottom = selected.top + (selected.left - selected.right);
          }else{
            selected.bottom = selected.top + (selected.right - selected.left);
          }
        }
      }
    }

    // 画面外にウインドウが出ないようにする補正処理
    CorrectRect(&selected, &windowRect);

    // システムウインドウとの吸着処理
    // 移動中にしか吸着処理はしない
    if(bDrag){
      if(bStick){
        // カーソルの位置にウインドウがあればそのウインドウに吸着する
        //POINT pt;
        //::GetCursorPos(&pt);

        // すべてのウインドウのうち、自分自身以外で可視なウインドウから調査
        // カーソルがかぶっているもっとも直近なウインドウを探して自動で大きさを調整する
        //selected = rect;
        StickRect(&selected, &windowRect, 50, 50);
      }
    }
    ::InvalidateRect(hWnd, NULL, FALSE);
    return TRUE;
  case WM_SIZE:
    ::GetClientRect(hWnd, &windowRect);
    return TRUE;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// ===========================================
//	キーコンフィグ系関数
// ===========================================
void SetCurrentKeyConfigToGUI(HWND hWnd, KEYINFO *kup, KEYINFO *kdown)
{
  LPTSTR up		= ::GetKeyInfoString(kup);
  LPTSTR down		= ::GetKeyInfoString(kdown);

  ::SetDlgItemText(hWnd, IDC_EDIT_KEYBIND_ACTIVEWINDOW, up);
  ::SetDlgItemText(hWnd, IDC_EDIT_KEYBIND_DESKTOP, down);

  ::GlobalFree(up);
  ::GlobalFree(down);
}

void SetCurrentKeyConfigToGUI(HWND hWnd)
{
  ::SetCurrentKeyConfigToGUI(hWnd, &::g_activeSSKeyInfo, &::g_desktopSSKeyInfo);
}

// キー設定用、キーフックプロシージャ(not グローバル / グローバルはDLLを利用しなければ行えない)
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
  //nCodeが0未満のときは、CallNextHookExが返した値を返す
  if (nCode < 0)  return CallNextHookEx(g_hKeyConfigHook,nCode,wp,lp);

  if (nCode==HC_ACTION) {
    //キーの遷移状態のビットをチェックして
    //WM_KEYDOWNとWM_KEYUPをDialogに送信する
    if ( IF_KEY_PRESS(lp) ) {
      PostMessage(g_hKeyConfigDlg,WM_KEYDOWN,wp,lp);
      return TRUE;
    }else{
      PostMessage(g_hKeyConfigDlg,WM_KEYUP,wp,lp);
      return TRUE;
    }
  }
  return CallNextHookEx(g_hKeyConfigHook,nCode,wp,lp);
}

INT_PTR CALLBACK DlgKeyConfigProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
  static UINT targetID = -1;
  BYTE keyTbl[256];
  int optKey = 0;

  // 一時格納用バッファ
  static KEYINFO activeSSKeyInfo = {0};
  static KEYINFO desktopSSKeyInfo = {0};

  switch( msg ){
  case WM_INITDIALOG:  // ダイアログボックスが作成されたとき
    ::SetWindowTopMost(g_hKeyConfigDlg); // ウインドウを最前面にします
    ::SetCurrentKeyConfigToGUI(hDlg); // 現在のキー設定をGUI上に反映させます

    // 一時格納用バッファを初期化します
    activeSSKeyInfo = ::g_activeSSKeyInfo;
    desktopSSKeyInfo = ::g_desktopSSKeyInfo;

    // ウインドウのタイトルを規定のものに設定します
    ::SetWindowText(g_hKeyConfigDlg, DLG_KEYCONFIG_PROC_WINDOW_TITLE);
    return TRUE;

  case WM_KEYDOWN:
    if( !::GetKeyboardState((PBYTE)&keyTbl) ){
      ShowLastError();
      exit(-1);
    }

    // 入力された補助キーを判断して代入
    KEYINFO tmp;
    ::ClearKeyInfo(&tmp);
    if( keyTbl[VK_CONTROL] & 0x80 ){
      // wpと一緒だったらwp使えばいいので入力しません
      if(wp != VK_CONTROL)
        tmp.ctrlKey = VK_CONTROL;
    }
    if( keyTbl[VK_SHIFT] & 0x80 ){
      if(wp != VK_SHIFT)
        tmp.shiftKey = VK_SHIFT;
    }
    if( keyTbl[VK_MENU] & 0x80 ){
      if(wp != VK_MENU)
        tmp.altKey = VK_MENU;
    }
    tmp.key = wp;

    // 入力されたキーをUI上に反映させます
    {
      LPTSTR lpKeyConfigBuffer = ::GetKeyInfoString(&tmp);
      ::SetDlgItemText(g_hKeyConfigDlg, targetID, lpKeyConfigBuffer);
      ::GlobalFree(lpKeyConfigBuffer);
    }

    // 現在アクティブに編集してる構造体に情報保存s
    if(targetID == IDC_EDIT_KEYBIND_ACTIVEWINDOW){
      activeSSKeyInfo = tmp;
    }else if(targetID == IDC_EDIT_KEYBIND_DESKTOP){
      desktopSSKeyInfo = tmp;
    }
    return TRUE;

  case WM_KEYUP:
    if(targetID == IDC_EDIT_KEYBIND_ACTIVEWINDOW){
      ::SetDlgItemText(g_hKeyConfigDlg, ID_KEYBIND_ACTIVEWINDOW, DLG_KEYCONFIG_DEFAULT_BUTTON_TITLE);
    }else if(targetID == IDC_EDIT_KEYBIND_DESKTOP){
      ::SetDlgItemText(g_hKeyConfigDlg, ID_KEYBIND_DESKTOP, DLG_KEYCONFIG_DEFAULT_BUTTON_TITLE);
    }

    ::SetWindowText(g_hKeyConfigDlg, DLG_KEYCONFIG_PROC_WINDOW_TITLE);
    if(::g_hKeyConfigHook){
      ::UnhookWindowsHookEx(g_hKeyConfigHook);
    }
    g_hKeyConfigHook = NULL;
    return TRUE;

  case WM_COMMAND:     // ダイアログボックス内の何かが選択されたとき
    switch( LOWORD( wp ) ){
    case IDOK:       // 適用ボタンが選択された
      ::StopHook();

      ::g_activeSSKeyInfo = activeSSKeyInfo;
      ::g_desktopSSKeyInfo = desktopSSKeyInfo;

      RegistKey(g_activeSSKeyInfo, WM_ACTIVEWINDOW_SS);
      RegistKey(g_desktopSSKeyInfo, WM_DESKTOP_SS);
      if(!::StartHook())
        ::ShowLastError();

      EndDialog(g_hKeyConfigDlg, LOWORD(wp));
      ::g_hKeyConfigDlg = NULL;
      return TRUE;

    case IDCANCEL:   // 「キャンセル」ボタンが選択された
      // ダイアログボックスを消す
      EndDialog(g_hKeyConfigDlg, LOWORD(wp));
      ::g_hKeyConfigDlg = NULL;
      return TRUE;

    case ID_KEYBIND_ACTIVEWINDOW:
      g_hKeyConfigHook = ::SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, NULL, GetWindowThreadProcessId(hDlg, NULL));
      targetID = IDC_EDIT_KEYBIND_ACTIVEWINDOW;
      ::SetDlgItemText(g_hKeyConfigDlg, ID_KEYBIND_ACTIVEWINDOW, DLG_KEYCONFIG_ASK_BUTTON_TITLE);
      ::SetWindowText(g_hKeyConfigDlg, DLG_KEYCONFIG_ASK);
      return TRUE;

    case ID_KEYBIND_DESKTOP:
      g_hKeyConfigHook = ::SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, NULL, GetWindowThreadProcessId(hDlg, NULL));
      targetID = IDC_EDIT_KEYBIND_DESKTOP;
      ::SetDlgItemText(g_hKeyConfigDlg, ID_KEYBIND_DESKTOP, DLG_KEYCONFIG_ASK_BUTTON_TITLE);
      ::SetWindowText(g_hKeyConfigDlg, DLG_KEYCONFIG_ASK);
      return TRUE;

    case IDDEFAULT: // デフォルトボタンが押されたとき
      // setup default key config
      ::QuickSetKeyInfo(&activeSSKeyInfo, VK_CONTROL, VK_F9);
      ::QuickSetKeyInfo(&desktopSSKeyInfo, VK_MENU, VK_F9);

      // 現在のキー設定をGUIに反映します
      SetCurrentKeyConfigToGUI(hDlg, &activeSSKeyInfo, &desktopSSKeyInfo);
      return TRUE;
    }
  }

  return FALSE;  // DefWindowProc()ではなく、FALSEを返すこと！
}

// ===========================================
//	メインウインドウ(非表示状態)
// ===========================================
void OnCommand(HWND hWnd, int id, HWND hwndCtl, UINT codeNotify)
{
  switch(id){
  case IDM_EXIT:
    ::SaveConfig();
    ::DestroyWindow(hWnd);
    break;
  case IDM_INSPECT:
    StartInspect();
    break;
  case IDM_CAPTURE:
    StartCapture();
    break;
  case IDM_KEYCONFIG:
    // キー設定ダイアログ表示
    // 常に一つのウインドウだけ表示
    if(::g_hKeyConfigDlg == NULL)
      ::g_hKeyConfigDlg = ::CreateDialog(::g_hInstance, MAKEINTRESOURCE(IDD_KEYCONFIG_DIALOG), hWnd, DlgKeyConfigProc);
    break;
  case IDM_OUTPUT_GYAZO:
    ::g_uploadTarget = IDM_OUTPUT_GYAZO;
    break;
  case IDM_OUTPUT_PREVIEW:
    ::g_uploadTarget = IDM_OUTPUT_PREVIEW;
    break;
  }
}

void OnClose(HWND hWnd)
{
  ::SaveConfig();
  ::DestroyWindow(hWnd);
}

BOOL ShowGyazoIMContextMenu(HWND hWnd, UINT menuId)
{
  HMENU hMenu = ::LoadMenu(NULL, MAKEINTRESOURCE(menuId));
  HMENU hSubMenu = ::GetSubMenu(hMenu, 0);

  POINT point;
  ::GetCursorPos(&point);

  ::SetForegroundWindow(hWnd);

  // モニタが複数あったら、モニタごとの調整メニューを表示するように
  HMENU hView = CreatePopupMenu();
  MENUITEMINFO mii = {0};
  mii.wID = IDM_OUTPUT;
  mii.cbSize = sizeof(MENUITEMINFO);
  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU;
  mii.fType = MFT_STRING;
  mii.hSubMenu = hView;
  mii.dwTypeData = L"出力先";
  InsertMenuItem(hSubMenu, 3, TRUE, &mii);

  // 出力先追加
  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
  mii.dwTypeData = L"プレビュー";
  mii.wID = IDM_OUTPUT_PREVIEW; // 2500 - 2600 reserved for monitors
  InsertMenuItem(hView, 0, TRUE, &mii);

  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
  mii.dwTypeData = L"gyazo.com";
  mii.wID = IDM_OUTPUT_GYAZO; // 2500 - 2600 reserved for monitors
  InsertMenuItem(hView, 1, TRUE, &mii);

  // チェック
  CheckMenuItem(hSubMenu, ::g_uploadTarget, MF_BYCOMMAND | MF_CHECKED);

  ::TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, 0, hWnd, NULL);
  ::PostMessage(hWnd, WM_NULL, 0, 0);
  return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  static UINT taskBarMsg;

  switch(message){
    HANDLE_MSG(hWnd, WM_MOUSEMOVE, OnMouseMove);
    HANDLE_MSG(hWnd, WM_LBUTTONDOWN, OnLButtonDown);
    HANDLE_MSG(hWnd, WM_RBUTTONDOWN, OnRButtonDown);
    HANDLE_MSG(hWnd, WM_DESTROY, OnDestroy);
    HANDLE_MSG(hWnd, WM_COMMAND, OnCommand);
    HANDLE_MSG(hWnd, WM_CLOSE, OnClose);

  case WM_CREATE:
    // デスクトップコンポジションの無効化
    /*
    if( FAILED(::DwmEnableComposition(DWM_EC_DISABLECOMPOSITION)) ){
    ::ShowLastError();
    return FALSE;
    }
    */

    // 設定ファイル読み込み
    LoadConfig();

    // キーボード設定
    ::SetWindowHandle(hWnd);
    ::RegistKey(g_activeSSKeyInfo, WM_ACTIVEWINDOW_SS);
    ::RegistKey(g_desktopSSKeyInfo, WM_DESKTOP_SS);

    if( !::StartHook() )
      ::ShowLastError();

    // タスクトレイ復帰用
    taskBarMsg = RegisterWindowMessage(TEXT("TaskbarCreated"));
    break;

  case WM_ACTIVEWINDOW_SS:
    if( IF_KEY_PRESS(lParam) ){
      ::ScreenShotAndUpload_ActiveWindow(hWnd, SCREENSHOT_FILEPATH);
    }
    break;

  case WM_DESKTOP_SS:
    if( IF_KEY_PRESS(lParam) ){
      ::ScreenShotAndUpload_Desktop(hWnd, SCREENSHOT_FILEPATH);
    }
    break;

  case WM_TASKTRAY:
    switch(lParam){
    case WM_LBUTTONDOWN:
      ::StartCapture();
      break;
    case WM_RBUTTONDOWN:
      ::ShowGyazoIMContextMenu(hWnd, IDR_MENU);
      //::ShowContextMenu(hWnd, IDR_MENU);
      break;
    }
    break;
  }

  // エクスプローラーの再起動時に
  // 自分自身のタスクトレイアイコンを復元
  if(message == taskBarMsg){
    TasktrayAddIcon(g_hInstance, WM_TASKTRAY, ID_TASKTRAY, IDI_MAIN, S_TASKTRAY_TIPS, hWnd);
    return 0;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  MSG msg;

  MyRegisterClass(hInstance);

  // 多重起動防止
	CMutex mutex;
	try{
		mutex.createMutex(MUTEX_NAME);
	}catch(std::exception e){
		::ErrorMessageBox(L"多重起動です");
		exit(0);
  }

  // アプリケーションの初期化を実行します:
  if (!InitInstance (hInstance, nCmdShow)){
    return FALSE;
  }

  // メイン メッセージ ループ:
  while (GetMessage(&msg, NULL, 0, 0)){
    if (!TranslateAccelerator(msg.hwnd, NULL, &msg)){
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return (int) msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  //wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.style = 0;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  RegisterClassEx(&wcex);

  wcex.style = 0;
  //wcex.lpfnWndProc = LayerWndProc;
  wcex.lpfnWndProc = ::LayerWndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = ::layer1WindowClass;
  wcex.hIconSm = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  RegisterClassEx(&wcex);

  wcex.style = 0;
  //wcex.lpfnWndProc = SelectedAreaWndProc;
  wcex.lpfnWndProc = ::SelectedAreaWndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = ::layer2WindowClass;
  wcex.hIconSm = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
  HWND hWnd;
  g_hInstance = hInstance; // グローバル変数にインスタンス処理を格納します

  hWnd = CreateWindowEx(0, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
  g_hWnd = hWnd;

  if(!hWnd){
    ::ShowLastError();
    return FALSE;
  }

  // アイコンの設定
  TasktrayAddIcon(g_hInstance, WM_TASKTRAY, ID_TASKTRAY, IDI_MAIN, S_TASKTRAY_TIPS, hWnd);

  ShowWindow(hWnd, SW_HIDE);
  UpdateWindow(hWnd);
  return TRUE;
}

