// NestedTar.cpp

#include "StdAfx.h"

#include "../../../Common/MyCom.h"

#include "../../Common/StreamBinder.h"
#include "../../Common/StreamUtils.h"

#include "../../../Windows/Thread.h"

#include "NestedTar.h"
#include "LoadCodecs.h"

using namespace NWindows;
static bool NestedTar_IsTarPath(const UString &path);
static HRESULT NestedTar_CreatePipe(
    IInArchive *outerArchive,
    UInt32 index,
    ISequentialInStream **stream);
static HRESULT NestedTar_CreateSeekable(
    IInArchive *outerArchive,
    UInt32 index,
    IInStream *outerStream,
    IInStream **stream);

class CNestedTarExtractCallback Z7_final:
  public IArchiveExtractCallback,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_1(IArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(IProgress)

  UInt32 _index;
  CMyComPtr<ISequentialOutStream> _outStream;

public:
  HRESULT Result;

  CNestedTarExtractCallback(UInt32 index,
      ISequentialOutStream *outStream):
      _index(index),
      _outStream(outStream),
      Result(S_OK)
      {}
};

Z7_COM7F_IMF(CNestedTarExtractCallback::SetTotal(UInt64 /* size */))
{
  return S_OK;
}

Z7_COM7F_IMF(CNestedTarExtractCallback::SetCompleted(
    const UInt64 * /* completeValue */))
{
  return S_OK;
}

Z7_COM7F_IMF(CNestedTarExtractCallback::GetStream(UInt32 index,
    ISequentialOutStream **outStream, Int32 askExtractMode))
{
  *outStream = NULL;
  if (index != _index ||
      askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    return S_OK;

  if (_outStream)
  {
    _outStream->AddRef();
    *outStream = _outStream;
  }
  return S_OK;
}

Z7_COM7F_IMF(CNestedTarExtractCallback::PrepareOperation(
    Int32 /* askExtractMode */))
{
  return S_OK;
}

Z7_COM7F_IMF(CNestedTarExtractCallback::SetOperationResult(Int32 opRes))
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
    Result = E_FAIL;
  return S_OK;
}

class CNestedTarPipe Z7_final:
  public ISequentialInStream,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_1(ISequentialInStream)

  CMyComPtr<IInArchive> _archive;
  UInt32 _index;
  CStreamBinder _binder;
  NWindows::CThread _thread;
  CMyComPtr<ISequentialInStream> _inStream;
  CMyComPtr<ISequentialOutStream> _outStream;
  HRESULT _extractResult;
  bool _started;
  bool _finished;

  void Process()
  {
    {
      CNestedTarExtractCallback *callbackSpec =
          new CNestedTarExtractCallback(_index, _outStream);
      CMyComPtr<IArchiveExtractCallback> callback = callbackSpec;
      _extractResult = _archive->Extract(&_index, 1, false, callback);
      if (_extractResult == S_OK)
        _extractResult = callbackSpec->Result;
    }
    _outStream.Release();
  }

  static THREAD_FUNC_DECL ThreadFunction(void *param)
  {
    ((CNestedTarPipe *)param)->Process();
    return 0;
  }

  HRESULT Start()
  {
    if (_started)
      return S_OK;

    RINOK(_binder.Create_ReInit())
    _binder.CreateStreams2(_inStream, _outStream);
    const WRes wres = _thread.Create(ThreadFunction, this);
    if (wres != 0)
    {
      _inStream.Release();
      _outStream.Release();
      return HRESULT_FROM_WIN32(wres);
    }
    _started = true;
    return S_OK;
  }

public:
  CNestedTarPipe(IInArchive *archive, UInt32 index):
      _archive(archive),
      _index(index),
      _extractResult(S_OK),
      _started(false),
      _finished(false)
      {}

  ~CNestedTarPipe()
  {
    _inStream.Release();
    if (_thread.IsCreated())
      _thread.Wait_Close();
    _outStream.Release();
  }
};

Z7_COM7F_IMF(CNestedTarPipe::Read(void *data, UInt32 size,
    UInt32 *processedSize))
{
  if (processedSize)
    *processedSize = 0;
  if (size == 0)
    return S_OK;
  if (_finished)
    return S_OK;
  RINOK(Start())

  UInt32 processed = 0;
  UInt32 *realProcessedSize = processedSize ? processedSize : &processed;
  RINOK(_inStream->Read(data, size, realProcessedSize))

  if (*realProcessedSize == 0)
  {
    if (_thread.IsCreated())
      _thread.Wait_Close();
    _started = false;
    _finished = true;
    if (_extractResult != S_OK)
      return _extractResult;
  }
  return S_OK;
}

// Restart the outer stream for backward seeks without caching the tar.
class CNestedTarSeekableStream Z7_final:
  public IInStream,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_2(ISequentialInStream, IInStream)

  CMyComPtr<IInArchive> _archive;
  CMyComPtr<IInStream> _outerStream;
  CMyComPtr<ISequentialInStream> _stream;
  UInt32 _index;
  UInt64 _pos;
  UInt64 _size;
  bool _sizeKnown;
  bool _started;

  HRESULT Start()
  {
    _stream.Release();
    RINOK(InStream_SeekToBegin(_outerStream))
    RINOK(NestedTar_CreatePipe(_archive, _index, &_stream))
    _started = true;
    return S_OK;
  }

  HRESULT ReadStreamPart(void *data, UInt32 size, UInt32 *processedSize)
  {
    if (!_started)
      RINOK(Start())
    RINOK(_stream->Read(data, size, processedSize))
    _pos += *processedSize;
    return S_OK;
  }

  HRESULT SkipTo(UInt64 target)
  {
    Byte buf[1 << 16];
    while (_pos < target)
    {
      const UInt64 remaining = target - _pos;
      const UInt32 size = remaining < sizeof(buf) ?
          (UInt32)remaining : (UInt32)sizeof(buf);
      UInt32 processed;
      RINOK(ReadStreamPart(buf, size, &processed))
      if (processed == 0)
        return E_FAIL;
    }
    return S_OK;
  }

  HRESULT EnsureSize()
  {
    if (_sizeKnown)
      return S_OK;
    Byte buf[1 << 16];
    for (;;)
    {
      UInt32 processed;
      RINOK(ReadStreamPart(buf, sizeof(buf), &processed))
      if (processed == 0)
        break;
    }
    _size = _pos;
    _sizeKnown = true;
    return S_OK;
  }

public:
  CNestedTarSeekableStream(IInArchive *archive, UInt32 index,
      IInStream *outerStream):
      _archive(archive),
      _outerStream(outerStream),
      _index(index),
      _pos(0),
      _size(0),
      _sizeKnown(false),
      _started(false)
      {}
};

Z7_COM7F_IMF(CNestedTarSeekableStream::Read(void *data, UInt32 size,
    UInt32 *processedSize))
{
  if (processedSize)
    *processedSize = 0;
  if (_sizeKnown)
  {
    if (_pos >= _size)
      return S_OK;
    const UInt64 remaining = _size - _pos;
    if (size > remaining)
      size = (UInt32)remaining;
  }
  if (size == 0)
    return S_OK;
  UInt32 processed = 0;
  UInt32 *realProcessedSize = processedSize ? processedSize : &processed;
  RINOK(ReadStreamPart(data, size, realProcessedSize))
  if (*realProcessedSize == 0 && !_sizeKnown)
  {
    _size = _pos;
    _sizeKnown = true;
  }
  return S_OK;
}

Z7_COM7F_IMF(CNestedTarSeekableStream::Seek(Int64 offset,
    UInt32 seekOrigin, UInt64 *newPosition))
{
  UInt64 base;
  UInt64 target;
  switch (seekOrigin)
  {
    case STREAM_SEEK_SET:
      base = 0;
      break;
    case STREAM_SEEK_CUR:
      base = _pos;
      break;
    case STREAM_SEEK_END:
      RINOK(EnsureSize())
      base = _size;
      break;
    default:
      return STG_E_INVALIDFUNCTION;
  }

  if (offset < 0)
  {
    const UInt64 delta = (UInt64)(-(offset + 1)) + 1;
    if (delta > base)
      return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    target = base - delta;
  }
  else
  {
    const UInt64 delta = (UInt64)offset;
    if (delta > (UInt64)(Int64)-1 - base)
      return E_INVALIDARG;
    target = base + delta;
  }

  if (target < _pos)
  {
    _stream.Release();
    _pos = 0;
    _started = false;
  }
  RINOK(SkipTo(target))
  if (newPosition)
    *newPosition = _pos;
  return S_OK;
}

static bool IsTarFormat(const CCodecs *codecs, const CArc &arc)
{
  if (arc.FormatIndex < 0)
    return false;
  return codecs->Formats[(unsigned)arc.FormatIndex].Name
      .IsEqualTo_Ascii_NoCase("tar");
}

static int FindTarFormatIndex(const CCodecs *codecs)
{
  FOR_VECTOR (i, codecs->Formats)
    if (codecs->Formats[i].Name.IsEqualTo_Ascii_NoCase("tar"))
      return (int)i;
  return -1;
}

HRESULT NestedTar_OpenSingle(
    CCodecs *codecs,
    const COpenOptions &baseOptions,
    const CArc &outerArc,
    CArchiveLink &nestedLink,
    bool &found,
    bool &sequential,
    IInStream *outerStream)
{
  found = false;
  sequential = false;
  if (IsTarFormat(codecs, outerArc))
    return S_OK;

  UInt32 numItems;
  RINOK(outerArc.Archive->GetNumberOfItems(&numItems))
  if (numItems == (UInt32)(Int32)-1)
    return S_OK;

  UInt32 tarIndex = (UInt32)(Int32)-1;
  CReadArcItem tarItem;
  for (UInt32 i = 0; i < numItems; i++)
  {
    CReadArcItem item;
    RINOK(outerArc.GetItem(i, item))

    bool isAnti;
    RINOK(outerArc.IsItem_Anti(i, isAnti))
    if (isAnti)
      continue;

    if (outerArc.Ask_Deleted)
    {
      bool isDeleted;
      RINOK(Archive_IsItem_Deleted(outerArc.Archive, i, isDeleted))
      if (isDeleted)
        continue;
    }

    if (outerArc.Ask_Aux)
    {
      bool isAux;
      RINOK(Archive_IsItem_Aux(outerArc.Archive, i, isAux))
      if (isAux)
        continue;
    }

    #ifdef SUPPORT_ALT_STREAMS
    if (item.IsAltStream)
      continue;
    #endif

    if (item.IsDir
        || item.PathParts.Size() != 1
        || tarIndex != (UInt32)(Int32)-1
        || !NestedTar_IsTarPath(item.Path))
      return S_OK;

    tarIndex = i;
    tarItem = item;
  }

  if (tarIndex == (UInt32)(Int32)-1)
    return S_OK;

  CMyComPtr<ISequentialInStream> seqStream;
  CMyComPtr<IInStream> inStream;
  CMyComPtr<IInArchiveGetStream> getStream;
  if (outerArc.Archive->QueryInterface(IID_IInArchiveGetStream,
      (void **)&getStream) == S_OK && getStream)
  {
    if (getStream->GetStream(tarIndex, &seqStream) == S_OK && seqStream)
      seqStream.QueryInterface(IID_IInStream, &inStream);
  }
  if (!inStream && (outerStream || outerArc.InStream))
  {
    IInStream *restartStream = outerStream;
    if (!restartStream)
      restartStream = outerArc.InStream;
    seqStream.Release();
    RINOK(NestedTar_CreateSeekable(outerArc.Archive, tarIndex,
        restartStream, &inStream))
  }
  else if (!seqStream)
    RINOK(NestedTar_CreatePipe(outerArc.Archive, tarIndex, &seqStream))

  const int tarFormatIndex = FindTarFormatIndex(codecs);
  if (tarFormatIndex < 0)
    return S_OK;

  CObjectVector<COpenType> types;
  COpenType openType;
  openType.FormatIndex = tarFormatIndex;
  types.Add(openType);

  CIntVector excludedFormats;
  COpenOptions op;
  #ifndef Z7_SFX
  op.props = baseOptions.props;
  #else
  UNUSED_VAR(baseOptions)
  #endif
  op.codecs = codecs;
  op.types = &types;
  op.excludedFormats = &excludedFormats;
  op.openType = openType;
  op.stream = inStream;
  op.seqStream = seqStream;
  op.callback = NULL;
  op.filePath = tarItem.Path;

  CArc nestedArc;
  nestedArc.Path = tarItem.Path;
  nestedArc.filePath = tarItem.Path;
  nestedArc.SubfileIndex = tarIndex;

  const HRESULT openResult = nestedArc.OpenStream(op);
  if (openResult == S_FALSE)
    return S_OK;
  RINOK(openResult)

  if (!IsTarFormat(codecs, nestedArc))
  {
    nestedArc.Close();
    return S_OK;
  }

  nestedArc.InStream = inStream;
  nestedLink.Arcs.Add(nestedArc);
  nestedLink.IsOpen = true;
  found = true;
  sequential = !inStream;
  return S_OK;
}

static bool NestedTar_IsTarPath(const UString &path)
{
  const int sep = path.ReverseFind_PathSepar();
  const int dot = path.ReverseFind_Dot();
  if (dot < 0 || dot < sep)
    return false;
  return UString(path.Ptr((unsigned)(dot + 1))).IsEqualTo_Ascii_NoCase("tar");
}

static HRESULT NestedTar_CreatePipe(
    IInArchive *outerArchive,
    UInt32 index,
    ISequentialInStream **stream)
{
  *stream = NULL;
  CNestedTarPipe *pipeSpec = new CNestedTarPipe(outerArchive, index);
  CMyComPtr<ISequentialInStream> pipe = pipeSpec;
  *stream = pipe.Detach();
  return S_OK;
}

static HRESULT NestedTar_CreateSeekable(
    IInArchive *outerArchive,
    UInt32 index,
    IInStream *outerStream,
    IInStream **stream)
{
  *stream = NULL;
  CNestedTarSeekableStream *streamSpec =
      new CNestedTarSeekableStream(outerArchive, index, outerStream);
  CMyComPtr<IInStream> streamTemp = streamSpec;
  *stream = streamTemp.Detach();
  return S_OK;
}
