#include <string>
#include <iostream>
#include <windows.h>
#include <GdiPlus.h>

#include <curl/curl.h>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#pragma comment(lib,"gdiplus")

using namespace std;
using namespace rapidjson;

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
	using namespace Gdiplus;
	UINT  num = 0;          // number of image encoders
	UINT  size = 0;         // size of the image encoder array in bytes
	ImageCodecInfo* pImageCodecInfo = NULL;
	GetImageEncodersSize(&num, &size);
	if (size == 0)
		return -1;  // Failure
	pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL)
		return -1;  // Failure
	GetImageEncoders(num, size, pImageCodecInfo);
	for (UINT j = 0; j < num; ++j)
	{
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;  // Success
		}
	}
	free(pImageCodecInfo);
	return 0;
}

int writer(char *data, size_t size, size_t nmemb, std::string *outputData)
{
	int result = 0;
	if (outputData != NULL) {
		outputData->append(data, size * nmemb);
		result = size * nmemb;
	}

	// need to add else statement
	return result;
}

void toClipboard(string &s)
{
	char json[1024];
	strcpy(json, s.c_str());

	Document document;

	#if 0
		// "normal" parsing, decode strings to new buffers. Can use other input stream via ParseStream().
		if (document.Parse(json).HasParseError())
			return;
	#else
		// In-situ parsing, decode strings directly in the source string. Source must be string.
		{
			char buffer[1024];
			memcpy(buffer, s.c_str(), s.size());
			if (document.ParseInsitu(buffer).HasParseError())
				return;
		}
	#endif

	assert(document.IsObject());    // Document is a JSON value represents the root of DOM. Root can be either an object or array.
	assert(document.HasMember("url"));
	assert(document["url"].IsString());
	//printf("url = %s\n", document["url"].GetString());

	OpenClipboard(0);
	EmptyClipboard();
	HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, 1024);
	if (!hg) {
		CloseClipboard();
		return;
	}
	memcpy(GlobalLock(hg), document["url"].GetString(), 1024);
	GlobalUnlock(hg);
	SetClipboardData(CF_TEXT, hg);
	CloseClipboard();
	GlobalFree(hg);
}

int takeScreenshot()
{
	using namespace Gdiplus;
	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;

	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	{
		HDC scrdc, memdc;
		HBITMAP membit;
		scrdc = ::GetDC(0);
		int Height = GetSystemMetrics(SM_CYSCREEN);
		int Width = GetSystemMetrics(SM_CXSCREEN);
		memdc = CreateCompatibleDC(scrdc);
		membit = CreateCompatibleBitmap(scrdc, Width, Height);
		HBITMAP hOldBitmap = (HBITMAP)SelectObject(memdc, membit);
		BitBlt(memdc, 0, 0, Width, Height, scrdc, 0, 0, SRCCOPY);
		Gdiplus::Bitmap bitmap(membit, NULL);
		CLSID clsid;

		SYSTEMTIME SysTime;
		GetLocalTime(&SysTime);
		wchar_t fileName[200];
		memset(fileName, 0, sizeof(fileName));
		wsprintf(fileName, L"C:\\screens\\screen%02d.%02d.%02d-%02d.%02d.%02d.%02d.png", SysTime.wYear, SysTime.wMonth, SysTime.wDay, SysTime.wHour, SysTime.wMinute, SysTime.wSecond, SysTime.wMilliseconds);

		CreateDirectory(L"C:\\screens\\", NULL);

		GetEncoderClsid(L"image/png", &clsid);
		bitmap.Save(fileName, &clsid);
		SelectObject(memdc, hOldBitmap);
		DeleteObject(memdc);
		DeleteObject(membit);
		::ReleaseDC(0, scrdc);

		curl_global_init(CURL_GLOBAL_ALL);

		CURL* curl = curl_easy_init();
		if (curl) {
			std::string outputData;

			curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);

			curl_slist* headers = 0;
			// Disable "Expect: 100-continue"
			headers = curl_slist_append(headers, "Expect:");
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
			curl_easy_setopt(curl, CURLOPT_HTTPPOST, 1);

			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outputData);

			curl_httppost* post_start = 0;
			curl_httppost* post_end = 0;

			int len = wcslen(fileName);
			char* szFile = (char*)_alloca(len + 1);
			wcstombs(szFile, fileName, len + 1);

			curl_formadd(&post_start, &post_end,
				CURLFORM_COPYNAME, "file",
				CURLFORM_FILE, szFile,
				CURLFORM_END);

			curl_easy_setopt(curl, CURLOPT_HTTPPOST, post_start);

			curl_easy_setopt(curl, CURLOPT_URL, "https://dropfile.to/upload");

			curl_easy_perform(curl);

			curl_easy_cleanup(curl);
			curl_formfree(post_start);
			curl_slist_free_all(headers);

			std::cout << outputData << std::endl;
			toClipboard(outputData);
		}

		curl_global_cleanup();

	}
	GdiplusShutdown(gdiplusToken);

	return 0;
}

int main()
{
	printf("Usage: Press PrntScr to clipboard screenshot link.\n\n");

	enum { PRTSCRN_KEYID = 1 };
	RegisterHotKey(0, PRTSCRN_KEYID, MOD_NOREPEAT, 0x2C);
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0))
	{
		PeekMessage(&msg, 0, 0, 0, 0x0001);
		switch (msg.message) {
			case WM_HOTKEY:
				if (msg.wParam == PRTSCRN_KEYID) {
					HWND foreground = GetForegroundWindow();
					if (foreground)
					{
						WCHAR window_title[256];
						GetWindowText(foreground, window_title, 256);
						std::wcout << window_title << std::endl;
					}

					takeScreenshot();
				}
		}
	}
	return 0;
}