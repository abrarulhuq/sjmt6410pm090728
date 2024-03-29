//
// Copyright (c) Samsung Electronics. Co. LTD.  All rights reserved.
//
/*++
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

*/


#include <windows.h>
#include <pmplatform.h>
//#define DEBUG    1

static HANDLE    hMutex;

/*----------------------------------------------------------------------------
*Function: CreateUAOmutex
*Implementation Notes: Create Mutex handle 
-----------------------------------------------------------------------------*/
HANDLE CreateUAOMutex(void)
{

    hMutex = CreateMutex( NULL,                       // default security attributes
                          FALSE,                      // initially not owned
                          NULL);                      // unnamed mutex
    
    return hMutex;
}

/*----------------------------------------------------------------------------
*Function: LockUAOMutex
*Implementation Notes: lock mutex 
-----------------------------------------------------------------------------*/
DWORD LockUAOMutex(void)
{
    DWORD                Status;
        

    Status = WaitForSingleObject(hMutex, INFINITE);
    if(Status == WAIT_OBJECT_0) 
    {
        Status = TRUE;
    } 
    else 
    {
        Status = GetLastError();
        RETAILMSG(1,(TEXT("DD::MUTEX LOCK ERROR : %d\r\n"),Status));
        Status = FALSE;
    }
    
      
    return Status;
}

/*----------------------------------------------------------------------------
*Function: UnlockUAOMutex
*Implementation Notes: unlock mutex
-----------------------------------------------------------------------------*/
DWORD UnlockUAOMutex(void)
{
    DWORD                Status;
   
    Status = ReleaseMutex(hMutex);
    if(Status != TRUE) 
    {
        Status = GetLastError();
        RETAILMSG(1,(TEXT("DD::MUTEX UNLOCK ERROR : %d\r\n"),Status));
        
    }
          
    return Status;
}

/*----------------------------------------------------------------------------
*Function: DeleteUAOMutex
*Implementation Notes: delete mutex handle 
-----------------------------------------------------------------------------*/
void DeleteUAOMutex(void)
{
    if (hMutex == NULL)
        return;

    CloseHandle(hMutex);
}



/*----------------------------------------------------------------------------
*Function: printD

*Parameters:         fmt        :
*Implementation Notes: Debug print
-----------------------------------------------------------------------------*/
#ifdef DEBUG
void printD(char* fmt, ...) 
{
    char str[512];
    wchar_t wstr[512];
    int alen;
    int wlen;

    vsprintf(str, fmt, (char *)(&fmt+1));
    alen = strlen(str);
    wlen = MultiByteToWideChar(CP_ACP, 0, str, alen, 0, 0);
    MultiByteToWideChar(CP_ACP, 0, str, alen, wstr, wlen);
    wstr[wlen] = 0;
    NKDbgPrintfW(wstr);

}
#else
void printD(char *fmt, ...)
{
}
#endif

