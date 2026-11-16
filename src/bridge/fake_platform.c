/* Simulated WinUSB API boundary. This object cannot enumerate/open physical USB. */
#define H1_BUILD_DLL
#include "fake_platform.h"
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>
typedef struct {
    DWORD generation,stage;
    HANDLE cancel,thread;
    OVERLAPPED *ov;
    BYTE *buffer;
    DWORD size,read,error,length,delay,actual,status;
    H1FakeRecord record;
    UCHAR alternate;
} Fake;
static SRWLOCK lock=SRWLOCK_INIT;
static DWORD stage,generation,response_error,response_length=MAXDWORD,response_delay,reset_error,layout=3;
static LONG active,open_count;
static DWORD burn_packet_size=512,after_cycle_packet_size=512,cycle_count;
static H1FakeRecord records[8192];static DWORD record_count;
static BYTE last_bytes[0x10000];static DWORD last_length;
static BOOL failure(DWORD error){SetLastError(error);return FALSE;}
H1_API void WINAPI H1FakeStage(DWORD value){AcquireSRWLockExclusive(&lock);stage=value;generation++;ReleaseSRWLockExclusive(&lock);}
H1_API void WINAPI H1FakeResponse(DWORD error,DWORD length,DWORD delay){AcquireSRWLockExclusive(&lock);response_error=error;response_length=length;response_delay=delay;ReleaseSRWLockExclusive(&lock);}
H1_API void WINAPI H1FakeResetStatus(DWORD error){AcquireSRWLockExclusive(&lock);reset_error=error;ReleaseSRWLockExclusive(&lock);}
H1_API void WINAPI H1FakeLayout(DWORD value){AcquireSRWLockExclusive(&lock);layout=value;ReleaseSRWLockExclusive(&lock);}
H1_API void WINAPI H1FakeBurnPacketSizes(DWORD before,DWORD after){AcquireSRWLockExclusive(&lock);burn_packet_size=before;after_cycle_packet_size=after;cycle_count=0;ReleaseSRWLockExclusive(&lock);}
H1_API DWORD WINAPI H1FakeCycles(void){AcquireSRWLockShared(&lock);DWORD n=cycle_count;ReleaseSRWLockShared(&lock);return n;}
H1_API DWORD WINAPI H1FakeActive(void){return (DWORD)InterlockedCompareExchange(&active,0,0);}
H1_API DWORD WINAPI H1FakeOpenCount(void){return (DWORD)InterlockedCompareExchange(&open_count,0,0);}
H1_API DWORD WINAPI H1FakeRecords(H1FakeRecord *out,DWORD capacity){AcquireSRWLockShared(&lock);DWORD n=record_count;if(out&&capacity>=n)memcpy(out,records,n*sizeof(*out));ReleaseSRWLockShared(&lock);return n;}
H1_API DWORD WINAPI H1FakeLastBytes(BYTE *out,DWORD capacity){AcquireSRWLockShared(&lock);DWORD n=last_length;if(out&&capacity>=n)memcpy(out,last_bytes,n);ReleaseSRWLockShared(&lock);return n;}
H1_API void WINAPI H1FakeClearTrace(void){AcquireSRWLockExclusive(&lock);record_count=last_length=0;ReleaseSRWLockExclusive(&lock);}
BOOL H1FakeLocate(const H1Match *match,wchar_t *id,wchar_t **path) {
    AcquireSRWLockShared(&lock);DWORD current=stage,gen=generation;ReleaseSRWLockShared(&lock);
    WORD vid=current==1?0x601a:0x0888,pid=current==1?0x4740:0xa880;
    if(!current||match->vendor!=vid||match->product!=pid){*path=NULL;return FALSE;}
    /* PnP instance persists across a port cycle; the open handle generation does not. */
    swprintf(id,2048,L"USB\\VID_%04X&PID_%04X\\SIMULATED",vid,pid);
    *path=HeapAlloc(GetProcessHeap(),0,128*sizeof(wchar_t));if(!*path)return FALSE;
    swprintf(*path,128,L"fake://%lu/%lu",current,gen);return TRUE;
}
HANDLE H1FakeOpen(const wchar_t *path) {
    DWORD requested=0,gen=0;
    if(swscanf(path,L"fake://%lu/%lu",&requested,&gen)!=2)return INVALID_HANDLE_VALUE;
    AcquireSRWLockShared(&lock);BOOL valid=requested==stage&&gen==generation;ReleaseSRWLockShared(&lock);
    if(!valid){SetLastError(ERROR_DEVICE_NOT_CONNECTED);return INVALID_HANDLE_VALUE;}
    Fake *f=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*f));if(!f)return INVALID_HANDLE_VALUE;
    f->stage=requested;f->generation=gen;f->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!f->cancel){HeapFree(GetProcessHeap(),0,f);return INVALID_HANDLE_VALUE;}
    InterlockedIncrement(&open_count);return (HANDLE)f;
}
void H1FakeClose(HANDLE file){Fake *f=(Fake *)file;if(f->thread){SetEvent(f->cancel);WaitForSingleObject(f->thread,INFINITE);CloseHandle(f->thread);}CloseHandle(f->cancel);HeapFree(GetProcessHeap(),0,f);InterlockedDecrement(&open_count);}
DWORD H1FakeCycle(HANDLE file){(void)file;AcquireSRWLockExclusive(&lock);DWORD error=reset_error;cycle_count++;if(!error){generation++;if(stage==2)burn_packet_size=after_cycle_packet_size;}ReleaseSRWLockExclusive(&lock);return error;}
BOOL WINAPI H1FakeInitialize(HANDLE file,PWINUSB_INTERFACE_HANDLE result){*result=file;return TRUE;}
BOOL WINAPI H1FakeFree(WINUSB_INTERFACE_HANDLE usb){(void)usb;return TRUE;}
static BOOL connected(Fake *f){AcquireSRWLockShared(&lock);BOOL ok=f->stage==stage&&f->generation==generation;ReleaseSRWLockShared(&lock);return ok;}
BOOL WINAPI H1FakeDescriptor(WINUSB_INTERFACE_HANDLE usb,UCHAR type,UCHAR index,USHORT lang,PUCHAR buffer,ULONG size,PULONG got) {
    (void)index;(void)lang;Fake *f=usb;*got=0;if(!connected(f))return failure(ERROR_DEVICE_NOT_CONNECTED);
    BYTE device[18]={18,1,0,2,0,0,0,64,0x1a,0x60,0x40,0x47,0,1,1,2,0,1};
    BYTE config[32]={9,2,32,0,1,1,0,0xc0,1,9,4,0,0,2,0xff,0,0x50,0,7,5,1,2,0,2,0,7,5,0x81,2,0,2,0};
    if(f->stage==2){device[8]=0x88;device[9]=8;device[10]=0x80;device[11]=0xa8;}
    const BYTE *src=type==1?device:config;DWORD length=type==1?18:32;
    if(type!=1&&type!=2)return failure(ERROR_NOT_SUPPORTED);
    if(length>size)length=size;
    if(length)memcpy(buffer,src,length);
    *got=length;return TRUE;
}
BOOL WINAPI H1FakeInterface(WINUSB_INTERFACE_HANDLE usb,UCHAR alt,PUSB_INTERFACE_DESCRIPTOR out){(void)usb;if(alt)return failure(ERROR_NO_MORE_ITEMS);ZeroMemory(out,sizeof(*out));out->bLength=9;out->bDescriptorType=4;out->bNumEndpoints=2;return TRUE;}
BOOL WINAPI H1FakePipe(WINUSB_INTERFACE_HANDLE usb,UCHAR alt,UCHAR index,PWINUSB_PIPE_INFORMATION out){Fake *f=usb;(void)alt;if(index>1)return failure(ERROR_NO_MORE_ITEMS);AcquireSRWLockShared(&lock);DWORD bits=layout,packet=f->stage==2?burn_packet_size:512;ReleaseSRWLockShared(&lock);ZeroMemory(out,sizeof(*out));out->PipeType=UsbdPipeTypeBulk;out->PipeId=index?((bits&2)?0x81:0x82):((bits&1)?1:2);out->MaximumPacketSize=(USHORT)packet;return TRUE;}
BOOL WINAPI H1FakeAlternate(WINUSB_INTERFACE_HANDLE usb,UCHAR alt){Fake *f=usb;f->alternate=alt;return TRUE;}
BOOL WINAPI H1FakePolicy(WINUSB_INTERFACE_HANDLE usb,UCHAR pipe,ULONG policy,ULONG size,PVOID value){(void)usb;(void)pipe;(void)value;return policy==PIPE_TRANSFER_TIMEOUT&&size==sizeof(DWORD)?TRUE:failure(ERROR_INVALID_PARAMETER);}
static void complete(Fake *f,BOOL cancelled) {
    DWORD count=f->length==MAXDWORD?f->size:f->length;
    f->status=cancelled?ERROR_OPERATION_ABORTED:f->error;f->actual=0;
    if(!f->status&&count>f->size)f->status=ERROR_INVALID_DATA;
    if(!f->status) {
        if(f->read) {
            if(!f->record.pipe&&f->record.setup[0]==0x80&&f->record.setup[1]==6) {
                ULONG got=0;if(!H1FakeDescriptor(f,f->record.setup[3],f->record.setup[2],0,f->buffer,count,&got))f->status=GetLastError();count=got;
            } else for(DWORD i=0;i<count;i++)f->buffer[i]=(BYTE)(i^0xa5);
        }
        if(!f->status)f->actual=count;
    }
    f->record.actual=f->actual;f->record.status=f->status;f->record.sum=0;
    for(DWORD i=0;i<f->actual;i++)f->record.sum+=f->buffer[i];
    AcquireSRWLockExclusive(&lock);
    if(record_count<8192)records[record_count++]=f->record;
    last_length=f->actual>sizeof(last_bytes)?sizeof(last_bytes):f->actual;
    if(last_length)memcpy(last_bytes,f->buffer,last_length);
    ReleaseSRWLockExclusive(&lock);
}
static DWORD WINAPI delayed(void *arg){Fake *f=arg;DWORD result=WaitForSingleObject(f->cancel,f->delay);complete(f,result==WAIT_OBJECT_0);InterlockedDecrement(&active);SetEvent(f->ov->hEvent);return 0;}
static BOOL begin(Fake *f,DWORD pipe,DWORD read,PUCHAR buffer,ULONG size,PULONG got,LPOVERLAPPED ov,const BYTE *setup) {
    if(got)*got=0;
    if(!connected(f))return failure(ERROR_DEVICE_NOT_CONNECTED);
    AcquireSRWLockShared(&lock);f->error=response_error;f->length=response_length;f->delay=response_delay;ReleaseSRWLockShared(&lock);
    f->buffer=buffer;f->size=size;f->read=read;f->ov=ov;ZeroMemory(&f->record,sizeof(f->record));f->record.pipe=pipe;f->record.read=read;f->record.size=size;
    if(setup)memcpy(f->record.setup,setup,8);
    ResetEvent(f->cancel);
    if(!f->delay){complete(f,FALSE);if(got)*got=f->actual;return f->status?failure(f->status):TRUE;}
    InterlockedIncrement(&active);f->thread=CreateThread(NULL,0,delayed,f,0,NULL);
    if(!f->thread){InterlockedDecrement(&active);return FALSE;}
    return failure(ERROR_IO_PENDING);
}
BOOL WINAPI H1FakeControl(WINUSB_INTERFACE_HANDLE usb,WINUSB_SETUP_PACKET setup,PUCHAR buffer,ULONG size,PULONG got,LPOVERLAPPED ov){return begin(usb,0,setup.RequestType>>7,buffer,size,got,ov,(BYTE *)&setup);}
BOOL WINAPI H1FakeRead(WINUSB_INTERFACE_HANDLE usb,UCHAR pipe,PUCHAR buffer,ULONG size,PULONG got,LPOVERLAPPED ov){return begin(usb,pipe,1,buffer,size,got,ov,NULL);}
BOOL WINAPI H1FakeWrite(WINUSB_INTERFACE_HANDLE usb,UCHAR pipe,PUCHAR buffer,ULONG size,PULONG got,LPOVERLAPPED ov){return begin(usb,pipe,0,buffer,size,got,ov,NULL);}
BOOL WINAPI H1FakeResult(WINUSB_INTERFACE_HANDLE usb,LPOVERLAPPED ov,LPDWORD got,BOOL wait) {
    Fake *f=usb;*got=0;if(f->ov!=ov)return failure(ERROR_INVALID_PARAMETER);
    if(!f->thread){*got=f->actual;return f->status?failure(f->status):TRUE;}
    if(WaitForSingleObject(ov->hEvent,wait?INFINITE:0)!=WAIT_OBJECT_0)return failure(ERROR_IO_INCOMPLETE);
    WaitForSingleObject(f->thread,INFINITE);CloseHandle(f->thread);f->thread=NULL;*got=f->actual;
    return f->status?failure(f->status):TRUE;
}
BOOL WINAPI H1FakeCancel(HANDLE file,LPOVERLAPPED ov){(void)ov;Fake *f=(Fake *)file;return SetEvent(f->cancel);}
