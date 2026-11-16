#ifndef H1_BRIDGE_H
#define H1_BRIDGE_H
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <usb.h>
#ifdef H1_BUILD_DLL
#define H1_API __declspec(dllexport)
#else
#define H1_API
#endif

/* This is the original x86 H1 contract, not a general WinDriver SDK replacement. */
typedef struct {
    WORD vendor,product;
    BYTE device_class,device_subclass,interface_class,interface_subclass,interface_protocol;
} H1Match;
typedef BOOL (WINAPI *H1Attach)(void *,const void *,void *);
typedef void (WINAPI *H1Detach)(void *,void *);
typedef BOOL (WINAPI *H1Power)(void *,DWORD,void *);
typedef struct {H1Attach attach;H1Detach detach;H1Power power;void *user;} H1Events;

H1_API char *WINAPI WD_DriverName(const char *);
H1_API DWORD WINAPI OsEventCreate(HANDLE *);
H1_API BOOL WINAPI OsEventClose(HANDLE);
H1_API DWORD WINAPI OsEventSignal(HANDLE);
H1_API const char *WINAPI Stat2Str(DWORD);
H1_API DWORD WINAPI WDU_Init(void **,const H1Match *,DWORD,const H1Events *,const char *,DWORD);
H1_API void WINAPI WDU_Uninit(void *);
H1_API DWORD WINAPI WDU_GetDeviceInfo(void *,void **);
H1_API void WINAPI WDU_PutDeviceInfo(void *);
H1_API DWORD WINAPI WDU_SetInterface(void *,DWORD,DWORD);
H1_API DWORD WINAPI WDU_ResetDevice(void *,DWORD);
H1_API DWORD WINAPI WDU_Transfer(void *,DWORD,DWORD,DWORD,void *,DWORD,DWORD *,const BYTE *,DWORD);
H1_API DWORD WINAPI WDU_TransferDefaultPipe(void *,DWORD,DWORD,void *,DWORD,DWORD *,const BYTE *,DWORD);
H1_API DWORD WINAPI WDU_TransferBulk(void *,DWORD,DWORD,DWORD,void *,DWORD,DWORD *,DWORD);
H1_API const char *WINAPI H1BridgeBuildInfo(void);

#if H1_BRIDGE_TEST
H1_API void *WINAPI H1TestAttach(void *,WORD,WORD);
H1_API DWORD WINAPI H1TestDetach(void *);
H1_API void WINAPI H1TestResponse(DWORD,DWORD,DWORD);
H1_API DWORD WINAPI H1TestLastTransfer(BYTE *,DWORD);
H1_API LONG WINAPI H1TestActiveTransfers(void);
#endif
#endif
