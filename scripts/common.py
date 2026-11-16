"""Shared deterministic build helpers. Never installs drivers or starts recovery."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,urllib.request

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'.cache';OUT=ROOT/'build';DIST=ROOT/'dist'
VERSION=(ROOT/'VERSION').read_text(encoding='ascii').strip()
LOCK=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def run(args,**kwargs):
    return subprocess.run(list(map(str,args)),check=True,**kwargs)

def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def copy(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    if destination.exists() and os.path.samefile(source,destination):return
    shutil.copyfile(source,destination)

def download(spec):
    CACHE.mkdir(exist_ok=True);path=CACHE/spec['file']
    if path.exists():
        if sha(path)!=spec['sha256']:raise ValueError(f'Cached dependency checksum mismatch: {path.name}')
        return path
    pending=path.with_suffix(path.suffix+'.part')
    print('Downloading '+spec['file'],flush=True)
    request=urllib.request.Request(spec['url'],headers={'User-Agent':'BBKH1-Recovery-build'})
    with urllib.request.urlopen(request,timeout=90) as src,pending.open('wb') as dst:
        shutil.copyfileobj(src,dst)
    if sha(pending)!=spec['sha256']:raise ValueError('Downloaded dependency checksum mismatch: '+path.name)
    pending.replace(path);return path

def sevenzip(explicit=None):
    options=[explicit,os.environ.get('SEVENZIP'),shutil.which('7z'),r'C:\Program Files\7-Zip\7z.exe']
    for option in options:
        if option and Path(option).is_file():return Path(option).resolve()
    # 7zr is sufficient for the locked toolchain's 7z SFX. RAR uses bsdtar.
    return download(LOCK['sevenzr'])

def compilers(args):
    directory=CACHE/'toolchain/w64devkit/bin'
    cc32=Path(args.cc32 or directory/'i686-w64-mingw32-gcc.exe').resolve()
    cc64=Path(args.cc64 or directory/'x86_64-w64-mingw32-gcc.exe').resolve()
    if not cc32.is_file() or not cc64.is_file():raise FileNotFoundError('Run bootstrap, or pass --cc32 and --cc64.')
    os.environ['PATH']=str(cc64.parent)+os.pathsep+os.environ.get('PATH','')
    return cc32,cc64

def compile_setup(cc,include,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-municode','-static-libgcc','-Wl,--no-insert-timestamp',
         '-I'+str(include),ROOT/'src/installer/setup.c','-o',destination,'-lbcrypt','-lshell32','-lsetupapi','-lcfgmgr32','-ladvapi32'])
