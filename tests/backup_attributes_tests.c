#include <windows.h>
#include <stdio.h>
#include "backup_attributes.h"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Failed line %d\n",__LINE__);return 1;}}while(0)
int main(void) {
    SIZE_T size=0x70000;BYTE *image=VirtualAlloc(NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(image);
    for(int i=0;i<2;i++)memcpy(image+h1_attribute_sites[i],h1_attribute_old,11);
    DWORD old;CHECK(VirtualProtect(image,size,PAGE_EXECUTE_READ,&old));
    CHECK(!h1_fix_backup_attributes(image,size));
    for(int i=0;i<2;i++){MEMORY_BASIC_INFORMATION m;CHECK(!memcmp(image+h1_attribute_sites[i],h1_attribute_new,11));CHECK(VirtualQuery(image+h1_attribute_sites[i],&m,sizeof(m)));CHECK(m.Protect==PAGE_EXECUTE_READ);}
    CHECK(!h1_fix_backup_attributes(image,size));
    CHECK(h1_fix_backup_attributes(image,16)==ERROR_INVALID_DATA);
    CHECK(h1_fix_backup_attributes(NULL,size)==ERROR_INVALID_DATA);
    CHECK(VirtualProtect(image,size,PAGE_READWRITE,&old));
    for(int bad=0;bad<2;bad++) {
        for(int i=0;i<2;i++)memcpy(image+h1_attribute_sites[i],h1_attribute_old,11);
        image[h1_attribute_sites[bad]]=0;
        CHECK(h1_fix_backup_attributes(image,size)==ERROR_REVISION_MISMATCH);
        CHECK(!memcmp(image+h1_attribute_sites[1-bad],h1_attribute_old,11));
        CHECK(image[h1_attribute_sites[bad]]==0);
    }
    VirtualFree(image,0,MEM_RELEASE);puts("backup attribute patch: 6 cases passed; no USB");return 0;
}
