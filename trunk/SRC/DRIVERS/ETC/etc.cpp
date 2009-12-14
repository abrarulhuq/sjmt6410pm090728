
#include <bsp.h>
#include "etc.h"
#include <iic.h>


#define MYMSG(x)	RETAILMSG(0, x)
#define MYERR(x)	RETAILMSG(1, x)

#define	PMIC_ADDR	0xCC


volatile S3C6410_GPIO_REG *g_pGPIOReg = NULL;
volatile S3C6410_SYSCON_REG *g_pSysConReg = NULL;
volatile BSP_ARGS *g_pBspArgs = NULL;
static HANDLE m_MutexEtc = NULL;
static HANDLE g_hFileI2C = INVALID_HANDLE_VALUE;



DWORD ETC_Init(DWORD dwContext);
BOOL ETC_Deinit(DWORD InitHandle);
DWORD ETC_Open(DWORD InitHandle, DWORD dwAccess, DWORD dwShareMode);
BOOL ETC_Close(DWORD OpenHandle);
BOOL ETC_IOControl(DWORD OpenHandle, DWORD dwIoControlCode,
    PBYTE pInBuf, DWORD nInBufSize,
    PBYTE pOutBuf, DWORD nOutBufSize, PDWORD pBytesReturned);
void ETC_PowerUp(DWORD InitHandle);
void ETC_PowerDown(DWORD InitHandle);



static BOOL i2c_Initialize(void)
{
	UINT32 IICClock, uiIICDelay, bytes;
	BOOL bRet;

	g_hFileI2C = CreateFile(_T("IIC0:"),
						GENERIC_READ|GENERIC_WRITE,
						FILE_SHARE_READ|FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, 0, 0);
	if (INVALID_HANDLE_VALUE == g_hFileI2C)
	{
		MYERR((_T("[ETC] g_hFileI2C = CreateFile(%d)\r\n"), GetLastError()));
		return FALSE;
	}

	IICClock = 100000;	// 100 kHz
	bRet = DeviceIoControl(g_hFileI2C, IOCTL_IIC_SET_CLOCK,
		&IICClock, sizeof(UINT32), NULL, 0, (LPDWORD)&bytes, NULL);
	if (FALSE == bRet)
	{
		MYERR((_T("[ETC] bRet = DeviceIoControl(IOCTL_IIC_SET_CLOCK)\r\n")));
		return FALSE;
	}

	uiIICDelay = Clk_0;
	bRet = DeviceIoControl(g_hFileI2C, IOCTL_IIC_SET_DELAY,
		&uiIICDelay, sizeof(UINT32), NULL, 0, (LPDWORD)&bytes, NULL);
	if (FALSE == bRet)
	{
		MYERR((_T("[ETC] bRet = DeviceIoControl(IOCTL_IIC_SET_DELAY)\r\n")));
		return FALSE;
	}

	return TRUE;
}
static void i2c_Deinitialize(void)
{
	if (INVALID_HANDLE_VALUE != g_hFileI2C)
	{
		CloseHandle(g_hFileI2C);
		g_hFileI2C = INVALID_HANDLE_VALUE;
	}
}
static UCHAR i2c_ReadRegister(UCHAR Reg)
{
	if (INVALID_HANDLE_VALUE == g_hFileI2C)
	{
		MYERR((_T("[ETC] INVALID_HANDLE_VALUE == g_hFileI2C\r\n")));
		return 0;
	}

	IIC_IO_DESC IIC_AddressData, IIC_Data;
	UCHAR buff[2] = {0,};
	DWORD bytes;
	BOOL bRet;

	IIC_AddressData.SlaveAddress = PMIC_ADDR;
	IIC_AddressData.Data = &Reg;
	IIC_AddressData.Count = 1;

	IIC_Data.SlaveAddress = PMIC_ADDR;
	IIC_Data.Data = buff;
	IIC_Data.Count = 1;

	bRet = DeviceIoControl(g_hFileI2C,  IOCTL_IIC_READ,
			&IIC_AddressData, sizeof(IIC_IO_DESC),
			&IIC_Data, sizeof(IIC_IO_DESC),
			&bytes, NULL);
	if (FALSE == bRet)
		MYERR((_T("[ETC] ERROR - IIC_ReadRegister(%d, %d)\r\n"), Reg, buff[0]));

	return buff[0];
}
static void i2c_WriteRegister(UCHAR Reg, UCHAR Val)
{
	if (INVALID_HANDLE_VALUE == g_hFileI2C)
	{
		MYERR((_T("[ETC] INVALID_HANDLE_VALUE == g_hFileI2C\r\n")));
		return;
	}

	IIC_IO_DESC IIC_Data;
	UCHAR buff[2];
	BOOL bRet;

	IIC_Data.SlaveAddress = PMIC_ADDR;
	IIC_Data.Data = buff;
	IIC_Data.Count = 2;

	buff[0] = Reg;
	buff[1] = Val;
	bRet = DeviceIoControl(g_hFileI2C, IOCTL_IIC_WRITE,
			&IIC_Data, sizeof(IIC_IO_DESC), NULL, 0, NULL, NULL);
	if (FALSE == bRet)
		MYERR((_T("[ETC] ERROR - IIC_WriteRegister(%d, %d)\r\n"), Reg, Val));
}



DWORD ETC_Init(DWORD dwContext)
{
	PHYSICAL_ADDRESS ioPhysicalBase = {0,0};

	ioPhysicalBase.LowPart = S3C6410_BASE_REG_PA_GPIO;
	g_pGPIOReg = (volatile S3C6410_GPIO_REG *)MmMapIoSpace(ioPhysicalBase, sizeof(S3C6410_GPIO_REG), FALSE);
	if (NULL == g_pGPIOReg)
	{
		MYERR((_T("[ETC] g_pGPIOReg = MmMapIoSpace()\r\n")));
		goto goto_err;
	}

	ioPhysicalBase.LowPart = S3C6410_BASE_REG_PA_SYSCON;
	g_pSysConReg = (volatile S3C6410_SYSCON_REG *)MmMapIoSpace(ioPhysicalBase, sizeof(S3C6410_SYSCON_REG), FALSE);
	if (NULL == g_pSysConReg)
	{
		MYERR((_T("[ETC] g_pSysConReg = MmMapIoSpace()\r\n")));
		goto goto_err;
	}

	ioPhysicalBase.LowPart = IMAGE_SHARE_ARGS_PA_START;
	g_pBspArgs = (volatile BSP_ARGS *)MmMapIoSpace(ioPhysicalBase, sizeof(BSP_ARGS), FALSE);
	if (NULL == g_pBspArgs)
	{
		MYERR((_T("[ETC] NULL == g_pBspArgs\r\n")));
		goto goto_err;
	}

	m_MutexEtc = CreateMutex(NULL, FALSE, NULL);
	if (NULL == m_MutexEtc)
	{
		MYERR((_T("[ETC] NULL == m_MutexEtc\r\n")));
		goto goto_err;
	}

	if (FALSE == i2c_Initialize())
	{
		MYERR((_T("[ETC] FALSE == i2c_Initialize()\r\n")));
		goto goto_err;
	}

    return 0x12345678;
goto_err:
	ETC_Deinit(0);

	return 0;
}

BOOL ETC_Deinit(DWORD InitHandle)
{
	i2c_Deinitialize();

	if (m_MutexEtc)
	{
		CloseHandle(m_MutexEtc);
		m_MutexEtc = NULL;
	}

	if (g_pBspArgs)
	{
		MmUnmapIoSpace((PVOID)g_pBspArgs, sizeof(BSP_ARGS));
		g_pBspArgs = NULL;
	}

	if (g_pSysConReg)
	{
		MmUnmapIoSpace((PVOID)g_pSysConReg, sizeof(S3C6410_SYSCON_REG));
		g_pSysConReg = NULL;
	}

	if (g_pGPIOReg)
	{
		MmUnmapIoSpace((PVOID)g_pGPIOReg, sizeof(S3C6410_GPIO_REG));
		g_pGPIOReg = NULL;
	}

	return TRUE;
}

DWORD ETC_Open(DWORD InitHandle, DWORD dwAccess, DWORD dwShareMode)
{
	return InitHandle;
}

BOOL ETC_Close(DWORD OpenHandle)
{
	return TRUE;
}

BOOL ETC_IOControl(DWORD OpenHandle, DWORD dwIoControlCode,
    PBYTE pInBuf, DWORD nInBufSize,
    PBYTE pOutBuf, DWORD nOutBufSize, PDWORD pBytesReturned)
{
	BOOL bRet = FALSE;
	HANDLE hEvent = NULL;

	if (WAIT_OBJECT_0 != WaitForSingleObject(m_MutexEtc, INFINITE))
		MYERR((_T("[ETC] WAIT_OBJECT_0 != WaitForSingleObject\r\n")));
	switch (dwIoControlCode)
	{
	case IOCTL_SET_POWER_WLAN:
		hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, _T("SDMMCCH2CardDetect_Event"));
		if (hEvent)
		{
			UCHAR i2c_Val = i2c_ReadRegister(0x00);
			MYMSG((_T("[ETC] %d = i2c_ReadRegister(0x00)\r\n"), i2c_Val));

			if ((BOOL)nInBufSize)
			{
				g_pGPIOReg->GPEDAT = (g_pGPIOReg->GPEDAT & ~(0x1<<0)) | (0x1<<0);

				g_pBspArgs->bSDMMCCH2CardDetect = TRUE;
				SetEvent(hEvent);

				i2c_Val |= (1<<2);
				i2c_WriteRegister(0x00, i2c_Val);
			}
			else
			{
				i2c_Val &= ~(1<<2);
				i2c_WriteRegister(0x00, i2c_Val);

				g_pBspArgs->bSDMMCCH2CardDetect = FALSE;
				SetEvent(hEvent);

				g_pGPIOReg->GPEDAT = (g_pGPIOReg->GPEDAT & ~(0x1<<0)) | (0x0<<0);
			}
			CloseHandle(hEvent);
		}
	case IOCTL_GET_POWER_WLAN:
		bRet = (g_pGPIOReg->GPEDAT & (0x1<<0));
		break;

	case IOCTL_SET_POWER_WCDMA:
		if ((BOOL)nInBufSize)
			g_pGPIOReg->GPEDAT = (g_pGPIOReg->GPEDAT & ~(0x1<<1)) | (0x1<<1);
		else
			g_pGPIOReg->GPEDAT = (g_pGPIOReg->GPEDAT & ~(0x1<<1)) | (0x0<<1);
	case IOCTL_GET_POWER_WCDMA:
		bRet = (g_pGPIOReg->GPEDAT & (0x1<<1));
		break;


	case IOCTL_SET_BOARD_UUID:
		if (pInBuf && 16 == nInBufSize)	// g_pBspArgs->uuid[16];
		{
			PVOID pMarshalledInBuf = NULL;
			if (FAILED(CeOpenCallerBuffer(&pMarshalledInBuf, pInBuf, nInBufSize, ARG_I_PTR, TRUE)))
			{
				RETAILMSG(1, (_T("ETC_IOControl: CeOpenCallerBuffer failed in IOCTL_SET_BOARD_UUID for IN buf.\r\n")));
				return FALSE;
			}

			memcpy((void *)&g_pBspArgs->uuid[0], pMarshalledInBuf, 16);

			if (FAILED(CeCloseCallerBuffer(pMarshalledInBuf, pInBuf, nInBufSize, ARG_I_PTR)))
			{
				RETAILMSG(1, (_T("ETC_IOControl: CeCloseCallerBuffer failed in IOCTL_SET_BOARD_UUID for IN buf.\r\n")));
				return FALSE;
			}
		}
		break;
	}
	ReleaseMutex(m_MutexEtc);

	return bRet;
}

void ETC_PowerUp(DWORD InitHandle)
{
}

void ETC_PowerDown(DWORD InitHandle)
{
}

BOOL WINAPI ETC_DllMain(HINSTANCE DllInstance, DWORD Reason, LPVOID Reserved)
{
	switch (Reason)
	{
	case DLL_PROCESS_ATTACH:
		DEBUGREGISTER(DllInstance);
		break;
	}
	return TRUE;
}
