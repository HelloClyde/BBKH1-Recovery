"""Portable Windows build entry. all/build/test/package never flashes hardware."""
import argparse
from common import *

def build(args):
    cc32,cc64=compilers(args);bridge=ROOT/'src/bridge';dest=OUT/'bridge';dest.mkdir(parents=True,exist_ok=True)
    base=[cc32,'-std=c11','-O2','-Wall','-Wextra','-Werror','-static-libgcc','-Wl,--no-insert-timestamp']
    for name,fake,transfer in [('WDAPI901.DLL',0,1),('WDAPI901-readonly.dll',0,0),('WDAPI901-test.dll',1,1),('WDAPI901-test-readonly.dll',1,0)]:
        # MinGW's automatic DLL image base hashes the output path. Pin it so
        # clean checkouts in different directories produce the same DLL bytes.
        preferred={'WDAPI901.DLL':'0x69980000','WDAPI901-readonly.dll':'0x6a000000','WDAPI901-test.dll':'0x6b000000','WDAPI901-test-readonly.dll':'0x6c000000'}
        flags=['-shared','-Wl,--kill-at','-Wl,--image-base,'+preferred[name],f'-DH1_FAKE_PLATFORM={fake}',f'-DH1_ENABLE_TRANSFER={transfer}']
        if transfer:flags+=['-DH1_DIAGNOSTICS=1','-DH1_BURN_PREFLIGHT=1','-DH1_PC_ATTRIBUTE_FIX=1']
        sources=[bridge/'core.c']+([bridge/'fake_platform.c'] if fake else [])
        libraries=[] if fake else ['-lsetupapi','-ladvapi32','-lole32','-lcfgmgr32','-lwinusb']
        run(base+flags+sources+libraries+['-o',dest/name])
    for name,source,unicode in [('inspect.exe',bridge/'inspect.c',True),('H1BridgeCheck.exe',bridge/'probe.c',True),('bridge_tests.exe',ROOT/'tests/tests.c',True),('preflight_tests.exe',ROOT/'tests/preflight_tests.c',True),('backup_attributes_tests.exe',ROOT/'tests/backup_attributes_tests.c',False)]:
        run(base+(['-municode'] if unicode else [])+['-I'+str(bridge),source,'-o',dest/name])
    driver=OUT/'driver';upstream=ROOT/'third_party/libwdi'
    if not(driver/'embedded.h').exists():raise FileNotFoundError('Run bootstrap before build.')
    common=[cc64,'-std=gnu99','-O2','-static-libgcc','-DWINVER=0x0601','-D_WIN32_WINNT=0x0601','-D_WIN32_IE=0x0700','-DCOBJMACROS','-I'+str(driver),'-I'+str(upstream),'-I'+str(upstream/'libwdi'),'-Wl,--no-insert-timestamp']
    run([cc64.parent/'x86_64-w64-mingw32-windres.exe','libwdi.rc',driver/'libwdi-resource.o'],cwd=upstream/'libwdi')
    libraries=['-lsetupapi','-lole32','-lntdll','-lcomctl32','-luser32','-lgdi32','-lshell32','-ladvapi32','-lcrypt32','-lwintrust','-lversion']
    run(common+['-shared','-DLIBWDI_DLL_EXPORT','-Wl,--image-base,0x180000000','-Wl,--out-implib,'+str(driver/'libwdi.dll.a')]+[upstream/'libwdi'/n for n in ['logging.c','tokenizer.c','vid_data.c','pki.c','libwdi_dlg.c','libwdi.c']]+[driver/'libwdi-resource.o']+libraries+['-o',driver/'libwdi.dll'],cwd=driver)
    run(common+['-Dmain=wdi_cli_main_utf8','-c',upstream/'examples/wdi-simple.c','-o',driver/'wdi-simple-utf8.o'],cwd=driver)
    run(common+['-municode',ROOT/'src/driver/wdi_utf8.c',driver/'wdi-simple-utf8.o',driver/'libwdi.dll.a','-o',driver/'wdi-simple.exe'],cwd=driver)
    artifacts={str(p.relative_to(OUT)):sha(p) for folder in [dest,driver] for p in folder.iterdir() if p.suffix.lower() in ['.dll','.exe']}
    sources={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'src').rglob('*') if p.is_file()}
    save(OUT/'build.json',dict(version=VERSION,artifacts=artifacts,sources=sources,toolchain=run([cc64,'--version'],capture_output=True,text=True).stdout.splitlines()[0],physical_usb=False))
    print('Built compatibility layer and driver installer from source; no hardware accessed.',flush=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['bootstrap','build','bundle','test','package','all'])
    p.add_argument('--cc32');p.add_argument('--cc64');p.add_argument('--sevenzip');p.add_argument('--official',type=Path)
    args=p.parse_args()
    if args.action in ['bootstrap','all']:
        from dependencies import bootstrap
        bootstrap(args)
    if args.action in ['build','all']:build(args)
    if args.action in ['bundle','all']:
        from package import bundle
        bundle(args)
    if args.action in ['test','all']:
        from validate import validate
        validate(args)
    if args.action in ['package','all']:
        from package import package
        package(args)
if __name__=='__main__':main()
