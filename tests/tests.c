/* Native x86 tests against the fake WinUSB boundary ONLY. No hardware access. */
#include "bridge.h"
#include "fake_platform.h"
#include <stdio.h>
#include <wchar.h>
typedef struct {
    __typeof__(&WDU_Init) init;__typeof__(&WDU_Uninit) uninit;
    __typeof__(&WDU_GetDeviceInfo) info;__typeof__(&WDU_PutDeviceInfo) put;
    __typeof__(&WDU_TransferDefaultPipe) control;__typeof__(&WDU_TransferBulk) bulk;
    __typeof__(&WDU_Transfer) transfer;__typeof__(&WDU_ResetDevice) reset;
    __typeof__(&WDU_SetInterface) set;__typeof__(&H1BridgeBuildInfo) build;
    __typeof__(&OsEventCreate) create;__typeof__(&OsEventSignal) signal;__typeof__(&OsEventClose) close;
    __typeof__(&Stat2Str) message;__typeof__(&WD_DriverName) name;
    __typeof__(&H1FakeStage) stage;__typeof__(&H1FakeResponse) response;
    __typeof__(&H1FakeResetStatus) reset_status;__typeof__(&H1FakeLayout) layout;
    __typeof__(&H1FakeActive) active;__typeof__(&H1FakeOpenCount) open_count;
    __typeof__(&H1FakePendingWaiters) waiters;
    __typeof__(&H1FakeRecords) records;__typeof__(&H1FakeLastBytes) last;__typeof__(&H1FakeClearTrace) clear;
} API;
static API api;static FILE *report;static DWORD cases;
#define LOAD(field,symbol) do{FARPROC address=GetProcAddress(dll,#symbol);if(!address)return 2;memcpy(&api.field,&address,sizeof(address));}while(0)
#define REQUIRE(x,label) do{if(!(x)){fprintf(report,"],\"passed\":false,\"failed\":\"%s\",\"line\":%d}\n",label,__LINE__);fclose(report);fprintf(stderr,"FAIL %s line %d\n",label,__LINE__);return 1;}}while(0)
#define PASS(label) do{fprintf(report,"%s\"%s\"",cases?",":"",label);fflush(report);cases++;}while(0)
typedef struct {volatile LONG attached,detached;void *volatile device;void *reg;HANDLE change;BOOL reject,self_stop;WORD vid,pid;} Context;
static BOOL WINAPI attach_cb(void *device,const void *info,void *user) {
    Context *c=user;USB_DEVICE_DESCRIPTOR d;memcpy(&d,info,sizeof(d));c->vid=d.idVendor;c->pid=d.idProduct;
    if(!c->reject)InterlockedExchangePointer(&c->device,device);
    InterlockedIncrement(&c->attached);SetEvent(c->change);
    if(c->self_stop)api.uninit(c->reg);
    return !c->reject;
}
static void WINAPI detach_cb(void *device,void *user){(void)device;Context *c=user;InterlockedExchangePointer(&c->device,NULL);InterlockedIncrement(&c->detached);SetEvent(c->change);}
static BOOL wait_count(volatile LONG *value,LONG expected){ULONGLONG end=GetTickCount64()+2500;while(InterlockedCompareExchange(value,0,0)<expected&&GetTickCount64()<end)Sleep(1);return *value>=expected;}
static BOOL wait_active(void){for(unsigned i=0;i<1000&&!api.active();i++)Sleep(1);return api.active()!=0;}
static DWORD registration(Context *c,WORD vid,WORD pid){H1Match m={vid,pid,0,0,0,0,0};H1Events e={attach_cb,detach_cb,NULL,c};c->change=CreateEventW(NULL,TRUE,FALSE,NULL);return api.init(&c->reg,&m,1,&e,NULL,1);}
typedef struct {void *device;DWORD status,bytes,timeout,pipe;} Pending;
static DWORD WINAPI read_thread(void *arg){Pending *p=arg;BYTE buffer[64];p->status=api.bulk(p->device,p->pipe,1,0,buffer,sizeof(buffer),&p->bytes,p->timeout);return 0;}
int wmain(int argc,wchar_t **argv) {
    if(argc!=4)return 2;
    BOOL readonly=!wcscmp(argv[3],L"readonly");
    report=_wfopen(argv[2],L"wb");if(!report)return 2;
    fprintf(report,"{\"scope\":\"native x86, fake WinUSB API boundary and real registration worker/overlapped logic; no physical USB\",\"readonly\":%s,\"cases\":[",readonly?"true":"false");
    HMODULE dll=LoadLibraryW(argv[1]);REQUIRE(dll,"load_dll");
    LOAD(build,H1BridgeBuildInfo);REQUIRE(strstr(api.build(),"FAKE PLATFORM"),"refuse_physical_backend");
    LOAD(init,WDU_Init);LOAD(uninit,WDU_Uninit);LOAD(info,WDU_GetDeviceInfo);LOAD(put,WDU_PutDeviceInfo);
    LOAD(control,WDU_TransferDefaultPipe);LOAD(bulk,WDU_TransferBulk);LOAD(transfer,WDU_Transfer);
    LOAD(reset,WDU_ResetDevice);LOAD(set,WDU_SetInterface);LOAD(create,OsEventCreate);LOAD(signal,OsEventSignal);LOAD(close,OsEventClose);LOAD(message,Stat2Str);LOAD(name,WD_DriverName);
    LOAD(stage,H1FakeStage);LOAD(response,H1FakeResponse);LOAD(reset_status,H1FakeResetStatus);LOAD(layout,H1FakeLayout);
    LOAD(active,H1FakeActive);LOAD(open_count,H1FakeOpenCount);LOAD(records,H1FakeRecords);LOAD(last,H1FakeLastBytes);LOAD(clear,H1FakeClearTrace);
    LOAD(waiters,H1FakePendingWaiters);
    PASS("fake_backend_required");
    HANDLE event=NULL;REQUIRE(api.create(&event)==0&&event,"event_create");REQUIRE(WaitForSingleObject(event,0)==WAIT_TIMEOUT,"event_empty");
    REQUIRE(api.signal(event)==0&&WaitForSingleObject(event,0)==WAIT_OBJECT_0&&WaitForSingleObject(event,0)==WAIT_TIMEOUT,"event_auto_reset");REQUIRE(api.close(event),"event_close");PASS("original_event_contract");
    REQUIRE(api.create(NULL)==ERROR_INVALID_PARAMETER&&api.signal(NULL)==0x20000007,"event_errors");PASS("event_error_codes");
    REQUIRE(!strcmp(api.name("windrvr6"),"windrvr6")&&api.message(ERROR_ACCESS_DENIED)&&api.message(ERROR_SEM_TIMEOUT),"messages");PASS("driver_name_and_errors");
    H1Match wrong={0x07c4,0xa4a5,0,0,0,0,0};H1Events callbacks={attach_cb,detach_cb,NULL,NULL};void *bad=(void *)1;
    REQUIRE(api.init(&bad,&wrong,1,&callbacks,NULL,0)==ERROR_NOT_SUPPORTED&&!bad,"normal_disk");PASS("normal_mass_storage_rejected");
    H1Match boot_match={0x601a,0x4740,0,0,0,0,0};
    REQUIRE(api.init(&bad,&boot_match,2,&callbacks,NULL,0)==ERROR_NOT_SUPPORTED&&!bad,"multi_match");
    REQUIRE(api.init(&bad,&boot_match,1,&callbacks,NULL,2)==ERROR_NOT_SUPPORTED&&!bad,"init_options");PASS("unsupported_registration_options");
    api.stage(0);REQUIRE(api.init(&bad,&boot_match,1,&callbacks,NULL,0)==0&&bad,"zero_options");api.uninit(bad);PASS("zero_registration_options_supported");
    api.stage(0);Context boot={0},burn={0};
    REQUIRE(registration(&boot,0x601a,0x4740)==0&&registration(&burn,0x0888,0xa880)==0,"register_both");PASS("boot_and_burn_workers_register_without_devices");
    api.layout(1);api.stage(1);Sleep(250);REQUIRE(!boot.attached&&!api.open_count(),"wrong_endpoint_layout");PASS("missing_bulk_endpoint_rejected_and_closed");
    api.layout(3);REQUIRE(wait_count(&boot.attached,1)&&boot.device&&boot.vid==0x601a&&boot.pid==0x4740,"boot_attach");PASS("real_worker_enumerates_simulated_boot_device");
    void *device=boot.device,*cached_device=device,*snapshot=NULL;REQUIRE(api.info(device,&snapshot)==0&&snapshot,"device_info");USB_DEVICE_DESCRIPTOR desc;memcpy(&desc,snapshot,sizeof(desc));api.put(snapshot);
    REQUIRE(desc.idVendor==0x601a&&desc.idProduct==0x4740,"descriptor_prefix");PASS("legacy_device_info_layout");
    REQUIRE(api.set(device,0,0)==0&&api.set(device,1,0)==ERROR_NOT_SUPPORTED,"interface_validate");
    REQUIRE(api.set(device,0,1)==(readonly?ERROR_ACCESS_DENIED:0),"alternate_change");PASS("interface_and_alternate_mapping");
    BYTE buffer[256]={0},setup[8]={0x80,6,0,1,0,0,18,0};DWORD bytes=0;
    REQUIRE(api.control(device,1,0,buffer,18,&bytes,setup,1000)==0&&bytes==18&&!memcmp(buffer,&desc,18),"device_descriptor");PASS("standard_device_descriptor_read");
    setup[3]=2;setup[6]=32;REQUIRE(api.control(device,1,0,buffer,32,&bytes,setup,1000)==0&&bytes==32&&buffer[2]==32,"config_descriptor");PASS("complete_config_descriptor_read");
    BYTE address[8]={0x40,1,0,0x80,0,0x40,0,0};api.clear();
    REQUIRE(api.control(device,0,0,NULL,0,&bytes,address,800000)==(readonly?ERROR_ACCESS_DENIED:0),"address_request");
    H1FakeRecord record[16];
    if(!readonly){REQUIRE(api.records(record,16)==1&&!memcmp(record[0].setup,address,8)&&record[0].size==0,"wire_setup");}
    else {REQUIRE(!api.records(NULL,0),"blocked_control_no_backend_io");}
    PASS("original_set_address_setup_packet");
    BYTE cache[8]={0x40,3,0,0,0,0,0,0},start1[8]={0x40,4,0,0x80,0,0,0,0},start2[8]={0x40,5,0,0x80,0,0x40,0,0};
    REQUIRE(api.control(device,0,0,NULL,0,&bytes,cache,800000)==(readonly?ERROR_ACCESS_DENIED:0),"cache");
    REQUIRE(api.control(device,0,0,NULL,0,&bytes,start1,800000)==(readonly?ERROR_ACCESS_DENIED:0),"start1");
    REQUIRE(api.control(device,0,0,NULL,0,&bytes,start2,800000)==(readonly?ERROR_ACCESS_DENIED:0),"start2");PASS("original_cache_and_execution_requests");
    BYTE *large=HeapAlloc(GetProcessHeap(),0,0x10000),*captured=HeapAlloc(GetProcessHeap(),0,0x10000);REQUIRE(large&&captured,"buffers");
    for(unsigned i=0;i<0x10000;i++)large[i]=(BYTE)(i*7);
    api.clear();REQUIRE(api.bulk(device,1,0,0,large,0x10000,&bytes,1000)==(readonly?ERROR_ACCESS_DENIED:0),"bulk_out");
    if(!readonly)REQUIRE(bytes==0x10000&&api.last(captured,0x10000)==0x10000&&!memcmp(large,captured,0x10000),"out_data_integrity");
    else {REQUIRE(!bytes&&!api.records(NULL,0),"blocked_bulk_out_no_backend_io");}
    PASS("64k_original_download_chunk_integrity");
    REQUIRE(api.bulk(device,0x81,1,0,buffer,32,&bytes,1000)==(readonly?ERROR_ACCESS_DENIED:0),"bulk_in");
    if(!readonly){REQUIRE(bytes==32&&buffer[0]==0xa5&&buffer[31]==(31^0xa5),"in_data");}
    PASS("bulk_input_mapping");
    bytes=99;REQUIRE(api.bulk(device,1,1,0,buffer,32,&bytes,1000)==ERROR_INVALID_PARAMETER&&!bytes,"wrong_direction");
    REQUIRE(api.bulk(device,2,0,0,buffer,32,&bytes,1000)==ERROR_NOT_SUPPORTED,"wrong_pipe");PASS("direction_and_endpoint_validation");
    REQUIRE(api.control(device,1,0,buffer,17,&bytes,setup,1000)==ERROR_INVALID_PARAMETER,"control_size");
    REQUIRE(api.control(device,1,0,buffer,4097,&bytes,setup,1000)==ERROR_INVALID_PARAMETER,"control_max");
    REQUIRE(api.bulk(device,0,0,0,buffer,32,&bytes,1000)==ERROR_INVALID_PARAMETER,"bulk_ep0");PASS("control_length_and_bulk_ep0_validation");
    REQUIRE(api.bulk(device,1,0,0,NULL,1,&bytes,1000)==ERROR_INVALID_PARAMETER,"null_buffer");
    REQUIRE(api.bulk(device,1,0,1,buffer,32,&bytes,1000)==ERROR_INVALID_PARAMETER,"unsupported_flags");PASS("buffer_and_transfer_option_validation");
    if(!readonly) {
        api.response(0,7,0);memset(buffer,0xcc,sizeof(buffer));
        REQUIRE(api.bulk(device,0x81,1,0,buffer,32,&bytes,1000)==0&&bytes==7&&buffer[7]==0xcc,"short_input");PASS("short_input_preserves_actual_length");
        api.clear();REQUIRE(api.bulk(device,1,0,0,buffer,32,&bytes,1000)==ERROR_WRITE_FAULT&&bytes==7&&api.records(NULL,0)==1,"short_output");PASS("short_output_is_error_without_automatic_retry");
        api.response(ERROR_CRC,MAXDWORD,0);api.clear();REQUIRE(api.bulk(device,1,0,0,buffer,32,&bytes,1000)==ERROR_CRC&&!bytes&&api.records(NULL,0)==1,"crc_error");PASS("write_error_is_propagated_without_retry");
        api.response(0,MAXDWORD,15);REQUIRE(api.bulk(device,0x81,1,0,buffer,32,&bytes,1000)==0&&bytes==32&&!api.active(),"async_complete");PASS("overlapped_completion_and_cleanup");
        api.response(0,MAXDWORD,200);api.clear();REQUIRE(api.bulk(device,0x81,1,0,buffer,32,&bytes,10)==ERROR_SEM_TIMEOUT&&!api.active(),"timeout");PASS("timeout_cancels_and_drains_overlapped_request");
        REQUIRE(api.records(record,16)==1&&record[0].status==ERROR_OPERATION_ABORTED,"cancel_result");PASS("timeout_does_not_leave_live_buffer_references");
        api.response(0,MAXDWORD,10);REQUIRE(api.bulk(device,0x81,1,0,buffer,32,&bytes,0)==0&&bytes==32,"infinite_timeout");PASS("zero_timeout_means_infinite_wait");
        api.response(0,MAXDWORD,0);api.reset_status(ERROR_ACCESS_DENIED);REQUIRE(api.reset(device,0)==ERROR_ACCESS_DENIED,"reset_fail");
        REQUIRE(api.info(device,&snapshot)==0,"failed_reset_preserves_connection");api.put(snapshot);PASS("port_cycle_access_error_is_not_faked_success");
        REQUIRE(api.reset(device,1)==ERROR_NOT_SUPPORTED,"reset_options");PASS("unsupported_reset_options_rejected");
        api.reset_status(0);REQUIRE(api.reset(device,0)==0,"reset_begin");
        REQUIRE(api.info(device,&snapshot)==0&&snapshot,"cached_handle_waits_for_reset_rebind");api.put(snapshot);
        REQUIRE(wait_count(&boot.detached,1)&&wait_count(&boot.attached,2)&&boot.device&&boot.device!=device,"reset_reattach");
        REQUIRE(api.info(device,&snapshot)==0,"reset_alias_still_valid");api.put(snapshot);device=boot.device;PASS("port_cycle_preserves_original_cached_handle_during_reenumeration");
        REQUIRE(api.reset(device,0)==0&&api.info(cached_device,&snapshot)==0,"reset_chain");api.put(snapshot);
        REQUIRE(wait_count(&boot.attached,3),"second_reset_attached");device=boot.device;PASS("cached_handle_survives_multiple_explicit_reset_rebinds");
        api.response(0,MAXDWORD,5000);Pending p={device,0,0,0,0x81};HANDLE t=CreateThread(NULL,0,read_thread,&p,0,NULL);REQUIRE(t&&wait_active(),"pending_unplug");
        api.stage(2);REQUIRE(WaitForSingleObject(t,2500)==WAIT_OBJECT_0&&p.status==ERROR_OPERATION_ABORTED&&!api.active(),"unplug_cancellation");CloseHandle(t);PASS("unplug_cancels_inflight_io");
    } else {REQUIRE(api.reset(device,0)==ERROR_ACCESS_DENIED,"readonly_reset");PASS("readonly_reset_denied");api.stage(2);}
    REQUIRE(wait_count(&burn.attached,1)&&burn.device&&burn.vid==0x0888&&burn.pid==0xa880,"burn_attach");PASS("boot_to_burn_stage_switch");
    REQUIRE(api.info(device,&snapshot)==ERROR_INVALID_HANDLE&&!snapshot,"stale_boot");PASS("old_stage_handle_is_invalid");
    REQUIRE(api.info(cached_device,&snapshot)==ERROR_INVALID_HANDLE&&!snapshot,"reset_alias_invalid_after_stage_change");PASS("reset_alias_does_not_follow_an_unrelated_device_stage");
    api.response(0,MAXDWORD,0);REQUIRE(api.bulk(burn.device,1,0,0,large,0x4000,&bytes,1000)==(readonly?ERROR_ACCESS_DENIED:0),"burn_chunk");PASS("16k_original_burn_chunk_mapping");
    if(!readonly) {
        api.response(0,MAXDWORD,5000);Pending p={burn.device,0,0,0,0x81};HANDLE t=CreateThread(NULL,0,read_thread,&p,0,NULL);REQUIRE(t&&wait_active(),"pending_uninit");
        ULONGLONG begin=GetTickCount64();api.uninit(burn.reg);burn.reg=NULL;
        REQUIRE(WaitForSingleObject(t,1000)==WAIT_OBJECT_0&&p.status==ERROR_OPERATION_ABORTED&&!api.active()&&GetTickCount64()-begin<1500,"uninit_cancel");CloseHandle(t);PASS("uninit_cancels_infinite_wait_without_global_lock_deadlock");
    } else api.uninit(burn.reg);
    api.uninit(boot.reg);api.uninit(boot.reg);CloseHandle(boot.change);CloseHandle(burn.change);
    REQUIRE(!api.open_count(),"all_devices_closed");PASS("all_registrations_and_device_files_closed");
    api.stage(1);api.response(0,MAXDWORD,0);Context reject={0};reject.reject=TRUE;REQUIRE(registration(&reject,0x601a,0x4740)==0&&wait_count(&reject.attached,1),"reject_callback");api.uninit(reject.reg);CloseHandle(reject.change);
    REQUIRE(!reject.detached&&!api.open_count(),"rejected_device_not_detached");PASS("callback_rejection_does_not_emit_detach");
    Context self={0};self.self_stop=TRUE;REQUIRE(registration(&self,0x601a,0x4740)==0&&wait_count(&self.attached,1)&&wait_count(&self.detached,1),"callback_uninit");api.uninit(self.reg);CloseHandle(self.change);PASS("callback_self_uninit_defers_cleanup_without_self_join");
    if(!readonly) {
        Context stalled={0};REQUIRE(registration(&stalled,0x601a,0x4740)==0&&wait_count(&stalled.attached,1),"stalled_registration");
        void *old=stalled.device;REQUIRE(api.reset(old,0)==0,"stalled_reset");api.stage(0);
        Pending p={old,0,0,1000,0x81};HANDLE t=CreateThread(NULL,0,read_thread,&p,0,NULL);REQUIRE(t,"waiter_thread");
        for(unsigned i=0;i<1000&&!api.waiters();i++)Sleep(1);
        REQUIRE(api.waiters()==1,"reset_waiter_registered");api.uninit(stalled.reg);
        REQUIRE(WaitForSingleObject(t,1000)==WAIT_OBJECT_0&&p.status==ERROR_INVALID_HANDLE&&!api.waiters()&&!api.open_count(),"reset_waiter_cancelled");
        CloseHandle(t);CloseHandle(stalled.change);PASS("uninit_interrupts_reset_rebind_wait_and_retains_registration_lifetime");
    }
    api.stage(0);DWORD before=0,after=0;REQUIRE(GetProcessHandleCount(GetCurrentProcess(),&before),"handles_before");
    for(unsigned i=0;i<20;i++){Context c={0};REQUIRE(registration(&c,0x601a,0x4740)==0,"repeat_register");api.uninit(c.reg);CloseHandle(c.change);}
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(),&after)&&after<=before&&!api.open_count(),"handles_after");PASS("repeated_registration_has_no_handle_leak");
    HeapFree(GetProcessHeap(),0,large);HeapFree(GetProcessHeap(),0,captured);FreeLibrary(dll);
    fprintf(report,"],\"passed\":true,\"case_count\":%lu,\"physical_usb_access\":false}\n",cases);fclose(report);
    printf("%s: %lu offline API/worker/overlapped cases passed.\n",readonly?"Read-only":"Transfer",cases);return 0;
}
