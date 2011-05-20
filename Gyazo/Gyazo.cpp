#include "Gyazo.h"

Gyazo::Gyazo()
{
	// defaultの挙動
	this->m_settings[L"copy_url"] = L"yes";
	this->m_settings[L"open_browser"] = L"yes";
}

Gyazo::~Gyazo()
{
}

// id.txtを自動生成してる
string getId()
{
	const char*	 idFile			= "id.txt";
	string idStr;

	// まずはファイルから ID をロード
	ifstream ifs;

	ifs.open(idFile);
	if (!ifs.fail()) {
		// ID を読み込む
		ifs >> idStr;
		ifs.close();
	} else{
		// defaultを設定: 日付(strftime)
		char		timebuf[64];
		struct tm	dt;
		time_t		now	= time(NULL);

		localtime_s(&dt, &now);
		strftime(timebuf, 64, "%Y%m%d%H%M%S", &dt);
		
		// ID 確定
		idStr = timebuf;

		// ID を保存する
		ofstream ofs;
		ofs.open(idFile);
		ofs << idStr;
		ofs.close();
	}

	return idStr;
}

// 指定された URL (char*) をブラウザで開く
VOID execUrl(const char* str)
{
	size_t  slen;
	size_t  dcount;
	slen  = strlen(str) + 1; // NULL

	TCHAR *wcUrl = (TCHAR *)malloc(slen * sizeof(TCHAR));
	
	// ワイド文字に変換
	mbstowcs_s(&dcount, wcUrl, slen, str, slen);
	
	// open コマンドを実行
	SHELLEXECUTEINFO lsw = {0};
	lsw.cbSize = sizeof(SHELLEXECUTEINFO);
	lsw.lpVerb = _T("open");
	lsw.lpFile = wcUrl;

	ShellExecuteEx(&lsw);

	free(wcUrl);
}

// 任意のテキストをクリップボードに書き込みます
BOOL Gyazo::SetClipboardText(LPCTSTR string)
{
	int len = ::lstrlen(string) + 1; // NULL(+1)
	
	// '\0'用+1
	HANDLE windowsMemory = ::GlobalAlloc(GHND, len * sizeof(TCHAR));
	if(windowsMemory == NULL){
		return FALSE;
	}

	LPTSTR buffer = (LPTSTR)::GlobalLock(windowsMemory);
	if(buffer == NULL){
		::GlobalFree(windowsMemory);
		return FALSE;
	}
	::lstrcpy(buffer, string);
	::GlobalUnlock(windowsMemory);

	if( !::OpenClipboard(NULL) ){
		::GlobalFree(windowsMemory);
		return FALSE;
	}

	if( !::EmptyClipboard() ){
		::CloseClipboard();
		::GlobalFree(windowsMemory);
		return FALSE;
	}

	if( ::SetClipboardData(CF_UNICODETEXT, windowsMemory) == NULL ){
		::CloseClipboard();
		::GlobalFree(windowsMemory);
		return FALSE;
	}
	::CloseClipboard();
	::GlobalFree(windowsMemory); // 開放しちゃ駄目みたいだけど、実質解放しても機能してるので解放
	// おそらくクリップボード用のバッファにすでに複製されてる??
	// あるいはシステム側に保護されてる?
	return TRUE;
}

string makeBoundary(map<string,string> data, string boundary)
{
	const string crlf = "\r\n";
	::ostringstream buf;

	// メッセージの構成
	// -- "id" part
	buf << "--" + boundary + crlf;
	buf << "content-disposition: form-data; name=\"id\"";
	buf << crlf;
	buf << crlf;
	buf << data["id"];
	buf << crlf;
	
	// -- "imagedata" part
	buf << "--" + boundary + crlf;
	buf << "content-disposition: form-data; name=\"imagedata\"; filename=\"data.png\"" + crlf;
	buf << "Content-type: image/png" + crlf;
	buf << crlf;

	buf << data["data.png"];

	buf << crlf;
	buf << "--" + boundary + "--";
	buf << crlf;
	return buf.str();
}

// HTTP Postします(multipart/form-data only)
// headerも設定できる
HINTERNET Gyazo::HTTPPost(map<wstring, wstring> config, string boundary_data)
{
	trace(L"Gyazo::HTTPPost start\n");

	wstring multipart_header(L"Content-type: multipart/form-data; boundary=");
	multipart_header += L"----BOUNDARYBOUNDARY----";

	LPCWSTR lpwcUploadServer;	// アップロード先サーバ
	LPCWSTR lpwcUploadPath;		// アップロード先パス

	LPCWSTR lpwcId;			// 認証用ID
	LPCWSTR lpwcPassword;	// 認証用パスワード

	// アップロード先
	if (m_settings.count(L"upload_server")) {
		lpwcUploadServer = m_settings[L"upload_server"].c_str();
	}else{
		lpwcUploadServer = L"gyazo.com";
	}
	if (m_settings.count(L"upload_path")) {
		lpwcUploadPath = m_settings[L"upload_path"].c_str();
	}else{
		lpwcUploadPath = L"/upload.cgi";
	}

	// 認証データ準備
	if (m_settings.count(L"use_auth") && m_settings[L"use_auth"] == L"yes") {
		if (m_settings.count(L"auth_id")) {
			lpwcId = m_settings[L"auth_id"].c_str();
		}else{
			lpwcId = L"";
		}
		if (m_settings.count(L"auth_pw")) {
			lpwcPassword = m_settings[L"auth_pw"].c_str();
		}else{
			lpwcPassword = L"";
		}
	}else{
		lpwcId = NULL;
		lpwcPassword = NULL;
	}

	// WinInet を準備 (proxy は 規定の設定を利用)
	HINTERNET hSession    = InternetOpen(NULL, 
		INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if(NULL == hSession) {
		trace(L"cannot configure wininet");
		return NULL;
	}

	// SSL
	DWORD dwFlags = INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_RELOAD;
	if (m_settings.count(L"use_ssl") && m_settings[L"use_ssl"] == L"yes") {
		dwFlags |= INTERNET_FLAG_SECURE;
		if (m_settings.count(L"ssl_check_cert") && m_settings[L"ssl_check_cert"] == L"no") {
			dwFlags |= INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
		}
	}

	// 接続先
	HINTERNET hConnection = InternetConnect(hSession, 
		lpwcUploadServer, INTERNET_DEFAULT_HTTP_PORT,
		lpwcId, lpwcPassword, INTERNET_SERVICE_HTTP, 0, NULL);
	if(NULL == hSession) {
		trace(L"cannot initiate connection");
		return NULL;
	}

	// 要求先の設定
	HINTERNET hRequest    = HttpOpenRequest(hConnection,
		_T("POST"), lpwcUploadPath, NULL,
		NULL, NULL, dwFlags, NULL);
	if(NULL == hSession) {
		trace(L"cannot compose post request");
		return NULL;
	}
	
	// UserAgentの設定
	LPCTSTR ua = _T("User-Agent: Gyazowin/1.0\r\n");
	BOOL bResult = HttpAddRequestHeaders(
		hRequest, ua, _tcslen(ua),
		HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
	if (FALSE == bResult) {
		trace(L"Cannot set user agent");
		return NULL;
	}

	if ( !HttpSendRequest(hRequest,
                (LPTSTR)multipart_header.c_str(),
				lstrlen((LPTSTR)multipart_header.c_str()),
                (LPVOID)boundary_data.c_str(),
				(DWORD)boundary_data.length()) )
	{
		trace(L"cannot send http request");
		::ShowLastError();
		return NULL;
	}
	return hRequest;
}

// HTTPPostしてResponseをstringで返却します
string Gyazo::HTTPPostAndReadData(map<wstring, wstring> config, string boundary_data)
{
	HINTERNET hRequest = HTTPPost(config, boundary_data);
	if(hRequest == NULL){
		trace(L"failed to upload\n");
		throw new exception("failed to upload");
		//return NULL;
	}

	// 要求を送信
	// 要求は成功
	DWORD resLen = 8;
	TCHAR resCode[8];
	
	// status code を取得
	HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE, resCode, &resLen, 0);
	if( _ttoi(resCode) != 200 ) {
		trace(L"error status code: %d\n", resCode);
		return NULL;
	}

	// upload 成功，結果 (URL) を読取る
	DWORD len;
	char  resbuf[1024];
	string result;

	while(InternetReadFile(hRequest, (LPVOID) resbuf, 1024, &len) && len != 0){
		result.append(resbuf, len);
	}
	
	// 取得結果は NULL terminate されていないので
	result += '\0';

	::InternetCloseHandle(hRequest);
	return result;
}

// 指定されたmapを、multipart/form-dataで送信し、その返却値をstringで得ます
string Gyazo::HTTPPostMultipart(map<string, string> files)
{
	const string boundary = "----BOUNDARYBOUNDARY----";
	return HTTPPostAndReadData(this->m_settings, makeBoundary(files, boundary));
}

// 指定のID, データでファイルをgyazoサーバーに送信します
// 返却値はstring
string Gyazo::Upload(string id, string data)
{
	map<string,string> m;
	m["id"] = id; // IDを取得
	m["data.png"] = data;
	return HTTPPostMultipart(m);
}

string Gyazo::UploadFile(string id, wstring filePath)
{
	ifstream png;
	png.open(filePath, ios::binary);
	if (png.fail()) {
		trace(L"png open failed");
		png.close();
		return NULL;
	}

	ostringstream o;
	o << png.rdbuf();
	string data = o.str();
	png.close();

	return Upload(id, data);
}

string Gyazo::UploadFile(wstring filePath)
{
	return UploadFile(::getId(), filePath);
}

BOOL Gyazo::UploadFileAndOpenURL(HWND hWnd, wstring filePath)
{
	// アップロード確認
	if (m_settings.count(L"up_dialog") && m_settings[L"up_dialog"] == L"yes") {
		if (MessageBox(hWnd,_T("アップロードしますか？"),_T("Question"),MB_OK|MB_ICONQUESTION|MB_YESNO) != IDYES) {
			return FALSE;
		}
	}
	
	// gyazoサーバーにアップロードします
	string clipURL = UploadFile(getId(), filePath);
	wstring wClipURL = str2wstr(clipURL);
	trace(L"gyazo URL: %s\n", wClipURL.c_str());

	// クリップボードに URL をコピー
	if (m_settings.count(L"copy_url") && m_settings[L"copy_url"] == L"yes") {
		this->SetClipboardText(wClipURL.c_str());
	}
			
	// URL を起動
	if (m_settings.count(L"open_browser") && m_settings[L"open_browser"] == L"yes") {
		execUrl(clipURL.c_str());
	}
	return TRUE;
}
