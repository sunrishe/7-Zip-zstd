// SmartExtract.h

#ifndef ZIP7_INC_SMART_EXTRACT_H
#define ZIP7_INC_SMART_EXTRACT_H

#include "Extract.h"

Z7_PURE_INTERFACES_BEGIN

DECLARE_INTERFACE(ISmartExtractCallback)
{
  enum EConflict
  {
    kConflict_Cancel,
    kConflict_Rename,
    kConflict_Overwrite
  };

  virtual EConflict AskFolderConflict(const FString &path) = 0;
  virtual void ThereAreNoFiles() = 0;
};

Z7_PURE_INTERFACES_END

struct CSmartExtractOptions
{
  bool Enabled;
  UStringVector RemovePathParts;
  ISmartExtractCallback *Callback;

  CSmartExtractOptions():
      Enabled(false),
      Callback(NULL)
      {}
};

HRESULT PrepareSmartExtract(
    CCodecs *codecs,
    const COpenOptions &openOptions,
    const UString &arcPath,
    CArchiveLink &arcLink,
    CArchiveLink &nestedLink,
    CArchiveLink *&extractLink,
    CExtractOptions &options,
    CSmartExtractOptions &smartOptions);

#endif
