// Tool.h
#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <easyx.h>
#pragma comment(lib,"MSIMG32.LIB")
class Tool
{
public:
	// 原样绘制（不拉伸）
	static void putimage_alpha(int x, int y, IMAGE* img)
	{
		int w = img->getwidth();
		int h = img->getheight();
		AlphaBlend(GetImageHDC(NULL), x, y, w, h, GetImageHDC(img), 0, 0, w, h,
			{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA });
	}
	// 拉伸绘制（指定目标宽高，保持 alpha 透明）
	static void putimage_alpha(int x, int y, int dstW, int dstH, IMAGE* img)
	{
		int sw = img->getwidth();
		int sh = img->getheight();
		AlphaBlend(GetImageHDC(NULL), x, y, dstW, dstH, GetImageHDC(img), 0, 0, sw, sh,
			{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA });
	}
};

#endif

//通用的工具，在其他项目亦可使用
