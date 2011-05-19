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

// �f�o�b�O�Ƃ��ĕۑ����Desktop�ɂ���
// �t�@�C������{���t����}.png
#define DEBUG_LOCAL_SAVE
#define WM_TASKTRAY (WM_APP + 1)
#define ID_TASKTRAY 1
#define S_TASKTRAY_TIPS L"Gyazo"
#define MUTEX_NAME L"GyazoIM"
#define IF_KEY_PRESS(lp) ((lp & (1 << 31)) == 0)

HINSTANCE g_hInstance = NULL;
TCHAR szWindowClass[] = L"GyazoIM";
TCHAR szTitle[] = L"GyazoIM";
HWND g_hWnd = NULL;
HWND g_hSelectedArea = NULL;
HWND oldHWND = NULL;
BOOL bStartCapture = FALSE;
HHOOK g_hook = NULL;

// �N���C�A���g����X�N���[�����W�ɕϊ�
POINT mousePressedPt = {0};		// �}�E�X�N���b�N�����ꏊ(�n�_)
POINT mouseReleasedPt = {0};	// �}�E�X�𗣂����ꏊ(�I�_)
RECT mouseSelectedArea = {0};	// �}�E�X�ɂ���đI������Ă���̈�

// �h���b�O����ԊǗ�
BOOL bDrag = FALSE; 

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void NoticeRedraw(HWND hWnd)
{
	::InvalidateRect(hWnd, NULL, FALSE);
	::UpdateWindow(hWnd);
	::RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	::SendMessage(hWnd, WM_PAINT, 0, 0);
}

// �w�肳�ꂽ�E�C���h�E���������܂�
BOOL HighlightWindow(HWND hWnd)
{
	HDC hdc = ::GetWindowDC(hWnd);
	if(hdc == NULL){
		return FALSE;
	}

	HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
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

// Desktop�̎w�肵���͈͂��L���v�`�����ăA�b�v���[�h
void ScreenShotAndUpload(HWND forErrorMessage, LPCTSTR path, RECT *rect)
{
	try{
		::Screenshot::ScreenshotDesktop(path, rect);
		::MessageBeep(MB_ICONASTERISK); // �B�e�������̎��_�ŏo��

		Gyazo *g = new Gyazo();
		g->UploadFileAndOpenURL(forErrorMessage, path);
		delete g;
	}catch(exception e){
		::ErrorMessageBox(L"%s", e);
	}

	::MessageBeep(MB_ICONASTERISK); // ���e�������̎��_�ŏo��
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp)
{
	if( nCode < 0 ) //nCode�����AHC_NOREMOVE�̎��͉������Ȃ�
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

// �C���X�y�N�g���[�h�J�n
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

// �C���X�y�N�g���[�h����
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
	static HFONT selectedAreaFont = NULL;

	switch(message){
	case WM_CREATE:
		selectedAreaFont = CreateFont(18,    //�t�H���g����
			0,                    //������
			0,                    //�e�L�X�g�̊p�x
			0,                    //�x�[�X���C���Ƃ����Ƃ̊p�x
			FW_REGULAR,            //�t�H���g�̏d���i�����j
			FALSE,                //�C�^���b�N��
			FALSE,                //�A���_�[���C��
			FALSE,                //�ł�������
			ANSI_CHARSET,    //�����Z�b�g
			OUT_DEFAULT_PRECIS,    //�o�͐��x
			CLIP_DEFAULT_PRECIS,//�N���b�s���O���x
			PROOF_QUALITY,        //�o�͕i��
			FIXED_PITCH | FF_MODERN,//�s�b�`�ƃt�@�~���[
			L"Tahoma");    //���̖�
		break;
	case WM_CLOSE:
	case WM_DESTROY:
		if(selectedAreaFont)
			::DeleteObject(selectedAreaFont);
		break;
	case WM_PAINT:
		// �I��̈�S�̂�h��Ԃ��Ɠ�����
		// �E���ɑ傫����\�����܂�
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);

		// ��U�S���N���C�A���g�̈��������
		RECT rect;
		::GetClientRect(hWnd, &rect);
		::FillRectBrush(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, RGB(255,0,0));

		if(bDrag){
			HFONT hOldFont = SelectFont(hdc, selectedAreaFont);

			// �g�t���̎l�p�`��`��
			HBRUSH hBrush = ::CreateSolidBrush(RGB(100,100,100));
			HBRUSH hOldBrush = (HBRUSH)::SelectObject(hdc, hBrush);

			HPEN hPen = ::CreatePen(PS_DASH, 1, RGB(255,255,255));
			HPEN hOldPen = (HPEN)::SelectObject(hdc, hPen);
			
			rect = mouseSelectedArea;
			::Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);

			// ��荞�ݔ͈͕\��
			::SetBkMode(hdc, TRANSPARENT);
			TCHAR buf[256];
			::wsprintf(buf, L"%d,%d", rect.right - rect.left, rect.bottom - rect.top);

			// �e�̕`��
			::SetTextColor(hdc, RGB(0,0,0));
			::TextOut(hdc, rect.left+2, rect.top+2, buf, lstrlen(buf));

			// �{�̂̕`��
			::SetTextColor(hdc, RGB(255,255,255));
			::TextOut(hdc, rect.left, rect.top, buf, lstrlen(buf));

			// �߂�
			SelectFont(hdc, hOldFont);
			SelectPen(hdc, hOldPen);
			SelectBrush(hdc, hOldBrush);

			// �g�p�����I�u�W�F�N�g�̔j��
			::DeleteObject(hBrush);
			::DeleteObject(hPen);
		}

		::EndPaint(hWnd, &ps);
		::ReleaseDC(hWnd, hdc);
		break;
	}
	return ::DefWindowProc(hWnd, message, wParam, lParam);
}

// �L���v�`�����[�h�J�n
BOOL StartCapture()
{
	RECT rect;
	::GetWindowRect(::GetDesktopWindow(), &rect);

	// �����̓����ȃE�C���h�E
	// �}�E�X�C�x���g���󂯎�����肵�܂�
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
	::SetLayeredWindowAttributes(hLayerWnd, 0, 1, LWA_ALPHA);

	// �I��̈��`�悷�邽�߂����̓����E�C���h�E
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

HWND WindowFromCursorPos()
{
	POINT pt;
	::GetCursorPos(&pt);
	return ::WindowFromPoint(pt);
}

void OnLButtonDown(HWND hWnd, BOOL isDoubleClick, int x, int y, UINT keyFlags)
{
	if( bStartCapture ){
		HWND h = WindowFromCursorPos();
		if(h == NULL){
			::ErrorMessageBox(L"�}�E�X�J�[�\�����ɃE�C���h�E��������܂���");
			return;
		}

		::NoticeRedraw(oldHWND);
		::NoticeRedraw(h);
		StopInspect();

		// �I��̈���폜����R�[�h�������ɓ����
		RECT rect;
		::GetWindowRect(h, &rect);
		::ScreenShotAndUpload(hWnd, L"inspect.png", &rect);
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
	::TasktrayDeleteIcon(hWnd, ID_TASKTRAY);
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
		// �L�[�{�[�h�ݒ�
		::SetWindowHandle(hWnd);
		
		KEYINFO keyInfo;
		::ClearKeyInfo(&keyInfo);
		keyInfo.key = VK_F9;
		::RegistKey(keyInfo, WM_APP + 2);

		if( !::StartHook() )
			::ShowLastError();

		// �^�X�N�g���C���A�p
		taskBarMsg = RegisterWindowMessage(TEXT("TaskbarCreated"));
		break;

	case WM_APP + 2:
		if( IF_KEY_PRESS(lParam) ){
			// �L�[�������ꂽ���_�ł̃A�N�e�B�u�E�C���h�E���擾����
			RECT rect;
			::GetWindowRect(::GetForegroundWindow(), &rect);
			ScreenShotAndUpload(hWnd, L"screenshot.png", &rect);
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

	// �G�N�X�v���[���[�̍ċN������
	// �������g�̃^�X�N�g���C�A�C�R���𕜌�
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

		// �I��̈��`�悷�邽�߂ɁA�I��̈�͈̔͂��v�Z
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
	mousePressedPt.x = pt.x;
	mousePressedPt.y = pt.y;
	trace(L"mouse ldown: %d,%d\n", x, y);
}

void RectangleNormalize(RECT *rect)
{
	// ��ɍ����_�̍\���̂ɕϊ�
	if(rect->right - rect->left < 0){
		// ���E�t
		int tmp = rect->left;
		rect->left = rect->right;
		rect->right = tmp;
	}
	if(rect->bottom - rect->top < 0){
		int tmp = rect->top;
		rect->top = rect->bottom;
		rect->bottom = tmp;
	}
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
		
	// �����_�̍\���̂ɐ��K�����܂�
	RECT rect = ::mouseSelectedArea; // ��Ɨp�ɕ���
	RectangleNormalize(&rect);

	// �I��̈�̍ĕ`��
	::SendMessage(g_hSelectedArea, WM_PAINT, 0, 0);

	ScreenShotAndUpload(hWnd, L"capture.png", &rect);
}

void Layer_OnPaint(HWND hWnd)
{
	if(bDrag){
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);

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

	// TODO: �����ɃR�[�h��}�����Ă��������B
	MSG msg;

	// �O���[�o������������������Ă��܂��B
	MyRegisterClass(hInstance);

	// ���d�N���h�~
	CMutex mutex;
	try{
		mutex.createMutex(MUTEX_NAME);
	}catch(std::exception e){
		::ErrorMessageBox(L"���d�N���ł�");
		exit(0);
	}

	// �A�v���P�[�V�����̏����������s���܂�:
	if (!InitInstance (hInstance, nCmdShow)){
		return FALSE;
	}
	
	// ���C�� ���b�Z�[�W ���[�v:
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
	g_hInstance = hInstance; // �O���[�o���ϐ��ɃC���X�^���X�������i�[���܂�

	hWnd = CreateWindowEx(0, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
	g_hWnd = hWnd;

	if(!hWnd){
		::ShowLastError();
		return FALSE;
	}

	// �A�C�R���̐ݒ�
	TasktrayAddIcon(g_hInstance, WM_TASKTRAY, ID_TASKTRAY, IDI_MAIN, S_TASKTRAY_TIPS, hWnd);

	ShowWindow(hWnd, SW_HIDE);
	UpdateWindow(hWnd);
	return TRUE;
}

