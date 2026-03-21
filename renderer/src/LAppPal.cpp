/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppPal.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <Model/CubismMoc.hpp>
#include "LAppDefine.hpp"

#ifdef _WIN32
#include <Windows.h>

static std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wide[0], wlen);
    return wide;
}
#endif

using std::endl;
using namespace Csm;
using namespace std;
using namespace LAppDefine;

double LAppPal::s_currentFrame = 0.0;
double LAppPal::s_lastFrame = 0.0;
double LAppPal::s_deltaTime = 0.0;

csmByte* LAppPal::LoadFileAsBytes(const string filePath, csmSizeInt* outSize)
{
    const char* path = filePath.c_str();

    int size = 0;

#ifdef _WIN32
    std::wstring widePath = Utf8ToWide(filePath);
    struct _stat statBuf;
    if (_wstat(widePath.c_str(), &statBuf) == 0)
#else
    struct stat statBuf;
    if (stat(path, &statBuf) == 0)
#endif
    {
        size = statBuf.st_size;

        if (size == 0)
        {
            if (DebugLogEnable)
            {
                PrintLogLn("Stat succeeded but file size is zero. path:%s", path);
            }
            return NULL;
        }
    }
    else
    {
        if (DebugLogEnable)
        {
            PrintLogLn("Stat failed. errno:%d path:%s", errno, path);
        }
        return NULL;
    }

    std::fstream file;
#ifdef _WIN32
    file.open(widePath.c_str(), std::ios::in | std::ios::binary);
#else
    file.open(path, std::ios::in | std::ios::binary);
#endif
    if (!file.is_open())
    {
        if (DebugLogEnable)
        {
            PrintLogLn("File open failed. path:%s", path);
        }
        return NULL;
    }

    char* buf = new char[size];
    file.read(buf, size);
    file.close();

    *outSize = size;
    return reinterpret_cast<csmByte*>(buf);
}

void LAppPal::ReleaseBytes(csmByte* byteData)
{
    delete[] byteData;
}

csmFloat32  LAppPal::GetDeltaTime()
{
    return static_cast<csmFloat32>(s_deltaTime);
}

void LAppPal::UpdateTime()
{
    s_currentFrame = glfwGetTime();
    s_deltaTime = s_currentFrame - s_lastFrame;
    s_lastFrame = s_currentFrame;
}

void LAppPal::PrintLog(const csmChar* format, ...)
{
    va_list args;
    csmChar buf[256];
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args); // 標準出力でレンダリング
    std::cout << buf;
    va_end(args);
}

void LAppPal::PrintLogLn(const csmChar* format, ...)
{
    va_list args;
    csmChar buf[256];
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args); // 標準出力でレンダリング
    std::cout << buf << std::endl;
    va_end(args);
}

void LAppPal::PrintMessage(const csmChar* message)
{
    PrintLog("%s", message);
}

void LAppPal::PrintMessageLn(const csmChar* message)
{
    PrintLogLn("%s", message);
}

bool LAppPal::FileExists(const std::string& path)
{
#ifdef _WIN32
    std::wstring widePath = Utf8ToWide(path);
    struct _stat st;
    return _wstat(widePath.c_str(), &st) == 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}
