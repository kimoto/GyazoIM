#include "Screenshot.h"

/*=========================
* 色操作
==========================*/
// 色の距離を算出
double between_color(BYTE r1, BYTE g1, BYTE b1, BYTE r2, BYTE g2, BYTE b2){
  return sqrt( (double)(( r1 - r2 ) * ( r1 - r2 ) + ( g1 - g2 ) * ( g1 - g2 ) + ( b1 - b2 ) * ( b1 - b2 )) );
}

// 指定された色と完全一致するインデックスを返します
int find_color_by_clt(png_colorp table, size_t size, BYTE r, BYTE g, BYTE b){
  for(int i=0; i<(int)size; i++){
    if(table[i].red == r && table[i].green == g && table[i].blue == b){
      return i;
    }
  }
  return -1;
}

// 指定された色に近い、パレットのインデックスを返します
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

// 指定されたウインドウのビットマップ情報を取得して返却します
HBITMAP Screenshot::GetBitmapFromWindow(HWND window, BITMAPINFO *pbmi, void **pbits, RECT *rect)
{
	// デバイスコンテキスト取得
	HDC hdc = ::GetWindowDC(window);
	if(hdc == NULL){
		::ReleaseDC(window, hdc);
		::ShowLastError();
		return NULL;
	}

	// デバイスコンテキスト複製
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
	pbmi->bmiHeader.biBitCount = ::GetDeviceCaps(hdc, BITSPIXEL); // デスクトップのカラーbit数取得
	pbmi->bmiHeader.biSizeImage = pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biHeight * (pbmi->bmiHeader.biBitCount / 8); // カラーbit数に合わせて倍数が変わる

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

// MIME-TYPEをもとにEncoderを取得します
BOOL Screenshot::GetClsidEncoderFromMimeType(LPCTSTR format, LPCLSID lpClsid)
{
	UINT num, size;
	if( ::GetImageEncodersSize(&num, &size) != Ok ){
		::ShowLastError();
		return FALSE;
	}

	// バッファを確保
	ImageCodecInfo *info = (ImageCodecInfo *)::GlobalAlloc(GMEM_FIXED, size);

	// エンコーダーの情報を転送
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

// ファイル名をもとに、Enocderを取得します
BOOL Screenshot::GetClsidEncoderFromFileName(LPCTSTR fileName, LPCLSID lpClsid)
{
	UINT num, size;
	if( ::GetImageEncodersSize(&num, &size) != Ok ){
		::ShowLastError();
		return FALSE;
	}

	// バッファを確保
	ImageCodecInfo *info = (ImageCodecInfo *)::GlobalAlloc(GMEM_FIXED, size);

	// エンコーダーの情報を転送
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

// 指定されたファイル名で、hBitmapを保存します
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

// 指定のウインドウの、指定の範囲をスクリーンショットします
BOOL Screenshot::ScreenshotWindow(LPCTSTR fileName, HWND window, RECT *rect)
{
	HBITMAP hBitmap = ScreenshotInMemory(window, rect);
	//BOOL bRet = SaveToFileAutoDetectFormat(hBitmap, fileName);
  //BOOL bRet = SaveToPngFile(hBitmap, fileName);
  BOOL bRet = SaveToFileMagick(hBitmap, fileName);
	::DeleteObject(hBitmap);
	return bRet;
}

// ファイルに保存せず、HBITMAP形式で返却します
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

  // 実質的な減色処理

  // 色の統計を取得する
  // 最も使われてる上位256色のカラーパレットを作りたい
  map<uint, uint>color_sets;
  for(int y=0; y<height; y++){
    for(int x=0; x<width; x++){
      int r = p[y * bmp.bmWidthBytes + x * 4 + 2];   // R
      int g = p[y * bmp.bmWidthBytes + x * 4 + 1];   // G
      int b = p[y * bmp.bmWidthBytes + x * 4 + 0];   // B

      // intを32bitとして、R,G,Bの順番に格納する、んでそれをキーにしてmapに格納
      // mapの右側は個数(カウントされた)
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
  png_color colors[256] = {0}; // 完全に透過な黒が初期化値

  if(color_sets.size() <= 256){
    // color_setsの個数 == 登場する色数
    //::ErrorMessageBox(L"登場する色数: %d", color_sets.size());

    // 出現回数の多いモノを優先してパレットを作る、減色処理はしていない
    // 同じカウントの色が消えてしまう問題
    // value -> keyのmapに変換して(自動的にソートする)
    map<uint,uint>::iterator it = color_sets.begin();
    map<uint,uint> sorted_map;

    while(it != color_sets.end()){
      int count = it->second; // 色の出現回数を取得

      // その出現回数がすでに出力先マップに提供されていたら
      // かぶらないように+1する
      // まだかぶっていたらまた+1する、繰り返す
      int n = 256;
      while(n-- > 0){
        map<uint,uint>::iterator tt = sorted_map.find(count);
        if(tt == sorted_map.end()){
          break;
        }
        count++;
      }
      sorted_map.insert(map<uint,uint>::value_type(count, it->first));
      // 同じ回数出現する色だとこのときに消えてしまう可能性あり
      it++;
    }

    //::ErrorMessageBox(L"減色(されてしまった)後の色数: %d", sorted_map.size());

    // valueの高い順に256個取得してパレットを設定する
    int count = 0;
    map<uint,uint>::reverse_iterator rit = sorted_map.rbegin();
    while(rit != sorted_map.rend()){
      if(count >= 256){
        perror("256以上のカラーインデックスがあります");
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
    //::ErrorMessageBox(L"カラーパレットの数: %d", color_table_count);

    ::png_set_PLTE(png_ptr, info_ptr, colors, color_table_count);

    color_type = PNG_COLOR_TYPE_PALETTE;
  }else{
    // desktop nanode ALPHA ha iranai
    color_type = PNG_COLOR_TYPE_RGB;
  }

  // IHDRチャンク
  ::png_set_IHDR(png_ptr, info_ptr, width, height,
    depth,
    color_type,
    PNG_INTERLACE_NONE,
    //PNG_INTERLACE_ADAM7,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
    );

  // カキコミ関数の設定
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
        row_pointers[y][x * pixel_bytes + 3] = (png_byte)255; // A(255 = 不透明, 0 = 透明)
      }else if(color_type == PNG_COLOR_TYPE_PALETTE){
        // カラーインデックスの色を参照する
        BYTE orig_r = p[y * bmp.bmWidthBytes + x * 4 + 2];
        BYTE orig_g = p[y * bmp.bmWidthBytes + x * 4 + 1];
        BYTE orig_b = p[y * bmp.bmWidthBytes + x * 4 + 0];

        int index = ::find_bestcolor_by_clt(colors, color_table_count, orig_r, orig_g, orig_b);
        if(index == -1){
          perror("近い色が見つかりませんでした");
          exit(1);
        }
        row_pointers[y][x * pixel_bytes + 0] = index;
      }else{
        perror("未対応のフォーマットです"); // 未対応
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
  // HBITMAPからデータを取得
  BITMAP bmp;
  ::GetObject(hBitmap, sizeof(BITMAP), &bmp);
  int len = bmp.bmWidth * bmp.bmHeight * (bmp.bmBitsPixel / 8);
  BYTE *p = (BYTE *)malloc(len * sizeof(BYTE));
  ::GetBitmapBits(hBitmap, len, p);
  int width = bmp.bmWidth;
  int height = bmp.bmHeight;
  int depth = 8;

  // ImageMagickに渡す
  ::Magick::Image image;
  image.read(width, height, "BGRA", Magick::CharPixel, p);

  // ImageMagickで処理
  image.quantizeDither(true);
  image.quantizeColors(256); // 256に減色処理
  image.quantize();
  
  // HBITMAPに戻す
  image.write(0, 0, width, height, "BGRA", Magick::CharPixel, p);
  ::SetBitmapBits(hBitmap, len, p);

  return SaveToPngFile(hBitmap, fileName);
}

// Desktopのスクリーンショットを撮影してファイルに保存します
BOOL Screenshot::ScreenshotDesktop(LPCTSTR fileName, RECT *rect)
{
	return ScreenshotWindow(fileName, GetDesktopWindow(), rect);
}
