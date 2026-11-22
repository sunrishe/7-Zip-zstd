// ContextMenuFlags.h

#ifndef ZIP7_INC_CONTEXT_MENU_FLAGS_H
#define ZIP7_INC_CONTEXT_MENU_FLAGS_H

namespace NContextMenuFlags
{
  const UInt32 kExtract = 1 << 0;
  const UInt32 kExtractHere = 1 << 1;
  const UInt32 kExtractTo = 1 << 2;
  const UInt32 kSmartExtract = 1 << 3;

  const UInt32 kTest = 1 << 4;
  const UInt32 kOpen = 1 << 5;
  const UInt32 kOpenAs = 1 << 6;

  const UInt32 kCompress = 1 << 8;
  const UInt32 kCompressTo7z = 1 << 9;
  const UInt32 kCompressEmail = 1 << 10;
  const UInt32 kCompressTo7zEmail = 1 << 11;
  const UInt32 kCompressToZip = 1 << 12;
  const UInt32 kCompressToZipEmail = 1 << 13;
  const UInt32 kSmartExtractToDesktop = 1 << 14;
  const UInt32 kCompressToTar = 1 << 15;
  const UInt32 kCompressToBz2 = 1 << 16;
  const UInt32 kCompressToGz = 1 << 17;
  const UInt32 kCompressToXz = 1 << 18;
  const UInt32 kCompressToZst = 1 << 19;
  const UInt32 kCompressToBr = 1 << 20;
  const UInt32 kCompressToLz4 = 1 << 21;
  const UInt32 kCompressToLz5 = 1 << 22;
  const UInt32 kCompressToLizard = 1 << 23;
  const UInt32 kCompressToWim = 1 << 24;

  const UInt32 kCRC_Cascaded = (UInt32)1 << 30;
  const UInt32 kCRC = (UInt32)1 << 31;

  const UInt32 kDefaultContextMenuItems =
      kExtract | kSmartExtract | kSmartExtractToDesktop | kTest |
      kOpen | kOpenAs | kCompress | kCompressTo7z | kCompressToZip |
      kCompressToZst | kCRC | kCRC_Cascaded;
}

#endif
