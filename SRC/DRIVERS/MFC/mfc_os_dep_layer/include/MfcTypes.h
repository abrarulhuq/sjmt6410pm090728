//
// Copyright (c) Samsung Electronics. Co. LTD.  All rights reserved.
//
/*++
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

*/


#ifndef __SAMSUNG_SYSLSI_APDEV_MFC_TYPE_H__
#define __SAMSUNG_SYSLSI_APDEV_MFC_TYPE_H__

#ifdef __cplusplus
extern "C" {
#endif


#ifdef _WIN32_WCE
#include <windows.h>

#else

#include <linux/types.h>

typedef enum {FALSE, TRUE} BOOL;

#endif


#ifdef __cplusplus
}
#endif

#endif /* __SAMSUNG_SYSLSI_APDEV_MFC_TYPE_H__ */
