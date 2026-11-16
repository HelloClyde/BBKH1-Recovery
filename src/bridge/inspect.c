/* Load/inspect a bridge DLL without calling WDU_Init or opening a USB device. */
#include "bridge.h"
#include <stdio.h>
#include <wchar.h>
int wmain(int argc,wchar_t **argv) {
    if(argc!=2)return 2;
    HMODULE dll=LoadLibraryExW(argv[1],NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!dll){fprintf(stderr,"DLL load failed: %lu\n",GetLastError());return 1;}
    const char *names[]={"WD_DriverName","OsEventCreate","OsEventSignal","OsEventClose","Stat2Str","WDU_Init","WDU_Uninit","WDU_GetDeviceInfo","WDU_PutDeviceInfo","WDU_SetInterface","WDU_ResetDevice","WDU_Transfer","WDU_TransferDefaultPipe","WDU_TransferBulk"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!GetProcAddress(dll,names[i])){fprintf(stderr,"Missing export %s\n",names[i]);FreeLibrary(dll);return 1;}
    __typeof__(&H1BridgeBuildInfo) info;FARPROC proc=GetProcAddress(dll,"H1BridgeBuildInfo");if(!proc){FreeLibrary(dll);return 1;}memcpy(&info,&proc,sizeof(proc));
    printf("{\"passed\":true,\"imports_resolved\":14,\"build\":\"%s\",\"usb_registration_called\":false}\n",info());FreeLibrary(dll);return 0;
}
