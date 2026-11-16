"""Integrate a hash-pinned, unmodified official RAR with the built compatibility layer."""
import zipfile
from common import *
BUNDLE=OUT/'bundle'

def extract_stored_setup(archive,destination,metadata):
    """Read the sole M0/stored entry from this exact hash-locked original RAR.
    The range is locked and its result is independently hashed. No legacy
    pathname is decoded, and this is deliberately not a general RAR reader.
    """
    offset=metadata['setup_offset'];size=metadata['setup_size']
    if offset<0 or size<=0 or offset+size>archive.stat().st_size:
        raise ValueError('Stored setup range is outside the locked archive')
    destination.parent.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256();remaining=size
    with archive.open('rb') as source,destination.open('wb') as target:
        source.seek(offset)
        while remaining:
            block=source.read(min(1024*1024,remaining))
            if not block:raise ValueError('Truncated stored setup')
            target.write(block);digest.update(block);remaining-=len(block)
    if digest.hexdigest()!=metadata['setup_sha256']:
        raise ValueError('Original Inno setup checksum mismatch')

def official_archive(args):
    if args.official:
        path=args.official.resolve()
    else:
        spec=dict(LOCK['official']);spec['file']=spec['asset']
        repo=os.environ.get('GITHUB_REPOSITORY','HelloClyde/BBKH1-Recovery')
        spec['url']=f"https://github.com/{repo}/releases/download/{spec['release']}/{spec['asset']}"
        path=download(spec)
    if path.stat().st_size!=LOCK['official']['size'] or sha(path)!=LOCK['official']['sha256']:
        raise ValueError('Official recovery RAR does not match the locked V1.41 original.')
    return path

def bundle(args):
    _,cc64=compilers(args);archive=official_archive(args)
    extracted=CACHE/'official-v1.41';marker=extracted/'verified-extraction.json'
    if not marker.exists():
        extracted.mkdir(parents=True,exist_ok=True)
        setup=CACHE/'official-setup.exe'
        extract_stored_setup(archive,setup,LOCK['official'])
        run([CACHE/'innoextract/innoextract.exe','--extract','--silent','--output-dir',extracted,setup])
    candidates=list(extracted.rglob('super_recover.exe'))
    if len(candidates)!=1:raise ValueError('Expected exactly one official super_recover.exe')
    app=candidates[0].parent
    BUNDLE.mkdir(parents=True,exist_ok=True);rows=[]
    def add(path,source,destination,expected=None):
        if expected and (path.stat().st_size!=expected['size'] or sha(path)!=expected['sha256']):
            raise ValueError('Extracted official file mismatch: '+destination)
        target=BUNDLE/source;target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() and path.stat().st_size>100*1024*1024:
            try:os.link(path,target)
            except OSError:copy(path,target)
        else:copy(path,target)
        rows.append('\t'.join([sha(target),str(target.stat().st_size),source.replace('/','\\'),destination.replace('/','\\')]))
    for entry in LOCK['official']['files']:
        destination=entry['destination']
        if destination=='legacy-driver/wdapi901-original.dll':
            candidates=list(extracted.rglob('wdapi901.dll'))
            if len(candidates)!=1:raise ValueError('Expected original wdapi901.dll in official sys directory')
            source=candidates[0]
        else:source=app/Path(destination).name
        add(source,'payload/recovery/'+entry['source'],destination,entry)
    save(marker,dict(archive_sha256=sha(archive),files_verified=len(LOCK['official']['files'])))
    add(OUT/'bridge/WDAPI901.DLL','payload/recovery/WDAPI901.DLL','WDAPI901.DLL')
    for source,dest in [('bridge/inspect.exe','tools/inspect.exe'),('bridge/H1BridgeCheck.exe','tools/readonly/H1BridgeCheck.exe'),('bridge/WDAPI901-readonly.dll','tools/readonly/WDAPI901-readonly.dll'),('driver/wdi-simple.exe','tools/driver/wdi-simple.exe'),('driver/libwdi.dll','tools/driver/libwdi.dll')]:
        add(OUT/source,dest,dest)
    manifest=('\n'.join(rows)+'\n').encode('ascii');(BUNDLE/'payload-manifest.tsv').write_bytes(manifest)
    (OUT/'manifest_hash.h').write_text('#define H1_MANIFEST_SHA256 "'+hashlib.sha256(manifest).hexdigest()+'"\n#define H1_PACKAGE_VERSION "'+VERSION+'"\n',encoding='ascii')
    compile_setup(cc64,OUT,BUNDLE/'tools/H1Setup.exe')
    for source,dest in [('LICENSE','LICENSE'),('NOTICE.md','NOTICE.md'),('docs/USER_GUIDE.zh-CN.md','使用说明.md'),('docs/VALIDATION.zh-CN.md','验证边界.md')]:copy(ROOT/source,BUNDLE/dest)
    for name in ['COPYING','COPYING-LGPL']:copy(ROOT/'third_party/libwdi'/name,BUNDLE/'third_party'/name)
    # Include corresponding upstream source for the distributed driver components.
    source_zip=BUNDLE/'third_party/libwdi-1.5.1-source.zip'
    files=run(['git','-c','safe.directory='+(ROOT/'third_party/libwdi').as_posix(),'-C',ROOT/'third_party/libwdi','ls-files','-z'],capture_output=True).stdout.decode('utf-8').split('\0')
    source_entries={}
    for name in sorted(filter(None,files)):
        path=ROOT/'third_party/libwdi'/name
        if path.is_file():source_entries['libwdi-1.5.1/'+name]=path
    source_entries['h1/wdi_utf8.c']=ROOT/'src/driver/wdi_utf8.c'
    write_zip(source_zip,source_entries)
    (BUNDLE/'启动恢复.cmd').write_bytes(b'@echo off\r\n"%~dp0tools\\H1Setup.exe" --launch-recovery\r\nif errorlevel 1 (\r\n echo Preparation or startup failed. Keep the message above and the logs.\r\n pause\r\n exit /b 1\r\n)\r\n')
    (BUNDLE/'仅校验文件.cmd').write_bytes(b'@echo off\r\n"%~dp0tools\\H1Setup.exe" --dry-run --launch-recovery\r\npause\r\n')
    (BUNDLE/'打开日志.cmd').write_bytes(b'@echo off\r\nif exist "%~dp0H1RecoveryV141\\H1USBBridge.log" (\r\n start "" notepad.exe "%~dp0H1RecoveryV141\\H1USBBridge.log"\r\n) else (\r\n echo No H1USBBridge.log exists yet.\r\n pause\r\n)\r\n')
    included=[str(p.relative_to(BUNDLE)).replace('\\','/') for p in BUNDLE.rglob('*') if p.is_file() and not any(part.casefold() in ['h1recoveryv141','back_up','back_up2','backups'] for part in p.relative_to(BUNDLE).parts)]
    # Authoritative allowlist, never package a live directory by recursion later.
    payloads=[row.split('\t')[2].replace('\\','/') for row in rows]
    static=['payload-manifest.tsv','tools/H1Setup.exe','LICENSE','NOTICE.md','使用说明.md','验证边界.md','third_party/COPYING','third_party/COPYING-LGPL','third_party/libwdi-1.5.1-source.zip','启动恢复.cmd','仅校验文件.cmd','打开日志.cmd']
    allowed=sorted(set(payloads+static));assert set(allowed)<=set(included)
    save(OUT/'bundle.json',dict(version=VERSION,allowed_files=allowed,manifest_sha256=hashlib.sha256(manifest).hexdigest(),bridge_sha256=sha(OUT/'bridge/WDAPI901.DLL'),official_rar_sha256=LOCK['official']['sha256'],physical_usb=False))
    print(f'Prepared {len(rows)} verified payload files; recovery was not started.',flush=True)

def write_zip(destination,entries):
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,path in sorted(entries.items()):
            if any(part.casefold() in ['back_up','back_up2','backups','h1recoveryv141'] for part in Path(name).parts):raise ValueError('Refusing runtime/private path in package')
            info=zipfile.ZipInfo(name,(2026,11,16,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            with path.open('rb') as src,z.open(info,'w',force_zip64=True) as dst:shutil.copyfileobj(src,dst)
    with zipfile.ZipFile(destination) as z:
        if z.testzip() is not None:raise ValueError('ZIP CRC check failed')

def package(args):
    validation=json.loads((OUT/'verification.json').read_text(encoding='utf-8'));assert validation['passed']
    built=json.loads((OUT/'build.json').read_text(encoding='utf-8'))
    assert all(sha(OUT/name)==digest for name,digest in built['artifacts'].items())
    assert all(sha(ROOT/name)==digest for name,digest in built['sources'].items())
    record=json.loads((OUT/'bundle.json').read_text(encoding='utf-8'))
    assert sha(BUNDLE/'payload-manifest.tsv')==record['manifest_sha256']
    for line in (BUNDLE/'payload-manifest.tsv').read_text().splitlines():
        digest,size,source,dest=line.split('\t');path=BUNDLE/source
        assert path.stat().st_size==int(size) and sha(path)==digest,source
    public=dict(version=VERSION,offline_validation=validation,documented_hardware_validation=json.loads((ROOT/'docs/hardware-validation.json').read_text(encoding='utf-8')),physical_hardware_test_performed_by_this_build=False)
    save(BUNDLE/'验证记录.json',public)
    entries={name:BUNDLE/name for name in record['allowed_files']};entries['验证记录.json']=BUNDLE/'验证记录.json'
    checks={name:sha(path) for name,path in entries.items()};save(BUNDLE/'SHA256.json',checks);entries['SHA256.json']=BUNDLE/'SHA256.json'
    DIST.mkdir(exist_ok=True)
    full=DIST/f'H1-V1.41-Win10x64-v{VERSION}-complete.zip'
    print('Compressing complete recovery package...',flush=True);write_zip(full,entries)
    small={'WDAPI901.DLL':OUT/'bridge/WDAPI901.DLL','WDAPI901-readonly.dll':OUT/'bridge/WDAPI901-readonly.dll','H1BridgeCheck.exe':OUT/'bridge/H1BridgeCheck.exe','inspect.exe':OUT/'bridge/inspect.exe','使用说明.md':ROOT/'docs/USER_GUIDE.zh-CN.md','LICENSE':ROOT/'LICENSE','NOTICE.md':ROOT/'NOTICE.md','验证记录.json':BUNDLE/'验证记录.json'}
    bridge=DIST/f'H1-Win10x64-v{VERSION}-compatibility.zip';write_zip(bridge,small)
    source={}
    for directory,prefix in [(ROOT,''),(ROOT/'third_party/libwdi','third_party/libwdi/')]:
        names=run(['git','-c','safe.directory='+directory.as_posix(),'-C',directory,'ls-files','-z'],capture_output=True).stdout.decode('utf-8').split('\0')
        for name in filter(None,names):
            path=directory/name
            if path.is_file():source[prefix+name]=path
    source_archive=DIST/f'BBKH1-Recovery-v{VERSION}-source.zip';write_zip(source_archive,source)
    save(DIST/'verification.json',public)
    results={path.name:sha(path) for path in [full,bridge,source_archive,DIST/'verification.json']}
    (DIST/'SHA256SUMS.txt').write_text(''.join(digest+'  '+name+'\n' for name,digest in sorted(results.items())),encoding='utf-8')
    save(OUT/'package.json',dict(version=VERSION,assets=[dict(name=name,sha256=digest,size=(DIST/name).stat().st_size) for name,digest in results.items()]))
    print(json.dumps(dict(packaged=list(results),physical_usb=False)),flush=True)
