/* H1 Win10 x64 complete installer.
   Recovery starts only with --launch-recovery. --dry-run never starts it.
   --files-only performs no driver installation. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <tlhelp32.h>
#include "manifest_hash.h"
#ifndef H1_PACKAGE_VERSION
#define H1_PACKAGE_VERSION "0.2.0-D1"
#endif
#define H1_WIDE_INNER(x) L##x
#define H1_WIDE(x) H1_WIDE_INNER(x)

#define MAX_FILES 100
#define PATH_CAP 8192
typedef struct {char sha[65];unsigned long long size;wchar_t source[1024],dest[1024];} Entry;
static wchar_t package_root[PATH_CAP],target_root[PATH_CAP],backup_root[PATH_CAP];
static Entry entries[MAX_FILES];static unsigned count;
static BOOL dry_run,files_only,launch_recovery;
static FILE *result_log;
static void status(const wchar_t *message){wprintf(L"%ls\n",message);fflush(stdout);}
static BOOL join(wchar_t *out,size_t cap,const wchar_t *dir,const wchar_t *name){return _snwprintf(out,cap,L"%ls\\%ls",dir,name)>0&&wcslen(out)<cap-1;}
static BOOL exists(const wchar_t *path){return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;}
static BOOL absolute(const wchar_t *source,wchar_t *out){DWORD n=GetFullPathNameW(source,PATH_CAP,out,NULL);if(!n||n>=PATH_CAP)return FALSE;while(n>3&&(out[n-1]==L'\\'||out[n-1]==L'/'))out[--n]=0;return n>3;}
static BOOL child_of(const wchar_t *path,const wchar_t *root){size_t n=wcslen(root);return !_wcsnicmp(path,root,n)&&(path[n]==0||path[n]==L'\\'||path[n]==L'/');}
static BOOL relative_safe(const wchar_t *path){return *path&&*path!=L'\\'&&*path!=L'/'&&!wcschr(path,L':')&&!wcsstr(path,L"..")&&!wcschr(path,L'"');}
static BOOL make_dir(const wchar_t *path){int r=SHCreateDirectoryExW(NULL,path,NULL);return r==ERROR_SUCCESS||r==ERROR_ALREADY_EXISTS||r==ERROR_FILE_EXISTS;}
static BOOL parent_dir(const wchar_t *path){wchar_t copy[PATH_CAP];wcscpy(copy,path);wchar_t *last=wcsrchr(copy,L'\\');if(!last)return FALSE;*last=0;return make_dir(copy);}
static BOOL file_hash(const wchar_t *path,char *out,unsigned long long *length) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);if(f==INVALID_HANDLE_VALUE)return FALSE;
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;BOOL ok=FALSE;DWORD bytes=0;BYTE digest[32],*buffer=NULL;unsigned long long total=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0)goto done;
    buffer=malloc(1024*1024);if(!buffer)goto done;
    for(;;){if(!ReadFile(f,buffer,1024*1024,&bytes,NULL))goto done;if(!bytes)break;total+=bytes;if(BCryptHashData(hash,buffer,bytes,0)<0)goto done;}
    if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
    for(unsigned i=0;i<32;i++)sprintf(out+i*2,"%02x",digest[i]);
    if(length)*length=total;
    ok=TRUE;
done:
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    free(buffer);CloseHandle(f);return ok;
}
static BOOL admin(void){SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID sid=NULL;BOOL yes=FALSE;if(AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(NULL,sid,&yes);FreeSid(sid);}return yes;}
static DWORD process(const wchar_t *exe,wchar_t *command,const wchar_t *cwd) {
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,cwd,&si,&pi))return GetLastError();
    CloseHandle(pi.hThread);WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return code;
}
static int fail(const wchar_t *message,DWORD code){wprintf(L"ERROR: %ls (code %lu). Installation is NOT marked complete.\n",message,code);if(result_log){fprintf(result_log,"recovery_program_started=false\nresult=failed\nerror=%lu\n",code);fclose(result_log);}return 1;}
static int recovery_running(void) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return -1;
    PROCESSENTRY32W entry={0};entry.dwSize=sizeof(entry);int found=0;
    if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);return -1;}
    do{if(!_wcsicmp(entry.szExeFile,L"super_recover.exe")){found=1;break;}}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
static int finish_install(const char *result) {
    if(launch_recovery) {
        wchar_t exe[PATH_CAP],command[PATH_CAP+4];
        if(!join(exe,PATH_CAP,target_root,L"super_recover.exe"))return fail(L"Recovery path is too long",ERROR_FILENAME_EXCED_RANGE);
        _snwprintf(command,sizeof(command)/sizeof(*command),L"\"%ls\"",exe);
        STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
        if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,target_root,&si,&pi))return fail(L"Could not start recovery",GetLastError());
        fprintf(result_log,"recovery_program_started=true\nrecovery_process_id=%lu\n",pi.dwProcessId);
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        status(L"Recovery program started. Continue in its window. This program can write the H1 firmware.");
    } else fprintf(result_log,"recovery_program_started=false\n");
    fprintf(result_log,"result=%s\n",result);fclose(result_log);result_log=NULL;return 0;
}
static BOOL load_manifest(void) {
    wchar_t path[PATH_CAP];join(path,PATH_CAP,package_root,L"payload-manifest.tsv");char hash[65];
    if(!file_hash(path,hash,NULL)||strcmp(hash,H1_MANIFEST_SHA256)){status(L"Manifest checksum mismatch.");return FALSE;}
    FILE *f=_wfopen(path,L"rb");if(!f)return FALSE;char line[4096];
    while(fgets(line,sizeof(line),f)) {
        if(count==MAX_FILES){fclose(f);return FALSE;}
        char source[1024],dest[1024];Entry *e=&entries[count];
        if(sscanf(line,"%64s\t%llu\t%1023s\t%1023s",e->sha,&e->size,source,dest)!=4||strlen(e->sha)!=64){fclose(f);return FALSE;}
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source,-1,e->source,1024)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,dest,-1,e->dest,1024)||!relative_safe(e->source)||!relative_safe(e->dest)){fclose(f);return FALSE;}
        count++;
    }
    fclose(f);return count>0;
}
static BOOL verify_payload(void) {
    for(unsigned i=0;i<count;i++) {
        wchar_t path[PATH_CAP];join(path,PATH_CAP,package_root,entries[i].source);char hash[65];unsigned long long size=0;
        wprintf(L"Verify [%u/%u] %ls\n",i+1,count,entries[i].source);fflush(stdout);
        if(!file_hash(path,hash,&size)||size!=entries[i].size||strcmp(hash,entries[i].sha))return FALSE;
    }
    return TRUE;
}
static BOOL copy_payload(void) {
    SYSTEMTIME now;GetLocalTime(&now);_snwprintf(backup_root,PATH_CAP,L"%ls\\backups\\%04u%02u%02u-%02u%02u%02u-%lu",target_root,now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    for(unsigned i=0;i<count;i++) {
        wchar_t source[PATH_CAP],dest[PATH_CAP],backup[PATH_CAP];join(source,PATH_CAP,package_root,entries[i].source);join(dest,PATH_CAP,target_root,entries[i].dest);
        char hash[65];unsigned long long length=0;
        if(exists(dest)) {
            if(file_hash(dest,hash,&length)&&length==entries[i].size&&!strcmp(hash,entries[i].sha)){wprintf(L"Keep unchanged: %ls\n",entries[i].dest);continue;}
            join(backup,PATH_CAP,backup_root,entries[i].dest);
            if(!parent_dir(backup)||!CopyFileW(dest,backup,TRUE))return FALSE;
            wprintf(L"Backed up existing: %ls\n",entries[i].dest);
        }
        if(!parent_dir(dest)||!CopyFileW(source,dest,FALSE))return FALSE;
        /* Verify copied content, not only successful file API return values. */
        if(!file_hash(dest,hash,&length)||length!=entries[i].size||strcmp(hash,entries[i].sha))return FALSE;
        wprintf(L"Installed: %ls\n",entries[i].dest);fflush(stdout);
    }
    return TRUE;
}
static int binding(WORD vid,WORD pid) {
    HDEVINFO list=SetupDiGetClassDevsW(NULL,L"USB",NULL,DIGCF_ALLCLASSES|DIGCF_PRESENT);if(list==INVALID_HANDLE_VALUE)return -1;
    wchar_t wanted[64];swprintf(wanted,64,L"USB\\VID_%04X&PID_%04X",vid,pid);SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);int result=0;
    for(DWORD i=0;SetupDiEnumDeviceInfo(list,i,&dev);i++) {
        wchar_t ids[4096]={0};DWORD type=0;if(!SetupDiGetDeviceRegistryPropertyW(list,&dev,SPDRP_HARDWAREID,&type,(BYTE *)ids,sizeof(ids)-4,NULL))continue;
        BOOL matched=FALSE;for(wchar_t *p=ids;*p;p+=wcslen(p)+1){size_t n=wcslen(wanted);if(!_wcsnicmp(p,wanted,n)&&(!p[n]||p[n]==L'&'))matched=TRUE;}
        if(!matched)continue;
        wchar_t service[128]={0};ULONG status=0,problem=0;SetupDiGetDeviceRegistryPropertyW(list,&dev,SPDRP_SERVICE,&type,(BYTE *)service,sizeof(service)-4,NULL);
        if(CM_Get_DevNode_Status(&status,&problem,dev.DevInst,0)!=CR_SUCCESS||problem||_wcsicmp(service,L"WinUSB")){result=2;break;}
        result=1;
    }
    SetupDiDestroyDeviceInfoList(list);return result;
}
int wmain(int argc,wchar_t **argv) {
    _setmode(_fileno(stdout),_O_U8TEXT);_setmode(_fileno(stderr),_O_U8TEXT);
    wchar_t exe[PATH_CAP],target_arg[PATH_CAP]=L"";DWORD n=GetModuleFileNameW(NULL,exe,PATH_CAP);if(!n||n>=PATH_CAP)return 1;
    wcscpy(package_root,exe);wchar_t *last=wcsrchr(package_root,L'\\');if(!last)return 1;*last=0;last=wcsrchr(package_root,L'\\');if(!last)return 1;*last=0;
    for(int i=1;i<argc;i++) {
        if(!wcscmp(argv[i],L"--dry-run"))dry_run=TRUE;
        else if(!wcscmp(argv[i],L"--files-only"))files_only=TRUE;
        else if(!wcscmp(argv[i],L"--launch-recovery"))launch_recovery=TRUE;
        else if(!wcscmp(argv[i],L"--target")&&i+1<argc){if(wcslen(argv[i+1])>=PATH_CAP)return 2;wcscpy(target_arg,argv[++i]);}
        else if(!wcscmp(argv[i],L"--help")){status(L"H1Setup [--dry-run | --files-only] [--target directory] [--launch-recovery]\nDefault target: H1RecoveryV141 beside the startup script. Starts recovery only when explicitly requested with --launch-recovery. --dry-run never starts recovery.");return 0;}
        else return 2;
    }
    SYSTEM_INFO sys;GetNativeSystemInfo(&sys);if(sys.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64)return fail(L"This package requires x64 Windows",ERROR_NOT_SUPPORTED);
    /* Anchor to the script/package location, not the elevated process CWD. */
    wchar_t local_target[PATH_CAP];
    if(!join(local_target,PATH_CAP,package_root,L"H1RecoveryV141"))return fail(L"Installation path is too long",ERROR_FILENAME_EXCED_RANGE);
    if(!*target_arg)wcscpy(target_arg,local_target);
    if(!absolute(target_arg,target_root))return fail(L"Invalid installation directory",ERROR_INVALID_NAME);
    if(wcslen(target_root)>PATH_CAP-2048||wcslen(package_root)>PATH_CAP-2048)return fail(L"Installation path is too long",ERROR_FILENAME_EXCED_RANGE);
    wchar_t windows[PATH_CAP];GetWindowsDirectoryW(windows,PATH_CAP);
    /* Permit the dedicated local runtime folder; keep source/installer folders protected. */
    BOOL local_runtime=!_wcsicmp(target_root,local_target);
    if((child_of(target_root,package_root)&&!local_runtime)||child_of(target_root,windows)||wcschr(target_root,L'"'))return fail(L"Unsafe installation directory",ERROR_INVALID_NAME);
    if(!dry_run&&launch_recovery) {
        int running=recovery_running();
        if(running<0)return fail(L"Could not check whether recovery is already running",ERROR_ACCESS_DENIED);
        if(running)return fail(L"Recovery is already running; no files were changed",ERROR_BUSY);
    }
    if(!dry_run&&!files_only&&!admin()) {
        wchar_t args[PATH_CAP+64];_snwprintf(args,sizeof(args)/sizeof(*args),L"--target \"%ls\"%ls",target_root,launch_recovery?L" --launch-recovery":L"");
        SHELLEXECUTEINFOW shell={0};shell.cbSize=sizeof(shell);shell.fMask=SEE_MASK_NOCLOSEPROCESS;shell.lpVerb=L"runas";shell.lpFile=exe;shell.lpParameters=args;shell.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&shell))return fail(L"Administrator approval was cancelled or unavailable",GetLastError());
        WaitForSingleObject(shell.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(shell.hProcess,&code);CloseHandle(shell.hProcess);return (int)code;
    }
    status(L"H1 V1.41 integrated recovery / USB bridge " H1_WIDE(H1_PACKAGE_VERSION));
    status(launch_recovery?L"Recovery will start after successful preparation. It can write device firmware.":L"Preparation only: recovery will not be started.");
    wprintf(L"Target: %ls\nMode: %ls\n",target_root,dry_run?L"VERIFY/PLAN ONLY":(files_only?L"FILES ONLY / NO DRIVER CHANGES":L"COMPLETE INSTALL"));
    if(!load_manifest()||!verify_payload())return fail(L"Payload verification failed",ERROR_CRC);
    if(dry_run){status(L"PLAN: copy complete official files, preserve replaced files, load-check bridge, preinstall WinUSB 601A:4740 and 0888:A880.");status(launch_recovery?L"PLAN: start recovery after successful preparation.":L"PLAN: do not start recovery.");status(L"PLAN COMPLETE: no files installed, no driver operations, no recovery execution.");return 0;}
    if(!make_dir(target_root)||!copy_payload())return fail(L"Could not install verified files",GetLastError());
    wchar_t log_path[PATH_CAP];join(log_path,PATH_CAP,target_root,L"installation-result.txt");result_log=_wfopen(log_path,L"wb");if(!result_log)return fail(L"Could not write installation report",GetLastError());
    fprintf(result_log,"version=%s\nfiles_verified=true\n",H1_PACKAGE_VERSION);
    wchar_t inspector[PATH_CAP],dll[PATH_CAP],command[PATH_CAP*2];join(inspector,PATH_CAP,target_root,L"tools\\inspect.exe");join(dll,PATH_CAP,target_root,L"WDAPI901.DLL");
    _snwprintf(command,sizeof(command)/sizeof(*command),L"\"%ls\" \"%ls\"",inspector,dll);
    DWORD code=process(inspector,command,target_root);if(code)return fail(L"Bridge DLL loading/exports check failed",code);
    if(files_only){fprintf(result_log,"drivers=skipped\n");status(L"FILES READY. Driver installation intentionally skipped.");return finish_install("files_only_ready");}
    wchar_t driver[PATH_CAP];join(driver,PATH_CAP,target_root,L"tools\\driver\\wdi-simple.exe");
    const WORD vids[2]={0x601a,0x0888},pids[2]={0x4740,0xa880};const wchar_t *names[2]={L"BBK H1 USB Boot",L"BBK H1 USB BURN"};
    for(unsigned i=0;i<2;i++) {
        wchar_t dest[PATH_CAP];join(dest,PATH_CAP,target_root,i?L"drivers\\burn":L"drivers\\boot");
        _snwprintf(command,sizeof(command)/sizeof(*command),L"\"%ls\" --name \"%ls\" --manufacturer \"BBK H1\" --vid 0x%04x --pid 0x%04x --type 0 --dest \"%ls\" --inf h1_%ls.inf --timeout 120000",driver,names[i],vids[i],pids[i],dest,i?L"burn":L"boot");
        wprintf(L"Installing/preinstalling WinUSB for %04x:%04x ...\n",vids[i],pids[i]);fflush(stdout);
        code=process(driver,command,target_root);if(code)return fail(L"WinUSB package installation failed",code);
        int state=binding(vids[i],pids[i]);if(state<0||state==2)return fail(L"Connected device is not ready with WinUSB",ERROR_NOT_READY);
        fprintf(result_log,"%04x:%04x=%s\n",vids[i],pids[i],state==1?"present_bound_winusb":"package_preinstalled_device_absent");
        wprintf(L"%04x:%04x: %ls\n",vids[i],pids[i],state==1?L"CONNECTED / WINUSB READY":L"PREINSTALLED / AUTO-MATCH WHEN DEVICE APPEARS");
    }
    fprintf(result_log,"drivers=preinstalled_both_ids\n");
    wprintf(L"\nINSTALLATION COMPLETE: %ls\n",target_root);return finish_install("installed");
}
