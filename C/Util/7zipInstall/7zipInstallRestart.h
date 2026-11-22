/* 7zipInstallRestart.h - 7-Zip Installer restart helpers */

#ifndef ZIP7_INC_7ZIP_INSTALL_RESTART_H
#define ZIP7_INC_7ZIP_INSTALL_RESTART_H

#include <windows.h>

typedef struct
{
  HWND Owner;
  LPCWSTR Caption;
  BOOL SilentMode;
} CInstallRestartOptions;

DWORD InstallerRestart_ReleaseFiles(
    const WCHAR * const *files,
    UINT numFiles,
    const CInstallRestartOptions *options);

void InstallerRestart_End(void);

BOOL InstallerRestart_RestartExplorer(void);

#endif
