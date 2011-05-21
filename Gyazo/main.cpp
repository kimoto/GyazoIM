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
BOOL bDrag = FALSE; 

LPCTSTR layer1WindowClass = L"GyazoIM_Layer1";
LPCTSTR layer2WindowClass = L"GyazoIM_Layer2";

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

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

// 指定されたウインドウを強調します
BOOL HighlightWindow(HWND hWnd)
{
	HDC hdc = ::GetWindowDC(hWnd);
	if(hdc == NULL){
		return FALSE;
	}

	HPEN hPen = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
	HBRUSH hBrush = (HBRUSH)::GetStockObject(HOLLOW_BRUSH);

	HGDIOBJ hPrevPen = ::SelectObject(hdc, hPen);
	HGDIOBJ hPrevBrush = ::SelectObject(hdc, hBrush);
	
	RECT rect;
	::GetWindowRect(hWnd, &rect);
	::Rectangle(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);

	::SelectObject(hdc, hPrevPen);
	::SelectObject(hdc, hPrevBrush);

	::DeleteObject(hPen);
	::DeleteObject(hBrush);

	::ReleaseDC(hWnd, hdc);
	return TRUE;
}

// Desktopの指定した範囲をキャプチャしてアップロード
void ScreenShotAndUpload(HWND forErrorMessage, LPCTSTR path, RECT *rect)
{
	try{
		::Screenshot::ScreenshotDesktop(path, rect);
		::MessageBeep(MB_ICONASTERISK); // 撮影音をこの時点で出す

		Gyazo *g = new Gyazo();
		g->UploadFileAndOpenURL(forErrorMessage, path);
		delete g;
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

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp)
{
	if( nCode < 0 ) //nCodeが負、HC_NOREMOVEの時は何もしない
		return CallNextHookEx( g_hook, nCode, wp, lp );

	if( nCode == HC_ACTION){
		MSLLHOOKSTRUCT *msg = (MSLLHOOKSTRUCT *)lp;
		if( wp == WM_MOUSEMOVE || wp == WM_LBUTTONDOWN ){
			::PostMessage(g_hWnd, wp, 0, MAKELPARAM(msg->pt.x, msg->pt.y));
		}else if( wp == WM_RBUTTONDOWN ){
			return TRUE;
		}else if( wp == WM_RBUTTONUP ){
			::PostMessage(g_hWnd, WM_RBUTTONDOWN, 0, MAKELPARAM(msg->pt.x, msg->pt.y));
			return TRUE;
		}
	}
	return CallNextHookEx(g_hook, nCode, 0, lp);
}

// インスペクトモード開始
BOOL StartInspect()
{
	g_hook = ::SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, g_hInstance, 0);
	if(!g_hook){
		::ShowLastError();
		return FALSE;
	}
	bStartCapture = TRUE;
	return TRUE;
}

// インスペクトモード無効
BOOL StopInspect()
{
	if(g_hook){
		if( !::UnhookWindowsHookEx(g_hook) ){
			::ShowLastError();
			return FALSE;
		}
		g_hook = NULL;
	}
	bStartCapture = FALSE;
	return TRUE;
}

LRESULT CALLBACK SelectedAreaWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HFONT selectedAreaFont = NULL;

	switch(message){
	case WM_CREATE:
		selectedAreaFont = CreateFont(18,    //フォント高さ
			0,                    //文字幅
			0,                    //テキストの角度
			0,                    //ベースラインとｘ軸との角度
			FW_REGULAR,            //フォントの重さ（太さ）
			FALSE,                //イタリック体
			FALSE,                //アンダーライン
			FALSE,                //打ち消し線
			ANSI_CHARSET,    //文字セット
			OUT_DEFAULT_PRECIS,    //出力精度
			CLIP_DEFAULT_PRECIS,//クリッピング精度
			PROOF_QUALITY,        //出力品質
			FIXED_PITCH | FF_MODERN,//ピッチとファミリー
			CURSOR_FONT);    //書体名
		break;
	case WM_CLOSE:
	case WM_DESTROY:
		if(selectedAreaFont)
			::DeleteObject(selectedAreaFont);
		break;
	case WM_PAINT:
		return TRUE;
		// 選択領域全体を塗りつぶすと同時に
		// 右下に大きさを表示します
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);

		// 一旦全部クライアント領域を初期化
		RECT rect;
		::GetWindowRect(hWnd, &rect);
		::FillRectBrush(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, RGB(255,0,0));

		if(bDrag){
			HFONT hOldFont = SelectFont(hdc, selectedAreaFont);

			// 枠付きの四角形を描画
			HBRUSH hBrush = ::CreateSolidBrush(RGB(100,100,100));
			HBRUSH hOldBrush = (HBRUSH)::SelectObject(hdc, hBrush);

			HPEN hPen = ::CreatePen(PS_DASH, 1, RGB(255,255,255));
			HPEN hOldPen = (HPEN)::SelectObject(hdc, hPen);
			
			rect = mouseSelectedArea;
			::Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);

			// 取り込み範囲表示
			::SetBkMode(hdc, TRANSPARENT);
			TCHAR buf[256];
			::wsprintf(buf, L"%d,%d", rect.right - rect.left, rect.bottom - rect.top);

			// 影の描画
			::SetTextColor(hdc, RGB(0,0,0));
			::TextOut(hdc, rect.left+2, rect.top+2, buf, lstrlen(buf));

			// 本体の描画
			::SetTextColor(hdc, RGB(255,255,255));
			::TextOut(hdc, rect.left, rect.top, buf, lstrlen(buf));

			// 戻す
			SelectFont(hdc, hOldFont);
			SelectPen(hdc, hOldPen);
			SelectBrush(hdc, hOldBrush);

			// 使用したオブジェクトの破棄
			::DeleteObject(hBrush);
			::DeleteObject(hPen);
		}

		::EndPaint(hWnd, &ps);
		::ReleaseDC(hWnd, hdc);
		break;
	}
	return ::DefWindowProc(hWnd, message, wParam, lParam);
}

// キャプチャモード開始
BOOL StartCapture()
{
	RECT rect;
	::GetWindowRect(::GetDesktopWindow(), &rect);
	
	// get full screen size
	int w,h,x,y;
	w = rect.right - rect.left;
	h = rect.bottom - rect.top;
	x = rect.left;
	y = rect.top;
	
	// ただの透明なウインドウ
	// マウスイベントを受け取ったりします
	HWND hLayerWnd = CreateWindowEx(
		WS_EX_TRANSPARENT | WS_EX_TOPMOST,
		::layer1WindowClass, ::layer1WindowClass, WS_POPUP,
		x, y, w, h, NULL, NULL, g_hInstance, NULL);
	::SetForegroundWindow(hLayerWnd);

	// 選択領域を描画するためだけの透明ウインドウ
	HWND hSelectedArea = CreateWindowEx(
		WS_EX_LAYERED,
		::layer2WindowClass, ::layer2WindowClass, WS_POPUP,
		x, y, w, h, NULL, NULL, g_hInstance, NULL);
	//SetLayeredWindowAttributes(hSelectedArea, RGB(255,0,0), 100, LWA_COLORKEY | LWA_ALPHA);	// 赤単色を
	SetLayeredWindowAttributes(hSelectedArea, RGB(255,0,0), 100, LWA_ALPHA | LWA_COLORKEY);	// 赤単色を
	g_hSelectedArea = hSelectedArea;
	
	// ウインドウの順列を設定
	// layer1(mouse event) -> layer2 -> desktopって感じ
	::SetForegroundWindow(hSelectedArea);
	::SetForegroundWindow(hLayerWnd);
	
	// ウインドウを表示します
	::ShowWindow(hLayerWnd, SW_SHOW);
	::UpdateWindow(hLayerWnd);

	::ShowWindow(hSelectedArea, SW_SHOW);
	::UpdateWindow(hSelectedArea);
	return FALSE;
}

BOOL StopCapture()
{
	return FALSE;
}

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
	}
}

void OnClose(HWND hWnd)
{
	::SaveConfig();
	::DestroyWindow(hWnd);
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
		if( FAILED(::DwmEnableComposition(DWM_EC_DISABLECOMPOSITION)) ){
			::ShowLastError();
			return FALSE;
		}

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
		case WM_RBUTTONDOWN:
			::ShowContextMenu(hWnd, IDR_MENU);
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

void Layer_OnMouseMove(HWND hWnd, int x, int y, UINT keyFlags)
{
	if(bDrag){
		POINT pt = {x, y};
		::ClientToScreen(hWnd, &pt);
		trace(L"dragging: %d,%d\n", pt.x, pt.y);

		// 選択領域を描画するために、選択領域の範囲を計算
		mouseSelectedArea.left = mousePressedPt.x;
		mouseSelectedArea.top = mousePressedPt.y;
		mouseSelectedArea.right = x;
		mouseSelectedArea.bottom = y;

		trace(L"selected area: w:%d,h:%d\n", mouseSelectedArea.right - mouseSelectedArea.left,
			mouseSelectedArea.bottom - mouseSelectedArea.top);

		::NoticeRedraw(hWnd);
		::NoticeRedraw(::g_hSelectedArea);
	}
}

void Layer_OnMouseLButtonDown(HWND hWnd, BOOL isDoubleClick, int x, int y, UINT keyFlags)
{
	POINT pt = {x, y};
	::ClientToScreen(hWnd, &pt);

	bDrag = TRUE;
	mousePressedPt = pt;
	trace(L"mouse ldown: %d,%d\n", x, y);
}

void Layer_OnMouseLButtonUp(HWND hWnd, int x, int y, UINT keyFlags)
{
	POINT pt = {x, y};
	::ClientToScreen(hWnd, &pt);

	bDrag = FALSE;
	::mouseReleasedPt = pt;
	trace(L"mouse lup: %d,%d\n", x, y);

	::DestroyWindow(hWnd);
	::DestroyWindow(::g_hSelectedArea);
		
	// 左上基点の構造体に正規化します
	RECT rect = ::mouseSelectedArea; // 作業用に複製
	RectangleNormalize(&rect);

	// 選択領域の再描画
	::SendMessage(g_hSelectedArea, WM_PAINT, 0, 0);

	ScreenShotAndUpload(hWnd, L"capture.png", &rect);
}

void Layer_OnPaint(HWND hWnd)
{
	/*
	PAINTSTRUCT ps;
	HDC hdc = ::BeginPaint(hWnd, &ps);
	
	RECT rect;
	::GetClientRect(hWnd, &rect);
	HBRUSH brush = ::CreateSolidBrush(RGB(255,0,0));
	::FillRect(hdc, &rect, brush);
	::DeleteObject(brush);

	::EndPaint(hWnd, &ps);
	*/

	if(bDrag){
		trace(L"layer_onPaint\n");
		/*
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);

		RECT rect;
		::GetWindowRect(hWnd, &rect);
		::FillRectBrush(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, RGB(255,255,255));
		::FillRectBrush(hdc, mouseSelectedArea.left, mouseSelectedArea.top,
			mouseSelectedArea.right - mouseSelectedArea.left,
			mouseSelectedArea.bottom - mouseSelectedArea.top, RGB(0,0,0));

		::EndPaint(hWnd, &ps);
					*/
	}
}

LRESULT CALLBACK LayerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message){
		HANDLE_MSG(hWnd, WM_MOUSEMOVE, Layer_OnMouseMove);
		HANDLE_MSG(hWnd, WM_LBUTTONDOWN, Layer_OnMouseLButtonDown);
		HANDLE_MSG(hWnd, WM_LBUTTONUP, Layer_OnMouseLButtonUp);
		HANDLE_MSG(hWnd, WM_PAINT, Layer_OnPaint);

	case WM_ERASEBKGND: // 背景の初期化のための描画を無効化、これによって一瞬ちらつかなくなる
		return FALSE;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// TODO: ここにコードを挿入してください。
	MSG msg;

	// グローバル文字列を初期化しています。
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
	wcex.lpfnWndProc = LayerWndProc;
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
	wcex.lpfnWndProc = SelectedAreaWndProc;
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

