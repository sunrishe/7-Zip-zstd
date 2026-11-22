// CompressFormats.h

#ifndef ZIP7_INC_COMPRESS_FORMATS_H
#define ZIP7_INC_COMPRESS_FORMATS_H

#include "../../../Common/MyTypes.h"

#include "ContextMenuFlags.h"

namespace NCompressFormats
{
  struct CFormat
  {
    UInt32 Flag;
    const char *ArcType;
    const char *Extension;
  };

  static const CFormat kFormats[] =
  {
    { NContextMenuFlags::kCompressToBr,     "brotli", "br"  },
    { NContextMenuFlags::kCompressToBz2,    "bzip2",  "bz2" },
    { NContextMenuFlags::kCompressToGz,     "gzip",   "gz"  },
    { NContextMenuFlags::kCompressToLz4,    "lz4",    "lz4" },
    { NContextMenuFlags::kCompressToLz5,    "lz5",    "lz5" },
    { NContextMenuFlags::kCompressToLizard, "lizard", "liz" },
    { NContextMenuFlags::kCompressToTar,    "tar",    "tar" },
    { NContextMenuFlags::kCompressToWim,    "wim",    "wim" },
    { NContextMenuFlags::kCompressToXz,     "xz",     "xz"  },
    { NContextMenuFlags::kCompressToZst,    "zstd",   "zst" }
  };

  inline const CFormat *FindByFlag(UInt32 flag)
  {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(kFormats); i++)
      if (kFormats[i].Flag == flag)
        return &kFormats[i];
    return NULL;
  }
}

#endif
