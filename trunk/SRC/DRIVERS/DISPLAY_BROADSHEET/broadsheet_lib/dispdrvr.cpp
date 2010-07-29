
#include <windows.h>
#include <winddi.h>
#include <gpe.h>

#include <bsp.h>
#include "dispdrvr.h"
#include "dirtyrect.h"
#include "s1d13521.h"


#define MYMSG(x)	RETAILMSG(0, x)
#define MYERR(x)	RETAILMSG(1, x)

#ifdef __cplusplus
extern "C" {
#endif
extern DWORD	g_dwDebugLevel;
extern BOOL		g_bDirtyRect;
extern BOOL		g_bDirtyRectNotify;
extern void S1d13521Initialize(void *pS1d13521, void *pGPIOReg);
extern void S1d13521SetDibBuffer(void *pv);
extern void S1d13521PowerHandler(BOOL bOff);
extern ULONG S1d13521DrvEscape(ULONG iEsc,	ULONG cjIn, PVOID pvIn, ULONG cjOut, PVOID pvOut);
#ifdef __cplusplus
}
#endif 


#define S1D13521_BASE_PA	0x30000000
static volatile S1D13521_REG *g_pS1D13521Reg = NULL;
static volatile S3C6410_GPIO_REG *g_pGPIOPReg = NULL;
static volatile S3C6410_SROMCON_REG *g_pSROMReg = NULL;

static void workDirtyRect(RECT rect);
static CRITICAL_SECTION	g_CS;
static HDIRTYRECT g_hDirtyRect = NULL;
static HANDLE g_hMQ = NULL;


void *DispDrvrPhysicalFrameBuffer = (void *)0;    
int DispDrvr_cxScreen = LCD_WIDTH;
int DispDrvr_cyScreen = LCD_HEIGHT;
int DispDrvr_fbSize = (LCD_WIDTH * LCD_HEIGHT / 2);
int DispDrvr_bpp = LCD_BPP;
#if	(LCD_BPP == 4)
EGPEFormat DispDrvr_format = gpe4Bpp;
int DispDrvr_palSize = 16;
RGBQUAD DispDrvr_palette[16];
#elif (LCD_BPP == 8)
EGPEFormat DispDrvr_format = gpe8Bpp;
int DispDrvr_palSize = 256;
RGBQUAD DispDrvr_palette[256];
#else
#error LCD_BPP must be 4 or 8
#endif



void DispDrvrInitialize(void)
{
	PHYSICAL_ADDRESS ioPhysicalBase = {0,0};

	ioPhysicalBase.LowPart = S3C6410_BASE_REG_PA_SROMCON;
	g_pSROMReg = (volatile S3C6410_SROMCON_REG *)MmMapIoSpace(ioPhysicalBase, sizeof(S3C6410_SROMCON_REG), FALSE);
	if (NULL == g_pSROMReg)
	{
		MYERR((_T("[S1D13521_ERR] pSROMregs = MmMapIoSpace()\r\n")));
		return;
	}
	g_pSROMReg->SROM_BW = (g_pSROMReg->SROM_BW & ~(0xF<<16)) |
							(0<<19) |		// nWBE/nBE(for UB/LB) control for Memory Bank1(0=Not using UB/LB, 1=Using UB/LB)
							(0<<18) |		// Wait enable control for Memory Bank1 (0=WAIT disable, 1=WAIT enable)
							(1<<16);		// Data bus width control for Memory Bank1 (0=8-bit, 1=16-bit)
	g_pSROMReg->SROM_BC4 = (0x7<<28) |	// Tacs
							(0x7<<24) | 	// Tcos
							(0xF<<16) | 	// Tacc
							(0x7<<12) | 	// Tcoh
							(0x7<< 8) | 	// Tah
							(0x7<< 4) | 	// Tacp
							(0x0<< 0);		// PMC

	ioPhysicalBase.LowPart = S3C6410_BASE_REG_PA_GPIO;
	g_pGPIOPReg = (volatile S3C6410_GPIO_REG *)MmMapIoSpace(ioPhysicalBase, sizeof(S3C6410_GPIO_REG), FALSE);
	if (NULL == g_pGPIOPReg)
	{
		MYERR((_T("[S1D13521_ERR] g_pGPIOPReg = MmMapIoSpace()\r\n")));
		return;
	}
	// GPN[8] : EPD_HRDY(8)
	g_pGPIOPReg->GPNCON = (g_pGPIOPReg->GPNCON & ~(0x3<<16)) | (0x0<<16);	// input mode
	g_pGPIOPReg->GPNPUD = (g_pGPIOPReg->GPNPUD & ~(0x3<<16)) | (0x0<<16);	// pull-up/down disable

	ioPhysicalBase.LowPart = S1D13521_BASE_PA;
	g_pS1D13521Reg = (volatile S1D13521_REG *)MmMapIoSpace(ioPhysicalBase, sizeof(S1D13521_REG), FALSE);
	if (NULL == g_pS1D13521Reg)
	{
		MYERR((_T("[S1D13521_ERR] g_pS1D13521Reg != MmMapIoSpace()\r\n")));
		return;
	}

	for (int i=0, j=0, v=0; i<DispDrvr_palSize; i++)
	{
#if	(LCD_BPP == 4)
		v = i * 0x11;
#elif	(LCD_BPP == 8)
		v = i;
#endif
		DispDrvr_palette[i].rgbRed = DispDrvr_palette[i].rgbGreen = DispDrvr_palette[i].rgbBlue = v;
		DispDrvr_palette[i].rgbReserved = 0;
	}

	InitializeCriticalSection(&g_CS);
	g_hDirtyRect = DirtyRect_Init(workDirtyRect);
	S1d13521Initialize((void *)g_pS1D13521Reg, (void *)g_pGPIOPReg);

	MSGQUEUEOPTIONS msgopts;
	memset(&msgopts, 0, sizeof(msgopts));
	msgopts.dwSize = sizeof(msgopts);
	msgopts.dwFlags = 0;	//MSGQUEUE_ALLOW_BROKEN;
	msgopts.dwMaxMessages = 0;	// no max number of messages
	msgopts.cbMaxMessage = sizeof(DIRTYRECTINFO);	// max size of each msg
	msgopts.bReadAccess = FALSE;
	g_hMQ = CreateMsgQueue(MSGQUEUE_NAME, &msgopts);
	MYMSG((_T("[S1D13521_MSG] CreateMsgQueue(Write) == 0x%X\r\n"), g_hMQ));
}

void DispDrvrSetDibBuffer(void *pv)
{
	S1d13521SetDibBuffer(pv);
}

void DispDrvrDirtyRectDump(LPCRECT prc)
{
	DirtyRect_Add(g_hDirtyRect, *prc);
}

void DispDrvrPowerHandler(BOOL bOff)
{
	if (bOff)
	{
		// MEM0_WEn[15:14] - output0(00), output1(01), output disable(1x)
		g_pGPIOPReg->MEM0CONSLP0 = (g_pGPIOPReg->MEM0CONSLP0 & ~(0x3<<14)) | (0x1<<14);
		// MEM0_CSn[13:12] - output0(00), output1(01), output disable(1x)
		g_pGPIOPReg->MEM0CONSLP0 = (g_pGPIOPReg->MEM0CONSLP0 & ~(0x3<<12)) | (0x1<<12);
		// MEM0_D[3:0] - output0(0000), output1(0001), output disable(1x)
		g_pGPIOPReg->MEM0CONSLP0 = (g_pGPIOPReg->MEM0CONSLP0 & ~(0xF<<0)) | (0x0<<0);
		// MEM0_WAIT[19:18] - output0(00), output1(01), input(1x)
		g_pGPIOPReg->MEM0CONSLP1 = (g_pGPIOPReg->MEM0CONSLP1 & ~(0x3<<18)) | (0x2<<18);
		// MEM0_OEn[1:0] - output0(00), output1(01), output disable(1x)
		g_pGPIOPReg->MEM0CONSLP1 = (g_pGPIOPReg->MEM0CONSLP1 & ~(0x3<<0)) | (0x1<<0);
	}
	else
	{
		g_pSROMReg->SROM_BW = (g_pSROMReg->SROM_BW & ~(0xF<<16)) |
								(0<<19) |	// nWBE/nBE(for UB/LB) control for Memory Bank1(0=Not using UB/LB, 1=Using UB/LB)
								(0<<18) |	// Wait enable control for Memory Bank1 (0=WAIT disable, 1=WAIT enable)
								(1<<16);	// Data bus width control for Memory Bank1 (0=8-bit, 1=16-bit)
		g_pSROMReg->SROM_BC4 = (0x7<<28) |	// Tacs
								(0x7<<24) | // Tcos
								(0xF<<16) | // Tacc
								(0x7<<12) | // Tcoh
								(0x7<< 8) | // Tah
								(0x7<< 4) | // Tacp
								(0x0<< 0);	// PMC
	}
	S1d13521PowerHandler(bOff);
}

ULONG DispDrvrDrvEscape(SURFOBJ *pso, ULONG iEsc,
	ULONG cjIn, PVOID pvIn, ULONG cjOut, PVOID pvOut)
{
	int nRetVal = 0;	// default return value: "not supported"

	switch (iEsc)
	{
	case QUERYESCSUPPORT:
		if ((DRVESC_BASE < *(DWORD *)pvIn) && (DRVESC_MAX > *(DWORD *)pvIn))
			nRetVal = 1;
		break;

	default:
		if (DRVESC_BASE < iEsc && DRVESC_MAX > iEsc)
		{
			EnterCriticalSection(&g_CS);
			nRetVal = S1d13521DrvEscape(iEsc, cjIn, pvIn, cjOut, pvOut);
			LeaveCriticalSection(&g_CS);
		}
		break;
	}

	return nRetVal;
}


static void workDirtyRect(RECT rect)
{
	if (g_bDirtyRect)
	{
		IMAGERECT imgRect = {NULL, &rect};	// NULL(GPE BUFFER), rect(DIRTY RECT)

		EnterCriticalSection(&g_CS);
		S1d13521DrvEscape(DRVESC_IMAGE_UPDATE, sizeof(IMAGERECT), (PVOID)&imgRect, 0, NULL);
		LeaveCriticalSection(&g_CS);
	}
	if (g_bDirtyRectNotify)
	{
		DIRTYRECTINFO dri = {0,};
		DWORD dwFlags = 0;	// MSGQUEUE_MSGALERT;

		memcpy(&dri.rect, &rect, sizeof(rect));
		dri.time = GetTickCount();
		if (FALSE == WriteMsgQueue(g_hMQ, (LPVOID)&dri, sizeof(dri), 0, dwFlags))
		{
			DWORD dwError = GetLastError();

			MYERR((_T("[S1D13521_ERR] WriteMsgQueue() == %d, "), dwError));
			switch (dwError)
			{
			case ERROR_OUTOFMEMORY:
				MYERR((_T("ERROR_OUTOFMEMORY\r\n")));
				break;
			case ERROR_INSUFFICIENT_BUFFER:
				MYERR((_T("ERROR_INSUFFICIENT_BUFFER\r\n")));
				break;
			case ERROR_PIPE_NOT_CONNECTED:
				MYERR((_T("ERROR_PIPE_NOT_CONNECTED. Notify Stop.\r\n")));
				g_bDirtyRectNotify = FALSE;
				break;
			case ERROR_TIMEOUT:
				MYERR((_T("ERROR_TIMEOUT\r\n")));
				break;
			default:
				MYERR((_T("ERROR_UNKNOWN ???\r\n")));
				break;
			}
		}
	}
	if (g_dwDebugLevel)
	{
		RETAILMSG((DRVESC_SET_DIRTYRECT==g_dwDebugLevel || DRVESC_GET_DIRTYRECT==g_dwDebugLevel),
			(_T("%d workDirtyRect(%d, %d, %d, %d)\r\n"), g_bDirtyRect, rect.left, rect.top, rect.right, rect.bottom));
	}
}

