/* 7zipInstallRestart.c - 7-Zip Installer restart helpers */

#include "Precomp.h"

#include <stdlib.h>

#include "7zipInstallRestart.h"
#include "../../7zTypes.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
#include <RestartManager.h>
#include <TlHelp32.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

#define wcscat lstrcatW
#define wcslen (size_t)lstrlenW

typedef struct
{
  DWORD SessionHandle;
  BOOL Opened;
  BOOL RestartNeeded;
} CRestartManagerContext;

typedef DWORD (WINAPI * Func_RmStartSession)(DWORD *, DWORD, WCHAR *);
typedef DWORD (WINAPI * Func_RmRegisterResources)(DWORD, UINT, LPCWSTR *,
    UINT, RM_UNIQUE_PROCESS *, UINT, LPCWSTR *);
typedef DWORD (WINAPI * Func_RmGetList)(DWORD, UINT *, UINT *,
    RM_PROCESS_INFO *, LPDWORD);
typedef DWORD (WINAPI * Func_RmShutdown)(DWORD, ULONG,
    RM_WRITE_STATUS_CALLBACK);
typedef DWORD (WINAPI * Func_RmRestart)(DWORD, DWORD,
    RM_WRITE_STATUS_CALLBACK);
typedef DWORD (WINAPI * Func_RmEndSession)(DWORD);

typedef struct
{
  HMODULE Module;
  Func_RmStartSession StartSession;
  Func_RmRegisterResources RegisterResources;
  Func_RmGetList GetList;
  Func_RmShutdown Shutdown;
  Func_RmRestart Restart;
  Func_RmEndSession EndSession;
  BoolInt Loaded;
} CRestartManagerApi;

static CRestartManagerContext g_RestartManager;
static CRestartManagerApi g_RestartManagerApi;
static BOOL g_RestartManager_AtExitSet;
static BOOL g_ExplorerRestartNeeded;

static void CpyAscii(WCHAR *dest, const char *s)
{
  for (;;)
  {
    Byte b = (Byte)*s++;
    *dest++ = b;
    if (b == 0)
      return;
  }
}

static void CatAscii(WCHAR *dest, const char *s)
{
  dest += wcslen(dest);
  CpyAscii(dest, s);
}

static BoolInt RestartManager_Load(void)
{
  HMODULE module;

  if (g_RestartManagerApi.Loaded)
    return (g_RestartManagerApi.Module != NULL);

  g_RestartManagerApi.Loaded = True;
  module = LoadLibraryW(L"Rstrtmgr.dll");
  if (!module)
    return False;

  g_RestartManagerApi.StartSession = (Func_RmStartSession)
      Z7_CAST_FUNC_C GetProcAddress(module, "RmStartSession");
  g_RestartManagerApi.RegisterResources = (Func_RmRegisterResources)
      Z7_CAST_FUNC_C GetProcAddress(module, "RmRegisterResources");
  g_RestartManagerApi.GetList = (Func_RmGetList)
      Z7_CAST_FUNC_C GetProcAddress(module, "RmGetList");
  g_RestartManagerApi.Shutdown = (Func_RmShutdown)
      Z7_CAST_FUNC_C GetProcAddress(module, "RmShutdown");
  g_RestartManagerApi.Restart = (Func_RmRestart)
      Z7_CAST_FUNC_C GetProcAddress(module, "RmRestart");
  g_RestartManagerApi.EndSession = (Func_RmEndSession)
      Z7_CAST_FUNC_C GetProcAddress(module, "RmEndSession");

  if (!g_RestartManagerApi.StartSession
      || !g_RestartManagerApi.RegisterResources
      || !g_RestartManagerApi.GetList
      || !g_RestartManagerApi.Shutdown
      || !g_RestartManagerApi.Restart
      || !g_RestartManagerApi.EndSession)
  {
    FreeLibrary(module);
    return False;
  }

  g_RestartManagerApi.Module = module;
  return True;
}

static BoolInt IsExplorerProcess(DWORD processId)
{
  PROCESSENTRY32 processEntry;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  BoolInt isExplorer = False;
  if (snapshot == INVALID_HANDLE_VALUE)
    return False;

  processEntry.dwSize = sizeof(processEntry);
  if (Process32First(snapshot, &processEntry))
  {
    do
    {
      if (processEntry.th32ProcessID == processId)
      {
        isExplorer = (lstrcmpiW(processEntry.szExeFile, L"explorer.exe") == 0);
        break;
      }
    }
    while (Process32Next(snapshot, &processEntry));
  }
  CloseHandle(snapshot);
  return isExplorer;
}

static BoolInt IsProcessAlive(DWORD processId)
{
  HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, processId);
  if (!handle)
    return GetLastError() != ERROR_INVALID_PARAMETER;
  {
    const DWORD waitResult = WaitForSingleObject(handle, 0);
    CloseHandle(handle);
    return waitResult != WAIT_OBJECT_0;
  }
}

static void AppendProcessList(const RM_PROCESS_INFO *processes, UINT count,
    WCHAR *message, size_t capacity)
{
  UINT i;
  for (i = 0; i < count && i < 12; i++)
  {
    const WCHAR *name = processes[i].strAppName;
    if (name[0] == 0)
      name = L"Unknown application";
    if (wcslen(message) + 6 >= capacity)
      break;
    CpyAscii(message + wcslen(message), "\n  - ");
    if (wcslen(message) + wcslen(name) + 2 < capacity)
    {
      wcscat(message, name);
      {
        WCHAR pidText[40];
        wsprintfW(pidText, L" (PID %lu)",
            (unsigned long)processes[i].Process.dwProcessId);
        if (wcslen(message) + wcslen(pidText) < capacity)
          wcscat(message, pidText);
      }
    }
  }
  if (count > 12 && wcslen(message) + 24 < capacity)
    wcscat(message, L"\n  - ...");
}

void InstallerRestart_End(void)
{
  if (!g_RestartManager.Opened)
    return;
  if (RestartManager_Load())
  {
    if (g_RestartManager.RestartNeeded)
    {
      g_RestartManagerApi.Restart(g_RestartManager.SessionHandle, 0, NULL);
      g_RestartManager.RestartNeeded = False;
    }
    g_RestartManagerApi.EndSession(g_RestartManager.SessionHandle);
  }
  g_RestartManager.Opened = False;
}

BOOL InstallerRestart_RestartExplorer(void)
{
  if (!g_ExplorerRestartNeeded)
    return True;
  if ((INT_PTR)ShellExecuteW(NULL, L"open", L"explorer.exe", NULL,
      NULL, SW_SHOWNORMAL) <= 32)
    return False;
  g_ExplorerRestartNeeded = False;
  return True;
}

static void Z7_CDECL InstallerRestart_AtExit(void)
{
  InstallerRestart_End();
  InstallerRestart_RestartExplorer();
}

static DWORD StopExplorerProcesses(const RM_PROCESS_INFO *processes, UINT count)
{
  HANDLE *handles;
  UINT i;
  UINT numHandles = 0;
  DWORD result = ERROR_SUCCESS;
  DWORD deadline;

  handles = (HANDLE *)malloc(count * sizeof(handles[0]));
  if (!handles)
    return ERROR_NOT_ENOUGH_MEMORY;

  for (i = 0; i < count; i++)
  {
    const DWORD processId = processes[i].Process.dwProcessId;
    HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
        FALSE, processId);
    if (!handle)
    {
      const DWORD openError = GetLastError();
      if (openError != ERROR_INVALID_PARAMETER && result == ERROR_SUCCESS)
        result = openError;
      continue;
    }

    if (!TerminateProcess(handle, 0))
    {
      DWORD exitCode;
      const DWORD terminateError = GetLastError();
      if (!GetExitCodeProcess(handle, &exitCode)
          || exitCode == STILL_ACTIVE)
      {
        if (result == ERROR_SUCCESS)
          result = terminateError;
        CloseHandle(handle);
        continue;
      }
      CloseHandle(handle);
      continue;
    }

    handles[numHandles++] = handle;
    g_ExplorerRestartNeeded = True;
  }

  if (g_ExplorerRestartNeeded && !g_RestartManager_AtExitSet)
  {
    g_RestartManager_AtExitSet = True;
    atexit(InstallerRestart_AtExit);
  }

  deadline = GetTickCount() + 1500;
  for (i = 0; i < numHandles; i++)
  {
    for (;;)
    {
      const DWORD waitResult = WaitForSingleObject(handles[i], 50);
      if (waitResult == WAIT_OBJECT_0)
        break;
      if (waitResult == WAIT_FAILED)
      {
        if (result == ERROR_SUCCESS)
          result = GetLastError();
        break;
      }
      if ((LONG)(deadline - GetTickCount()) <= 0)
      {
        if (result == ERROR_SUCCESS)
          result = ERROR_TIMEOUT;
        break;
      }
    }
    CloseHandle(handles[i]);
  }

  free(handles);
  return result;
}

DWORD InstallerRestart_ReleaseFiles(
    const WCHAR * const *files,
    UINT numFiles,
    const CInstallRestartOptions *options)
{
  WCHAR sessionKey[CCH_RM_SESSION_KEY + 1];
  DWORD res;
  DWORD rebootReasons = 0;
  UINT needed = 0;
  UINT count = 0;
  RM_PROCESS_INFO *processes = NULL;
  BoolInt manualOnly = False;
  HWND owner = NULL;
  LPCWSTR caption = L"Setup";
  BOOL silentMode = False;

  if (options)
  {
    owner = options->Owner;
    caption = options->Caption ? options->Caption : caption;
    silentMode = options->SilentMode;
  }

  if (numFiles == 0)
    return ERROR_SUCCESS;

  if (!RestartManager_Load())
    return ERROR_CALL_NOT_IMPLEMENTED;

  for (;;)
  {
    UINT i;
    UINT liveCount;
    BoolInt onlyExplorer = True;

    res = g_RestartManagerApi.StartSession(
        &g_RestartManager.SessionHandle, 0, sessionKey);
    if (res != ERROR_SUCCESS)
      return res;
    g_RestartManager.Opened = True;
    g_RestartManager.RestartNeeded = False;

    res = g_RestartManagerApi.RegisterResources(
        g_RestartManager.SessionHandle, numFiles,
        (LPCWSTR *)files, 0, NULL, 0, NULL);
    if (res != ERROR_SUCCESS)
    {
      InstallerRestart_End();
      return res;
    }

    needed = 0;
    count = 0;
    processes = NULL;
    res = g_RestartManagerApi.GetList(
        g_RestartManager.SessionHandle, &needed, &count,
        NULL, &rebootReasons);
    if (res == ERROR_SUCCESS)
    {
      InstallerRestart_End();
      return ERROR_SUCCESS;
    }
    if (res != ERROR_MORE_DATA)
    {
      InstallerRestart_End();
      return res;
    }

    processes = (RM_PROCESS_INFO *)malloc(needed * sizeof(RM_PROCESS_INFO));
    if (!processes)
    {
      InstallerRestart_End();
      return ERROR_NOT_ENOUGH_MEMORY;
    }
    count = needed;
    res = g_RestartManagerApi.GetList(
        g_RestartManager.SessionHandle, &needed, &count,
        processes, &rebootReasons);
    if (res != ERROR_SUCCESS)
    {
      free(processes);
      InstallerRestart_End();
      return res;
    }

    liveCount = 0;
    for (i = 0; i < count; i++)
    {
      if (IsProcessAlive(processes[i].Process.dwProcessId))
      {
        if (liveCount != i)
          processes[liveCount] = processes[i];
        liveCount++;
      }
    }
    count = liveCount;
    if (count == 0)
    {
      free(processes);
      InstallerRestart_End();
      return ERROR_SUCCESS;
    }

    for (i = 0; i < count; i++)
    {
      if (!IsExplorerProcess(processes[i].Process.dwProcessId))
      {
        onlyExplorer = False;
        if (!processes[i].bRestartable)
          manualOnly = True;
      }
    }

    if (silentMode)
    {
      free(processes);
      InstallerRestart_End();
      return ERROR_BUSY;
    }

    {
      WCHAR message[4096];
      int result;
      message[0] = 0;
      if (onlyExplorer)
      {
        CpyAscii(message,
            "Setup will restart Windows Explorer to release the shell extension.\n\nContinue?");
        result = MessageBoxW(owner, message, caption,
            MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (result != IDOK)
        {
          free(processes);
          InstallerRestart_End();
          return ERROR_CANCELLED;
        }

        res = StopExplorerProcesses(processes, count);
        free(processes);
        if (res != ERROR_SUCCESS)
        {
          InstallerRestart_End();
          return res;
        }

        return ERROR_SUCCESS;
      }

      if (manualOnly)
      {
        CpyAscii(message,
            "The following applications are using files that Setup must update:");
        AppendProcessList(processes, count, message, Z7_ARRAY_SIZE(message));
        CatAscii(message,
            "\n\nApplications other than Windows Explorer cannot be restarted automatically.\nClose them manually, then click Retry.\n\nCancel defers file replacement until the next Windows restart.");
        result = MessageBoxW(owner, message, caption,
            MB_RETRYCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
        free(processes);
        if (result != IDRETRY)
        {
          InstallerRestart_End();
          return ERROR_CANCELLED;
        }
        InstallerRestart_End();
        Sleep(300);
        continue;
      }

      CpyAscii(message,
          "The following applications are using files that Setup must update:");
      AppendProcessList(processes, count, message, Z7_ARRAY_SIZE(message));
      CatAscii(message,
          "\n\nSetup can close and restart these applications automatically.\nUnsaved data in these applications may be lost.\n\nYes: close and restart automatically\nNo: I will close them manually\nCancel: defer replacement until the next Windows restart");
      result = MessageBoxW(owner, message, caption,
          MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
      if (result == IDNO)
      {
        free(processes);
        InstallerRestart_End();
        manualOnly = True;
        Sleep(300);
        continue;
      }
      if (result != IDYES)
      {
        free(processes);
        InstallerRestart_End();
        return ERROR_CANCELLED;
      }

      res = g_RestartManagerApi.Shutdown(g_RestartManager.SessionHandle,
          RmForceShutdown, NULL);
      free(processes);
      if (res != ERROR_SUCCESS)
      {
        InstallerRestart_End();
        return res;
      }
      g_RestartManager.RestartNeeded = True;
      if (!g_RestartManager_AtExitSet)
      {
        g_RestartManager_AtExitSet = True;
        atexit(InstallerRestart_AtExit);
      }
      return ERROR_SUCCESS;
    }
  }
}
