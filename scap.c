#include "scap.h"
#ifdef _ENABLE_SCAP
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

//We will compress our screenshots
//extern "C"
//{
	#include "zlib.h"
//}

void take_screenshot(const wchar_t* fname)
{
	HDC hScreenDC,hDC;
	HBITMAP hBitmap;
	HGDIOBJ hOld;
	BITMAPFILEHEADER bmpFile;
	BITMAPINFO bmpInfo;
	int width,height;
	char* pixels;
	gzFile out;
	 
	hScreenDC = GetDC(0);

	width = GetDeviceCaps(hScreenDC, HORZRES);
	height = GetDeviceCaps(hScreenDC, VERTRES);
	
	hDC = CreateCompatibleDC(hScreenDC);
	hBitmap = CreateCompatibleBitmap(hScreenDC,width,height);
	hOld = SelectObject(hDC,hBitmap);
	BitBlt(hDC,0,0,width,height,hScreenDC,0,0,SRCCOPY|CAPTUREBLT);
	SelectObject(hDC,hOld);
	DeleteObject(hDC);
	
	memset(&bmpFile,'\0',sizeof(BITMAPFILEHEADER));
	memset(&bmpInfo,'\0',sizeof(BITMAPINFO));
	
	bmpInfo.bmiHeader.biSize = sizeof(bmpInfo.bmiHeader);
	bmpInfo.bmiHeader.biWidth = width;
	bmpInfo.bmiHeader.biHeight = height;
	bmpInfo.bmiHeader.biBitCount = 16;
	bmpInfo.bmiHeader.biPlanes = 1;
	bmpInfo.bmiHeader.biClrUsed = 0;
	bmpInfo.bmiHeader.biClrImportant = 0;
	
	GetDIBits(hScreenDC,hBitmap,0,0,NULL,&bmpInfo,DIB_RGB_COLORS);
	
	pixels = (char*)malloc(bmpInfo.bmiHeader.biSizeImage);
	bmpInfo.bmiHeader.biCompression = BI_RGB;
	
	GetDIBits(hScreenDC,hBitmap,0,height,pixels,&bmpInfo,DIB_RGB_COLORS);
	
	DeleteObject(hBitmap);
	ReleaseDC(NULL,hScreenDC);
	
	bmpFile.bfType = 0x4D42;
	bmpFile.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFO);
	bmpFile.bfSize = bmpFile.bfOffBits + bmpInfo.bmiHeader.biSizeImage;
	
	out = gzopen_w(fname,"wb");
	gzwrite(out,&bmpFile,sizeof(BITMAPFILEHEADER));
	gzwrite(out,&bmpInfo,sizeof(BITMAPINFO));
	gzwrite(out,pixels,bmpInfo.bmiHeader.biSizeImage);
	gzclose(out);
	
	free(pixels);
}

#endif