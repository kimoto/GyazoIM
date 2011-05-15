#include <Windows.h>
#include <time.h>
#include <tchar.h>
#include <map>
#include <fstream>
#include <sstream>
#include "Util.h"

#include <GdiPlus.h>
#pragma comment(lib, "gdiplus.lib")

#include <WinInet.h>
#pragma comment(lib, "wininet.lib")

using namespace std;

class Gyazo
{
private:
	BOOL _UploadFile(LPCTSTR fileName);
	BOOL SetClipboardText(LPCTSTR text);
	HINTERNET Gyazo::HTTPPost(map<wstring, wstring> config, string boundary_data);
	string Gyazo::HTTPPostAndReadData(map<wstring, wstring> config, string boundary_data);
	string Gyazo::HTTPPostMultipart(map<string, string> files);

public:
	Gyazo();
	~Gyazo();
	
	// 設定情報
	map<wstring, wstring> m_settings;

	// 指定のID, データでファイルをgyazoサーバーに送信します
	string Gyazo::Upload(string id, string data);
	string Gyazo::UploadFile(string id, wstring filePath);
	string Gyazo::UploadFile(wstring filePath); // ID自動生成版

	// gyazoサーバー上にアップしつつついでに、クリップボードにURLコピーしたりとかする
	BOOL Gyazo::UploadFileAndOpenURL(HWND hWnd, wstring filePath);
};
