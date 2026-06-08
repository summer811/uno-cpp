#pragma once
#include <string>
#include <conio.h>
#include <windows.h>
#include <graphics.h>

class Picture {
	private:
		string picturePath;
		int x, y, w, h;
		IMAGE img;
public:
	Picture(string picturePath_t, int x_t, int y_t, int w_t, int h_t)
		{
			picturePath = picturePath_t;
			x = x_t;
			y = y_t;
			w = w_t;
			h = h_t;
	#ifdef UNICODE
		{
			int n = MultiByteToWideChar(CP_ACP, 0, picturePath.c_str(), -1, nullptr, 0);
			std::wstring wpath(n, L'\0');
			MultiByteToWideChar(CP_ACP, 0, picturePath.c_str(), -1, &wpath[0], n);
			loadimage(&img, wpath.c_str());
		}
#else
		loadimage(&img, picturePath.c_str());
#endif
		}
		void draw() {
			putimage(x, y, &img);
		}
};
Picture bgp1("/Assets/Picture/bg.jpg", 0, 0, 200, 30);
Picture bgp2("/Assets/Picture/bg2.jpg", 0, 0, 200, 30);
Picture bgp3("/Assets/Picture/bg3.jpg", 0, 0, 200, 30);