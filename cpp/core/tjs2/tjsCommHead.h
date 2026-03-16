//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// tjs common header
// TJS2 公共头文件 —— 预编译头的根节点，引入所有编译单元共用的低频变动头文件
//---------------------------------------------------------------------------

/*
        Add headers that would not be frequently changed.
        添加不会频繁变动的头文件。
*/
#ifndef tjsCommHeadH
#define tjsCommHeadH

#ifdef __WIN32__
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
// #include "targetver.h"
// #include <windows.h>

#include <vector>

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>
#define TJS_CRTDBG_MAP_ALLOC
#endif // _DEBUG

#endif

#include <cstring>

#ifndef __USE_UNIX98
#define __USE_UNIX98
#endif

#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <memory>

#include <vector>
#include <string>
#include <stdexcept>

#include "tjsConfig.h"
#include "tjs.h"

//---------------------------------------------------------------------------
#endif
