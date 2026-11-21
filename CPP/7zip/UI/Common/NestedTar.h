// NestedTar.h

#ifndef ZIP7_INC_NESTED_TAR_H
#define ZIP7_INC_NESTED_TAR_H

#include "OpenArchive.h"

class CCodecs;

HRESULT NestedTar_OpenSingle(
    CCodecs *codecs,
    const COpenOptions &baseOptions,
    const CArc &outerArc,
    CArchiveLink &nestedLink,
    bool &found,
    bool &sequential,
    IInStream *outerStream = NULL);

#endif
