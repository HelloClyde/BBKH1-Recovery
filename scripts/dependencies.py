"""Acquire pinned public dependencies and extract driver resources. No installation."""
import shutil,zipfile
from common import *

def bootstrap(args):
    CACHE.mkdir(exist_ok=True);OUT.mkdir(exist_ok=True)
    commit=run(['git','-c','safe.directory='+(ROOT/'third_party/libwdi').as_posix(),'-C',ROOT/'third_party/libwdi','rev-parse','HEAD'],capture_output=True,text=True).stdout.strip()
    if commit!=LOCK['libwdi']['commit']:raise ValueError('libwdi submodule is not at the locked commit')
    archiver=sevenzip(args.sevenzip)
    archive=download(LOCK['innoextract']);inno_dir=CACHE/'innoextract'
    if not(inno_dir/'innoextract.exe').exists():
        with zipfile.ZipFile(archive) as z:z.extractall(inno_dir)
    if not(args.cc32 and args.cc64):
        archive=download(LOCK['toolchain']);destination=CACHE/'toolchain'
        if not(destination/'w64devkit/bin/gcc.exe').exists():
            destination.mkdir(exist_ok=True)
            run([archiver,'x','-y','-bsp0','-bso0','-o'+str(destination),archive])
    compilers(args)
    archive=download(LOCK['upx']);upx_dir=CACHE/'upx'
    if not(upx_dir/'upx-5.0.2-win64/upx.exe').exists():
        with zipfile.ZipFile(archive) as z:z.extractall(upx_dir)
    zadig=download(LOCK['zadig']);unpacked=CACHE/'zadig-unpacked.exe'
    if not unpacked.exists():run([upx_dir/'upx-5.0.2-win64/upx.exe','-d',zadig,'-o',unpacked],capture_output=True)
    if sha(unpacked)!=LOCK['zadig']['unpacked_sha256']:raise ValueError('Unpacked Zadig checksum mismatch')
    data=unpacked.read_bytes();resources=[]
    for entry in LOCK['zadig']['resources']:
        item=data[entry['offset']:entry['offset']+entry['size']]
        if hashlib.sha256(item).hexdigest()!=entry['sha256']:raise ValueError('Driver resource checksum mismatch: '+entry['name'])
        resources.append((entry['subdir'],entry['name'],item,entry['creation_time']))
    upstream=ROOT/'third_party/libwdi'
    for name in ['winusb.inf.in','winusb.cat.in','libusb0.inf.in','libusb0.cat.in','libusbk.inf.in','libusbk.cat.in','usbser.inf.in','usbser.cat.in']:
        resources.append(('',name,(upstream/'libwdi'/name).read_bytes(),1700000000))
    driver=OUT/'driver';driver.mkdir(exist_ok=True)
    with (driver/'embedded.h').open('w',encoding='ascii') as f:
        f.write('#include <stdint.h>\nstruct res {char *subdir;char *name;size_t size;int64_t creation_time;const unsigned char *data;};\n')
        for i,(_,_,data,_) in enumerate(resources):
            f.write(f'static const unsigned char resource_{i}[]={{\n')
            for at in range(0,len(data),32):f.write(','.join('0x%02x'%v for v in data[at:at+32])+',\n')
            f.write('};\n')
        f.write('const struct res resource[]={\n')
        for i,(directory,name,data,date) in enumerate(resources):f.write('{"%s","%s",%d,%d,resource_%d},\n'%(directory,name,len(data),date,i))
        f.write('};\nconst int nb_resources=sizeof(resource)/sizeof(resource[0]);\n')
    (driver/'config.h').write_text('#define _GNU_SOURCE 1\n#define WDK_DIR "official_zadig_embedded"\n#define WDF_VER 1011\n#define OPT_M64 1\n#define ENABLE_LOGGING 1\n#define INCLUDE_DEBUG_LOGGING 1\n#define PACKAGE_NAME "libwdi"\n#define PACKAGE_VERSION "1.5.1"\n',encoding='ascii')
    save(OUT/'dependencies.json',dict(libwdi_commit=commit,resources=[dict(name=n,size=len(b),sha256=hashlib.sha256(b).hexdigest()) for _,n,b,_ in resources],driver_installation_executed=False))
    print('Dependencies verified; Zadig was not executed and no drivers were installed.',flush=True)
