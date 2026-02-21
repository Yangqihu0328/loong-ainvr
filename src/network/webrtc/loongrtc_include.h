// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_WEBRTC_LOONGRTC_INCLUDE_H_
#define LOONG_NETWORK_WEBRTC_LOONGRTC_INCLUDE_H_

// loong-rtc is a C library whose headers contain patterns incompatible with
// strict C++ mode (e.g. struct members sharing a name with their typedef,
// and macro redefinitions of system symbols like STATUS).  Treating the
// header as a system header suppresses all diagnostics originating from it.
#pragma GCC system_header

extern "C" {
#include <loongrtc/loongrtc.h>
}

#endif  // LOONG_NETWORK_WEBRTC_LOONGRTC_INCLUDE_H_
