/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2014 - 2019
*
*  TITLE:       MAIN.C
*
*  VERSION:     1.100
*
*  DATE:        04 Jan 2019
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/

#include "global.h"

#pragma data_seg("shrd")
volatile LONG           g_lApplicationInstances = 0;
#pragma data_seg()

#define TsmiParamsKey   L"Parameters"
#define TsmiVBoxDD      L"VBoxDD.dll"

#define T_PROGRAMTITLE  L"VirtualBox Hardened Loader v1.10.0.1901"

TABLE_DESC              g_PatchData = { NULL, 0 };

//
// Help output.
//
#define T_HELP	L"Sets parameters for Tsugumi driver.\r\n\r\n\
Optional parameters to execute: \r\n\r\n\
LOADER [/s] or [Table]\r\n\r\n\
  /s - stop monitoring and purge system cache.\r\n\
  Table - optional, custom VBoxDD patch table fullpath.\r\n\r\n\
  Example: ldr.exe vboxdd.bin"

/*
* SetTsmiParams
*
* Purpose:
*
* Set patch chains data to the registry.
*
*/
BOOL SetTsmiParams(
    VOID
)
{
    BOOL cond = FALSE, bResult = FALSE;
    HKEY hRootKey, hParamsKey;
    LRESULT lRet = ERROR_BAD_ARGUMENTS;

    hRootKey = NULL;
    hParamsKey = NULL;

    do {

        lRet = RegCreateKeyEx(HKEY_LOCAL_MACHINE, L"Software\\Tsugumi", 0, NULL, 0, KEY_ALL_ACCESS,
            NULL, &hRootKey, NULL);

        if ((lRet != ERROR_SUCCESS) || (hRootKey == NULL)) {
            cuiPrintText(TEXT("Ldr: Cannot create/open Tsugumi key"), TRUE);
            break;
        }

        lRet = RegCreateKey(hRootKey, TsmiParamsKey, &hParamsKey);
        if ((lRet != ERROR_SUCCESS) || (hParamsKey == NULL)) {
            cuiPrintText(TEXT("Ldr: Cannot create/open Tsugumi->Parameters key"), TRUE);
            break;
        }

        lRet = ERROR_BAD_ARGUMENTS;
        if ((g_PatchData.DDTablePointer) && (g_PatchData.DDTableSize > 0)) {
            lRet = RegSetValueEx(hParamsKey, TsmiVBoxDD, 0, REG_BINARY,
                (LPBYTE)g_PatchData.DDTablePointer, g_PatchData.DDTableSize);
            if (lRet != ERROR_SUCCESS) {
                cuiPrintText(TEXT("Ldr: Cannot write VBoxDD patch table"), TRUE);
                break;
            }
        }
        else {
            RegDeleteValue(hParamsKey, TsmiVBoxDD);
        }

        bResult = TRUE;

    } while (cond);

    if (hRootKey) {
        RegCloseKey(hRootKey);
    }
    if (hParamsKey) {
        RegCloseKey(hParamsKey);
    }

    return bResult;
}

/*
* FetchCustomPatchData
*
* Purpose:
*
* Load custom patch table.
* Returned buffer must be freed with HeapFree after usage.
*
*/
PVOID FetchCustomPatchData(
    _In_ LPWSTR lpFileName,
    _Inout_opt_ PDWORD pdwPatchDataSize
)
{
    DWORD   dwFileSize;
    HANDLE  hFile;
    PVOID   DataBuffer = NULL;

    LARGE_INTEGER FileSize;

    //
    // Validate input parameter.
    //
    if (lpFileName == NULL)
        return NULL;

    //
    // Open file with custom patch table.
    //
    hFile = CreateFile(lpFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return NULL;

    //
    // Get file size for buffer, allocate it and read data.
    //
    RtlSecureZeroMemory(&FileSize, sizeof(LARGE_INTEGER));
    if (GetFileSizeEx(hFile, &FileSize)) {
        dwFileSize = FileSize.LowPart;
        if (dwFileSize > 0 && dwFileSize <= 4096) {
            DataBuffer = (PVOID)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwFileSize);
            if (DataBuffer != NULL) {

                if (ReadFile(hFile, DataBuffer, dwFileSize, &dwFileSize, NULL)) {

                    // Check if optional parameter is set and return data size on true.
                    if (pdwPatchDataSize != NULL) {
                        *pdwPatchDataSize = dwFileSize;
                    }
                }
            }
        }
    }
    CloseHandle(hFile);
    return DataBuffer;
}

/*
* CreatePatchTable
*
* Purpose:
*
* Create patch table depending on installed VBox dll.
*
*/
BOOL CreatePatchTable(
    VOID
)
{
    BOOL    cond = FALSE, bResult = FALSE;
    DWORD   dwSize, cch;
    HKEY    hKey = NULL;
    LRESULT lRet;
    TCHAR   szBuffer[MAX_PATH * 2], szTempFile[MAX_PATH * 2];

    do {

        lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("Software\\Oracle\\VirtualBox"),
            0, KEY_READ, &hKey);

        //
        // If key not exists, return FALSE and loader will exit.
        //
        if ((lRet != ERROR_SUCCESS) || (hKey == NULL)) {
            cuiPrintText(TEXT("Ldr: Cannot open VirtualBox registry key"), TRUE);
            break;
        }

        //
        // Read VBox location.
        //
        RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
        dwSize = MAX_PATH * sizeof(TCHAR);
        lRet = RegQueryValueEx(hKey, TEXT("InstallDir"), NULL, NULL, (LPBYTE)&szBuffer, &dwSize);
        if (lRet != ERROR_SUCCESS) {
            cuiPrintText(TEXT("Ldr: Cannot query VirtualBox installation directory"), TRUE);
            break;
        }

        _strcat(szBuffer, TEXT("VBoxDD.dll"));
        RtlSecureZeroMemory(szTempFile, sizeof(szTempFile));
        cch = ExpandEnvironmentStrings(TEXT("%temp%\\"), szTempFile, MAX_PATH);
        if ((cch != 0) && (cch < MAX_PATH)) {
            _strcat(szTempFile, L"nyan.dll");
            if (CopyFile(szBuffer, szTempFile, FALSE) == FALSE)
                break;

            g_PatchData.DDTablePointer = NULL;
            g_PatchData.DDTableSize = 0;
            if (ProcessVirtualBoxFile(szTempFile, &g_PatchData.DDTablePointer, &g_PatchData.DDTableSize) == 0)
                bResult = TRUE;

            DeleteFile(szTempFile);
        }

    } while (cond);

    if (hKey) {
        RegCloseKey(hKey);
    }

    return bResult;
}

/*
* SendCommand
*
* Purpose:
*
* Call Tsugumi driver with IOCTL.
*
*/
VOID SendCommand(
    DWORD dwCmd,
    LPWSTR lpCmd
)
{
    ULONG  l = 0;
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    WCHAR  szBuffer[MAX_PATH * 2];

    // Open Tsugumi instance
    hDevice = NULL;
    _strcpy(szBuffer, TSUGUMI_SYM_LINK);
    hDevice = CreateFile(szBuffer,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice != INVALID_HANDLE_VALUE) {

        RtlSecureZeroMemory(szBuffer, sizeof(szBuffer));
        _strcpy(szBuffer, TEXT("Ldr: Tsugumi device handle opened = "));
        u64tostr((ULONG_PTR)hDevice, _strend(szBuffer));
        cuiPrintText(szBuffer, TRUE);

        DeviceIoControl(hDevice, dwCmd, NULL, 0, NULL, 0, &l, NULL);

        RtlSecureZeroMemory(szBuffer, sizeof(szBuffer));
        _strcpy(szBuffer, TEXT("Ldr: "));
        _strcat(szBuffer, lpCmd);
        _strcat(szBuffer, TEXT(" request"));

        if (l == 1)
            _strcat(szBuffer, TEXT(" successful"));
        else
            _strcat(szBuffer, TEXT(" failed"));
        cuiPrintText(szBuffer, TRUE);

        CloseHandle(hDevice);

        if (l == 1) {
            //force windows rebuild image cache
            cuiPrintText(TEXT("Ldr: purge system cache"), TRUE);
            supPurgeSystemCache();
        }

    }
    else {
        cuiPrintText(TEXT("Ldr: Cannot open Tsugumi device, make sure driver is loaded before running this program"), TRUE);
    }
}

/*
* VBoxLdrMain
*
* Purpose:
*
* Program entry point.
*
*/
void VBoxLdrMain(
    VOID
)
{
    BOOL    cond = FALSE, bFound = FALSE, bTryVBoxRestart = FALSE;
    LONG    x;
    ULONG   l = 0, uCmd = 0;
    PVOID   DataBufferDD = NULL;
    WCHAR   szBuffer[MAX_PATH * 2];

    __security_init_cookie();

    do {

        cuiInitialize(FALSE, NULL);

        SetConsoleTitle(T_PROGRAMTITLE);
        cuiPrintText(T_PROGRAMTITLE, TRUE);

        //
        // Check number of instances running.
        //
        x = InterlockedIncrement((PLONG)&g_lApplicationInstances);
        if (x > 1) {
            break;
        }

        //
        // Check OS version.
        //
        RtlGetNtVersionNumbers(&l, NULL, NULL);

        //
        // We support only Vista based OS.
        //
        if (l < 6) {
            cuiPrintText(TEXT("Ldr: This operation system version is not supported"), TRUE);
            break;
        }

        uCmd = TSUGUMI_IOCTL_REFRESH_LIST;

        // Parse command line.
        RtlSecureZeroMemory(szBuffer, sizeof(szBuffer));
        GetCommandLineParam(GetCommandLine(), 1, szBuffer, MAX_PATH, &l);
        if (l > 0) {

            if (_strcmpi(szBuffer, TEXT("/?")) == 0) {
                cuiPrintText(T_HELP, TRUE);
                InterlockedDecrement((PLONG)&g_lApplicationInstances);
                ExitProcess(0);
                break;
            }

            //check stop flag, if not set check second param
            if (_strcmpi(szBuffer, TEXT("/s")) == 0) {
                uCmd = TSUGUMI_IOCTL_MONITOR_STOP;
                SendCommand(uCmd, TEXT("TSUGUMI_IOCTL_MONITOR_STOP"));
                break;
            }
            else {
                l = 0;
                DataBufferDD = FetchCustomPatchData(szBuffer, &l);
                if ((DataBufferDD != NULL) && (l > 0)) {
                    g_PatchData.DDTablePointer = DataBufferDD;
                    g_PatchData.DDTableSize = l;
                    bFound = TRUE;
                }
                else {
                    cuiPrintText(TEXT("Ldr: Error reading file at parameter 1"), TRUE);
                    break;
                }
            }
        }

        //
        // Check if custom patch table present. If not - attempt to create own. Exit on failure.
        //
        if (bFound == FALSE) {
            bFound = CreatePatchTable();
            if (bFound == FALSE) {
                cuiPrintText(TEXT("Ldr: Could not load patch table"), TRUE);
                break;
            }
            else {
                cuiPrintText(TEXT("Ldr: Patch table created"), TRUE);
            }
        }

#ifndef _DEBUG
        //
        // Check if any VBox instances are running, they must be closed before our usage.
        //
        if (supProcessExist(L"VirtualBox.exe")) {
            cuiPrintText(TEXT("Ldr: VirtualBox is running, close it before"), TRUE);
            break;
        }
#endif

        if (!SetTsmiParams()) {
            cuiPrintText(TEXT("Ldr: Cannot write Tsugumi settings"), TRUE);
            break;
        }
        else {
            cuiPrintText(TEXT("Ldr: Tsugumi patch table parameters set"), TRUE);
        }

        //
        // Load signed Tsugumi.sys, otherwise we expect TDL already loaded unsigned driver.
        //
#ifdef _SIGNED_BUILD
        if (!supLoadDeviceDriver()) {
            cuiPrintText(TEXT("Ldr: Failed to load Tsugumi monitor driver"), TRUE);
            break;
        }
#else 
        bTryVBoxRestart = TRUE;
#endif
        //send command to driver
        SendCommand(uCmd, TEXT("TSUGUMI_IOCTL_REFRESH_LIST"));

    } while (cond);

    if (bTryVBoxRestart) {
        l = 0;
        if (supRestartVBoxDrv(&l)) {
            cuiPrintText(TEXT("Ldr: supRestartVBoxDrv success"), TRUE);
        }
        else {
            _strcpy(szBuffer, TEXT("Ldr: supRestartVBoxDrv = 0x"));
            ultohex(l, _strend(szBuffer));
            cuiPrintText(szBuffer, TRUE);
        }
    }

    cuiPrintText(TEXT("Ldr: exit"), TRUE);
    InterlockedDecrement((PLONG)&g_lApplicationInstances);
    ExitProcess(0);
}
