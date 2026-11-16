#ifndef H1_FAKE_PLATFORM_H
#define H1_FAKE_PLATFORM_H
#include "bridge.h"
#include <winusb.h>
typedef struct {DWORD pipe,read,size,actual,status,sum;BYTE setup[8];} H1FakeRecord;
H1_API void WINAPI H1FakeStage(DWORD);
H1_API void WINAPI H1FakeResponse(DWORD,DWORD,DWORD);
H1_API void WINAPI H1FakeResetStatus(DWORD);
H1_API void WINAPI H1FakeLayout(DWORD);
H1_API void WINAPI H1FakeBurnPacketSizes(DWORD,DWORD);
H1_API DWORD WINAPI H1FakeCycles(void);
H1_API DWORD WINAPI H1FakeActive(void);
H1_API DWORD WINAPI H1FakeOpenCount(void);
H1_API DWORD WINAPI H1FakePendingWaiters(void);
H1_API DWORD WINAPI H1FakeRecords(H1FakeRecord *,DWORD);
H1_API DWORD WINAPI H1FakeLastBytes(BYTE *,DWORD);
H1_API void WINAPI H1FakeClearTrace(void);
BOOL H1FakeLocate(const H1Match *,wchar_t *,wchar_t **);
HANDLE H1FakeOpen(const wchar_t *);
void H1FakeClose(HANDLE);
DWORD H1FakeCycle(HANDLE);
BOOL WINAPI H1FakeInitialize(HANDLE,PWINUSB_INTERFACE_HANDLE);
BOOL WINAPI H1FakeFree(WINUSB_INTERFACE_HANDLE);
BOOL WINAPI H1FakeDescriptor(WINUSB_INTERFACE_HANDLE,UCHAR,UCHAR,USHORT,PUCHAR,ULONG,PULONG);
BOOL WINAPI H1FakeInterface(WINUSB_INTERFACE_HANDLE,UCHAR,PUSB_INTERFACE_DESCRIPTOR);
BOOL WINAPI H1FakePipe(WINUSB_INTERFACE_HANDLE,UCHAR,UCHAR,PWINUSB_PIPE_INFORMATION);
BOOL WINAPI H1FakeAlternate(WINUSB_INTERFACE_HANDLE,UCHAR);
BOOL WINAPI H1FakePolicy(WINUSB_INTERFACE_HANDLE,UCHAR,ULONG,ULONG,PVOID);
BOOL WINAPI H1FakeControl(WINUSB_INTERFACE_HANDLE,WINUSB_SETUP_PACKET,PUCHAR,ULONG,PULONG,LPOVERLAPPED);
BOOL WINAPI H1FakeRead(WINUSB_INTERFACE_HANDLE,UCHAR,PUCHAR,ULONG,PULONG,LPOVERLAPPED);
BOOL WINAPI H1FakeWrite(WINUSB_INTERFACE_HANDLE,UCHAR,PUCHAR,ULONG,PULONG,LPOVERLAPPED);
BOOL WINAPI H1FakeResult(WINUSB_INTERFACE_HANDLE,LPOVERLAPPED,LPDWORD,BOOL);
BOOL WINAPI H1FakeCancel(HANDLE,LPOVERLAPPED);
#ifdef H1_FAKE_REPLACE_API
#define WinUsb_Initialize H1FakeInitialize
#define WinUsb_Free H1FakeFree
#define WinUsb_GetDescriptor H1FakeDescriptor
#define WinUsb_QueryInterfaceSettings H1FakeInterface
#define WinUsb_QueryPipe H1FakePipe
#define WinUsb_SetCurrentAlternateSetting H1FakeAlternate
#define WinUsb_SetPipePolicy H1FakePolicy
#define WinUsb_ControlTransfer H1FakeControl
#define WinUsb_ReadPipe H1FakeRead
#define WinUsb_WritePipe H1FakeWrite
#define WinUsb_GetOverlappedResult H1FakeResult
#define CancelIoEx H1FakeCancel
#endif
#endif
