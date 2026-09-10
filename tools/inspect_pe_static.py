"""Read-only PE inventory. Never loads, extracts or executes embedded code."""
import argparse, hashlib, json, re, struct
from pathlib import Path

def inspect(data):
    def u16(pos): return struct.unpack_from('<H', data, pos)[0]
    def u32(pos): return struct.unpack_from('<I', data, pos)[0]
    def u64(pos): return struct.unpack_from('<Q', data, pos)[0]
    images=[]
    for match in re.finditer(b'MZ',data):
        base=match.start()
        try:
            pe=base+u32(base+60)
            if data[pe:pe+4]!=b'PE\0\0': continue
            machine,count=u16(pe+4),u16(pe+6)
            optional,size=pe+24,u16(pe+20)
            if machine!=0x8664 or u16(optional)!=0x20b or not 1<=count<=96: continue
            sections=[]
            for i in range(count):
                pos=optional+size+40*i
                name=data[pos:pos+8].split(b'\0')[0].decode('ascii',errors='replace')
                vs,va,rawsize,raw=struct.unpack_from('<IIII',data,pos+8)
                if rawsize and base+raw+rawsize>len(data): raise ValueError('Section outside file')
                sections.append(dict(name=name,rva=va,virtual_size=vs,raw_offset=raw,raw_size=rawsize))
            def offset(rva):
                if rva<u32(optional+60): return base+rva
                for section in sections:
                    delta=rva-section['rva']
                    if 0<=delta<section['raw_size']: return base+section['raw_offset']+delta
                raise ValueError('Unmapped RVA')
            def string(pos):
                end=data.find(b'\0',pos,min(pos+1024,len(data)))
                if end<0: raise ValueError('Unterminated name')
                return data[pos:end].decode('ascii',errors='replace')
            imports={}
            import_rva=u32(optional+120)
            if import_rva:
                pos=offset(import_rva)
                for descriptor in range(256):
                    original,ts,forward,name,first=struct.unpack_from('<IIIII',data,pos+20*descriptor)
                    if not any((original,ts,forward,name,first)): break
                    dll=string(offset(name)); names=[]
                    table=offset(original or first)
                    for index in range(8192):
                        entry=u64(table+index*8)
                        if not entry: break
                        names.append('ordinal:'+str(entry&65535) if entry>>63 else string(offset(entry)+2))
                    imports[dll]=names
            images.append(dict(offset=base,offset_hex=hex(base),machine=hex(machine),is_dll=bool(u16(pe+22)&0x2000),sections=sections,imports=imports))
        except (struct.error,ValueError,OverflowError):
            continue
    bundles=[]
    for match in re.finditer(b'__CLANG_OFFLOAD_BUNDLE__',data):
        base=match.start(); pos=base+len(match.group())
        try:
            count=u64(pos);pos+=8
            if count>128: continue
            entries=[]
            for _ in range(count):
                offset,size,length=struct.unpack_from('<QQQ',data,pos);pos+=24
                if length>4096 or pos+length>len(data) or base+offset+size>len(data):
                    raise ValueError('Invalid bundle range')
                target=data[pos:pos+length].decode('ascii');pos+=length
                entries.append(dict(target=target,offset=base+offset,bytes=size,
                                    elf=bool(size>=4 and data[base+offset:base+offset+4]==b'\x7fELF')))
            bundles.append(dict(offset=base,entries=entries))
        except (struct.error,ValueError,UnicodeError):
            continue
    strings=[m.group().decode('ascii') for m in re.finditer(rb'[\x20-\x7e]{6,}',data)]
    terms=('hipImportExternalMemory','hipExternalMemoryGetMappedBuffer','hipLaunchKernel','nvngx_dlssnr','WEIGHTS_HT','weights.bin','interop:','staging ready:','inline mode','spin used','k_swin','k_reproject','k_import','k_export','OptiScaler','FSR','CreateSharedHandle')
    evidence=[s[:1600] for s in strings if any(t in s for t in terms)]
    return dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),pe_images=images,offload_bundles=bundles,
                architecture_markers=sorted(set(re.findall(r'gfx[0-9]{4}', '\n'.join(strings)))),
                clang_bundle_offsets=[m.start() for m in re.finditer(b'__CLANG_OFFLOAD_BUNDLE__',data)],
                selected_strings=sorted(set(evidence)),
                limits='Static headers/imports/strings only. Does not establish runtime behavior, model identity, safety, quality or performance.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file',type=Path);parser.add_argument('report',type=Path)
    args=parser.parse_args()
    if args.file.resolve()==args.report.resolve(): parser.error('Report must not overwrite input')
    report=inspect(args.file.read_bytes())
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('pe_images','selected_strings')},indent=2))
    for pe in report['pe_images']:
        print(pe['offset_hex'],'DLL' if pe['is_dll'] else 'EXE',[s['name'] for s in pe['sections']])
        print('Imports:',', '.join(pe['imports']))
    print('Selected strings:',len(report['selected_strings']))
