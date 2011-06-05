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

// �f�o�b�O�Ƃ��ĕۑ����Desktop�ɂ���
// �t�@�C������{���t����}.png
#define DEBUG_LOCAL_SAVE
#define WM_TASKTRAY (WM_APP + 1)
#define WM_ACTIVEWINDOW_SS (WM_USER + 1)
#define WM_DESKTOP_SS (WM_USER + 2)
#define ID_TASKTRAY 1
#define S_TASKTRAY_TIPS L"Gyazo"
#define MUTEX_NAME L"GyazoIM"
#define IF_KEY_PRESS(lp) ((lp & (1 << 31)) == 0)

#define DLG_KEYCONFIG_PROC_WINDOW_TITLE L"�L�[�ݒ�"
#define DLG_KEYCONFIG_ASK L"�L�[����͂��Ă�������"
#define DLG_KEYCONFIG_ASK_BUTTON_TITLE L"����"
#define DLG_KEYCONFIG_DEFAULT_BUTTON_TITLE L"�ݒ�"
#define DLG_MONITOR_GAMMA_WINDOW_TITLE_FORMAT L"%s�̃K���}����"

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

// �L�[�{�[�h�V���[�g�J�b�g�p�\����
KEYINFO g_activeSSKeyInfo = {0};
KEYINFO g_desktopSSKeyInfo = {0};

// �N���C�A���g����X�N���[�����W�ɕϊ�
POINT mousePressedPt = {0};		// �}�E�X�N���b�N�����ꏊ(�n�_)
POINT mouseReleasedPt = {0};	// �}�E�X�𗣂����ꏊ(�I�_)
RECT mouseSelectedArea = {0};	// �}�E�X�ɂ���đI������Ă���̈�

// �h���b�O����ԊǗ�
//BOOL bDrag = FALSE; 

LPCTSTR layer1WindowClass = L"GyazoIM_Layer1";
LPCTSTR layer2WindowClass = L"GyazoIM_Layer2";

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// ===========================================
//	�ݒ�t�@�C������n�֐�
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
//	�X�N���[���V���b�g�B�e�ȗ����n
// ===========================================
// Desktop�̎w�肵���͈͂��L���v�`�����ăA�b�v���[�h
UINT g_uploadTarget = IDM_OUTPUT_PREVIEW;
void ScreenShotAndUpload(HWND forErrorMessage, LPCTSTR path, RECT *rect)
{
  try{
    ::Screenshot::ScreenshotDesktop(path, rect);
    ::MessageBeep(MB_ICONASTERISK); // �B�e�������̎��_�ŏo��

    // ���M��͉E�N�����j���[�Őݒ肵���ꏊ
    // �f�t�H���g�̓��[�J���v���r���[
    if( g_uploadTarget == IDM_OUTPUT_GYAZO ){
      // gyazo�ŉ摜�f�[�^�𑗐M����
      Gyazo *g = new Gyazo();
      g->UploadFileAndOpenURL(forErrorMessage, path);
      delete g;
    }else{
      ::ExecuteFile(g_hWnd, path);
    }
  }catch(exception e){
    ::ErrorMessageBox(L"%s", e);
  }

  ::MessageBeep(MB_ICONASTERISK); // ���e�������̎��_�ŏo��
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
//	GUI�C�x���g�n(�C���X�y�N�g���[�h)
// ===========================================
// �C���X�y�N�g���[�h�J�n
BOOL StartInspect()
{
  if( !::StartMouseEventProxy(::g_hWnd, ::g_hInstance) ){
    ::ShowLastError();
    return FALSE;
  }
  bStartCapture = TRUE;
  return TRUE;
}

// �C���X�y�N�g���[�h����
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
//	GUI�C�x���g�n(�L���v�`�����[�h)
// ===========================================
void OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags)
{
  if(::bStartCapture) { // Inspect���[�h�̊ԈႢ
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

      // �I��̈���폜����R�[�h�������ɓ����
      RECT rect;
      ::GetWindowRect(h, &rect);
      ::ScreenShotAndUpload(hWnd, L"inspect.png", &rect);
    }
  }
}

// �C���X�y�N�g���ɉE�N���b�N�ŁA���~����
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

// �L���v�`�����[�h�J�n
BOOL StartCapture()
{
  RECT rect;
  ::GetWindowRect(::GetDesktopWindow(), &rect);
  rect.right = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
  rect.bottom = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

  // ��ʂ�������̂ŁA�ŏ���0,0�ŃE�C���h�E�쐬���Ƃ���
  // �����ł����炲����Ɗg��
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

  // �I��̈�
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

// �`�悷�邾���m�v���V�[�W���Ƃ��Ďg���Ă�
LRESULT CALLBACK SelectedAreaWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message){
  case WM_ERASEBKGND:
    return FALSE;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// ���ۂɃ}�E�X�C�x���g�Ƃ����������铧���ȃ��C���[
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

    // �������f�o�C�X�R���e�L�X�g�̍쐬
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
      // ���K�����[�hON
      bNormalize = TRUE;
      return TRUE;
    }

    if(wParam == VK_SHIFT){
      bStick = TRUE;
      return TRUE;
    }

    // �Ȃ񂩃L�[�����ꂽ�璆�f���܂�
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

    // �Ȃ񂩃L�[�����ꂽ�璆�f���܂�
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

      // �`��\�ȍŏ��P�ʂ����傫�������Ƃ������B�e�\
      int w = abs(rect.right - rect.left);
      int h = abs(rect.bottom - rect.top);

      if(CAPTURE_MIN_WIDTH < w && CAPTURE_MIN_HEIGHT < h){
        ::FillRect(::g_hMemDC, &rect, colorBrush);

        // �E�C���h�E�̃T�C�Y��`��
        HFONT hOldFont = SelectFont(g_hMemDC, hFont);

        // �}�E�X�J�[�\���̉E���ɍ��W��`�悷��
        POINT pt;
        ::GetCursorPos(&pt);

        //int parentMode = ::SetBkMode(::g_hMemDC, TRANSPARENT);
        ::SelectObject(::g_hMemDC, ::CreateSolidBrush(RGB(255,255,255)));
        ::TextFormatOut(::g_hMemDC, pt.x + 10, pt.y, L"%d,%d",
          rect.right - rect.left,
          rect.bottom - rect.top);

        // font��߂�
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

      // �`��\�ȍŏ��P�ʂ����傫�������Ƃ������B�e�\
      int w = abs(selected.right - selected.left);
      int h = abs(selected.bottom - selected.top);

      // �ŏ��T�C�Y�����ł��������ꍇ
      if(CAPTURE_MIN_WIDTH < w && CAPTURE_MIN_HEIGHT < h){
        ::InvalidateRect(hWnd, NULL, FALSE);
        ::MessageBeep(MB_ICONASTERISK);

        // �I��g�������܂�
        bCapture = FALSE;
        ::NoticeRedraw(hWnd);

        // �m�[�}���C�Y����selected���Ώ�
        // �B�e���܂�
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
    // ���_������������
    ::mousePressed.x = ::selected.left;
    ::mousePressed.y = ::selected.top;
    break;

  case WM_MOUSEWHEEL: // �I��̈�̊g��E�k��
    {
      int v =  GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 20; // �}�E�X�z�C�[���̈ړ���

      // ���ׂĂ̕����ɋϓ��Ɋg�傷��
      selected.left += -v;
      selected.top += -v;
      selected.right += v;
      selected.bottom += v;

      // �}�E�X���_�������Ȃ��Ƒ��Ɛ��������Ƃ�񂭂Ȃ�
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

      // �}�E�X�������|�C���g���猻�݂̈ʒu�ւ̈ړ��ʕ��Aleft/top���ړ�
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

      // �E�C���h�E�c���T�C�Y�̐��K��
      if(bNormalize){
        // ���̃T�C�Y�Ɠ��������̏c�̃T�C�Y�ɂ���
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

    // ��ʊO�ɃE�C���h�E���o�Ȃ��悤�ɂ���␳����
    CorrectRect(&selected, &windowRect);

    // �V�X�e���E�C���h�E�Ƃ̋z������
    // �ړ����ɂ����z�������͂��Ȃ�
    if(bDrag){
      if(bStick){
        // �J�[�\���̈ʒu�ɃE�C���h�E������΂��̃E�C���h�E�ɋz������
        //POINT pt;
        //::GetCursorPos(&pt);

        // ���ׂẴE�C���h�E�̂����A�������g�ȊO�ŉ��ȃE�C���h�E���璲��
        // �J�[�\�������Ԃ��Ă�������Ƃ����߂ȃE�C���h�E��T���Ď����ő傫���𒲐�����
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
//	�L�[�R���t�B�O�n�֐�
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

// �L�[�ݒ�p�A�L�[�t�b�N�v���V�[�W��(not �O���[�o�� / �O���[�o����DLL�𗘗p���Ȃ���΍s���Ȃ�)
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
  //nCode��0�����̂Ƃ��́ACallNextHookEx���Ԃ����l��Ԃ�
  if (nCode < 0)  return CallNextHookEx(g_hKeyConfigHook,nCode,wp,lp);

  if (nCode==HC_ACTION) {
    //�L�[�̑J�ڏ�Ԃ̃r�b�g���`�F�b�N����
    //WM_KEYDOWN��WM_KEYUP��Dialog�ɑ��M����
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

  // �ꎞ�i�[�p�o�b�t�@
  static KEYINFO activeSSKeyInfo = {0};
  static KEYINFO desktopSSKeyInfo = {0};

  switch( msg ){
  case WM_INITDIALOG:  // �_�C�A���O�{�b�N�X���쐬���ꂽ�Ƃ�
    ::SetWindowTopMost(g_hKeyConfigDlg); // �E�C���h�E���őO�ʂɂ��܂�
    ::SetCurrentKeyConfigToGUI(hDlg); // ���݂̃L�[�ݒ��GUI��ɔ��f�����܂�

    // �ꎞ�i�[�p�o�b�t�@�����������܂�
    activeSSKeyInfo = ::g_activeSSKeyInfo;
    desktopSSKeyInfo = ::g_desktopSSKeyInfo;

    // �E�C���h�E�̃^�C�g�����K��̂��̂ɐݒ肵�܂�
    ::SetWindowText(g_hKeyConfigDlg, DLG_KEYCONFIG_PROC_WINDOW_TITLE);
    return TRUE;

  case WM_KEYDOWN:
    if( !::GetKeyboardState((PBYTE)&keyTbl) ){
      ShowLastError();
      exit(-1);
    }

    // ���͂��ꂽ�⏕�L�[�𔻒f���đ��
    KEYINFO tmp;
    ::ClearKeyInfo(&tmp);
    if( keyTbl[VK_CONTROL] & 0x80 ){
      // wp�ƈꏏ��������wp�g���΂����̂œ��͂��܂���
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

    // ���͂��ꂽ�L�[��UI��ɔ��f�����܂�
    {
      LPTSTR lpKeyConfigBuffer = ::GetKeyInfoString(&tmp);
      ::SetDlgItemText(g_hKeyConfigDlg, targetID, lpKeyConfigBuffer);
      ::GlobalFree(lpKeyConfigBuffer);
    }

    // ���݃A�N�e�B�u�ɕҏW���Ă�\���̂ɏ��ۑ�s
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

  case WM_COMMAND:     // �_�C�A���O�{�b�N�X���̉������I�����ꂽ�Ƃ�
    switch( LOWORD( wp ) ){
    case IDOK:       // �K�p�{�^�����I�����ꂽ
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

    case IDCANCEL:   // �u�L�����Z���v�{�^�����I�����ꂽ
      // �_�C�A���O�{�b�N�X������
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

    case IDDEFAULT: // �f�t�H���g�{�^���������ꂽ�Ƃ�
      // setup default key config
      ::QuickSetKeyInfo(&activeSSKeyInfo, VK_CONTROL, VK_F9);
      ::QuickSetKeyInfo(&desktopSSKeyInfo, VK_MENU, VK_F9);

      // ���݂̃L�[�ݒ��GUI�ɔ��f���܂�
      SetCurrentKeyConfigToGUI(hDlg, &activeSSKeyInfo, &desktopSSKeyInfo);
      return TRUE;
    }
  }

  return FALSE;  // DefWindowProc()�ł͂Ȃ��AFALSE��Ԃ����ƁI
}

// ===========================================
//	���C���E�C���h�E(��\�����)
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
    // �L�[�ݒ�_�C�A���O�\��
    // ��Ɉ�̃E�C���h�E�����\��
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

  // ���j�^��������������A���j�^���Ƃ̒������j���[��\������悤��
  HMENU hView = CreatePopupMenu();
  MENUITEMINFO mii = {0};
  mii.wID = IDM_OUTPUT;
  mii.cbSize = sizeof(MENUITEMINFO);
  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU;
  mii.fType = MFT_STRING;
  mii.hSubMenu = hView;
  mii.dwTypeData = L"�o�͐�";
  InsertMenuItem(hSubMenu, 3, TRUE, &mii);

  // �o�͐�ǉ�
  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
  mii.dwTypeData = L"�v���r���[";
  mii.wID = IDM_OUTPUT_PREVIEW; // 2500 - 2600 reserved for monitors
  InsertMenuItem(hView, 0, TRUE, &mii);

  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
  mii.dwTypeData = L"gyazo.com";
  mii.wID = IDM_OUTPUT_GYAZO; // 2500 - 2600 reserved for monitors
  InsertMenuItem(hView, 1, TRUE, &mii);

  // �`�F�b�N
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
    // �f�X�N�g�b�v�R���|�W�V�����̖�����
    /*
    if( FAILED(::DwmEnableComposition(DWM_EC_DISABLECOMPOSITION)) ){
    ::ShowLastError();
    return FALSE;
    }
    */

    // �ݒ�t�@�C���ǂݍ���
    LoadConfig();

    // �L�[�{�[�h�ݒ�
    ::SetWindowHandle(hWnd);
    ::RegistKey(g_activeSSKeyInfo, WM_ACTIVEWINDOW_SS);
    ::RegistKey(g_desktopSSKeyInfo, WM_DESKTOP_SS);

    if( !::StartHook() )
      ::ShowLastError();

    // �^�X�N�g���C���A�p
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

  // �G�N�X�v���[���[�̍ċN������
  // �������g�̃^�X�N�g���C�A�C�R���𕜌�
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

