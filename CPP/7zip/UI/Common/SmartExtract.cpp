// SmartExtract.cpp

#include "StdAfx.h"

#include "../../../Common/Wildcard.h"

#include "../../Common/FilePathAutoRename.h"

#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileFind.h"
#include "../../../Windows/FileName.h"

#include "../Common/ExtractingFilePath.h"

#include "NestedTar.h"
#include "SmartExtract.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;

struct CSmartExtractRootItem
{
  UString Name;
  bool IsDir;
};

struct CSmartExtractInfo
{
  CObjectVector<CSmartExtractRootItem> Roots;
  UInt32 NumFiles;
  UInt32 MinDuplicateParts;

  CSmartExtractInfo():
      NumFiles(0),
      MinDuplicateParts((UInt32)(Int32)-1)
      {}
};

static HRESULT ScanSmartExtractInfoItem(
    const CArc &arc,
    UInt32 index,
    const UString &folderName,
    CSmartExtractInfo &info)
{
  CReadArcItem item;
  RINOK(arc.GetItem(index, item))

  if (item.PathParts.IsEmpty())
    return S_OK;

  #ifdef SUPPORT_ALT_STREAMS
  if (item.IsAltStream)
    return S_OK;
  #endif

  bool isAnti;
  RINOK(arc.IsItem_Anti(index, isAnti))
  if (isAnti)
    return S_OK;

  if (arc.Ask_Deleted)
  {
    bool isDeleted;
    RINOK(Archive_IsItem_Deleted(arc.Archive, index, isDeleted))
    if (isDeleted)
      return S_OK;
  }

  if (arc.Ask_Aux)
  {
    bool isAux;
    RINOK(Archive_IsItem_Aux(arc.Archive, index, isAux))
    if (isAux)
      return S_OK;
  }

  unsigned rootPos = 0;
  while (rootPos < item.PathParts.Size() &&
      item.PathParts[rootPos].IsEqualTo_Ascii_NoCase("."))
    rootPos++;
  if (rootPos == item.PathParts.Size())
    return S_OK;

  const UString &rootName = item.PathParts[rootPos];
  const bool rootIsDir = item.IsDir || item.PathParts.Size() > rootPos + 1;
  bool rootFound = false;
  FOR_VECTOR (r, info.Roots)
  {
    CSmartExtractRootItem &root = info.Roots[r];
    if (CompareFileNames(root.Name, rootName) == 0)
    {
      if (!rootIsDir)
        root.IsDir = false;
      rootFound = true;
      break;
    }
  }
  if (!rootFound)
  {
    CSmartExtractRootItem root;
    root.Name = rootName;
    root.IsDir = rootIsDir;
    info.Roots.Add(root);
  }

  if (item.IsDir)
    return S_OK;

  info.NumFiles++;

  UInt32 duplicateParts = 0;
  if (!folderName.IsEmpty())
  {
    for (;;)
    {
      if (duplicateParts >= item.PathParts.Size())
        break;
      if (CompareFileNames(item.PathParts[duplicateParts], folderName) != 0)
        break;
      duplicateParts++;
    }
  }
  if (duplicateParts >= item.PathParts.Size())
    duplicateParts = item.PathParts.Size() - 1;
  if (duplicateParts < info.MinDuplicateParts)
    info.MinDuplicateParts = duplicateParts;
  return S_OK;
}

static HRESULT ScanSmartExtractInfo(const CArc &arc, const UString &folderName,
    CSmartExtractInfo &info)
{
  UInt32 numItems;
  RINOK(arc.Archive->GetNumberOfItems(&numItems))

  if (numItems == (UInt32)(Int32)-1)
  {
    for (UInt32 i = 0;; i++)
    {
      const HRESULT res = ScanSmartExtractInfoItem(arc, i, folderName, info);
      if (res == E_INVALIDARG)
        break;
      RINOK(res)
    }
  }
  else
  {
    for (UInt32 i = 0; i < numItems; i++)
      RINOK(ScanSmartExtractInfoItem(arc, i, folderName, info))
  }

  if (info.NumFiles == 0)
    info.MinDuplicateParts = 0;
  return S_OK;
}

static bool MakeUniqueFolderPath(FString &path)
{
  if (!path.IsEmpty() && IsPathSepar(path.Back()))
    path.DeleteBack();
  if (!AutoRenamePath(path))
    return false;
  NFile::NName::NormalizeDirPathPrefix(path);
  return true;
}

static bool SmartExtractFolderExists(const FString &path)
{
  FString pathWithoutPrefix = path;
  if (!pathWithoutPrefix.IsEmpty() && IsPathSepar(pathWithoutPrefix.Back()))
    pathWithoutPrefix.DeleteBack();
  return NFind::DoesFileOrDirExist(pathWithoutPrefix);
}

static ISmartExtractCallback::EConflict AskFolderConflict(
    CSmartExtractOptions &smartOptions, const FString &path)
{
  if (!smartOptions.Callback)
    return ISmartExtractCallback::kConflict_Rename;
  return smartOptions.Callback->AskFolderConflict(path);
}

static HRESULT ResolveFolderConflict(
    const FString &path,
    CSmartExtractOptions &smartOptions,
    FString &resolvedPath)
{
  resolvedPath = path;
  if (!SmartExtractFolderExists(path))
    return S_OK;

  const ISmartExtractCallback::EConflict res =
      AskFolderConflict(smartOptions, path);
  if (res == ISmartExtractCallback::kConflict_Rename)
    return MakeUniqueFolderPath(resolvedPath) ? S_OK : E_ABORT;
  if (res != ISmartExtractCallback::kConflict_Overwrite)
    return E_ABORT;
  return S_OK;
}

HRESULT PrepareSmartExtract(
    CCodecs *codecs,
    const COpenOptions &openOptions,
    const UString &arcPath,
    CArchiveLink &arcLink,
    CArchiveLink &nestedLink,
    CArchiveLink *&extractLink,
    CExtractOptions &options,
    CSmartExtractOptions &smartOptions)
{
  extractLink = &arcLink;

  UString folderName = arcLink.GetArc()->DefaultName;
  CSmartExtractInfo info;
  RINOK(ScanSmartExtractInfo(*arcLink.GetArc(), folderName, info))

  bool nestedTarFound = false;
  bool nestedTarSequential = false;
  CArchiveLink nestedScanLink;
  RINOK(NestedTar_OpenSingle(codecs, openOptions, *arcLink.GetArc(),
      nestedScanLink, nestedTarFound, nestedTarSequential))
  if (nestedTarFound)
  {
    folderName = nestedScanLink.GetArc()->DefaultName;
    info.Roots.Clear();
    info.NumFiles = 0;
    info.MinDuplicateParts = (UInt32)(Int32)-1;
    RINOK(ScanSmartExtractInfo(*nestedScanLink.GetArc(), folderName, info))
    RINOK(nestedScanLink.Close())
    nestedScanLink.Release();

    if (info.NumFiles == 0)
    {
      if (smartOptions.Callback)
        smartOptions.Callback->ThereAreNoFiles();
      return E_ABORT;
    }

    if (nestedTarSequential)
    {
      // The metadata pass consumed the first stream. Start the extraction pass
      // from a freshly opened outer archive.
      COpenOptions reopenOptions = openOptions;
      reopenOptions.callback = NULL;
      reopenOptions.callbackSpec = NULL;
      if (arcLink.Arcs.Size() <= 1)
        RINOK(arcLink.ReOpen(reopenOptions))
    }

    bool nestedTarOpened = false;
    RINOK(NestedTar_OpenSingle(codecs, openOptions, *arcLink.GetArc(),
        nestedLink, nestedTarOpened, nestedTarSequential))
    if (!nestedTarOpened)
      return E_FAIL;
    extractLink = &nestedLink;
    if (nestedTarSequential)
      options.StdInMode = true;
  }

  if (info.NumFiles == 0)
  {
    if (smartOptions.Callback)
      smartOptions.Callback->ThereAreNoFiles();
    return E_ABORT;
  }

  FString baseFolder = options.OutputDir;
  if (baseFolder.IsEmpty())
    GetOnlyDirPrefix(us2fs(arcPath), baseFolder);
  if (baseFolder.IsEmpty())
    baseFolder.Add_PathSepar();
  NFile::NName::NormalizeDirPathPrefix(baseFolder);

  options.OutDirMode = NExtractOutDirMode::k_Direct;
  smartOptions.RemovePathParts.Clear();

  if (info.Roots.Size() == 1)
  {
    const CSmartExtractRootItem &root = info.Roots[0];
    if (root.IsDir)
    {
      options.OutputDir = baseFolder;
      options.OutputDir += us2fs(Get_Correct_FsFile_Name(root.Name));
      NFile::NName::NormalizeDirPathPrefix(options.OutputDir);

      if (SmartExtractFolderExists(options.OutputDir))
      {
        FString resolvedPath;
        RINOK(ResolveFolderConflict(options.OutputDir, smartOptions, resolvedPath))
        if (resolvedPath == options.OutputDir)
          options.OutputDir = baseFolder;
        else
        {
          options.OutputDir = resolvedPath;
          const UInt32 numRemoveParts =
              info.MinDuplicateParts > 1 ? info.MinDuplicateParts : 1;
          for (UInt32 i = 0; i < numRemoveParts; i++)
            smartOptions.RemovePathParts.Add(root.Name);
        }
      }
      else
      {
        options.OutputDir = baseFolder;
        for (UInt32 i = 1; i < info.MinDuplicateParts; i++)
          smartOptions.RemovePathParts.Add(root.Name);
      }
    }
    else
      options.OutputDir = baseFolder;
  }
  else
  {
    const UString correctedFolderName = Get_Correct_FsFile_Name(folderName);
    options.OutputDir = baseFolder;
    options.OutputDir += us2fs(correctedFolderName);
    NFile::NName::NormalizeDirPathPrefix(options.OutputDir);

    if (SmartExtractFolderExists(options.OutputDir))
    {
      FString resolvedPath;
      RINOK(ResolveFolderConflict(options.OutputDir, smartOptions, resolvedPath))
      options.OutputDir = resolvedPath;
    }

    UInt32 numDuplicateParts = info.MinDuplicateParts;
    if (numDuplicateParts == (UInt32)(Int32)-1)
      numDuplicateParts = 0;
    for (UInt32 i = 0; i < numDuplicateParts; i++)
      smartOptions.RemovePathParts.Add(folderName);
  }

  return S_OK;
}
