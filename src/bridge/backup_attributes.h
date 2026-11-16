/* H1 V1.41 PC-side backup list compatibility fix.
   The old list format reserves two hex digits for Windows file attributes.
   IntToHex(value,2) pads but does not truncate: compressed/not-indexed flags
   make 2810/2820 and shift every subsequent field. Preserve the low byte.
   Both call sites are validated before any code is changed. */
#ifndef H1_BACKUP_ATTRIBUTES_H
#define H1_BACKUP_ATTRIBUTES_H
#include <windows.h>
#include <string.h>
static const DWORD h1_attribute_sites[2]={0x6d62d,0x6d6ae};
static const BYTE h1_attribute_old[11]={0xba,2,0,0,0,0x8b,0x85,0x44,0xfd,0xff,0xff};
/* push 2; pop edx; movzx eax,byte [ebp-0x2bc]; nop (same size/stack/flags). */
static const BYTE h1_attribute_new[11]={0x6a,2,0x5a,0x0f,0xb6,0x85,0x44,0xfd,0xff,0xff,0x90};
static DWORD h1_fix_backup_attributes(BYTE *image,SIZE_T size) {
    if(!image||size<h1_attribute_sites[1]+11)return ERROR_INVALID_DATA;
    BOOL already=TRUE;
    for(unsigned i=0;i<2;i++) {
        BYTE *at=image+h1_attribute_sites[i];
        if(memcmp(at,h1_attribute_new,11))already=FALSE;
        if(memcmp(at,h1_attribute_old,11)&&memcmp(at,h1_attribute_new,11))return ERROR_REVISION_MISMATCH;
    }
    if(already)return ERROR_SUCCESS;
    /* Both sites share one code page; preserve its protection exactly once. */
    BYTE *first=image+h1_attribute_sites[0];
    SIZE_T extent=h1_attribute_sites[1]-h1_attribute_sites[0]+11;
    DWORD protection,unused;
    if(!VirtualProtect(first,extent,PAGE_EXECUTE_READWRITE,&protection))return GetLastError();
    for(unsigned i=0;i<2;i++)memcpy(image+h1_attribute_sites[i],h1_attribute_new,11);
    BOOL restored=VirtualProtect(first,extent,protection,&unused);
    if(!FlushInstructionCache(GetCurrentProcess(),image,size))return GetLastError();
    return restored?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
}
#endif
