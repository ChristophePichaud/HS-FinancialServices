// stdafx.h
// Precompiled header for the HS Financial Services MFC client application.
//
// Includes only stable, rarely-changing system and MFC headers so that the
// build system can cache this translation unit.

#pragma once

// Exclude rarely-used parts of Windows headers.
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRA_LEAN
#define NOMINMAX

// Target Windows 7 and later.
#ifndef WINVER
#  define WINVER       0x0601
#endif
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif

// MFC core and standard components.
#include <afxwin.h>
#include <afxext.h>
#include <afxdialogex.h>
#include <afxcmn.h>

// Winsock 2 (must come before any Windows.h includes not already pulled in).
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

// Standard library headers used throughout the application.
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdint>
#include <cassert>
