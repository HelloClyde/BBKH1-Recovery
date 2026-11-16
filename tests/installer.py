"""Exercise startup using an innocuous offline EXE; never start official recovery."""
from pathlib import Path
import ctypes,hashlib,json,shutil,subprocess,time
from common import ROOT,OUT,sha,compile_setup,compilers
import tempfile
SRC=ROOT/'src/installer';BUNDLE=OUT/'bundle';CC=compilers(CONFIG_ARGS)[1]

built=dict(setup_sha256=sha(BUNDLE/'tools/H1Setup.exe'),bridge_sha256=sha(OUT/'bridge/WDAPI901.DLL'))
assert sha(BUNDLE/'tools/H1Setup.exe')==built['setup_sha256']
(OUT/'tests').mkdir(exist_ok=True)
TEST=Path(tempfile.mkdtemp(prefix='installer-',dir=OUT/'tests'))
stub_source=TEST/'offline_stub.c'
stub_source.write_text(r'''
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
int wmain(int argc,wchar_t **argv) {
    wchar_t module[4096],cwd[4096];GetModuleFileNameW(NULL,module,4096);GetCurrentDirectoryW(4096,cwd);
    wchar_t *name=wcsrchr(module,92);if(!name)return 2;name++;
    if(!_wcsicmp(name,L"inspect.exe"))return GetFileAttributesW(L"inspector-fail.flag")!=INVALID_FILE_ATTRIBUTES?23:0;
    FILE *f=NULL;
    if(!_wcsicmp(name,L"H1Setup.exe")){wcscpy(name,L"stub-entry.txt");f=_wfopen(module,L"wb");}
    else f=_wfopen(L"stub-recovery.txt",L"wb");
    if(!f)return 3;
    char text[16384];int n=WideCharToMultiByte(CP_UTF8,0,GetCommandLineW(),-1,text,sizeof(text),NULL,NULL);
    if(n>0)fprintf(f,"%s\n",text);
    n=WideCharToMultiByte(CP_UTF8,0,cwd,-1,text,sizeof(text),NULL,NULL);
    if(n>0)fprintf(f,"cwd=%s\nOFFLINE STUB ONLY: no USB APIs\n",text);
    fclose(f);
    if(argc>1&&!wcscmp(argv[1],L"--hold"))Sleep(3000);
    return 0;
}
''',encoding='utf-8')
stub=TEST/'offline_stub.exe'
subprocess.run([str(CC),'-std=c11','-O2','-Wall','-Wextra','-Werror','-municode','-Wl,--no-insert-timestamp',str(stub_source),'-o',str(stub)],check=True)
import pefile
pe=pefile.PE(str(stub));libraries={d.dll.decode().lower() for d in pe.DIRECTORY_ENTRY_IMPORT}
imports={i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
assert not libraries&{'winusb.dll','setupapi.dll','cfgmgr32.dll'}
assert not imports&{'DeviceIoControl','ShellExecuteExW','CreateProcessW'}

# A separate compiled manifest accepts only this fake recovery, fake inspector and tiny data.
fixture=TEST/'离线启动测试 with spaces';fixture.mkdir()
(fixture/'payload').mkdir();(fixture/'tools').mkdir()
shutil.copyfile(stub,fixture/'payload/super_recover.exe')
shutil.copyfile(stub,fixture/'tools/inspect.exe')
(fixture/'payload/data0.dat').write_bytes(b'Offline fixture, not firmware')
rows=[]
for source,dest in [('payload/super_recover.exe','super_recover.exe'),('tools/inspect.exe','tools/inspect.exe'),('payload/data0.dat','data0.dat')]:
    p=fixture/source;source=source.replace('/','\\');dest=dest.replace('/','\\')
    rows.append(f'{sha(p)}\t{p.stat().st_size}\t{source}\t{dest}')
manifest=('\n'.join(rows)+'\n').encode()
(fixture/'payload-manifest.tsv').write_bytes(manifest)
(TEST/'manifest_hash.h').write_text('#define H1_MANIFEST_SHA256 "'+hashlib.sha256(manifest).hexdigest()+'"\n')
exe=fixture/'tools/H1Setup.exe';compile_setup(CC,TEST,exe)
records=[]
def run(name,args,expected=0,timeout=60):
    r=subprocess.run([str(x) for x in args],cwd=TEST,capture_output=True,timeout=timeout)
    text=(r.stdout+r.stderr).decode('utf-8',errors='replace')
    (TEST/(name+'.log')).write_text(text,encoding='utf-8')
    records.append(dict(name=name,exit_code=r.returncode,passed=r.returncode==expected))
    assert r.returncode==expected,(name,r.returncode,text[-1800:])
    return text
def target(name):return TEST/name
def wait_stub(directory):
    end=time.monotonic()+5
    while not (directory/'stub-recovery.txt').exists() and time.monotonic()<end:time.sleep(.025)
    assert (directory/'stub-recovery.txt').exists()
    report=(directory/'installation-result.txt').read_text()
    assert 'recovery_program_started=true' in report
    pid=int(report.split('recovery_process_id=')[1].splitlines()[0])
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong]
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    handle=kernel.OpenProcess(0x100000,False,pid)
    if handle:
        assert kernel.WaitForSingleObject(handle,5000)==0
        kernel.CloseHandle(handle)

t=target('dry-plan');text=run('dry-run-does-not-launch',[exe,'--dry-run','--launch-recovery','--target',t])
assert 'PLAN: start recovery after successful preparation.' in text and not t.exists()
t=target('prepared-only');run('prepare-only-does-not-launch',[exe,'--files-only','--target',t])
assert 'recovery_program_started=false' in (t/'installation-result.txt').read_text() and not (t/'stub-recovery.txt').exists()
t=target('launch-fake');run('preparation-then-stub-start',[exe,'--files-only','--launch-recovery','--target',t]);wait_stub(t)
assert ('cwd='+str(t)) in (t/'stub-recovery.txt').read_text(encoding='utf-8')
t=target('inspector-fail');t.mkdir();(t/'inspector-fail.flag').write_bytes(b'fail')
run('inspector-failure-prevents-start',[exe,'--files-only','--launch-recovery','--target',t],1)
assert not (t/'stub-recovery.txt').exists() and 'recovery_program_started=false' in (t/'installation-result.txt').read_text()
data=fixture/'payload/data0.dat';original=data.read_bytes();data.write_bytes(b'corrupt')
t=target('bad-payload');run('bad-payload-prevents-start',[exe,'--files-only','--launch-recovery','--target',t],1)
assert not t.exists();data.write_bytes(original)
(fixture/'payload-manifest.tsv').write_bytes(manifest+b'corrupt')
t=target('bad-manifest');run('bad-manifest-prevents-start',[exe,'--files-only','--launch-recovery','--target',t],1)
assert not t.exists();(fixture/'payload-manifest.tsv').write_bytes(manifest)

# The exact public CMD is tested against a benign helper, not the real installer.
entry=TEST/'中文入口 with spaces';(entry/'tools').mkdir(parents=True)
shutil.copyfile(stub,entry/'tools/H1Setup.exe');shutil.copyfile(BUNDLE/'启动恢复.cmd',entry/'启动恢复.cmd')
run('single-public-entry-forwards-launch-flag',['cmd.exe','/d','/c',entry/'启动恢复.cmd'])
assert '--launch-recovery' in (entry/'tools/stub-entry.txt').read_text(encoding='utf-8')

# Duplicate-start protection is exercised while only a harmless fake process runs.
hold=TEST/'hold';hold.mkdir();shutil.copyfile(stub,hold/'super_recover.exe')
p=subprocess.Popen([str(hold/'super_recover.exe'),'--hold'],cwd=hold)
end=time.monotonic()+2
while not (hold/'stub-recovery.txt').exists() and time.monotonic()<end:time.sleep(.025)
try:
    t=target('already-running');run('already-running-prevents-file-changes',[exe,'--files-only','--launch-recovery','--target',t],1)
    assert not t.exists()
finally:p.wait(timeout=6)

# Published test driver runs the real installer only with --files-only.
assert 'launch_recovery?L" --launch-recovery":L""' in (SRC/'setup.c').read_text()

# The production helper is run ONLY with --files-only and no launch flag.
full=target('完整文件准备 with spaces');full.mkdir();(full/'WDAPI901.DLL').write_bytes(b'Old fixture DLL for backup verification')
run('production-complete-files-only',[BUNDLE/'tools/H1Setup.exe','--files-only','--target',full],timeout=120)
report=(full/'installation-result.txt').read_text()
assert 'recovery_program_started=false' in report and 'drivers=skipped' in report
assert sha(full/'WDAPI901.DLL')==built['bridge_sha256']
assert (full/'packet1.dat').stat().st_size==941500622
backups=list((full/'backups').glob('*/WDAPI901.DLL'))
assert len(backups)==1 and backups[0].read_bytes()==b'Old fixture DLL for backup verification'
assert not (full/'stub-recovery.txt').exists()
record=dict(passed=True,cases=records,setup_sha256=built['setup_sha256'],
            test_source_sha256=sha(Path(__file__)),stub_import_libraries=sorted(libraries),
            offline_stub_recovery_executed=True,official_recovery_executed=False,
            driver_installation_executed=False,production_files_only_verified=True,
            reused_driver_and_copy_source_regions_verified=True,
            not_tested=['Actual Windows UAC interaction','Actual WinUSB driver installation/binding in this revision',
                        'Physical USB transfers, NAND writing/readback and complete recovery'],
            fixture='local temporary directory (not distributed)')
(OUT/'installer-verification.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(json.dumps(dict(passed=True,cases=len(records),official_recovery_executed=False,driver_installation_executed=False)))
