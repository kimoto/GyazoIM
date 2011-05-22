#include "Screenshot.h"

Screenshot::Screenshot()
{
}

Screenshot::~Screenshot()
{
}

// �w�肳�ꂽ�E�C���h�E�̃r�b�g�}�b�v�����擾���ĕԋp���܂�
HBITMAP Screenshot::GetBitmapFromWindow(HWND window, BITMAPINFO *pbmi, void **pbits, RECT *rect)
{
	// �f�o�C�X�R���e�L�X�g�擾
	HDC hdc = ::GetWindowDC(window);
	if(hdc == NULL){
		::ReleaseDC(window, hdc);
		::ShowLastError();
		return NULL;
	}

	// �f�o�C�X�R���e�L�X�g����
	HDC myhdc = ::CreateCompatibleDC(hdc);
	if(myhdc == NULL){
		::DeleteDC(myhdc);
		::ShowLastError();
		return NULL;
	}
	
	pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbmi->bmiHeader.biWidth = rect->right - rect->left;
	pbmi->bmiHeader.biHeight = rect->bottom - rect->top;
	pbmi->bmiHeader.biPlanes = 1;
	pbmi->bmiHeader.biBitCount = ::GetDeviceCaps(hdc, BITSPIXEL); // �f�X�N�g�b�v�̃J���[bit���擾
	pbmi->bmiHeader.biSizeImage = pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biHeight * (pbmi->bmiHeader.biBitCount / 8); // �J���[bit���ɍ��킹�Ĕ{�����ς��

	HBITMAP hBitmap = ::CreateDIBSection(hdc, pbmi, DIB_RGB_COLORS, pbits, NULL, 0);
	if(hBitmap == NULL){
		::ReleaseDC(window, hdc);
		::DeleteDC(myhdc);
		::ShowLastError();
		return NULL;
	}
	
	HBITMAP oldBitmap = (HBITMAP)::SelectObject(myhdc, hBitmap);
	if( ::BitBlt(myhdc, 0, 0, pbmi->bmiHeader.biWidth, pbmi->bmiHeader.biHeight, hdc, rect->left, rect->top, SRCCOPY) == 0 ){
		::SelectObject(myhdc, oldBitmap);
		::ReleaseDC(window, hdc);
		::DeleteDC(myhdc);
		::ShowLastError();
		return NULL;
	}

	::SelectObject(myhdc, oldBitmap);
	::ReleaseDC(window, hdc);
	::DeleteDC(myhdc);
	return hBitmap;
}

// MIME-TYPE�����Ƃ�Encoder���擾���܂�
BOOL Screenshot::GetClsidEncoderFromMimeType(LPCTSTR format, LPCLSID lpClsid)
{
	UINT num, size;
	if( ::GetImageEncodersSize(&num, &size) != Ok ){
		::ShowLastError();
		return FALSE;
	}

	// �o�b�t�@���m��
	ImageCodecInfo *info = (ImageCodecInfo *)::GlobalAlloc(GMEM_FIXED, size);

	// �G���R�[�_�[�̏���]��
	if( ::GetImageEncoders(num, size, info) != Ok ){
		::GlobalFree(info);
		return FALSE;
	}

	for(UINT i=0; i<num; i++){
		if( wcscmp(info[i].MimeType, format) == 0 ){
			*lpClsid = info[i].Clsid;
			::GlobalFree(info); // found
			return TRUE;
		}
	}

	::GlobalFree(info); // not found
	return FALSE;
}

// �t�@�C���������ƂɁAEnocder���擾���܂�
BOOL Screenshot::GetClsidEncoderFromFileName(LPCTSTR fileName, LPCLSID lpClsid)
{
	UINT num, size;
	if( ::GetImageEncodersSize(&num, &size) != Ok ){
		::ShowLastError();
		return FALSE;
	}

	// �o�b�t�@���m��
	ImageCodecInfo *info = (ImageCodecInfo *)::GlobalAlloc(GMEM_FIXED, size);

	// �G���R�[�_�[�̏���]��
	if( ::GetImageEncoders(num, size, info) != Ok ){
		::GlobalFree(info);
		return FALSE;
	}

	for(UINT i=0; i<num; i++){
		if( PathMatchSpecW(fileName, info[i].FilenameExtension)){
			*lpClsid = info[i].Clsid;
			::GlobalFree(info); // found
			return TRUE;
		}
	}

	::GlobalFree(info); // not found
	return FALSE;
}

// �w�肳�ꂽ�t�@�C�����ŁAhBitmap��ۑ����܂�
BOOL Screenshot::SaveToFileAutoDetectFormat(HBITMAP hBitmap, LPCTSTR fileName)
{
	ULONG_PTR token;
	::GdiplusStartupInput input;
	::GdiplusStartup(&token, &input, NULL);

	CLSID clsid;
	if( !GetClsidEncoderFromFileName(fileName, &clsid) ){
		::ShowLastError();
		::GdiplusShutdown(token);
		return FALSE;
	}

	Bitmap *b = new Bitmap(hBitmap, NULL);
	if( 0 != b->Save(fileName, &clsid) ){
		::ShowLastError();
		delete b;		
		::GdiplusShutdown(token);
		return FALSE;
	}
	
	delete b;
	::GdiplusShutdown(token);
	return TRUE;
}

// �w��̃E�C���h�E�́A�w��͈̔͂��X�N���[���V���b�g���܂�
BOOL Screenshot::ScreenshotWindow(LPCTSTR fileName, HWND window, RECT *rect)
{
	HBITMAP hBitmap = ScreenshotInMemory(window, rect);
	BOOL bRet = SaveToFileAutoDetectFormat(hBitmap, fileName);
	::DeleteObject(hBitmap);
	return bRet;
}

// �t�@�C���ɕۑ������AHBITMAP�`���ŕԋp���܂�
HBITMAP Screenshot::ScreenshotInMemory(HWND window, RECT *rect)
{
	BITMAPINFO bmi = {0};
	void *pbits = NULL;
	return GetBitmapFromWindow(window, &bmi, &pbits, rect);
}

// Desktop�̃X�N���[���V���b�g���B�e���ăt�@�C���ɕۑ����܂�
BOOL Screenshot::ScreenshotDesktop(LPCTSTR fileName, RECT *rect)
{
	return ScreenshotWindow(fileName, GetDesktopWindow(), rect);
}
