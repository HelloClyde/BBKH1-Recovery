/* A standalone client of the read-only bridge; never starts the vendor recovery EXE. */
#include "bridge.h"
#include <stdio.h>
#include <wchar.h>
typedef struct {
    __typeof__(&WDU_Init) init;__typeof__(&WDU_Uninit) uninit;
    __typeof__(&WDU_GetDeviceInfo) info;__typeof__(&WDU_PutDeviceInfo) put;
    __typeof__(&WDU_TransferDefaultPipe) control;__typeof__(&H1BridgeBuildInfo) build;
} API;
static API api;
static FILE *out;
static CRITICAL_SECTION output_lock;
static HANDLE observed;
static DWORD count;
static void bytes(const BYTE *data,DWORD size){fputc('"',out);for(DWORD i=0;i<size;i++)fprintf(out,"%02x",data[i]);fputc('"',out);}
static BOOL WINAPI attach(void *device,const void *initial,void *user) {
    (void)user;USB_DEVICE_DESCRIPTOR expected;memcpy(&expected,initial,sizeof(expected));
    void *snapshot=NULL;DWORD info_status=api.info(device,&snapshot);
    USB_DEVICE_DESCRIPTOR descriptor={0};if(!info_status&&snapshot)memcpy(&descriptor,snapshot,sizeof(descriptor));
    if(snapshot)api.put(snapshot);
    BYTE setup[8]={0x80,6,0,1,0,0,18,0};BYTE data[18]={0};DWORD got=0;
    DWORD status=api.control(device,1,0,data,sizeof(data),&got,setup,3000);
    BOOL ok=!status&&got==sizeof(descriptor)&&!memcmp(data,&expected,sizeof(descriptor));
    BYTE config[4096]={0};DWORD config_got=0,config_status=ERROR_INVALID_DATA;
    BYTE config_setup[8]={0x80,6,0,2,0,0,9,0};
    if(ok) {
        config_status=api.control(device,1,0,config,9,&config_got,config_setup,3000);
        if(!config_status&&config_got==9) {
            DWORD total=config[2]|config[3]<<8;
            if(total>=9&&total<=sizeof(config)) {
                config_setup[6]=(BYTE)total;config_setup[7]=(BYTE)(total>>8);
                config_status=api.control(device,1,0,config,total,&config_got,config_setup,3000);
                if(!config_status&&config_got!=total)config_status=ERROR_INVALID_DATA;
            } else config_status=ERROR_INVALID_DATA;
        }
    }
    EnterCriticalSection(&output_lock);
    fprintf(out,"%s{\"vendor_id\":%u,\"product_id\":%u,\"snapshot_status\":%lu,\"device_read_status\":%lu,\"device_read_ok\":%s,\"device_descriptor_hex\":",count?",\n":"",descriptor.idVendor,descriptor.idProduct,info_status,status,ok?"true":"false");
    bytes(data,got<=sizeof(data)?got:0);
    fprintf(out,",\"config_read_status\":%lu,\"config_descriptor_hex\":",config_status);
    bytes(config,config_got<=sizeof(config)?config_got:0);fprintf(out,"}");count++;
    LeaveCriticalSection(&output_lock);SetEvent(observed);return TRUE;
}
static void WINAPI detach(void *device,void *user){(void)device;(void)user;}
int wmain(int argc,wchar_t **argv) {
    if(argc!=3){fwprintf(stderr,L"Usage: H1BridgeProbe.exe readonly-bridge.dll output.json\n");return 2;}
    out=_wfopen(argv[2],L"wb");if(!out)return 2;
    HMODULE dll=LoadLibraryW(argv[1]);if(!dll){fprintf(stderr,"Bridge load failed: %lu\n",GetLastError());return 2;}
#define LOAD(member,name) do{FARPROC p=GetProcAddress(dll,#name);if(!p)return 2;memcpy(&api.member,&p,sizeof(p));}while(0)
    LOAD(init,WDU_Init);LOAD(uninit,WDU_Uninit);LOAD(info,WDU_GetDeviceInfo);
    LOAD(put,WDU_PutDeviceInfo);LOAD(control,WDU_TransferDefaultPipe);LOAD(build,H1BridgeBuildInfo);
    if(!strstr(api.build(),"READ ONLY")){fprintf(stderr,"Only read-only bridge accepted\n");return 2;}
    InitializeCriticalSection(&output_lock);observed=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!observed)return 2;
    fprintf(out,"{\"schema\":\"h1-readonly-bridge-probe-v1\",\"firmware_transfers\":false,\"driver_changes\":false,\"devices\":[\n");
    H1Match boot={0x601a,0x4740,0,0,0,0,0},burn={0x0888,0xa880,0,0,0,0,0};
    H1Events callbacks={attach,detach,NULL,NULL};void *boot_reg=NULL,*burn_reg=NULL;
    DWORD boot_status=api.init(&boot_reg,&boot,1,&callbacks,NULL,0);
    DWORD burn_status=api.init(&burn_reg,&burn,1,&callbacks,NULL,0);
    if(!boot_status&&!burn_status)WaitForSingleObject(observed,5000);
    if(boot_reg)api.uninit(boot_reg);
    if(burn_reg)api.uninit(burn_reg);
    fprintf(out,"\n],\"attached_device_count\":%lu,\"boot_registration_status\":%lu,\"burn_registration_status\":%lu}\n",count,boot_status,burn_status);
    fclose(out);CloseHandle(observed);DeleteCriticalSection(&output_lock);FreeLibrary(dll);
    printf("Read-only bridge: BOOT registration %lu; BURN registration %lu; devices notified %lu.\n",boot_status,burn_status,count);
    return boot_status||burn_status?1:0;
}
