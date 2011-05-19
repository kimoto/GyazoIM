#include "Screenshot.h"

Screenshot::Screenshot()
{
}

Screenshot::~Screenshot()
{
}

// �w�肳�ꂽ�E�C���h�E�̃r�b�g�}�b�v�����擾���ĕԋp���܂�
HBITMAP Screenshot::GetBitmapFromWindow(HWND window, BITMAPINFO *pbmi, void **pbits)
{
	RECT rect;
	if( !::GetWindowRect(window, &rect) ){
		::ShowLastError();
		return NULL;
	}
	trace(L"window size: %d,%d\n", rect.right - rect.left, rect.bottom - rect.top);

	// �E�C���h�E�̃T�C�Y���擾���܂�
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	return GetBitmapFromWindow(window, pbmi, pbits, &rect);
}

// �w�肳�ꂽ�E�C���h�E�̃r�b�g�}�b�v�����擾���ĕԋp���܂�
HBITMAP Screenshot::GetBitmapFromWindow(HWND window, BITMAPINFO *pbmi, void **pbits, RECT *rect)
{
	int width = rect->right - rect->left;
	int height = rect->bottom - rect->top;

	// �f�o�C�X�R���e�L�X�g�擾
	trace(L"get window dc\n");
	HDC hdc = ::GetWindowDC(window);
	if(hdc == NULL){
		::ReleaseDC(window, hdc);
		::ShowLastError();
		return NULL;
	}

	// �f�o�C�X�R���e�L�X�g����
	trace(L"create compatible dc\n");
	HDC myhdc = ::CreateCompatibleDC(hdc);
	if(myhdc == NULL){
		::DeleteDC(myhdc);
		::ShowLastError();
		return NULL;
	}

	// probably bitblt
	pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbmi->bmiHeader.biWidth = width;
	pbmi->bmiHeader.biHeight = height;
	pbmi->bmiHeader.biPlanes = 1;
	pbmi->bmiHeader.biBitCount = ::GetDeviceCaps(hdc, BITSPIXEL); // �f�X�N�g�b�v�̃J���[bit���擾
	//pbmi->bmiHeader.biBitCount = 8;
	trace(L"color bit: %d\n", pbmi->bmiHeader.biBitCount);
	pbmi->bmiHeader.biSizeImage = pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biHeight * (pbmi->bmiHeader.biBitCount / 8); // �J���[bit���ɍ��킹�Ĕ{�����ς��

	trace(L"create compatible bitmap\n");
	HBITMAP hBitmap = ::CreateDIBSection(hdc, pbmi, DIB_RGB_COLORS, pbits, NULL, 0);
	if(hBitmap == NULL){
		::ReleaseDC(window, hdc);
		::DeleteDC(myhdc);
		::ShowLastError();
		return NULL;
	}
	trace(L"select object: dc -> hbitmap\n");
	HBITMAP oldBitmap = (HBITMAP)::SelectObject(myhdc, hBitmap);
	
	trace(L"copy bitmap: window dc -> memory dc\n");
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

// �w�肳�ꂽhBitmap��ۑ����܂�
BOOL Screenshot::SaveBitmapToFile(HBITMAP hBitmap, BITMAPINFO *pbmi, void *pbits, LPCTSTR fileName)
{
	// header�\�z
	BITMAPFILEHEADER b;
	b.bfType = 0x4D42; // "BM"
	b.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	b.bfSize = b.bfOffBits + pbmi->bmiHeader.biSizeImage;

	trace(L"DIB to file\n");
	HANDLE hFile = ::CreateFile(fileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, 0, NULL);
	if(hFile == INVALID_HANDLE_VALUE){
		::ShowLastError();
		return FALSE;
	}
	
	DWORD writes = 0;
	if( ::WriteFile(hFile, &b, sizeof(b), &writes, NULL) == 0 ){
		::ShowLastError();
		return FALSE;
	}

	if( ::WriteFile(hFile, &pbmi->bmiHeader, sizeof(pbmi->bmiHeader), &writes, NULL) == 0 ){
		::ShowLastError();
		return FALSE;
	}

	if( ::SetFilePointer(hFile, b.bfOffBits, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ){
		::ShowLastError();
		return FALSE;
	}

	if( ::WriteFile(hFile, pbits, pbmi->bmiHeader.biSizeImage, &writes, NULL) == 0 ){
		::ShowLastError();
		return FALSE;
	}

	if( !::CloseHandle(hFile) ){
		::ShowLastError();
		return FALSE;
	}
	return TRUE;
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

	EncoderParameters params;
	params.Count = 1;
	params.Parameter[0].Guid = EncoderQuality;
	params.Parameter[0].Type = EncoderParameterValueTypeLong;
	params.Parameter[0].NumberOfValues = 1;
	int quality = 0;
	params.Parameter[0].Value = &quality;

	Bitmap *b = new Bitmap(hBitmap, NULL);
	if( 0 != b->Save(fileName, &clsid, &params) ){
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
	BITMAPINFO bmi = {0};
	void *pbits = NULL;
	HBITMAP hBitmap = GetBitmapFromWindow(window, &bmi, &pbits, rect);
	if(hBitmap == NULL){
		::ShowLastError();
		return FALSE;
	}
	BOOL bRet = SaveToFileAutoDetectFormat(hBitmap, fileName);
	
	// hbitmap��bits�̉��
	::DeleteObject(hBitmap);
	return bRet;
}

// Desktop�̃X�N���[���V���b�g���B�e���ăt�@�C���ɕۑ����܂�
BOOL Screenshot::ScreenshotDesktop(LPCTSTR fileName, RECT *rect)
{
	return ScreenshotWindow(fileName, GetDesktopWindow(), rect);
}
