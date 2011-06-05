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


void PNG_write(png_structp png_ptr, png_bytep buf, png_size_t size)
{
  FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
  if(!fp)
    return;

  fwrite(buf, sizeof(BYTE), size, fp);
  //fwrite("test", 1, strlen("test") * sizeof(BYTE), fp);
}

void PNG_flush(png_structp png_ptr){
  FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
  if(!fp)
    return;
  fflush(fp);
}


BOOL Screenshot::SaveToPngFile(HBITMAP h, LPCTSTR fileName)
{
  png_structp png_ptr = png_create_write_struct(
    PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) return (ERROR);

  if (setjmp(png_jmpbuf(png_ptr))){
   png_destroy_write_struct(&png_ptr, NULL);
   return (ERROR);
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, 0);
    return (ERROR);
  }
  
  BITMAP bmp;
  ::GetObject(h, sizeof(BITMAP), &bmp);

  int len = bmp.bmWidth * bmp.bmHeight * (bmp.bmBitsPixel / 8);

  BYTE *p = (BYTE *)malloc(len * sizeof(BYTE));
  ::GetBitmapBits(h, len, p);

  int width = bmp.bmWidth;
  int height = bmp.bmHeight;
  int depth = 8;

  //png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
  int color_type = PNG_COLOR_TYPE_RGB;

  // IHDR�`�����N
  // 24bit�t���J���[�ŏo��
  ::png_set_IHDR(png_ptr, info_ptr, width, height,
    depth,
    //PNG_COLOR_TYPE_RGB_ALPHA,
    color_type, // ���ߐF����Ȃ��̂ł͂����ăt�@�C���T�C�Y������
    //PNG_COLOR_TYPE_PALETTE,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
    );

  /*
  // 256�F�̃p���b�g���쐬����
  png_color colors[256] = {0};
  colors[0].blue = 255; //�F
  colors[0].red = 0; //�F
  colors[0].green = 0; //�F
  colors[1].blue = 0;
  colors[1].red = 255;
  colors[1].green = 0;
  ::png_set_PLTE(png_ptr, info_ptr, colors, 256);
  */

  // �J�L�R�~�֐��̐ݒ�
  FILE *fp = ::_wfopen(fileName, L"wb");
  png_init_io (png_ptr, fp);
  ::png_set_write_fn(png_ptr, fp, PNG_write, PNG_flush);
  
  int pixel_bytes = 0;
  if( color_type == PNG_COLOR_TYPE_RGB )
    pixel_bytes = 3;
  else if( color_type == PNG_COLOR_TYPE_RGB_ALPHA )
    pixel_bytes = 4;
  else if( color_type == PNG_COLOR_TYPE_PALETTE )
    pixel_bytes = 1;
  else
    pixel_bytes = 4; // default = PNG_COLOR_TYPE_RGB_ALPHA

  png_byte **row_pointers = (png_byte **)png_malloc(png_ptr, sizeof(png_byte *) * height);
  for(int y=0; y<height; y++){
    row_pointers[y] = (png_byte*)png_malloc(png_ptr, pixel_bytes * width);
    //memset(row_pointers[y], 0, width * pixel_bytes); // ���S�ɓ��߂ȍ��ɏ�����

    for(int x=0; x<width; x++){
      if(color_type == PNG_COLOR_TYPE_RGB){
        row_pointers[y][x * pixel_bytes + 0] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 2];   // R
        row_pointers[y][x * pixel_bytes + 1] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 1];   // G
        row_pointers[y][x * pixel_bytes + 2] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 0];   // B
      }else if(color_type == PNG_COLOR_TYPE_RGB_ALPHA){
        //row_pointers[y][x * pixel_bytes + 0] = x % 2; // ��
        row_pointers[y][x * pixel_bytes + 0] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 2];   // R
        row_pointers[y][x * pixel_bytes + 1] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 1];   // G
        row_pointers[y][x * pixel_bytes + 2] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 0];   // B
        row_pointers[y][x * pixel_bytes + 3] = (png_byte)255; // A(255 = �s����, 0 = ����)
      }else if(color_type == PNG_COLOR_TYPE_PALETTE){
        row_pointers[y][x * pixel_bytes + 0] = x % 2; // ��
      }else{
        ;
      }
    }
  }
  ::png_set_rows(png_ptr, info_ptr, (png_bytepp)row_pointers);
  //::png_write_image(png_ptr, row_pointers);

  ::png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
  ::png_destroy_write_struct(&png_ptr, NULL);
  ::png_write_end(png_ptr, info_ptr);

  ::fclose(fp);
  return TRUE;
}

// �w��̃E�C���h�E�́A�w��͈̔͂��X�N���[���V���b�g���܂�
BOOL Screenshot::ScreenshotWindow(LPCTSTR fileName, HWND window, RECT *rect)
{
	HBITMAP hBitmap = ScreenshotInMemory(window, rect);
	//BOOL bRet = SaveToFileAutoDetectFormat(hBitmap, fileName);
  BOOL bRet = SaveToPngFile(hBitmap, fileName);
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
