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
	
	// �ݒ���
	map<wstring, wstring> m_settings;

	// �w���ID, �f�[�^�Ńt�@�C����gyazo�T�[�o�[�ɑ��M���܂�
	string Gyazo::Upload(string id, string data);
	string Gyazo::UploadFile(string id, wstring filePath);
	string Gyazo::UploadFile(wstring filePath); // ID����������

	// gyazo�T�[�o�[��ɃA�b�v�����łɁA�N���b�v�{�[�h��URL�R�s�[������Ƃ�����
	BOOL Gyazo::UploadFileAndOpenURL(HWND hWnd, wstring filePath);
};
