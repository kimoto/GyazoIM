#pragma once
#include <Windows.h>
#include <string>
#include <GdiPlus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace ::std;
using namespace ::Gdiplus;
#include "Util.h"

// libpng functions
#include "png.h"
#include "zlib.h"
#pragma comment(lib, "libpng15.lib")

class Screenshot
{
public:
	Screenshot();
	~Screenshot();

	// �w�肳�ꂽ�E�C���h�E�̃r�b�g�}�b�v�����擾���ĕԋp���܂�
	static HBITMAP Screenshot::GetBitmapFromWindow(HWND window, BITMAPINFO *pbmi, void **pbits, RECT *rect);

	// MIME-TYPE�����Ƃ�Encoder���擾���܂�
	static BOOL Screenshot::GetClsidEncoderFromMimeType(LPCTSTR format, LPCLSID lpClsid);

	// �t�@�C���������ƂɁAEnocder���擾���܂�
	static BOOL Screenshot::GetClsidEncoderFromFileName(LPCTSTR fileName, LPCLSID lpClsid);

	// �w�肳�ꂽ�t�@�C�����ŁAhBitmap��ۑ����܂�
	static BOOL Screenshot::SaveToFileAutoDetectFormat(HBITMAP hBitmap, LPCTSTR fileName);

  // �w�肳�ꂽ�t�@�C�����ŁAhBitmap��ۑ����܂�
  // LibPng���C�u�����g�p
  static BOOL Screenshot::SaveToPngFile(HBITMAP hBitmap, LPCTSTR fileName);

	// �w��̃E�C���h�E�́A�w��͈̔͂��X�N���[���V���b�g���܂�
	static BOOL Screenshot::ScreenshotWindow(LPCTSTR fileName, HWND window, RECT *rect);

	// �t�@�C���ɕۑ������AHBITMAP�`���ŕԋp���܂�
	static HBITMAP Screenshot::ScreenshotInMemory(HWND window, RECT *rect);

	// Desktop�̃X�N���[���V���b�g���B�e���ăt�@�C���ɕۑ����܂�
	static BOOL Screenshot::ScreenshotDesktop(LPCTSTR fileName, RECT *rect);
};


