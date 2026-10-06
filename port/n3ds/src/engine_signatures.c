#include "cseries.h"
#include <xtl.h>
#include "engine_signatures.h"
_Static_assert(sizeof(XCALCSIG_SIGNATURE)==20,"Original save integrity field");
HANDLE WINAPI XCalculateSignatureBegin(DWORD flags)
{
    unsigned int token=n3ds_signature_begin(flags);
    return token?(HANDLE)(unsigned long)token:INVALID_HANDLE_VALUE;
}
DWORD WINAPI XCalculateSignatureUpdate(HANDLE handle,const BYTE *data,ULONG size)
{ return n3ds_signature_update((unsigned long)handle,data,size); }
DWORD WINAPI XCalculateSignatureEnd(HANDLE handle,PXCALCSIG_SIGNATURE signature)
{ return n3ds_signature_end((unsigned long)handle,signature?signature->Signature:NULL); }
