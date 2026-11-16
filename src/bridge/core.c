/* H1 V1.41 x86 WinDriver ABI over WinUSB. No USB I/O occurs in DllMain. */
#define H1_BUILD_DLL
#include "bridge.h"
#include <winioctl.h>
#include <usbioctl.h>
#include <winusb.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include <stdint.h>
#include <stdarg.h>
#ifndef H1_ENABLE_TRANSFER
#define H1_ENABLE_TRANSFER 0
#endif
#ifndef H1_FAKE_PLATFORM
#define H1_FAKE_PLATFORM 0
#endif
#ifndef H1_DIAGNOSTICS
#define H1_DIAGNOSTICS 0
#endif
#ifndef H1_BURN_PREFLIGHT
#define H1_BURN_PREFLIGHT 0
#endif
#ifndef H1_RAM_PREPARE_ONLY
#define H1_RAM_PREPARE_ONLY 0
#endif
#ifndef H1_PC_ATTRIBUTE_FIX
#define H1_PC_ATTRIBUTE_FIX 0
#endif
#if H1_PC_ATTRIBUTE_FIX && !H1_FAKE_PLATFORM
#include "backup_attributes.h"
static INIT_ONCE backup_fix_once=INIT_ONCE_STATIC_INIT;
static DWORD backup_fix_status;
static BOOL CALLBACK apply_backup_fix(PINIT_ONCE once,void *parameter,void **context) {
    (void)once;(void)parameter;(void)context;
    wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n||n>=32768){backup_fix_status=ERROR_INVALID_NAME;return TRUE;}
    const wchar_t *name=wcsrchr(path,L'\\');name=name?name+1:path;
    if(_wcsicmp(name,L"super_recover.exe"))return TRUE;
    BYTE *image=(BYTE*)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)image;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>4096){backup_fix_status=ERROR_INVALID_DATA;return TRUE;}
    IMAGE_NT_HEADERS *nt=(IMAGE_NT_HEADERS*)(image+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386){backup_fix_status=ERROR_INVALID_DATA;return TRUE;}
    backup_fix_status=h1_fix_backup_attributes(image,nt->OptionalHeader.SizeOfImage);
    return TRUE;
}
#endif
#if H1_FAKE_PLATFORM
#define H1_FAKE_REPLACE_API
#include "fake_platform.h"
#endif

_Static_assert(sizeof(void *)==4,"Original recovery requires an x86 DLL");
_Static_assert(sizeof(H1Match)==10,"H1 match ABI");
_Static_assert(sizeof(H1Events)==16,"H1 callback ABI");
_Static_assert(sizeof(USB_DEVICE_DESCRIPTOR)==18,"USB descriptor ABI");

typedef struct Reg Reg;
typedef struct Device {
    struct Device *next,*redirect;
    Reg *owner;
    void *token;
    HANDLE file,idle,gone;
    WINUSB_INTERFACE_HANDLE usb;
    USB_DEVICE_DESCRIPTOR descriptor;
    CRITICAL_SECTION io;
    LONG references,broken;
    BOOL alive,accepted,reset_pending;
    UCHAR alternate;
    USHORT packet_size;
#if H1_RAM_PREPARE_ONLY
    DWORD boot_address;
#endif
    wchar_t instance[2048];
} Device;
struct Reg {
    Reg *next;
    void *token;
    H1Match match;
    H1Events events;
    HANDLE stop,ready,thread,changed,users_idle;
    LONG users;
    DWORD thread_id;
    Device *devices,*current;
    BOOL stopping,cleanup_claimed,acknowledge;
    BOOL preflight_attempted,preflight_failed;
    ULONGLONG preflight_deadline;
};
static Reg *registrations;
static SRWLOCK state_lock=SRWLOCK_INIT,log_lock=SRWLOCK_INIT;
static LONG token_sequence;
#if H1_RAM_PREPARE_ONLY
static LONG ram_started;
static BOOL save_preparation_file(const wchar_t *name,const void *buffer,DWORD size) {
    wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n||n>=32700)return FALSE;
    wchar_t *last=wcsrchr(path,L'\\');if(!last)return FALSE;
    wcscpy(last+1,name);FILE *f=_wfopen(path,L"wb");if(!f)return FALSE;
    BOOL ok=fwrite(buffer,1,size,f)==size;fclose(f);return ok;
}
#endif
static char driver_name[64]="windrvr6";
static _Thread_local char status_message[256];
#if H1_FAKE_PLATFORM
H1_API DWORD WINAPI H1FakePendingWaiters(void) {
    DWORD n=0;AcquireSRWLockShared(&state_lock);
    for(Reg *r=registrations;r;r=r->next)n+=(DWORD)r->users;
    ReleaseSRWLockShared(&state_lock);return n;
}
#endif

static void trace(const char *operation,DWORD pipe,DWORD read,DWORD size,DWORD actual,DWORD status,const BYTE *setup) {
    wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n||n>=32730)return;
    wchar_t *last=wcsrchr(path,L'\\');if(!last)return;
    wcscpy(last+1,L"H1USBBridge.log");
    AcquireSRWLockExclusive(&log_lock);
    FILE *f=_wfopen(path,L"ab");
    if(f) {
        fprintf(f,"%llu %s pipe=%02lx read=%lu size=%lu actual=%lu status=%lu",(unsigned long long)GetTickCount64(),operation,pipe,read,size,actual,status);
        if(setup){fputs(" setup=",f);for(unsigned i=0;i<8;i++)fprintf(f,"%02x",setup[i]);}
        fputc('\n',f);fclose(f);
    }
    ReleaseSRWLockExclusive(&log_lock);
}
#if H1_DIAGNOSTICS
/* File logging must preserve LastError, including ERROR_IO_PENDING. */
static void diag(const char *format,...) {
    DWORD saved=GetLastError();wchar_t path[32768];
    DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(n&&n<32730) {
        wchar_t *last=wcsrchr(path,L'\\');
        if(last) {
            wcscpy(last+1,L"H1USBBridge.log");AcquireSRWLockExclusive(&log_lock);
            FILE *f=_wfopen(path,L"ab");
            if(f) {
                fprintf(f,"%llu diagnostic tid=%lu ",(unsigned long long)GetTickCount64(),GetCurrentThreadId());
                va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);
                fputc('\n',f);fclose(f);
            }
            ReleaseSRWLockExclusive(&log_lock);
        }
    }
    SetLastError(saved);
}
#else
#define diag(...) ((void)0)
#endif
static void *token(void) {return (void *)(uintptr_t)(((DWORD)InterlockedIncrement(&token_sequence)<<2)|1);}
static BOOL permitted(WORD vid,WORD pid) {return (vid==0x601a&&pid==0x4740)||(vid==0x0888&&pid==0xa880);}
static BOOL valid_match(const H1Match *m) {
    return m&&permitted(m->vendor,m->product)&&!m->device_class&&!m->device_subclass&&
        !m->interface_class&&!m->interface_subclass&&!m->interface_protocol;
}
static Reg *find_reg(void *h) {for(Reg *r=registrations;r;r=r->next)if(r->token==h)return r;return NULL;}
static void release_device(Device *d) {
    LeaveCriticalSection(&d->io);
    AcquireSRWLockExclusive(&state_lock);
    if(!--d->references)SetEvent(d->idle);
    ReleaseSRWLockExclusive(&state_lock);
}
static Device *acquire_device(void *h) {
    ULONGLONG end=GetTickCount64()+5000;
    for(;;) {
        Device *found=NULL;Reg *waiting=NULL;
        AcquireSRWLockExclusive(&state_lock);
        for(Reg *r=registrations;r&&!found&&!waiting;r=r->next)if(!r->stopping)
            for(Device *d=r->devices;d;d=d->next)if(d->token==h) {
                /* Only an explicitly requested reset preserves the caller's
                   cached handle. Ordinary unplug/stage changes never redirect. */
                while(d->redirect)d=d->redirect;
                if(d->alive&&!d->broken) {
                    if(!d->references++)ResetEvent(d->idle);
                    found=d;
                } else if(d->reset_pending&&GetTickCount64()<end) {
                    waiting=r;if(!r->users++)ResetEvent(r->users_idle);ResetEvent(r->changed);
                }
                break;
            }
        ReleaseSRWLockExclusive(&state_lock);
        if(waiting) {
            HANDLE events[2]={waiting->stop,waiting->changed};
            ULONGLONG now=GetTickCount64();DWORD delay=now>=end?0:(DWORD)(end-now);
            DWORD result=WaitForMultipleObjects(2,events,FALSE,delay);
            AcquireSRWLockExclusive(&state_lock);
            if(!--waiting->users)SetEvent(waiting->users_idle);
            ReleaseSRWLockExclusive(&state_lock);
            if(result!=WAIT_OBJECT_0+1)return NULL;
            continue;
        }
        if(found) {
            EnterCriticalSection(&found->io);
            if(WaitForSingleObject(found->gone,0)==WAIT_OBJECT_0||WaitForSingleObject(found->owner->stop,0)==WAIT_OBJECT_0) {
                release_device(found);found=NULL;
            }
        }
        return found;
    }
}

#if !H1_FAKE_PLATFORM
static void property(HDEVINFO list,SP_DEVINFO_DATA *dev,DWORD key,wchar_t *out,DWORD bytes) {
    DWORD type=0;ZeroMemory(out,bytes);
    if(!SetupDiGetDeviceRegistryPropertyW(list,dev,key,&type,(BYTE *)out,bytes-4,NULL))out[0]=0;
}
static wchar_t *interface_path(const GUID *guid,const wchar_t *instance) {
    HDEVINFO list=SetupDiGetClassDevsW(guid,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(list==INVALID_HANDLE_VALUE)return NULL;
    SP_DEVICE_INTERFACE_DATA item={0};item.cbSize=sizeof(item);wchar_t *result=NULL;
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(list,NULL,guid,i,&item);i++) {
        DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(list,&item,NULL,0,&bytes,NULL);
        if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes);
        if(!detail)break;
        detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);
        if(SetupDiGetDeviceInterfaceDetailW(list,&item,detail,bytes,NULL,&dev)) {
            wchar_t id[2048]={0};
            if(SetupDiGetDeviceInstanceIdW(list,&dev,id,2048,NULL)&&!_wcsicmp(id,instance)) {
                size_t count=(wcslen(detail->DevicePath)+1)*sizeof(wchar_t);
                result=HeapAlloc(GetProcessHeap(),0,count);if(result)memcpy(result,detail->DevicePath,count);
            }
        }
        HeapFree(GetProcessHeap(),0,detail);if(result)break;
    }
    SetupDiDestroyDeviceInfoList(list);return result;
}
static BOOL read_guids(HKEY key,wchar_t *guids,DWORD bytes) {
    DWORD type=0,size=bytes-4;ZeroMemory(guids,bytes);
    LONG status=RegQueryValueExW(key,L"DeviceInterfaceGUIDs",NULL,&type,(BYTE *)guids,&size);
    if(status!=ERROR_SUCCESS){size=bytes-4;status=RegQueryValueExW(key,L"DeviceInterfaceGUID",NULL,&type,(BYTE *)guids,&size);}
    if(status!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_MULTI_SZ))guids[0]=0;
    return guids[0]!=0;
}
static wchar_t *lookup_path(HDEVINFO list,SP_DEVINFO_DATA *dev,const wchar_t *instance) {
    wchar_t guids[2048]={0};HKEY key=SetupDiOpenDevRegKey(list,dev,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_QUERY_VALUE);
    if(key!=INVALID_HANDLE_VALUE) {
        if(!read_guids(key,guids,sizeof(guids))) {
            HKEY sub;if(RegOpenKeyExW(key,L"Device Parameters",0,KEY_QUERY_VALUE,&sub)==ERROR_SUCCESS){read_guids(sub,guids,sizeof(guids));RegCloseKey(sub);}
        }
        RegCloseKey(key);
    }
    for(wchar_t *p=guids;*p;p+=wcslen(p)+1) {
        GUID guid;if(SUCCEEDED(CLSIDFromString(p,&guid))) {wchar_t *path=interface_path(&guid,instance);if(path)return path;}
    }
    const GUID generic={0xa5dcbf10,0x6530,0x11d2,{0x90,0x1f,0x00,0xc0,0x4f,0xb9,0x51,0xed}};
    return interface_path(&generic,instance);
}
static BOOL locate(const H1Match *match,wchar_t *instance,wchar_t **path) {
    HDEVINFO list=SetupDiGetClassDevsW(NULL,L"USB",NULL,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(list==INVALID_HANDLE_VALUE)return FALSE;
    wchar_t wanted[64];swprintf(wanted,64,L"USB\\VID_%04X&PID_%04X",match->vendor,match->product);
    SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);BOOL found=FALSE;*path=NULL;
    for(DWORD i=0;SetupDiEnumDeviceInfo(list,i,&dev);i++) {
        wchar_t ids[4096],service[256];BOOL matches=FALSE;property(list,&dev,SPDRP_HARDWAREID,ids,sizeof(ids));
        for(wchar_t *p=ids;*p;p+=wcslen(p)+1) {
            for(wchar_t *c=p;*c;c++)*c=towupper(*c);
            size_t n=wcslen(wanted);if(!wcsncmp(p,wanted,n)&&(!p[n]||p[n]==L'&')){matches=TRUE;break;}
        }
        if(!matches)continue;
        property(list,&dev,SPDRP_SERVICE,service,sizeof(service));if(_wcsicmp(service,L"WinUSB"))continue;
        if(!SetupDiGetDeviceInstanceIdW(list,&dev,instance,2048,NULL))continue;
        *path=lookup_path(list,&dev,instance);if(*path){found=TRUE;break;}
    }
    SetupDiDestroyDeviceInfoList(list);return found;
}
static HANDLE open_file(const wchar_t *path) {return CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);}
static void close_file(HANDLE file){CloseHandle(file);}
#if H1_ENABLE_TRANSFER
/* WinUSB has no bus-reset API. H1 error recovery uses a documented, explicitly
   logged hub-port cycle. This is re-enumeration, NOT a fake ResetPipe success. */
static DWORD cycle_port(Device *d) {
    DWORD result=ERROR_NOT_FOUND,port=0,type=0,bytes=0;wchar_t parent_id[2048];
    HDEVINFO list=SetupDiGetClassDevsW(NULL,L"USB",NULL,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(list==INVALID_HANDLE_VALUE)return GetLastError();
    SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);DEVINST parent;
    if(SetupDiOpenDeviceInfoW(list,d->instance,NULL,0,&dev)&&
       SetupDiGetDeviceRegistryPropertyW(list,&dev,SPDRP_ADDRESS,&type,(BYTE *)&port,sizeof(port),NULL)&&port&&
       CM_Get_Parent(&parent,dev.DevInst,0)==CR_SUCCESS&&CM_Get_Device_IDW(parent,parent_id,2048,0)==CR_SUCCESS) {
        const GUID hubs={0xf18a0e88,0xc30c,0x11d0,{0x88,0x15,0x00,0xa0,0xc9,0x06,0xbe,0xd8}};
        wchar_t *path=interface_path(&hubs,parent_id);
        if(path) {
            HANDLE hub=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
            if(hub==INVALID_HANDLE_VALUE)result=GetLastError();
            else {
                BYTE storage[sizeof(USB_NODE_CONNECTION_INFORMATION_EX)+32*sizeof(USB_PIPE_INFO)]={0};
                PUSB_NODE_CONNECTION_INFORMATION_EX info=(PUSB_NODE_CONNECTION_INFORMATION_EX)storage;info->ConnectionIndex=port;
                if(!DeviceIoControl(hub,IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,storage,sizeof(storage),storage,sizeof(storage),&bytes,NULL))result=GetLastError();
                else if(info->ConnectionStatus!=DeviceConnected||info->DeviceIsHub||info->DeviceDescriptor.idVendor!=d->descriptor.idVendor||info->DeviceDescriptor.idProduct!=d->descriptor.idProduct)result=ERROR_DEVICE_NOT_CONNECTED;
                else {
                    USB_CYCLE_PORT_PARAMS params={port,0};
                    result=DeviceIoControl(hub,IOCTL_USB_HUB_CYCLE_PORT,&params,sizeof(params),&params,sizeof(params),&bytes,NULL)?0:GetLastError();
                    if(!result&&params.StatusReturned)result=ERROR_IO_DEVICE;
                }
                CloseHandle(hub);
            }
            HeapFree(GetProcessHeap(),0,path);
        }
    }
    SetupDiDestroyDeviceInfoList(list);return result;
}
#endif
#else
static BOOL locate(const H1Match *m,wchar_t *id,wchar_t **path){return H1FakeLocate(m,id,path);}
static HANDLE open_file(const wchar_t *path){return H1FakeOpen(path);}
static void close_file(HANDLE file){H1FakeClose(file);}
#if H1_ENABLE_TRANSFER
static DWORD cycle_port(Device *d){return H1FakeCycle(d->file);}
#endif
#endif

static void close_device(Device *d) {
    if(d->usb){WinUsb_Free(d->usb);d->usb=NULL;}
    if(d->file&&d->file!=INVALID_HANDLE_VALUE){close_file(d->file);d->file=NULL;}
}
static void discard_unattached(Device *d) {
    close_device(d);CloseHandle(d->idle);CloseHandle(d->gone);
    DeleteCriticalSection(&d->io);HeapFree(GetProcessHeap(),0,d);
}
static void detach(Reg *r,Device *d) {
    AcquireSRWLockExclusive(&state_lock);d->alive=FALSE;r->current=NULL;SetEvent(d->gone);SetEvent(r->changed);ReleaseSRWLockExclusive(&state_lock);
    CancelIoEx(d->file,NULL);
    WaitForSingleObject(d->idle,INFINITE);
    if(d->accepted&&r->events.detach)r->events.detach(d->token,r->events.user);
    close_device(d);trace("detach",0,0,0,0,0,NULL);
}
static void attach(Reg *r,Device *d) {
    AcquireSRWLockExclusive(&state_lock);d->alive=TRUE;d->next=r->devices;r->devices=d;r->current=d;ReleaseSRWLockExclusive(&state_lock);
    BOOL accepted=!r->events.attach||r->events.attach(d->token,&d->descriptor,r->events.user);
    d->accepted=!r->acknowledge||accepted;
    trace(d->accepted?"attach":"attach-rejected",0,0,d->descriptor.idProduct,0,0,NULL);
    if(!d->accepted)detach(r,d);
    else {
        AcquireSRWLockExclusive(&state_lock);
        for(Device *old=d->next;old;old=old->next)if(old->reset_pending&&!_wcsicmp(old->instance,d->instance)) {
            old->redirect=d;old->reset_pending=FALSE;
        }
        SetEvent(r->changed);ReleaseSRWLockExclusive(&state_lock);
    }
}
static Device *open_device(Reg *r,const wchar_t *instance,const wchar_t *path) {
    HANDLE file=open_file(path);if(file==INVALID_HANDLE_VALUE)return NULL;
    WINUSB_INTERFACE_HANDLE usb=NULL;if(!WinUsb_Initialize(file,&usb)){close_file(file);return NULL;}
    USB_DEVICE_DESCRIPTOR desc={0};ULONG got=0;USB_INTERFACE_DESCRIPTOR iface={0};
    BOOL valid=WinUsb_GetDescriptor(usb,USB_DEVICE_DESCRIPTOR_TYPE,0,0,(BYTE *)&desc,sizeof(desc),&got)&&got==sizeof(desc)&&
        desc.idVendor==r->match.vendor&&desc.idProduct==r->match.product&&WinUsb_QueryInterfaceSettings(usb,0,&iface)&&iface.bInterfaceNumber==0;
    BOOL input=FALSE,output=FALSE;USHORT input_packet=0,output_packet=0;
    if(valid)for(UCHAR i=0;i<iface.bNumEndpoints;i++) {
        WINUSB_PIPE_INFORMATION p={0};if(!WinUsb_QueryPipe(usb,0,i,&p)){valid=FALSE;break;}
        diag("endpoint vid=%04x pid=%04x pipe=%02x type=%u max_packet=%u interval=%u",desc.idVendor,desc.idProduct,p.PipeId,(unsigned)p.PipeType,p.MaximumPacketSize,p.Interval);
        if(p.PipeType==UsbdPipeTypeBulk&&p.PipeId==0x81){input=TRUE;input_packet=p.MaximumPacketSize;}
        if(p.PipeType==UsbdPipeTypeBulk&&p.PipeId==0x01){output=TRUE;output_packet=p.MaximumPacketSize;}
    }
    if(!valid||!input||!output){WinUsb_Free(usb);close_file(file);return NULL;}
    Device *d=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*d));
    if(!d){WinUsb_Free(usb);close_file(file);return NULL;}
    d->file=file;d->usb=usb;d->descriptor=desc;d->alternate=iface.bAlternateSetting;d->owner=r;d->token=token();
    d->packet_size=input_packet<output_packet?input_packet:output_packet;
    d->idle=CreateEventW(NULL,TRUE,TRUE,NULL);d->gone=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!d->idle||!d->gone){if(d->idle)CloseHandle(d->idle);if(d->gone)CloseHandle(d->gone);close_device(d);HeapFree(GetProcessHeap(),0,d);return NULL;}
    wcsncpy(d->instance,instance,2047);InitializeCriticalSection(&d->io);return d;
}
static BOOL preflight(Device *d) {
#if H1_ENABLE_TRANSFER && H1_BURN_PREFLIGHT
    Reg *r=d->owner;
    if(d->descriptor.idVendor==0x0888&&d->descriptor.idProduct==0xa880) {
        if(d->packet_size!=512) {
            if(!r->preflight_attempted) {
                r->preflight_attempted=TRUE;r->preflight_deadline=GetTickCount64()+5000;
                DWORD status=cycle_port(d);
                trace("burn-preflight-cycle",0,0,d->packet_size,0,status,NULL);
                if(status)r->preflight_failed=TRUE;
            } else if(GetTickCount64()>=r->preflight_deadline) {
                r->preflight_failed=TRUE;trace("burn-preflight-not-ready",0,0,d->packet_size,0,ERROR_NOT_READY,NULL);
            }
            return FALSE;
        }
        if(r->preflight_attempted)diag("burn-preflight-ready max_packet=%u",d->packet_size);
    }
#else
    (void)d;
#endif
    return TRUE;
}
static DWORD WINAPI worker(void *arg) {
    Reg *r=arg;HANDLE gates[2]={r->stop,r->ready};
    if(WaitForMultipleObjects(2,gates,FALSE,INFINITE)!=WAIT_OBJECT_0+1)return 0;
    while(WaitForSingleObject(r->stop,100)==WAIT_TIMEOUT) {
        wchar_t instance[2048]={0};wchar_t *path=NULL;BOOL seen=locate(&r->match,instance,&path);
        Device *current=r->current;
        if(current&&(!seen||InterlockedCompareExchange(&current->broken,0,0)||_wcsicmp(current->instance,instance)))detach(r,current);
        if(seen&&!r->current&&!r->preflight_failed&&WaitForSingleObject(r->stop,0)==WAIT_TIMEOUT) {
            Device *d=open_device(r,instance,path);
            if(d){
#if H1_RAM_PREPARE_ONLY
                if(d->descriptor.idVendor==0x0888&&d->descriptor.idProduct==0xa880) {
                    /* Withhold BURN from the official application's callback entirely. */
                    if(InterlockedCompareExchange(&ram_started,0,0)) {
                        char marker[96];int n=snprintf(marker,sizeof(marker),"%lu %u\n",GetCurrentProcessId(),d->packet_size);
                        save_preparation_file(L"H1RamPrepared.marker",marker,(DWORD)n);
                    }
                    trace("ram-only-burn-withheld",0,0,d->packet_size,0,0,NULL);
                    r->preflight_failed=TRUE;discard_unattached(d);
                } else
#endif
                if(preflight(d))attach(r,d);else discard_unattached(d);
            }
        }
        if(path)HeapFree(GetProcessHeap(),0,path);
    }
    if(r->current)detach(r,r->current);
    return 0;
}

H1_API char *WINAPI WD_DriverName(const char *name) {
    if(name){AcquireSRWLockExclusive(&state_lock);strncpy(driver_name,name,63);driver_name[63]=0;ReleaseSRWLockExclusive(&state_lock);}return driver_name;
}
H1_API DWORD WINAPI OsEventCreate(HANDLE *result) {
    if(!result)return ERROR_INVALID_PARAMETER;
    *result=CreateEventW(NULL,FALSE,FALSE,NULL);return *result?0:0x20000005;
}
H1_API BOOL WINAPI OsEventClose(HANDLE event){return event?CloseHandle(event):FALSE;}
H1_API DWORD WINAPI OsEventSignal(HANDLE event){return SetEvent(event)?0:0x20000007;}
H1_API const char *WINAPI Stat2Str(DWORD code) {
    if(!code)return "Success";
#if !H1_ENABLE_TRANSFER
    if(code==ERROR_ACCESS_DENIED)return "Read-only build: transfer/reset disabled";
#endif
    if(code==ERROR_NOT_SUPPORTED)return "Operation/options not supported by the H1 V1.41 bridge";
    if(!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,code,0,status_message,sizeof(status_message),NULL))snprintf(status_message,sizeof(status_message),"H1 bridge error 0x%08lx",code);
    return status_message;
}
H1_API const char *WINAPI H1BridgeBuildInfo(void) {
#if H1_PC_ATTRIBUTE_FIX
#if H1_FAKE_PLATFORM
    return "H1 bridge 0.2.2 BACKUP ATTRIBUTE FIX FAKE PLATFORM; no physical USB access";
#else
    return "H1 bridge 0.2.2; BURN preflight and two-digit backup attributes";
#endif
#else
#if H1_RAM_PREPARE_ONLY
#if H1_FAKE_PLATFORM
    return "H1 bridge RAM PREPARATION ONLY FAKE PLATFORM; no physical USB access";
#else
    return "H1 bridge RAM PREPARATION ONLY; BURN callbacks and commands blocked";
#endif
#else
#if H1_BURN_PREFLIGHT
#if H1_FAKE_PLATFORM
    return "H1 bridge 0.2.1 PREFLIGHT CANDIDATE FAKE PLATFORM; no physical USB access";
#else
    return "H1 bridge 0.2.1 PREFLIGHT CANDIDATE; hardware recovery unverified";
#endif
#else
#if H1_DIAGNOSTICS
#if H1_FAKE_PLATFORM
    return "H1 bridge 0.2.0-D1 DIAGNOSTIC FAKE PLATFORM; no physical USB access";
#else
    return "H1 bridge 0.2.0-D1 DIAGNOSTIC; transfer behavior unchanged; hardware recovery unverified";
#endif
#else
#if H1_FAKE_PLATFORM
    return "H1 bridge 0.2.0 FAKE PLATFORM; no physical USB access";
#elif H1_ENABLE_TRANSFER
    return "H1 bridge 0.2.0 TRANSFER CANDIDATE; hardware flashing not tested";
#else
    return "H1 bridge 0.2.0 READ ONLY; firmware transfers disabled";
#endif
#endif
#endif
#endif
#endif
}
H1_API DWORD WINAPI WDU_Init(void **result,const H1Match *match,DWORD count,const H1Events *events,const char *license,DWORD options) {
    (void)license;
    if(!result)return ERROR_INVALID_PARAMETER;
    /* Original H1 calls use WD_ACKNOWLEDGE (1): callback may reject a device. */
    *result=NULL;if((options&~1u)||count!=1||!valid_match(match)||!events)return ERROR_NOT_SUPPORTED;
#if H1_PC_ATTRIBUTE_FIX && !H1_FAKE_PLATFORM
    InitOnceExecuteOnce(&backup_fix_once,apply_backup_fix,NULL,NULL);
    diag("backup-attribute-fix status=%lu width=2 sites=2",backup_fix_status);
    if(backup_fix_status)return backup_fix_status;
#endif
#if H1_PC_FIX_VALIDATE_ONLY
    /* Dedicated offline-host test: no registrations, workers, USB or transfers. */
    return ERROR_ACCESS_DENIED;
#endif
#if H1_PC_ATTRIBUTE_FIX
    diag("version=0.2.2 init vid=%04x pid=%04x transfer=%u fake=%u",match->vendor,match->product,H1_ENABLE_TRANSFER,H1_FAKE_PLATFORM);
#else
#if H1_BURN_PREFLIGHT
    diag("version=0.2.1-preflight init vid=%04x pid=%04x transfer=%u fake=%u",match->vendor,match->product,H1_ENABLE_TRANSFER,H1_FAKE_PLATFORM);
#else
    diag("version=0.2.0-D1 init vid=%04x pid=%04x transfer=%u fake=%u",match->vendor,match->product,H1_ENABLE_TRANSFER,H1_FAKE_PLATFORM);
#endif
#endif
    Reg *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*r));if(!r)return ERROR_NOT_ENOUGH_MEMORY;
    r->match=*match;r->events=*events;r->acknowledge=(options&1)!=0;r->token=token();r->stop=CreateEventW(NULL,TRUE,FALSE,NULL);r->ready=CreateEventW(NULL,TRUE,FALSE,NULL);
    r->changed=CreateEventW(NULL,TRUE,FALSE,NULL);r->users_idle=CreateEventW(NULL,TRUE,TRUE,NULL);
    if(!r->stop||!r->ready||!r->changed||!r->users_idle){if(r->stop)CloseHandle(r->stop);if(r->ready)CloseHandle(r->ready);if(r->changed)CloseHandle(r->changed);if(r->users_idle)CloseHandle(r->users_idle);HeapFree(GetProcessHeap(),0,r);return ERROR_NOT_ENOUGH_MEMORY;}
    AcquireSRWLockExclusive(&state_lock);r->next=registrations;registrations=r;ReleaseSRWLockExclusive(&state_lock);
    r->thread=CreateThread(NULL,0,worker,r,0,&r->thread_id);
    if(!r->thread){WDU_Uninit(r->token);return ERROR_NOT_ENOUGH_MEMORY;}
    *result=r->token;
    if(!SetEvent(r->ready)){*result=NULL;WDU_Uninit(r->token);return ERROR_GEN_FAILURE;}
    return 0;
}
H1_API void WINAPI WDU_Uninit(void *h) {
    AcquireSRWLockExclusive(&state_lock);Reg *r=find_reg(h);
    if(!r||r->cleanup_claimed){ReleaseSRWLockExclusive(&state_lock);return;}
    r->stopping=TRUE;SetEvent(r->stop);
    if(r->thread&&GetCurrentThreadId()==r->thread_id){ReleaseSRWLockExclusive(&state_lock);return;}
    r->cleanup_claimed=TRUE;ReleaseSRWLockExclusive(&state_lock);
    if(r->thread)WaitForSingleObject(r->thread,INFINITE);
    WaitForSingleObject(r->users_idle,INFINITE);
    AcquireSRWLockExclusive(&state_lock);Reg **link=&registrations;
    while(*link&&*link!=r)link=&(*link)->next;
    if(*link)*link=r->next;
    ReleaseSRWLockExclusive(&state_lock);
    for(Device *d=r->devices;d;){Device *next=d->next;WaitForSingleObject(d->idle,INFINITE);close_device(d);CloseHandle(d->idle);CloseHandle(d->gone);DeleteCriticalSection(&d->io);HeapFree(GetProcessHeap(),0,d);d=next;}
    if(r->thread)CloseHandle(r->thread);
    CloseHandle(r->stop);CloseHandle(r->ready);CloseHandle(r->changed);CloseHandle(r->users_idle);HeapFree(GetProcessHeap(),0,r);
}
H1_API DWORD WINAPI WDU_GetDeviceInfo(void *h,void **result) {
    diag("get-info-enter handle=%p",h);
    if(!result)return ERROR_INVALID_PARAMETER;
    *result=NULL;Device *d=acquire_device(h);if(!d)return ERROR_INVALID_HANDLE;
    diag("get-info-locked handle=%p",h);
    /* The original H1 callbacks only read the 18-byte descriptor prefix.
       Its five GetDeviceInfo callers only release the snapshot. */
    void *info=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,1024);if(info)memcpy(info,&d->descriptor,sizeof(d->descriptor));
    release_device(d);*result=info;diag("get-info-end handle=%p allocated=%u",h,info!=NULL);return info?0:ERROR_NOT_ENOUGH_MEMORY;
}
H1_API void WINAPI WDU_PutDeviceInfo(void *info){if(info)HeapFree(GetProcessHeap(),0,info);}
H1_API DWORD WINAPI WDU_SetInterface(void *h,DWORD number,DWORD alternate) {
    Device *d=acquire_device(h);if(!d)return ERROR_INVALID_HANDLE;DWORD status=0;
    if(number||alternate>255)status=ERROR_NOT_SUPPORTED;
    else if(alternate!=d->alternate) {
#if H1_ENABLE_TRANSFER
        if(!WinUsb_SetCurrentAlternateSetting(d->usb,(UCHAR)alternate))status=GetLastError();else d->alternate=(UCHAR)alternate;
#else
        status=ERROR_ACCESS_DENIED;
#endif
    }
    release_device(d);return status;
}
H1_API DWORD WINAPI WDU_ResetDevice(void *h,DWORD options) {
    Device *d=acquire_device(h);if(!d)return ERROR_INVALID_HANDLE;DWORD status;
#if H1_ENABLE_TRANSFER
#if H1_RAM_PREPARE_ONLY
    if(d->descriptor.idProduct==0xa880){release_device(d);return ERROR_ACCESS_DENIED;}
#endif
    status=options?ERROR_NOT_SUPPORTED:cycle_port(d);
    if(!status){AcquireSRWLockExclusive(&state_lock);d->reset_pending=TRUE;InterlockedExchange(&d->broken,TRUE);SetEvent(d->gone);ReleaseSRWLockExclusive(&state_lock);}
#else
    (void)options;status=ERROR_ACCESS_DENIED;
#endif
    trace("reset-via-port-cycle",0,0,options,0,status,NULL);release_device(d);return status;
}
#if H1_ENABLE_TRANSFER
static DWORD transfer(Device *d,DWORD pipe,DWORD read,void *buffer,DWORD size,DWORD *done,const BYTE *setup,DWORD timeout) {
    DWORD status=0,bytes=0;OVERLAPPED ov={0};
    ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!ov.hEvent)return GetLastError();
    if(!WinUsb_SetPipePolicy(d->usb,(UCHAR)pipe,PIPE_TRANSFER_TIMEOUT,sizeof(timeout),&timeout))status=GetLastError();
    diag("set-timeout pipe=%02lx timeout_ms=%lu status=%lu",pipe,timeout,status);
    BOOL ok=FALSE;
    if(!status) {
        diag("winusb-enter pipe=%02lx read=%lu size=%lu",pipe,read,size);
        if(!pipe){WINUSB_SETUP_PACKET packet;memcpy(&packet,setup,8);ok=WinUsb_ControlTransfer(d->usb,packet,buffer,size,NULL,&ov);}
        else if(read)ok=WinUsb_ReadPipe(d->usb,(UCHAR)pipe,buffer,size,NULL,&ov);
        else ok=WinUsb_WritePipe(d->usb,(UCHAR)pipe,buffer,size,NULL,&ov);
        diag("winusb-return pipe=%02lx ok=%u last_error=%lu",pipe,ok!=FALSE,ok?0:GetLastError());
        if(ok)status=WinUsb_GetOverlappedResult(d->usb,&ov,&bytes,FALSE)?0:GetLastError();
        else {
            status=GetLastError();
            if(status==ERROR_IO_PENDING) {
                HANDLE events[3]={ov.hEvent,d->gone,d->owner->stop};
                diag("wait-enter pipe=%02lx timeout_ms=%lu",pipe,timeout);
                DWORD wait=WaitForMultipleObjects(3,events,FALSE,timeout?timeout:INFINITE);
                diag("wait-return pipe=%02lx result=%lu",pipe,wait);
                if(wait==WAIT_OBJECT_0)status=WinUsb_GetOverlappedResult(d->usb,&ov,&bytes,FALSE)?0:GetLastError();
                else {
                    status=wait==WAIT_TIMEOUT?ERROR_SEM_TIMEOUT:(wait==WAIT_FAILED?GetLastError():ERROR_OPERATION_ABORTED);
                    CancelIoEx(d->file,&ov);
                    diag("cancel-drain-enter pipe=%02lx status=%lu",pipe,status);
                    /* Drain cancellation before stack OVERLAPPED and caller buffer can die. */
                    DWORD ignored=0;WinUsb_GetOverlappedResult(d->usb,&ov,&ignored,TRUE);bytes=ignored;
                    diag("cancel-drain-end pipe=%02lx bytes=%lu",pipe,bytes);
                }
            }
        }
    }
    if(bytes>size){bytes=0;status=ERROR_INVALID_DATA;}
    /* Do not let legacy callers treat a partial firmware write as success.
       Never retry writes here: the device may already have consumed them. */
    if(!status&&!read&&bytes!=size)status=ERROR_WRITE_FAULT;
    if(status==ERROR_DEVICE_NOT_CONNECTED||status==ERROR_DEV_NOT_EXIST||status==ERROR_NO_SUCH_DEVICE){InterlockedExchange(&d->broken,TRUE);SetEvent(d->gone);}
    if(done)*done=bytes;
    CloseHandle(ov.hEvent);return status;
}
#endif
H1_API DWORD WINAPI WDU_Transfer(void *h,DWORD pipe,DWORD read,DWORD options,void *buffer,DWORD size,DWORD *done,const BYTE *setup,DWORD timeout) {
    diag("transfer-enter handle=%p pipe=%02lx read=%lu size=%lu timeout_ms=%lu",h,pipe,read,size,timeout);
    if(done)*done=0;
    if(read>1||options||(size&&!buffer))return ERROR_INVALID_PARAMETER;
    if(pipe!=0&&pipe!=1&&pipe!=0x81)return ERROR_NOT_SUPPORTED;
    if(pipe&&read!=(pipe>>7))return ERROR_INVALID_PARAMETER;
    if(!pipe&&(!setup||read!=(setup[0]>>7)||size>4096||(DWORD)(setup[6]|setup[7]<<8)!=size))return ERROR_INVALID_PARAMETER;
    Device *d=acquire_device(h);if(!d)return ERROR_INVALID_HANDLE;
    diag("transfer-locked handle=%p pipe=%02lx",h,pipe);
#if H1_DIAGNOSTICS
    if(d->descriptor.idProduct==0xa880&&pipe==1&&!read&&size==16384&&!memcmp(buffer,"B40B",4)) {
        DWORD words[12];memcpy(words,buffer,sizeof(words));
        diag("burn-command command=%lu tag=%08lx start=%lu count=%lu",words[1],words[8],words[9],words[10]);
    }
#endif
#if H1_RAM_PREPARE_ONLY
    if(d->descriptor.idProduct==0xa880){release_device(d);return ERROR_ACCESS_DENIED;}
#endif
    DWORD status=0,actual=0;
#if H1_ENABLE_TRANSFER
    status=transfer(d,pipe,read,buffer,size,&actual,setup,timeout);
#if H1_RAM_PREPARE_ONLY
    if(!status&&d->descriptor.idProduct==0x4740) {
        if(!pipe&&setup[0]==0x40&&setup[1]==1)
            d->boot_address=((DWORD)(setup[2]|setup[3]<<8)<<16)|(DWORD)(setup[4]|setup[5]<<8);
        if(pipe==1&&!read&&size==4096&&d->boot_address==0x80003000)
            save_preparation_file(L"H1CapturedBootArgs.bin",buffer,size);
        if(!pipe&&setup[0]==0x40&&setup[1]==5)InterlockedExchange(&ram_started,1);
    }
#endif
#else
    (void)timeout;
    if(pipe||setup[0]!=0x80||setup[1]!=USB_REQUEST_GET_DESCRIPTOR)status=ERROR_ACCESS_DENIED;
    else {ULONG n=0;if(!WinUsb_GetDescriptor(d->usb,setup[3],setup[2],(USHORT)(setup[4]|setup[5]<<8),buffer,size,&n))status=GetLastError();actual=n;}
#endif
    if(done)*done=actual;
#if H1_DIAGNOSTICS
    if(!status&&d->descriptor.idProduct==0xa880&&pipe==0x81&&actual==32){
        DWORD words[8];memcpy(words,buffer,sizeof(words));
        diag("burn-reply device=%lu status=%lu packets=%lu length=%lu page=%lu pages_per_block=%lu spare=%lu blocks=%lu",words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
    }
#endif
    trace("transfer",pipe,read,size,actual,status,pipe?NULL:setup);release_device(d);return status;
}
H1_API DWORD WINAPI WDU_TransferDefaultPipe(void *h,DWORD read,DWORD options,void *buffer,DWORD size,DWORD *done,const BYTE *setup,DWORD timeout) {return WDU_Transfer(h,0,read,options,buffer,size,done,setup,timeout);}
H1_API DWORD WINAPI WDU_TransferBulk(void *h,DWORD pipe,DWORD read,DWORD options,void *buffer,DWORD size,DWORD *done,DWORD timeout) {if(!pipe){if(done)*done=0;return ERROR_INVALID_PARAMETER;}return WDU_Transfer(h,pipe,read,options,buffer,size,done,NULL,timeout);}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void *reserved){(void)instance;(void)reason;(void)reserved;return TRUE;}
