"""Offline release checks. Production USB DLLs are inspected but never initialized."""
import re,runpy,struct
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
from common import *

def instruction_tests():
    header=(ROOT/'src/bridge/backup_attributes.h').read_text(encoding='utf-8')
    match=re.search(r'h1_attribute_new\[11\]=\{([^}]+)\}',header);assert match
    instruction=bytes(int(v.strip(),0) for v in match.group(1).split(','));assert len(instruction)==11
    count=0
    for attr in [0x10,0x20,0x01,0x27,0x2810,0x2820,0x410,0x1020,0x8010,0x10020,0x400010,0xffffffff]:
        for site in [0x46d62d,0x46d6ae]:
            u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x100000);u.mem_write(site,instruction+b'\xc3')
            u.mem_map(0x2000000,0x10000);u.mem_map(0x3000000,4096)
            sp=0x200f000;bp=0x200c000;stop=0x3000000
            u.mem_write(sp,struct.pack('<I',stop));u.mem_write(bp-0x2bc,struct.pack('<I',attr))
            saved={UC_X86_REG_EBX:0x12345678,UC_X86_REG_ESI:0xabcdef01,UC_X86_REG_EDI:0x87654321,UC_X86_REG_ECX:0x200d000}
            for reg,value in saved.items():u.reg_write(reg,value)
            u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_EBP,bp);u.reg_write(UC_X86_REG_EFLAGS,0x246)
            u.emu_start(site,stop,count=20)
            assert u.reg_read(UC_X86_REG_EAX)==attr&255 and u.reg_read(UC_X86_REG_EDX)==2
            assert u.reg_read(UC_X86_REG_ESP)==sp+4 and u.reg_read(UC_X86_REG_EBP)==bp and u.reg_read(UC_X86_REG_EFLAGS)==0x246
            assert all(u.reg_read(reg)==value for reg,value in saved.items())
            fixed=f'{attr&255:02X} 5D703D15 path';assert int(fixed[3:11],16)==0x5d703d15
            assert bool(int(fixed[:2],16)&16)==bool(attr&16)
            count+=1
    for attr in [0x2810,0x2820]:
        shifted=f'{attr:02X} 5D703D15 path'
        try:int(shifted[3:11],16)
        except ValueError:pass
        else:raise AssertionError('The original date-field misalignment must reproduce')
    return count

def validate(args):
    OUT.mkdir(exist_ok=True);tests=OUT/'tests';tests.mkdir(exist_ok=True)
    record=json.loads((OUT/'build.json').read_text(encoding='utf-8'))
    assert all(sha(OUT/name)==digest for name,digest in record['artifacts'].items())
    assert all(sha(ROOT/name)==digest for name,digest in record['sources'].items())
    required={'WD_DriverName','OsEventCreate','OsEventSignal','OsEventClose','Stat2Str','WDU_Init','WDU_Uninit','WDU_GetDeviceInfo','WDU_PutDeviceInfo','WDU_SetInterface','WDU_ResetDevice','WDU_Transfer','WDU_TransferDefaultPipe','WDU_TransferBulk'}
    audits=[]
    for name in ['WDAPI901.DLL','WDAPI901-readonly.dll','WDAPI901-test.dll','WDAPI901-test-readonly.dll']:
        p=pefile.PE(str(OUT/'bridge'/name));assert p.FILE_HEADER.Machine==0x14c
        libraries={d.dll.decode().lower() for d in p.DIRECTORY_ENTRY_IMPORT}
        imports={i.name.decode() for d in p.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
        exports={i.name.decode() for i in p.DIRECTORY_ENTRY_EXPORT.symbols if i.name};assert required<=exports
        if '-test' in name:
            assert not libraries&{'winusb.dll','setupapi.dll','cfgmgr32.dll'}
            assert not imports&{'DeviceIoControl','CreateFileW','CancelIoEx'}
        if 'readonly' in name:assert not imports&{'WinUsb_WritePipe','WinUsb_ReadPipe','WinUsb_ControlTransfer','DeviceIoControl'}
        audits.append(dict(name=name,sha256=sha(OUT/'bridge'/name),passed=True))
    jobs=[('transfer',[OUT/'bridge/bridge_tests.exe',OUT/'bridge/WDAPI901-test.dll',tests/'transfer.json','transfer']),
          ('readonly',[OUT/'bridge/bridge_tests.exe',OUT/'bridge/WDAPI901-test-readonly.dll',tests/'readonly.json','readonly']),
          ('preflight',[OUT/'bridge/preflight_tests.exe',OUT/'bridge/WDAPI901-test.dll']),
          ('attribute-native',[OUT/'bridge/backup_attributes_tests.exe']),
          ('inspect-production',[OUT/'bridge/inspect.exe',OUT/'bridge/WDAPI901.DLL']),
          ('inspect-readonly',[OUT/'bridge/inspect.exe',OUT/'bridge/WDAPI901-readonly.dll']),
          ('driver-cli-help',[OUT/'driver/wdi-simple.exe','--help']),
          ('driver-cli-unicode',[OUT/'driver/wdi-simple.exe','--h1-check-utf8','中文目录 with spaces'])]
    results=[]
    for name,command in jobs:
        r=run(command,cwd=OUT,capture_output=True,timeout=40)
        (tests/(name+'.log')).write_bytes(r.stdout+r.stderr)
        results.append(dict(name=name,passed=True))
        if name=='driver-cli-unicode':assert '中文目录 with spaces'.encode('utf-8') in r.stdout
    transfer=json.loads((tests/'transfer.json').read_text());readonly=json.loads((tests/'readonly.json').read_text())
    assert transfer['passed'] and transfer['case_count']==43 and readonly['passed']
    count=instruction_tests()
    archive_module=runpy.run_path(str(ROOT/'tests/archive.py'))
    archive_cases=archive_module['archive_tests']()
    runpy.run_path(str(ROOT/'tests/installer.py'),init_globals={'CONFIG_ARGS':args})
    installer=json.loads((OUT/'installer-verification.json').read_text(encoding='utf-8'));assert installer['passed']
    summary=dict(version=VERSION,passed=True,physical_usb_access=False,driver_installation_executed=False,
                 official_recovery_executed=False,transfer_cases=43,readonly_cases=readonly['case_count'],
                 preflight_cases=4,native_attribute_cases=6,attribute_instruction_cases=count,
                 archive_range_cases=archive_cases,
                 installer_cases=installer['cases'],binary_audits=audits,process_checks=results,
                 validation_scope='Fake WinUSB transport, DLL export/import inspection, instruction emulation, benign startup stub, and real installer --files-only. No NAND writes or driver changes.',
                 not_tested=['Physical USB communication and flashing in this build','UAC and actual driver binding','Complete v0.2.2 recovery from start to finish'])
    save(OUT/'verification.json',summary)
    print(json.dumps(dict(passed=True,transfer_cases=43,readonly_cases=readonly['case_count'],attribute_instruction_cases=count,installer_cases=len(installer['cases']),physical_usb=False)),flush=True)
