/* Tests only the fake USB platform. No actual USB access. */
#include "bridge.h"
#include "fake_platform.h"
#include <stdio.h>
typedef struct {HANDLE ready;LONG attached;} Context;
static BOOL WINAPI on_attach(void *h,const void *desc,void *arg){(void)h;(void)desc;Context *c=arg;InterlockedIncrement(&c->attached);SetEvent(c->ready);return TRUE;}
static void WINAPI on_detach(void *h,void *arg){(void)h;(void)arg;}
static BOOL WINAPI on_power(void *h,DWORD state,void *arg){(void)h;(void)state;(void)arg;return TRUE;}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;
    HMODULE dll=LoadLibraryW(argv[1]);if(!dll)return 2;
    __typeof__(&WDU_Init) init;__typeof__(&WDU_Uninit) uninit;__typeof__(&H1BridgeBuildInfo) build;
    __typeof__(&H1FakeStage) stage;__typeof__(&H1FakeBurnPacketSizes) packets;__typeof__(&H1FakeCycles) cycles;
    __typeof__(&H1FakeResetStatus) reset_status;__typeof__(&H1FakeOpenCount) opened;
#define LOAD(field,name) do{FARPROC p=GetProcAddress(dll,#name);if(!p)return 3;memcpy(&field,&p,sizeof(p));}while(0)
    LOAD(init,WDU_Init);LOAD(uninit,WDU_Uninit);LOAD(build,H1BridgeBuildInfo);LOAD(stage,H1FakeStage);
    LOAD(packets,H1FakeBurnPacketSizes);LOAD(cycles,H1FakeCycles);LOAD(reset_status,H1FakeResetStatus);LOAD(opened,H1FakeOpenCount);
    if(!strstr(build(),"FAKE PLATFORM"))return 3;
    const DWORD before[]={64,512,64,64},after[]={512,512,64,512},errors[]={0,0,0,ERROR_ACCESS_DENIED};
    const DWORD expected_cycle[]={1,0,1,1};unsigned passed=0;
    for(unsigned i=0;i<4;i++){
        packets(before[i],after[i]);reset_status(errors[i]);stage(2);
        Context c={CreateEventW(NULL,TRUE,FALSE,NULL),0};H1Events events={on_attach,on_detach,on_power,&c};
        H1Match m={0};m.vendor=0x0888;m.product=0xa880;void *reg=NULL;
        if(init(&reg,&m,1,&events,NULL,1))return 4;
        DWORD wait=WaitForSingleObject(c.ready,i==2?5400:1200);
        BOOL expected_attach=i<2;
        BOOL ok=(wait==WAIT_OBJECT_0)==expected_attach&&cycles()==expected_cycle[i]&&c.attached==(expected_attach?1:0);
        uninit(reg);CloseHandle(c.ready);if(opened())ok=FALSE;
        if(!ok){printf("FAIL preflight case %u\n",i);return 1;}passed++;
    }
    FreeLibrary(dll);printf("{\"preflight_cases\":%u,\"passed\":true,\"physical_usb\":false}\n",passed);return 0;
}
