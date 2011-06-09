#include "Screenshot.h"

/*=========================
* �F����
==========================*/
// �F�̋������Z�o
double between_color(BYTE r1, BYTE g1, BYTE b1, BYTE r2, BYTE g2, BYTE b2){
  return sqrt( (double)(( r1 - r2 ) * ( r1 - r2 ) + ( g1 - g2 ) * ( g1 - g2 ) + ( b1 - b2 ) * ( b1 - b2 )) );
}

// �w�肳�ꂽ�F�Ɗ��S��v����C���f�b�N�X��Ԃ��܂�
int find_color_by_clt(png_colorp table, size_t size, BYTE r, BYTE g, BYTE b){
  for(int i=0; i<(int)size; i++){
    if(table[i].red == r && table[i].green == g && table[i].blue == b){
      return i;
    }
  }
  return -1;
}

// �w�肳�ꂽ�F�ɋ߂��A�p���b�g�̃C���f�b�N�X��Ԃ��܂�
int find_nearcolor_by_clt(png_colorp table, size_t size, BYTE r, BYTE g, BYTE b){
  double min_between = 999;
  int min_index = -1;

  for(int i=0; i<(int)size; i++){
    double between = between_color(r, g, b, table[i].red, table[i].green, table[i].blue);
    if( between < min_between ){
      min_between = between;
      min_index = i;
    }
  }
  return min_index;
}

int find_bestcolor_by_clt(png_colorp table, size_t size, BYTE r, BYTE g, BYTE b)
{
  int index = -1;
  if( (index = ::find_color_by_clt(table, size, r, g, b)) == -1 ){
    return ::find_nearcolor_by_clt(table, size, r, g, b);
  }else{
    return index;
  }
}

png_color *find_by_palette(png_colorp palette, int num_palette, int color_index){
  int i;
  for(i=0; i<num_palette; i++){
    if( i == color_index )
      return (png_color *)&palette[i];
  }
  return NULL;
}

void PNG_write(png_structp png_ptr, png_bytep buf, png_size_t size)
{
  FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
  if(!fp) return;
  fwrite(buf, sizeof(BYTE), size, fp);
}

void PNG_flush(png_structp png_ptr){
  FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
  if(!fp) return;
  fflush(fp);
}

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
	//BOOL bRet = SaveToFileAutoDetectFormat(hBitmap, fileName);
  //BOOL bRet = SaveToPngFile(hBitmap, fileName);
  BOOL bRet = SaveToFileMagick(hBitmap, fileName);
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

  // �����I�Ȍ��F����

  // �F�̓��v���擾����
  // �ł��g���Ă���256�F�̃J���[�p���b�g����肽��
  map<uint, uint>color_sets;
  for(int y=0; y<height; y++){
    for(int x=0; x<width; x++){
      int r = p[y * bmp.bmWidthBytes + x * 4 + 2];   // R
      int g = p[y * bmp.bmWidthBytes + x * 4 + 1];   // G
      int b = p[y * bmp.bmWidthBytes + x * 4 + 0];   // B

      // int��32bit�Ƃ��āAR,G,B�̏��ԂɊi�[����A��ł�����L�[�ɂ���map�Ɋi�[
      // map�̉E���͌�(�J�E���g���ꂽ)
      uint packed = (r << 0) | (g << 8) | (b << 16);

      map<uint, uint>::iterator it = color_sets.find(packed);
      if( it == color_sets.end() ){
        color_sets.insert( map<uint,uint>::value_type(packed, 1) );
      }else{
        it->second++;
      }
    }
  }

  // if 256 color then palette style png
  int color_type = PNG_COLOR_TYPE_RGB_ALPHA;
  int color_table_count = 0;
  png_color colors[256] = {0}; // ���S�ɓ��߂ȍ����������l

  if(color_sets.size() <= 256){
    // color_sets�̌� == �o�ꂷ��F��
    //::ErrorMessageBox(L"�o�ꂷ��F��: %d", color_sets.size());

    // �o���񐔂̑������m��D�悵�ăp���b�g�����A���F�����͂��Ă��Ȃ�
    // �����J�E���g�̐F�������Ă��܂����
    // value -> key��map�ɕϊ�����(�����I�Ƀ\�[�g����)
    map<uint,uint>::iterator it = color_sets.begin();
    map<uint,uint> sorted_map;

    while(it != color_sets.end()){
      int count = it->second; // �F�̏o���񐔂��擾

      // ���̏o���񐔂����łɏo�͐�}�b�v�ɒ񋟂���Ă�����
      // ���Ԃ�Ȃ��悤��+1����
      // �܂����Ԃ��Ă�����܂�+1����A�J��Ԃ�
      int n = 256;
      while(n-- > 0){
        map<uint,uint>::iterator tt = sorted_map.find(count);
        if(tt == sorted_map.end()){
          break;
        }
        count++;
      }
      sorted_map.insert(map<uint,uint>::value_type(count, it->first));
      // �����񐔏o������F���Ƃ��̂Ƃ��ɏ����Ă��܂��\������
      it++;
    }

    //::ErrorMessageBox(L"���F(����Ă��܂���)��̐F��: %d", sorted_map.size());

    // value�̍�������256�擾���ăp���b�g��ݒ肷��
    int count = 0;
    map<uint,uint>::reverse_iterator rit = sorted_map.rbegin();
    while(rit != sorted_map.rend()){
      if(count >= 256){
        perror("256�ȏ�̃J���[�C���f�b�N�X������܂�");
        break;
      }

      int color = rit->second;
      int r = (color & 0xFF);
      int g = (color & 0xFF00) >> 8;
      int b = (color & 0xFF0000) >> 16;

      colors[count].red = r;
      colors[count].green = g;
      colors[count].blue = b;

      rit++; count++;
    }

    color_table_count = count;
    //::ErrorMessageBox(L"�J���[�p���b�g�̐�: %d", color_table_count);

    ::png_set_PLTE(png_ptr, info_ptr, colors, color_table_count);

    color_type = PNG_COLOR_TYPE_PALETTE;
  }else{
    // desktop nanode ALPHA ha iranai
    color_type = PNG_COLOR_TYPE_RGB;
  }

  // IHDR�`�����N
  ::png_set_IHDR(png_ptr, info_ptr, width, height,
    depth,
    color_type,
    PNG_INTERLACE_NONE,
    //PNG_INTERLACE_ADAM7,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
    );

  // �J�L�R�~�֐��̐ݒ�
  FILE *fp;
  ::_wfopen_s(&fp, fileName, L"wb");
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
    for(int x=0; x<width; x++){
      if(color_type == PNG_COLOR_TYPE_RGB){
        row_pointers[y][x * pixel_bytes + 0] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 2];   // R
        row_pointers[y][x * pixel_bytes + 1] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 1];   // G
        row_pointers[y][x * pixel_bytes + 2] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 0];   // B
      }else if(color_type == PNG_COLOR_TYPE_RGB_ALPHA){
        row_pointers[y][x * pixel_bytes + 0] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 2];   // R
        row_pointers[y][x * pixel_bytes + 1] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 1];   // G
        row_pointers[y][x * pixel_bytes + 2] = (png_byte)p[y * bmp.bmWidthBytes + x * 4 + 0];   // B
        row_pointers[y][x * pixel_bytes + 3] = (png_byte)255; // A(255 = �s����, 0 = ����)
      }else if(color_type == PNG_COLOR_TYPE_PALETTE){
        // �J���[�C���f�b�N�X�̐F���Q�Ƃ���
        BYTE orig_r = p[y * bmp.bmWidthBytes + x * 4 + 2];
        BYTE orig_g = p[y * bmp.bmWidthBytes + x * 4 + 1];
        BYTE orig_b = p[y * bmp.bmWidthBytes + x * 4 + 0];

        int index = ::find_bestcolor_by_clt(colors, color_table_count, orig_r, orig_g, orig_b);
        if(index == -1){
          perror("�߂��F��������܂���ł���");
          exit(1);
        }
        row_pointers[y][x * pixel_bytes + 0] = index;
      }else{
        perror("���Ή��̃t�H�[�}�b�g�ł�"); // ���Ή�
      }
    }
  }
  ::png_set_rows(png_ptr, info_ptr, (png_bytepp)row_pointers);
  ::png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
  ::png_destroy_write_struct(&png_ptr, NULL);
  ::png_write_end(png_ptr, info_ptr);

  ::fclose(fp);
  return TRUE;
}

BOOL Screenshot::SaveToFileMagick(HBITMAP hBitmap, LPCTSTR fileName)
{
  // HBITMAP����f�[�^���擾
  BITMAP bmp;
  ::GetObject(hBitmap, sizeof(BITMAP), &bmp);
  int len = bmp.bmWidth * bmp.bmHeight * (bmp.bmBitsPixel / 8);
  BYTE *p = (BYTE *)malloc(len * sizeof(BYTE));
  ::GetBitmapBits(hBitmap, len, p);
  int width = bmp.bmWidth;
  int height = bmp.bmHeight;
  int depth = 8;

  // ImageMagick�ɓn��
  ::Magick::Image image;
  image.read(width, height, "BGRA", Magick::CharPixel, p);

  // ImageMagick�ŏ���
  image.quantizeDither(true);
  image.quantizeColors(256); // 256�Ɍ��F����
  image.quantize();
  
  // HBITMAP�ɖ߂�
  image.write(0, 0, width, height, "BGRA", Magick::CharPixel, p);
  ::SetBitmapBits(hBitmap, len, p);

  return SaveToPngFile(hBitmap, fileName);
}

// Desktop�̃X�N���[���V���b�g���B�e���ăt�@�C���ɕۑ����܂�
BOOL Screenshot::ScreenshotDesktop(LPCTSTR fileName, RECT *rect)
{
	return ScreenshotWindow(fileName, GetDesktopWindow(), rect);
}
