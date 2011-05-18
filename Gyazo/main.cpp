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

#define MUTEX_NAME L"Gyazo"

HINSTANCE g_hInstance = NULL;
TCHAR szWindowClass[] = L"Gyazo";
TCHAR szTitle[] = L"Gyazo";
HWND g_hWnd = NULL;
HWND g_hSelectedArea = NULL;
HWND oldHWND = NULL;
BOOL bStartCapture = FALSE;
HHOOK g_hook = NULL;

// クライアントからスクリーン座標に変換
// マウスクリックした場所(始点)
POINT mousePressedPt = {0};

// マウスを離した場所(終点)
POINT mouseReleasedPt = {0};

// マウスによって選択されている領域
RECT mouseSelectedArea = {0};

// ドラッグ中状態管理
BOOL bDrag = FALSE; 

// デバッグとして保存先をDesktopにする
// ファイル名は{日付時間}.png
#define DEBUG_LOCAL_SAVE

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void TasktrayAddIcon(HINSTANCE hInstance, HWND hWnd)
{
#define WM_TASKTRAY (WM_APP + 1)
#define ID_TASKTRAY 1
#define S_TASKTRAY_TIPS L"Gyazo"
	NOTIFYICONDATA nid;
	nid.cbSize           = sizeof( NOTIFYICONDATA );
	nid.uFlags           = (NIF_ICON|NIF_MESSAGE|NIF_TIP);
	nid.hWnd             = hWnd;           // ウインドウ・ハンドル
	nid.hIcon            = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));          // アイコン・ハンドル
	nid.uID              = ID_TASKTRAY; 	// アイコン識別子の定数
	nid.uCallbackMessage = WM_TASKTRAY;    // 通知メッセージの定数
	lstrcpy( nid.szTip, S_TASKTRAY_TIPS );  // チップヘルプの文字列

	// アイコンの変更
	if( !Shell_NotifyIcon( NIM_ADD, &nid ) )
		::ShowLastError();
}

void TasktrayModifyIcon(HINSTANCE hInstance, HWND hWnd, UINT icon)
{
	NOTIFYICONDATA nid;
	nid.cbSize           = sizeof( NOTIFYICONDATA );
	nid.uFlags           = (NIF_ICON|NIF_MESSAGE|NIF_TIP);
	nid.hWnd             = hWnd;           // ウインドウ・ハンドル
	nid.hIcon            = ::LoadIcon(hInstance, MAKEINTRESOURCE(icon));          // アイコン・ハンドル
	nid.uID              = ID_TASKTRAY; 	// アイコン識別子の定数
	nid.uCallbackMessage = WM_TASKTRAY;    // 通知メッセージの定数
	lstrcpy( nid.szTip, S_TASKTRAY_TIPS );  // チップヘルプの文字列

	if( !::Shell_NotifyIcon(NIM_MODIFY, &nid) )
		::ShowLastError();
}

void TasktrayDeleteIcon(HWND hWnd)
{
	NOTIFYICONDATA nid; 
	nid.cbSize = sizeof(NOTIFYICONDATA); 
	nid.hWnd = hWnd;				// メインウィンドウハンドル
	nid.uID = ID_TASKTRAY;			// コントロールID
	
	if( !::Shell_NotifyIcon(NIM_DELETE, &nid) )
		::ShowLastError();
}

void NoticeRedraw(HWND hWnd)
{
	::InvalidateRect(hWnd, NULL, FALSE);
	::UpdateWindow(hWnd);
	::RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	::SendMessage(hWnd, WM_PAINT, 0, 0);
}

// 指定されたウインドウを強調します
BOOL HighlightWindow(HWND hWnd)
{
	HDC hdc = ::GetWindowDC(hWnd);
	if(hdc == NULL){
		return FALSE;
	}
	HGDIOBJ hPrevPen = ::SelectObject(hdc, CreatePen (PS_SOLID, 3, RGB(255, 0, 0)));
	HGDIOBJ hPrevBrush = ::SelectObject(hdc, ::GetStockObject(HOLLOW_BRUSH));
	
	RECT rect;
	::GetWindowRect(hWnd, &rect);
	::Rectangle(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);

	::SelectObject(hdc, hPrevPen);
	::SelectObject(hdc, hPrevBrush);

	::ReleaseDC(hWnd, hdc);
	return TRUE;
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
	}
	bStartCapture = FALSE;
	return TRUE;
}


LRESULT CALLBACK SelectedAreaWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message){
	case WM_PAINT:
		// 選択領域全体を塗りつぶすと同時に
		// 右下に大きさを表示します
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);

		// 一旦全部クライアント領域を初期化
		RECT rect;
		::GetClientRect(hWnd, &rect);
		::FillRectBrush(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, RGB(255,0,0));

		if(bDrag){
			trace(L"SelectedArea: WM_PAINT\n");
			rect.left = mouseSelectedArea.left;
			rect.top = mouseSelectedArea.top;
			rect.right = mouseSelectedArea.right;
			rect.bottom = mouseSelectedArea.bottom;

			HFONT hFont = CreateFont(18,    //フォント高さ
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
				L"Tahoma");    //書体名

			HFONT hOldFont = SelectFont(hdc, hFont);

			// 枠付きの四角形を描画
			HBRUSH hBrush = ::CreateSolidBrush(RGB(100,100,100));
			::SelectObject(hdc, hBrush);
			HPEN hPen = ::CreatePen(PS_DASH, 1, RGB(255,255,255));
			::SelectObject(hdc, hPen);
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

			SelectFont(hdc, hOldFont);
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

	// ただの透明なウインドウ
	// マウスイベントを受け取ったりします
	HWND hLayerWnd = CreateWindowEx(
		WS_EX_LAYERED,
		L"Layerd", L"Layerd", WS_POPUP,
		rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, g_hInstance, NULL);
	if(!hLayerWnd){
		::ShowLastError();
		return FALSE;
	}
	::SetForegroundWindow(hLayerWnd);
	SetLayeredWindowAttributes(hLayerWnd, 0, 1, LWA_ALPHA);

	// 選択領域を描画するためだけの透明ウインドウ
	HWND hSelectedArea = CreateWindowEx(
		WS_EX_LAYERED | WS_EX_TRANSPARENT,
		L"Layerd2", L"Layerd2", WS_POPUP,
		0, 0, rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, g_hInstance, NULL);
	::SetForegroundWindow(hSelectedArea);
	SetLayeredWindowAttributes(hSelectedArea, RGB(255,0,0), 100, LWA_COLORKEY | LWA_ALPHA);

	if(!hSelectedArea){
		::ShowLastError();
		return FALSE;
	}
	g_hSelectedArea = hSelectedArea;

	::ShowWindow(hSelectedArea, SW_SHOW);
	::UpdateWindow(hSelectedArea);
	
	::ShowWindow(hLayerWnd, SW_SHOW);
	::UpdateWindow(hLayerWnd);
	return FALSE;
}

BOOL StopCapture()
{
	return FALSE;
}

void OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags)
{
	if(::bStartCapture)
	{
		POINT pt = {x, y};
		trace(L"mouse: %d,%d\n", pt.x, pt.y);

		HWND h = ::WindowFromPoint(pt);
		if( h ){
			if(oldHWND != h){
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
		POINT pt;
		::GetCursorPos(&pt);
		HWND h = ::WindowFromPoint(pt);

		RECT rect;
		::GetWindowRect(h, &rect);
		::NoticeRedraw(oldHWND);
		::NoticeRedraw(h);

		StopInspect();

		// 選択領域を削除するコードをここに入れる
		
		try{
			Screenshot::ScreenshotDesktop(L"inspect.png", &rect);
			Gyazo *g = new Gyazo();
			g->UploadFileAndOpenURL(hWnd, L"screenshot.png");
			delete g;
		}catch(exception e){
			::ErrorMessageBox(L"%s", e);
		}
		::MessageBeep(MB_ICONASTERISK);
	}
}

void OnRButtonDown(HWND hWnd, BOOL isDoubleClick, int x, int y, UINT keyFlags)
{
	::NoticeRedraw(oldHWND);
	::MessageBeep(MB_ICONASTERISK);
	::StopInspect();
}

void OnDestroy(HWND hWnd)
{
	::StopInspect();
	::TasktrayDeleteIcon(hWnd);
	::PostQuitMessage(0);
}

void OnCommand(HWND hWnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch(id){
	case IDM_EXIT:
		::DestroyWindow(hWnd);
		break;
	case IDM_INSPECT:
		StartInspect();
		break;
	case IDM_CAPTURE:
		StartCapture();
		break;
	}
}

void OnClose(HWND hWnd)
{
	::DestroyWindow(hWnd);
}

#define IF_KEY_PRESS(lp) ((lp & (1 << 31)) == 0)
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message){
		HANDLE_MSG(hWnd, WM_MOUSEMOVE, OnMouseMove);
		HANDLE_MSG(hWnd, WM_LBUTTONDOWN, OnLButtonDown);
		HANDLE_MSG(hWnd, WM_RBUTTONDOWN, OnRButtonDown);
		HANDLE_MSG(hWnd, WM_DESTROY, OnDestroy);
		HANDLE_MSG(hWnd, WM_COMMAND, OnCommand);
		HANDLE_MSG(hWnd, WM_CLOSE, OnClose);

	case WM_CREATE:
		// キーボード設定
		::SetWindowHandle(hWnd);
		
		KEYINFO keyInfo;
		::ClearKeyInfo(&keyInfo);
		keyInfo.key = VK_F9;
		::RegistKey(keyInfo, WM_APP + 2);

		if( !::StartHook() )
			::ShowLastError();
		break;

	case WM_APP + 2:
		if( IF_KEY_PRESS(lParam) ){
			// キーが押された時点でのアクティブウインドウを取得する
			HWND activeWindow = ::GetForegroundWindow();

			// アクティブウインドウをスクリーンショット
			RECT rect;
			::GetWindowRect(activeWindow, &rect);
			
			try{
				::Screenshot::ScreenshotDesktop(L"activewindow.png", &rect);
				::MessageBeep(MB_ICONASTERISK); // 撮影音をこの時点で出す

				Gyazo *g = new Gyazo();
				g->UploadFileAndOpenURL(hWnd, L"activewindow.png");
				delete g;
			}catch(exception e){
				::ErrorMessageBox(L"%s", e);
			}

			::MessageBeep(MB_ICONINFORMATION); // 投稿音をこの時点で出す
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

		::InvalidateRect(hWnd, NULL, FALSE);
		::InvalidateRect(::g_hSelectedArea, NULL, FALSE);
	}else{
		::InvalidateRect(hWnd, NULL, FALSE);
		::InvalidateRect(::g_hSelectedArea, NULL, FALSE);
	}
}

void Layer_OnMouseLButtonDown(HWND hWnd, BOOL isDoubleClick, int x, int y, UINT keyFlags)
{
	POINT pt = {x, y};
	::ClientToScreen(hWnd, &pt);

	bDrag = TRUE;
	mousePressedPt.x = pt.x;
	mousePressedPt.y = pt.y;
	trace(L"mouse ldown: %d,%d\n", x, y);
}

void Layer_OnMouseLButtonUp(HWND hWnd, int x, int y, UINT keyFlags)
{
	POINT pt = {x, y};
	::ClientToScreen(hWnd, &pt);

	bDrag = FALSE;
	mouseReleasedPt.x = pt.x;
	mouseReleasedPt.y = pt.y;
	trace(L"mouse lup: %d,%d\n", x, y);
	::DestroyWindow(hWnd);
	::DestroyWindow(::g_hSelectedArea);

	// RECT構造体の範囲をキャプチャ
	RECT rect = ::mouseSelectedArea;
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	// 常に左上基点の構造体に変換
	if(width < 0){
		// 左右逆
		int tmp = rect.left;
		rect.left = rect.right;
		rect.right = tmp;
	}
	if(height < 0){
		int tmp = rect.top;
		rect.top = rect.bottom;
		rect.bottom = tmp;
	}
	trace(L"rect: %d,%d (%d,%d)\n", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
	::SendMessage(g_hSelectedArea, WM_PAINT, 0, 0);

	// 選択領域を削除するコードをここに入れる

	try{
		// 指定の範囲をキャプチャ
		::Screenshot::ScreenshotDesktop(L"capture.png", &rect);
		Gyazo *g = new Gyazo();
		g->UploadFileAndOpenURL(hWnd, L"capture.png");
		delete g;
	}catch(exception e){
		::ErrorMessageBox(L"%s", e);
	}
	::MessageBeep(MB_ICONASTERISK);
}

void Layer_OnPaint(HWND hWnd)
{
	if(bDrag){
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);

		/*
		if(false){
			int width = mouseSelectedArea.right - mouseSelectedArea.left;
			int height = mouseSelectedArea.bottom - mouseSelectedArea.top;

			::FillRectBrush(hdc, mouseSelectedArea.left, mouseSelectedArea.top,
				width, height, RGB(255,0,0));
				//mouseSelectedArea.right - mouseSelectedArea.left,
				//mouseSelectedArea.bottom - mouseSelectedArea.top, RGB(255,0,0));
			trace(L"drawing: %d,%d\n", width, height);
		}*/
	
		RECT rect;
		::GetWindowRect(hWnd, &rect);
		::FillRectBrush(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, RGB(255,255,255));

		int width = mouseSelectedArea.right - mouseSelectedArea.left;
		int height = mouseSelectedArea.bottom - mouseSelectedArea.top;

		::FillRectBrush(hdc, mouseSelectedArea.left, mouseSelectedArea.top, width, height, RGB(0,0,0));

		::EndPaint(hWnd, &ps);
	}
}

LRESULT CALLBACK LayerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message){
		HANDLE_MSG(hWnd, WM_MOUSEMOVE, Layer_OnMouseMove);
		HANDLE_MSG(hWnd, WM_LBUTTONDOWN, Layer_OnMouseLButtonDown);
		HANDLE_MSG(hWnd, WM_LBUTTONUP, Layer_OnMouseLButtonUp);
		HANDLE_MSG(hWnd, WM_PAINT, Layer_OnPaint);
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
	wcex.style = CS_DBLCLKS;
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

	wcex.style = CS_DBLCLKS;
	wcex.lpfnWndProc = LayerWndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = L"Layerd";
	wcex.hIconSm = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
	RegisterClassEx(&wcex);
	
	wcex.style = CS_DBLCLKS;
	wcex.lpfnWndProc = SelectedAreaWndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = L"Layerd2";
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
	TasktrayAddIcon(g_hInstance, hWnd);

	ShowWindow(hWnd, SW_HIDE);
	UpdateWindow(hWnd);
	return TRUE;
}

