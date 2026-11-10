"""Read PE/CLI metadata and method bytes only. Never load or execute the assembly."""
import argparse
import json
import struct
import hashlib
from pathlib import Path

TABLES = {
0:['u2','s','g','g','g'],1:['c:ResolutionScope','s','s'],
2:['u4','s','s','c:TypeDefOrRef','t:4','t:6'],3:['t:4'],4:['u2','s','b'],5:['t:6'],
6:['u4','u2','u2','s','b','t:8'],7:['t:8'],8:['u2','u2','s'],9:['t:2','c:TypeDefOrRef'],
10:['c:MemberRefParent','s','b'],11:['u2','c:HasConstant','b'],12:['c:HasCustomAttribute','c:CustomAttributeType','b'],
13:['c:HasFieldMarshal','b'],14:['u2','c:HasDeclSecurity','b'],15:['u2','u4','t:2'],16:['u4','t:4'],17:['b'],
18:['t:2','t:20'],19:['t:20'],20:['u2','s','c:TypeDefOrRef'],21:['t:2','t:23'],22:['t:23'],
23:['u2','s','b'],24:['u2','t:6','c:HasSemantics'],25:['t:2','c:MethodDefOrRef','c:MethodDefOrRef'],
26:['s'],27:['b'],28:['u2','c:MemberForwarded','s','t:26'],29:['u4','t:4'],30:['u4','u4'],31:['u4'],
32:['u4','u2','u2','u2','u2','u4','b','s','s'],33:['u4'],34:['u4','u4','u4'],
35:['u2','u2','u2','u2','u4','b','s','s','b'],36:['u4','t:35'],37:['u4','u4','u4','t:35'],
38:['u4','s','b'],39:['u4','u4','s','s','c:Implementation'],40:['u4','u4','s','c:Implementation'],
41:['t:2','t:2'],42:['u2','u2','c:TypeOrMethodDef','s'],43:['c:MethodDefOrRef','b'],44:['t:42','c:TypeDefOrRef']}
CODED = {
'ResolutionScope':(2,[0,26,35,1]),'TypeDefOrRef':(2,[2,1,27]),'MemberRefParent':(3,[2,1,26,6,27]),
'HasConstant':(2,[4,8,23]),'HasCustomAttribute':(5,[6,4,1,2,8,9,10,0,14,23,20,17,26,27,32,35,38,39,40,42,44,43]),
'CustomAttributeType':(3,[6,10]),'HasFieldMarshal':(1,[4,8]),'HasDeclSecurity':(2,[2,6,32]),
'HasSemantics':(1,[20,23]),'MethodDefOrRef':(1,[6,10]),'MemberForwarded':(1,[4,6]),
'Implementation':(2,[38,35,39]),'TypeOrMethodDef':(1,[2,6])}

def instructions(body):
    tokens={0x27,0x28,0x29,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x79,
            0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x8c,0x8d,0x8f,0xa3,0xa4,0xa5,0xc2,0xc6,0xd0}
    short=set(range(0x0e,0x14))|{0x1f,0xde}|set(range(0x2b,0x38))
    long={0x20,0x22,0xdd}|set(range(0x38,0x45))
    prefix_tokens={0x06,0x07,0x15,0x16,0x1c}
    valid=set(range(0x00,0x24))|set(range(0x25,0x77))|set(range(0x79,0xa6))|set(range(0xb3,0xbb))|{0xc2,0xc3,0xc6}|set(range(0xd0,0xe1))
    valid_prefix=set(range(0x00,0x08))|set(range(0x09,0x10))|set(range(0x11,0x1b))|set(range(0x1c,0x1f))
    position=0
    while position<len(body):
        offset=position;opcode=body[position];position+=1;token=False
        if opcode==0xfe:
            if position>=len(body):raise ValueError('truncated IL prefix')
            second=body[position];position+=1;opcode=0xfe00|second
            if second not in valid_prefix:raise ValueError(f'unsupported IL opcode {opcode:04x}')
            if second in prefix_tokens:size=4;token=True
            elif second in range(0x09,0x0f):size=2
            elif second in (0x12,0x19):size=1
            else:size=0
        elif opcode not in valid:raise ValueError(f'unsupported IL opcode {opcode:02x}')
        elif opcode in tokens:size=4;token=True
        elif opcode in short:size=1
        elif opcode in long:size=4
        elif opcode in (0x21,0x23):size=8
        elif opcode==0x45:
            if position+4>len(body):raise ValueError('truncated IL switch')
            count=int.from_bytes(body[position:position+4],'little')
            if count>(len(body)-position-4)//4:raise ValueError('invalid IL switch size')
            size=4+count*4
        else:size=0
        if position+size>len(body):raise ValueError('truncated IL operand')
        yield offset,opcode,(int.from_bytes(body[position:position+4],'little') if token else None)
        position+=size

def branch_targets(body):
    """Decode explicit IL branch destinations, not reachability or EH semantics."""
    decoded=list(instructions(body))
    boundaries={offset for offset,_,_ in decoded}
    branches=[]
    for index,(offset,opcode,_) in enumerate(decoded):
        end=decoded[index+1][0] if index+1<len(decoded) else len(body)
        if opcode in range(0x2b,0x38) or opcode==0xde:
            targets=[end+struct.unpack_from('<b',body,offset+1)[0]]
        elif opcode in range(0x38,0x45) or opcode==0xdd:
            targets=[end+struct.unpack_from('<i',body,offset+1)[0]]
        elif opcode==0x45:
            count=struct.unpack_from('<I',body,offset+1)[0]
            targets=[end+struct.unpack_from('<i',body,offset+5+4*i)[0]
                     for i in range(count)]
        else:
            continue
        if any(target not in boundaries for target in targets):
            raise ValueError(f'invalid IL branch target at {offset}')
        branches.append({'offset':offset,'opcode':f'{opcode:04x}',
                         'next_offset':end,'targets':targets})
    return branches

def field_reference_inventory(metadata, field_tokens):
    targets=set(field_tokens)
    references=[];failures=[];decoded=0;with_rva=0
    for method,row in enumerate(metadata.rows.get(6,[]),1):
        if not row[0]:continue
        with_rva+=1
        try:
            decoded_body=list(instructions(metadata.body(method)))
        except (ValueError,IndexError,KeyError,struct.error) as error:
            failures.append({'method_token':f'{0x06000000|method:08x}',
                             'error':str(error)})
            continue
        decoded+=1
        for offset,opcode,token in decoded_body:
            if token in targets:
                references.append({'method_token':f'{0x06000000|method:08x}',
                                   'offset':offset,'opcode':f'{opcode:04x}',
                                   'field_token':f'{token:08x}'})
    return {'methods_with_rva':with_rva,'decoded_methods':decoded,
            'complete':not failures,'decode_failures':failures,
            'field_tokens':[f'{token:08x}' for token in sorted(targets)],
            'references':references,
            'scope':'Direct encoded token operands in decodable MethodDef bodies only; not alias, array mutation, reflection, external code or reachability proof'}

def initialization_array_publication(body):
    fields=[*range(0x959,0x96d),0x973,0x96d,0x974,0x975,*range(0x96e,0x973)]
    expected=bytearray.fromhex('1f1d8d79030002')
    for index,field in enumerate(fields):
        expected.append(0x25)
        expected.extend(bytes([0x16+index]) if index<=8 else bytes([0x1f,index]))
        expected.append(0x7e)
        expected.extend((0x04000000|field).to_bytes(4,'little'))
        expected.append(0xa2)
    expected.extend(bytes.fromhex('807609000428490a00062a'))
    if body[6530:]!=expected:
        raise ValueError('unsupported initialization array publication bytes')
    return {'start_offset':6530,'element_type_token':'02000379',
            'field_tokens':[f'{0x04000000|field:08x}' for field in fields],
            'template_index':9,'published_field_token':'04000976',
            'publication_offset':6789,'following_call_token':'06000a49',
            'scope':'Exact encoded array construction and publication only; aliases, mutation, callees and initialization order remain unproved'}

def initialization_array_consumers(metadata):
    expected=(
        (0xa49,0x35b,145,'000001','13300300aa000000dd000011',
         '7079f145a163ddb9d710127e0c4d82d39b976250eac0a6be030d15a583e77025'),
        (0xa4a,0x35b,150,'000115118f7801128de40e','1330030024000000de000011',
         '222bcbeb8d4f4fe6ff974be79890d64020e2c8bb9bd4b5df058ee4451c660136'),
        (0xa4b,0x35c,6278,'200001','1e',
         '481d98670477ab01d39394e1b9e6b4a1fb1f943717205d63f16c9b206a4bdc1c'),
        (0xa4c,0x35c,131,'200102128de4','4a',
         'd9c7ace0b2778a05be142781a0f55aaba0f3fee8022110c0bc8a4b2363daf2bc'))
    methods=[]
    for method,owner,flags,signature,header,digest in expected:
        row=metadata.rows[6][method-1]
        actual_header=(metadata.tiny_header(method) if len(header)==2
                       else metadata.fat_header(method))
        body=metadata.body(method)
        if (metadata.owner[method]!=owner or row[1:3]!=[0,flags]
                or metadata.blob(row[4]).hex()!=signature
                or actual_header.hex()!=header
                or hashlib.sha256(body).hexdigest()!=digest):
            raise ValueError('unsupported initialization array consumer')
        methods.append({'method_token':f'{0x06000000|method:08x}',
                        'owner_token':f'{0x02000000|owner:08x}',
                        'implementation_flags':0,'flags':flags,'signature':signature,
                        'header':header,'body_size':len(body),'il_sha256':digest})
    return {'methods':methods,
            'scope':'Exact method declarations, headers and bodies only; referenced metadata, library behavior, query exception handling, aliases and initialization order remain unproved'}

def initialization_query_method(metadata):
    method=0xad1
    signature='10010215118f78011e0015128121011e0015128295011e00'
    header='1b30020043000000f4000011'
    digest='fdfa3c495c7bff73dd4b0df4463bc1d81c8b64d9cb42a8b51806e7a8790ba046'
    locals_signature='070315128125011e001e0015118f78011e00'
    sections=[{'bytes':'0110000002000700252c000a00000000',
               'clauses':[{'flags':2,'try_offset':7,'try_length':37,
                           'handler_offset':44,'handler_length':10,'extra':0}]}]
    row=metadata.rows[6][method-1]
    generic=[(index+1,entry[:3],metadata.string(entry[3]))
             for index,entry in enumerate(metadata.rows.get(42,[]))
             if entry[2]==(method<<1|1)]
    constraints=[entry for entry in metadata.rows.get(44,[]) if entry[0]==37]
    if (metadata.owner[method]!=0x369 or row[1:3]!=[0,150]
            or metadata.blob(row[4]).hex()!=signature
            or metadata.fat_header(method).hex()!=header
            or hashlib.sha256(metadata.body(method)).hexdigest()!=digest
            or metadata.blob(metadata.rows[17][0xf4-1][0]).hex()!=locals_signature
            or generic!=[(37,[0,0,method<<1|1],'T')] or constraints
            or metadata.exception_sections(method)!=sections):
        raise ValueError('unsupported initialization query method')
    return {'method_token':'06000ad1','owner_token':'02000369',
            'implementation_flags':0,'flags':150,'signature':signature,
            'header':header,'body_size':67,'il_sha256':digest,
            'local_signature_token':'110000f4','local_signature':locals_signature,
            'generic_parameter':{'token':'2a000025','number':0,'flags':0,
                                 'name':'T','constraints':[]},
            'exception_sections':sections,
            'scope':'Exact declaration, locals, generic parameter, body and small finally section only; referenced metadata, library semantics, control-flow validity and termination remain unproved'}

def initialization_query_bindings(metadata):
    members=((0x2e9,1804,'.ctor','200001'),
             (0x2ea,1804,'Contains','2001021300'),
             (0x2eb,1804,'Add','2001021300'),
             (0x2ec,1812,'.ctor','2002011c18'),
             (0x38,1281,'.ctor','200001'),(6,945,'Dispose','200001'),
             (7,721,'MoveNext','200002'),
             (0x302,1876,'GetEnumerator','200015128125011300'),
             (0x303,1884,'get_Current','20001300'),
             (0x317,1948,'Invoke','2001021300'),
             (0x318,388,'op_Implicit','000115118f780113001300'),
             (0x316,388,'op_Implicit','000115118f78011300118cf0'),
             (0xd5,1633,'op_Equality','0002020e0e'))
    specs=((225,'15128111010e'),(226,'1512829501128de4'),
           (234,'15128121011e00'),(235,'15128125011e00'),
           (243,'15128295011e00'),(48,'15118f78011e00'))
    types=((68,14,'HashSet`1','System.Collections.Generic'),
           (165,6,'Predicate`1','System'),(160,6,'Object','System'),
           (118,6,'IDisposable','System'),(90,6,'IEnumerator','System.Collections'),
           (72,6,'IEnumerable`1','System.Collections.Generic'),
           (73,6,'IEnumerator`1','System.Collections.Generic'),
           (204,6,'String','System'))
    for index,parent,name,signature in members:
        row=metadata.rows[10][index-1]
        if (row[0]!=parent or metadata.string(row[1])!=name
                or metadata.blob(row[2]).hex()!=signature):
            raise ValueError('unsupported initialization query binding')
    for index,signature in specs:
        if metadata.blob(metadata.rows[27][index-1][0]).hex()!=signature:
            raise ValueError('unsupported initialization query binding')
    for index,scope,name,namespace in types:
        row=metadata.rows[1][index-1]
        if (row[0],metadata.string(row[1]),metadata.string(row[2]))!=(scope,name,namespace):
            raise ValueError('unsupported initialization query binding')
    for index,name in ((1,'mscorlib'),(3,'System.Core')):
        row=metadata.rows[35][index-1]
        if (row[:5]!=[4,0,0,0,0] or metadata.blob(row[5]).hex()!='b77a5c561934e089'
                or metadata.string(row[6])!=name or metadata.string(row[7])
                or metadata.blob(row[8])):
            raise ValueError('unsupported initialization query binding')
    row=metadata.rows[43][0x1af-1]
    if row[0]!=5538 or metadata.blob(row[1]).hex()!='0a01128de4':
        raise ValueError('unsupported initialization query binding')
    return {'member_references':[{'token':f'0a{index:06x}','parent':parent,
                                  'name':name,'signature':signature}
                                 for index,parent,name,signature in members],
            'type_specifications':[{'token':f'1b{index:06x}','signature':signature}
                                   for index,signature in specs],
            'type_references':[{'token':f'01{index:06x}','resolution_scope':scope,
                               'name':name,'namespace':namespace}
                               for index,scope,name,namespace in types],
            'assembly_references':['mscorlib, Version=4.0.0.0, PublicKeyToken=b77a5c561934e089',
                                   'System.Core, Version=4.0.0.0, PublicKeyToken=b77a5c561934e089'],
            'method_specification':{'token':'2b0001af','method_token':'06000ad1',
                                    'instantiation':'0a01128de4'},
            'scope':'Selected encoded reference identities only; local Maybe/field identities, other consumer references, library behavior, aliases and timing remain unproved'}

def current_initialization_inventory(metadata):
    digest=hashlib.sha256(metadata.data).hexdigest()
    if digest!='ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5':
        raise ValueError('unsupported current initialization inventory assembly hash')
    inventory=field_reference_inventory(metadata,(0x040009e7,0x04000962,0x04000976))
    body=metadata.body(0xa48)
    decoded=list(instructions(body))
    transfers=set(range(0x2b,0x46))|{0x27,0x29,0x7a,0xdc,0xdd,0xde,0xfe11,0xfe1a}
    return {'assembly_sha256':digest,'status':'partial-byte-evidence-only',
            'certification_eligible':False,'field_reference_inventory':inventory,
            'array_publication':initialization_array_publication(body),
            'array_consumers':initialization_array_consumers(metadata),
            'query_method':initialization_query_method(metadata),
            'query_bindings':initialization_query_bindings(metadata),
            'ordinary_static_method':{
                'method_token':'06000a48','body_size':len(body),
                'instruction_count':len(decoded),
                'il_sha256':hashlib.sha256(body).hexdigest(),
                'return_offsets':[offset for offset,op,_ in decoded if op==0x2a],
                'explicit_control_transfers':[{'offset':offset,'opcode':f'{op:04x}'}
                    for offset,op,_ in decoded if op in transfers],
                'scope':'Encoded control transfers only; calls, exceptions, initialization order and termination remain unproved'}}

class Metadata:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        data = self.data
        pe = self.number(0x3c,4)
        if data[pe:pe+4] != b'PE\0\0': raise ValueError('not PE')
        section_count = self.number(pe+6,2)
        optional_size = self.number(pe+20,2)
        optional = pe+24
        self.sections=[]
        for i in range(section_count):
            offset=optional+optional_size+i*40
            self.sections.append((self.number(offset+12,4),self.number(offset+8,4),self.number(offset+20,4),self.number(offset+16,4)))
        directory=optional+(112 if self.number(optional,2)==0x20b else 96)
        cli=self.offset(self.number(directory+14*8,4))
        root=self.offset(self.number(cli+8,4))
        if data[root:root+4]!=b'BSJB': raise ValueError('not CLI metadata')
        position=root+16+self.number(root+12,4)
        position=(position+3)&~3
        count=self.number(position+2,2);position+=4
        self.streams={}
        for _ in range(count):
            start=self.number(position,4);size=self.number(position+4,4)
            name_start=position+8;end=data.index(0,name_start)
            name=data[name_start:end].decode('ascii')
            self.streams[name]=(root+start,size)
            position=(end+4)&~3
        table_stream=self.streams.get('#~',self.streams.get('#-'))
        if table_stream is None:raise ValueError('missing metadata table stream')
        table_start=table_stream[0]
        self.heap_sizes=data[table_start+6]
        valid=self.number(table_start+8,8)
        position=table_start+24
        self.counts={}
        for i in range(64):
            if valid&(1<<i): self.counts[i]=self.number(position,4);position+=4
        for table in (3,5,7,19,22):
            if self.counts.get(table,0):
                raise ValueError(f'unsupported metadata pointer table {table}')
        self.rows={}
        for table in range(64):
            count=self.counts.get(table,0)
            if not count: continue
            schema=TABLES.get(table)
            if schema is None: raise ValueError(f'unsupported metadata table {table}')
            widths=[self.width(kind) for kind in schema]
            rows=[]
            for _ in range(count):
                row=[]
                for width in widths: row.append(self.number(position,width));position+=width
                rows.append(row)
            self.rows[table]=rows
        self.owner=self.member_owners(6,5)
        self.field_owner=self.member_owners(4,4)
    def member_owners(self,table,column):
        limit=len(self.rows.get(table,[]))+1
        starts=[row[column] for row in self.rows.get(2,[])]
        if (any(not 1<=start<=limit for start in starts)
                or any(left>right for left,right in zip(starts,starts[1:]))
                or (starts and starts[0]!=1)
                or (not starts and limit!=1)):
            raise ValueError('invalid TypeDef member ownership ranges')
        return {member:index+1 for index,start in enumerate(starts)
                for member in range(start,starts[index+1] if index+1<len(starts) else limit)}
    def number(self,position,size):
        if position<0 or position+size>len(self.data):raise ValueError('truncated data')
        return int.from_bytes(self.data[position:position+size],'little')
    def offset(self,rva,size=1):
        for address,virtual,raw,raw_size in self.sections:
            if address<=rva<address+max(virtual,raw_size):
                displacement=rva-address
                if size<0 or displacement+size>raw_size or raw+displacement+size>len(self.data):
                    raise ValueError('RVA range is not backed by file data')
                return raw+displacement
        raise ValueError(f'invalid RVA {rva:x}')
    def width(self,kind):
        if kind=='u2':return 2
        if kind=='u4':return 4
        if kind in ('s','g','b'):
            return 4 if self.heap_sizes & {'s':1,'g':2,'b':4}[kind] else 2
        if kind.startswith('t:'):return 4 if self.counts.get(int(kind[2:]),0)>=65536 else 2
        bits,tables=CODED[kind[2:]]
        return 4 if max(self.counts.get(table,0) for table in tables)>=(1<<(16-bits)) else 2
    def string(self,index):
        start,size=self.streams['#Strings']
        if index>=size:raise ValueError('invalid string index')
        end=self.data.index(0,start+index,start+size)
        return self.data[start+index:end].decode('utf-8',errors='replace')
    def blob(self,index):
        heap=self.streams.get('#Blob')
        if heap is None:raise ValueError('missing blob heap')
        start,size=heap
        if start<0 or size<0 or start+size>len(self.data):raise ValueError('truncated blob heap')
        if index<0 or index>=size:raise ValueError('invalid blob index')
        position=start+index
        first=self.data[position]
        if first<0x80:width=1;length=first
        elif first<0xc0:width=2;length=first&0x3f
        elif first<0xe0:width=4;length=first&0x1f
        else:raise ValueError('invalid blob length encoding')
        if index+width>size:raise ValueError('truncated blob length')
        for byte in self.data[position+1:position+width]:length=(length<<8)|byte
        if index+width+length>size:raise ValueError('truncated blob payload')
        return self.data[position+width:position+width+length]
    def type_name(self,index):
        row=self.rows[2][index-1]
        return self.string(row[2])+'.'+self.string(row[1])
    def token_name(self,token):
        table,index=token>>24,token&0xffffff
        if table==6 and 0<index<=len(self.rows.get(6,[])):
            return self.type_name(self.owner[index])+'.'+self.string(self.rows[6][index-1][3])
        if table==4 and 0<index<=len(self.rows.get(4,[])):
            return self.type_name(self.field_owner[index])+'.'+self.string(self.rows[4][index-1][1])
        if table==10 and 0<index<=len(self.rows.get(10,[])):
            row=self.rows[10][index-1];name=self.string(row[1]);tag=row[0]&7;parent=row[0]>>3
            if tag==1 and parent:
                ref=self.rows[1][parent-1];return self.string(ref[2])+'.'+self.string(ref[1])+'.'+name
            return 'member:'+name
        return f'token:{token:08x}'
    def body(self,index):
        rva=self.rows[6][index-1][0]
        if not rva:return b''
        start=self.offset(rva);flag=self.data[start]
        if flag&3==2:
            size=flag>>2
            self.offset(rva,1+size)
            return self.data[start+1:start+1+size]
        if flag&3!=3:raise ValueError('invalid IL header')
        self.offset(rva,12)
        header=(self.number(start,2)>>12)*4
        if header<12:raise ValueError('invalid fat IL header size')
        size=self.number(start+4,4)
        self.offset(rva,header+size)
        return self.data[start+header:start+header+size]
    def tiny_header(self,index):
        rva=self.rows[6][index-1][0]
        if not rva:raise ValueError('missing tiny IL header')
        start=self.offset(rva)
        flag=self.data[start]
        if flag&3!=2:raise ValueError('invalid tiny IL header')
        self.offset(rva,1+(flag>>2))
        return self.data[start:start+1]
    def fat_header(self,index):
        rva=self.rows[6][index-1][0]
        if not rva:raise ValueError('missing fat IL header')
        start=self.offset(rva,12)
        if self.number(start,2)&3!=3:raise ValueError('invalid fat IL header')
        size=(self.number(start,2)>>12)*4
        if size<12:raise ValueError('invalid fat IL header size')
        self.offset(rva,size)
        return self.data[start:start+size]
    def exception_sections(self,index):
        body=self.body(index)
        rva=self.rows[6][index-1][0]
        if not rva:raise ValueError('missing IL method')
        start=self.offset(rva)
        if self.data[start]&3==2:return []
        header=self.fat_header(index)
        if not int.from_bytes(header[:2],'little')&8:return []
        section_rva=(rva+len(header)+len(body)+3)&~3
        boundaries={offset for offset,_,_ in instructions(body)}
        ends=boundaries|{len(body)}
        sections=[]
        while True:
            position=self.offset(section_rva,4)
            kind,small_size,reserved=struct.unpack_from('<BBH',self.data,position)
            if kind&0x3f!=1:raise ValueError('unsupported IL exception section kind')
            fat=bool(kind&0x40)
            form='fat' if fat else 'small'
            size=int.from_bytes(self.data[position+1:position+4],'little') if fat else small_size
            clause_size=24 if fat else 12
            if (not fat and reserved) or size<4+clause_size or (size-4)%clause_size:
                raise ValueError(f'invalid {form} IL exception section header')
            self.offset(section_rva,size)
            clauses=[]
            for offset in range(position+4,position+size,clause_size):
                flags,try_start,try_length,handler_start,handler_length,extra=struct.unpack_from(
                    '<IIIIII' if fat else '<HHBHBI',self.data,offset)
                if (flags not in (0,1,2,4) or not try_length or not handler_length
                        or try_start not in boundaries or try_start+try_length not in ends
                        or handler_start not in boundaries or handler_start+handler_length not in ends
                        or (flags in (2,4) and extra!=0)
                        or (flags==1 and (extra not in boundaries or extra>=handler_start))
                        or (flags==0 and (extra>>24 not in (1,2,27) or not extra&0xffffff))):
                    raise ValueError(f'invalid {form} IL exception clause')
                clauses.append({'flags':flags,'try_offset':try_start,'try_length':try_length,
                                'handler_offset':handler_start,'handler_length':handler_length,
                                'extra':extra})
            sections.append({'bytes':self.data[position:position+size].hex(),'clauses':clauses})
            if not kind&0x80:return sections
            section_rva=(section_rva+size+3)&~3
    def references(self,index):
        result=[]
        for offset,opcode,token in instructions(self.body(index)):
            if token is None or token>>24 not in (4,6,10):continue
            result.append({'offset':offset,'opcode':hex(opcode),'token':f'{token:08x}','name':self.token_name(token)})
        return result

def main_grid_asset_audit(metadata, content):
    assembly_hash=hashlib.sha256(metadata.data).hexdigest()
    if assembly_hash!='fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d':
        raise ValueError('game build differs; grid asset binding must be re-established')
    payload_offset=0x81e7c
    length=metadata.number(payload_offset-4,4)
    payload=metadata.data[payload_offset:payload_offset+length]
    if length!=160279 or hashlib.sha256(payload).hexdigest()!='eef517f43433d996124680b192922637ab7b1b1ba1ec6455db0f15c8888cfa5b':
        raise ValueError('unexpected string resource')
    if struct.unpack_from('<HH',payload)!=(0xd9a9,0x3701):
        raise ValueError('unsupported string resource header')
    def decode(argument):
        key_value=0xdc03e7be
        position=argument^0x5af63869
        if position<4 or position+7>len(payload):raise ValueError('invalid string record')
        key=bytes(payload[position+i]^((key_value>>(8*i))&255) for i in range(3))
        flags=int.from_bytes(payload[position+3:position+7],'little')^position^0x501fe1a2^key_value
        size=flags&0x0fffffff
        if flags&0xf0000000!=0x40000000 or size>4096 or position+7+size>len(payload):
            raise ValueError('unsupported string record encoding')
        data=bytearray(payload[position+7:position+7+size])
        previous=((size+11)^(key[1]+7))&255
        state=(key[0]|(key[2]<<8))+(previous<<3)
        half=0
        for index in range(size):
            if index%2==0:
                state=(state*0x343fd+0x269ec3)&0xffffffff
                half=state>>16
            original=data[index]
            data[index]=(original^key[1]^(previous+3)^(half&255))&255
            previous=original
            half>>=8
        return {'argument':f'{argument:08x}','payload_offset':position,
            'file_offset':payload_offset+position,'record_key':key.hex(),'flags':f'{flags:08x}',
            'text':bytes(data).decode('latin1')}
    loader_strings=[decode(argument) for argument in (0x5af611f9,0x5af64e65,0x5af71521)]
    if [item['text'] for item in loader_strings]!=['Content','.png','.psd']:
        raise ValueError('unexpected texture lookup rules')
    textures=[]
    for argument in (0x5af75435,0x5af754e1,0x5af754dc,0x5af7548a):
        record=decode(argument)
        relative=Path(record['text'])
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('invalid texture path')
        png=Path(content)/(str(relative)+'.png')
        psd=Path(content)/(str(relative)+'.psd')
        if psd.exists():raise ValueError('PSD override needs separate loader evidence')
        data=png.read_bytes()
        if data[:8]!=b'\x89PNG\r\n\x1a\n' or data[12:16]!=b'IHDR' or int.from_bytes(data[8:12],'big')!=13:
            raise ValueError('invalid PNG header')
        width,height=struct.unpack_from('>II',data,16)
        if (width,height)!=(82,71):raise ValueError('main-grid texture dimensions changed')
        textures.append({**record,'asset':record['text']+'.png','sha256':hashlib.sha256(data).hexdigest(),
            'width':width,'height':height,'psd_override_present':False})
    methods=(0xaab,0xaa9,0x97d,0x789,0x6b,0xe9a,0xe9b,0xe9c,0xe9d,0x71a,0x7fa,0xe9e,0x8fa,0x8fc,0x790,0x793,0x10e8,0x10ed)
    return {'schema':1,'assembly_sha256':assembly_hash,
        'method':'Hash-locked static resource decoding and PNG IHDR inspection; no assembly load or game execution',
        'scope':'Main-grid asset binding and dimensions for this installation only; not collision, raster or game observation equivalence',
        'resource':{'manifest_name':metadata.string(metadata.rows[40][0][2]),'payload_offset':payload_offset,
            'length':length,'sha256':hashlib.sha256(payload).hexdigest()},
        'decoder':{'branch':'normal same-assembly caller; reflected/missing-caller branches not generalized',
            'key':'dc03e7be','argument_xor':'5af63869','record_key_length':3,
            'key_derivation':'a8eb01b0 ^ 0ecf9c63 ^ 4d301b6b ^ 37176106',
            'metadata_hash':'b95cf74d0ecf9c63','normal_state':'00188886'},
        'method_il_hashes':{f'{0x06000000|index:08x}':hashlib.sha256(metadata.body(index)).hexdigest() for index in methods},
        'loader_strings':loader_strings,'textures':textures,
        'binding_chain':['0600006b writes four decoded texture loads to 04000411',
            '060008fa checks equal dimensions and stores first texture in 04000867',
            '060008fc reads 04000627 dimensions; 06000790 initializes main singleton 040006bf',
            '06000793 maps x=size.X*(Q+0.5f*R), y=size.Y*R',
            '060010e8 and 060010ed consume that same main singleton'],
        'loader_chain':['06000e9a joins Content and decoded path; 06000e9b chooses PNG without PSD override',
            '06000e9c -> 06000e9d -> 0600071a (IMG_Load, SDL2_image.dll)',
            '06000e9d reads surface dimensions; 060007fa/06000e9e do not resize',
            '06000e9b writes image dimensions to 04000627']}

def current_grid_string(payload, argument):
    """Decode only the bounded byte-string records used by the conditional grid audit."""
    position=argument^0xd2d32f94
    if position<4 or position+7>len(payload):
        raise ValueError('invalid current string record')
    key=bytes(payload[position+i]^((0xaa688a0f>>(8*i))&255) for i in range(3))
    flags=int.from_bytes(payload[position+3:position+7],'little')^position^0xcfc9ad3a^0xaa688a0f
    size=flags&0x0fffffff
    if flags&0xf0000000!=0x40000000 or size>4096 or position+7+size>len(payload):
        raise ValueError('unsupported current string record encoding')
    data=bytearray(payload[position+7:position+7+size])
    previous=((size+11)^(key[1]+7))&255
    state=(key[0]|(key[2]<<8))+(previous<<3)
    half=0
    for index in range(size):
        if index%2==0:
            state=(state*0x343fd+0x269ec3)&0xffffffff
            half=state>>16
        original=data[index]
        data[index]=(original^key[1]^(previous+3)^(half&255))&255
        previous=original
        half>>=8
    return {'argument':f'{argument:08x}','payload_offset':position,
            'record_key':key.hex(),'flags':f'{flags:08x}','text':bytes(data).decode('latin1')}

def current_cache_execute(body, start, fields, argument=0):
    """Interpret the bounded integer-only cache tail, never managed code or its guards."""
    decoded=list(instructions(body))
    boundaries={offset for offset,_,_ in decoded}
    stack=[]
    locals_={}
    pc=start
    def integer(value, bits=32):
        value&=(1<<bits)-1
        return (value-(1<<bits) if value>>(bits-1) else value,bits)
    def pop():
        if not stack:raise ValueError('cache IL stack underflow')
        return stack.pop()
    def local(index):
        if index not in locals_:raise ValueError('uninitialized cache IL local')
        return locals_[index]
    def pop_integer():
        value=pop()
        if not isinstance(value,tuple):
            raise ValueError('cache IL expected integer')
        return value
    for _ in range(10000):
        if pc not in boundaries:
            raise ValueError('invalid cache IL branch')
        op=body[pc]
        pc+=1
        if op in (2,3):stack.append(fields if op==2 else integer(argument,64))
        elif 6<=op<=9:stack.append(local(op-6))
        elif 10<=op<=13:locals_[op-10]=pop()
        elif op in (0x11,0x13):
            index=body[pc];pc+=1
            if op==0x11:stack.append(local(index))
            else:locals_[index]=pop()
        elif 0x16<=op<=0x1e:stack.append(integer(op-0x16))
        elif op in (0x1f,0x20):
            size=1 if op==0x1f else 4
            stack.append(integer(int.from_bytes(body[pc:pc+size],'little',signed=True)))
            pc+=size
        elif op in (0x58,0x59,0x5f,0x60,0x61,0x62,0x63):
            right,rb=pop_integer();left,lb=pop_integer()
            bits=lb if op in (0x62,0x63) else max(lb,rb)
            shift=right&(bits-1)
            if op==0x58:value=left+right
            elif op==0x59:value=left-right
            elif op==0x5f:value=left&right
            elif op==0x60:value=left|right
            elif op==0x61:value=left^right
            elif op==0x62:value=left<<shift
            else:value=left>>shift
            stack.append(integer(value,bits))
        elif op in (0x65,0x66,0x69,0x6a,0x6e):
            value,bits=pop_integer()
            if op==0x65:value=-value
            elif op==0x66:value=~value
            else:
                if op==0x6e:value&=(1<<bits)-1
                bits=32 if op==0x69 else 64
            stack.append(integer(value,bits))
        elif op in (0x2b,0x33):
            delta=int.from_bytes(body[pc:pc+1],'little',signed=True);pc+=1
            take=True
            if op==0x33:
                right,_=pop_integer();left,_=pop_integer();take=left<right
            if take:pc+=delta
        elif op in (0x7b,0x7d,0x8d):
            token=int.from_bytes(body[pc:pc+4],'little');pc+=4
            if op==0x8d:
                count,_=pop_integer()
                if token!=0x0100007a or count!=4:
                    raise ValueError('unsupported cache IL array')
                stack.append([integer(0) for _ in range(count)])
            elif op==0x7b:
                if pop() is not fields:raise ValueError('invalid cache receiver')
                if token not in (0x04000526,0x04000527) or token not in fields:
                    raise ValueError('unsupported or uninitialized cache field')
                stack.append(fields[token])
            else:
                value=pop_integer()
                if pop() is not fields:raise ValueError('invalid cache receiver')
                if token not in (0x04000526,0x04000527):
                    raise ValueError('unsupported cache field')
                fields[token]=value
        elif op in (0x94,0x9e):
            value=pop_integer() if op==0x9e else None
            index,_=pop_integer();array=pop()
            if not isinstance(array,list) or not 0<=index<len(array):
                raise ValueError('invalid cache array index')
            if op==0x94:stack.append(array[index])
            else:array[index]=value
        elif op==0x2a:
            if len(stack)>1:raise ValueError('invalid cache return stack')
            return pop_integer()[0] if stack else None
        else:raise ValueError(f'unsupported cache IL opcode {op:02x}')
    raise ValueError('cache IL instruction budget exceeded')

def current_cache_audit(metadata):
    expected={0x913:'96f05055a88820a820c19fa12fdabba098f70198221812c80267f219c7586982',
              0x914:'d7a6faaa4e9b66556a5e0086ace2c58426b4b6bfc1c85b430bdc2f1c1ffcb961'}
    bodies={rid:metadata.body(rid) for rid in expected}
    for rid,digest in expected.items():
        if hashlib.sha256(bodies[rid]).hexdigest()!=digest:
            raise ValueError('unsupported current cache IL hash')
    fields={}
    current_cache_execute(bodies[0x914],31,fields)
    result=current_cache_execute(bodies[0x913],43,fields)
    if result!=0:raise ValueError('current cache zero roundtrip failed')
    return {'status':'conditional-integer-il-zero-roundtrip',
            'method_il_sha256':{f'{0x06000000|rid:08x}':digest for rid,digest in expected.items()},
            'encrypted_zero':{f'{token:08x}':f'{value&0xffffffff:08x}' for token,(value,_) in fields.items()},
            'decrypted_zero':result,
            'assumptions':['Both methods take the successful caller and stack guard branches',
                           'Runtime initialization invokes this setter with zero before the getter']}

def current_dimension_metadata_audit(metadata):
    fields={0xb54:(0x3c9,0x6,'0608'),0xb55:(0x3c9,0x6,'0608'),
            0x12b3:(0x4e2,0x6,'060c'),0x12b4:(0x4e2,0x6,'060c'),
            0x9d4:(0x375,0x26,'06118f24'),0x22a:(0x206,0x26,'06128dec'),
            0xa00:(0x37b,0x6,'06118f24')}
    methods={0xcee:(0x3c9,0x1886,'2002010808'),0x133b:(0x4e2,0x1886,'2002010c0c'),
             0xb3d:(0x375,0x86,'200008'),0xb3e:(0x375,0x86,'200008'),
             0x646:(0x206,0x86,'2000118f24'),0xcf1:(0x3c9,0x86,'2000119388')}
    if metadata.counts.get(3,0) or metadata.counts.get(5,0):
        raise ValueError('unsupported current dimension metadata pointer tables')
    for rid,(owner,flags,signature) in fields.items():
        row=metadata.rows[4][rid-1]
        if (metadata.field_owner[rid],row[0],metadata.blob(row[2]))!=(owner,flags,bytes.fromhex(signature)):
            raise ValueError('unsupported current dimension field metadata')
    for rid,(owner,flags,signature) in methods.items():
        row=metadata.rows[6][rid-1]
        if (metadata.owner[rid],row[2],metadata.blob(row[4]))!=(owner,flags,bytes.fromhex(signature)):
            raise ValueError('unsupported current dimension method metadata')
    for rid in (0x3c9,0x4e2):
        if metadata.rows[2][rid-1][3]!=0x391:
            raise ValueError('unsupported current dimension value type base')
    base=metadata.rows[1][0xe4-1]
    if (metadata.string(base[1]),metadata.string(base[2]))!=('ValueType','System'):
        raise ValueError('unsupported current dimension value type identity')
    return {'status':'exact-metadata-signatures-and-owners-only',
            'fields':{f'{0x04000000|rid:08x}':{'owner':f'{0x02000000|owner:08x}',
                       'flags':flags,'signature':signature} for rid,(owner,flags,signature) in fields.items()},
            'methods':{f'{0x06000000|rid:08x}':{'owner':f'{0x02000000|owner:08x}',
                        'flags':flags,'signature':signature} for rid,(owner,flags,signature) in methods.items()},
            'value_type_base':'TypeRef 010000e4 System.ValueType',
            'scope':'Metadata identity only; assembly resolution, verification and runtime initialization are not proved'}

def current_dimension_leaf_audit(metadata):
    expected={
        0xcee:'02037d540b000402047d550b00042a',
        0x133b:'02037db312000402047db41200042a',
        0xb3d:'027bd40900047b540b00042a',
        0xb3e:'027bd40900047b550b00042a',
        0x646:'027b2a0200047b000a00042a',
        0xcf1:'027b540b00046b027b550b00046b733b1300062a'}
    for rid,body in expected.items():
        if metadata.body(rid)!=bytes.fromhex(body):
            raise ValueError('unsupported current dimension leaf IL')
    return {'status':'exact-il-leaf-dataflow-only',
            'metadata_proof':current_dimension_metadata_audit(metadata),
            'method_il_sha256':{f'{0x06000000|rid:08x}':hashlib.sha256(bytes.fromhex(body)).hexdigest()
                                for rid,body in expected.items()},
            'index2_constructor':{'x_field':'04000b54','y_field':'04000b55'},
            'surface_dimension_getters':{'stored_index2_field':'040009d4',
                                         'width_method':'06000b3d','height_method':'06000b3e'},
            'texture_dimension_getter':{'method':'06000646','texture_field':'0400022a',
                                        'dimension_field':'04000a00'},
            'index2_to_vector2':{'method':'06000cf1','conversion':'conv.r4 for each int32 component',
                                'constructor':'0600133b','x_field':'040012b3','y_field':'040012b4'},
            'unproved_obligations':['Caller initialization and runtime metadata resolution',
                                    'Conditional callers returning normally and complete runtime initialization',
                                    'Native surface dimensions and floating-point execution environment']}

def current_dimension_wiring_metadata_audit(metadata):
    fields={0x9d3:(0x375,0x26,'061188a4'),0x9d4:(0x375,0x26,'06118f24'),
            0x9d5:(0x375,0x26,'0608'),0x9d6:(0x375,0x26,'061d05'),
            0xa00:(0x37b,0x6,'06118f24'),
            **{rid:(0x206,0x26,'06128dec') for rid in (0x22a,0x22b,0x22c,0x22d)},
            0x44b:(0x285,0x26,'06119388'),0x449:(0x285,0x16,'06128a14'),
            0x44a:(0x285,0x16,'06128a14'),0x8fa:(0x335,0x16,'06128b78'),
            0x54d:(0x2de,0x6,'06128cc4'),0x8c2:(0x331,0x6,'06128818')}
    methods={0x644:(0x206,0x1886,'200401128dec128dec128dec128dec'),
             0xb3b:(0x375,0x1886,'2003011188a4118f241d05'),
             0xb3c:(0x375,0x1886,'2004011188a408081d05'),
             0xcf7:(0x3c9,0x896,'000202118f24118f24'),
             0x80c:(0x285,0x1886,'200101118f24'),0x80d:(0x285,0x1891,'000001')}
    if metadata.counts.get(3,0) or metadata.counts.get(5,0):
        raise ValueError('unsupported current wiring metadata pointer tables')
    try:
        for rid,(owner,flags,signature) in fields.items():
            row=metadata.rows[4][rid-1]
            if (metadata.field_owner[rid],row[0],metadata.blob(row[2]))!=(owner,flags,bytes.fromhex(signature)):
                raise ValueError('unsupported current wiring field metadata')
        for rid,(owner,flags,signature) in methods.items():
            row=metadata.rows[6][rid-1]
            if (metadata.owner[rid],row[1],row[2],metadata.blob(row[4]))!=(owner,0,flags,bytes.fromhex(signature)):
                raise ValueError('unsupported current wiring method metadata')
    except (IndexError,KeyError) as error:
        raise ValueError('truncated current wiring metadata') from error
    return {'status':'exact-metadata-signatures-owners-and-method-implementation-flags-only',
            'fields':{f'{0x04000000|rid:08x}':{'owner':f'{0x02000000|owner:08x}',
                       'flags':flags,'signature':signature} for rid,(owner,flags,signature) in fields.items()},
            'methods':{f'{0x06000000|rid:08x}':{'owner':f'{0x02000000|owner:08x}',
                        'implementation_flags':0,'flags':flags,'signature':signature}
                       for rid,(owner,flags,signature) in methods.items()},
            'scope':'Declaration identity only; type resolution, external dispatch and initialization remain unproved'}

def current_dimension_wiring_audit(metadata):
    expected={
        0x644:'02283800000a037b000a0004047b000a000428f70c00062d27037b000a0004057b000a000428f70c00062d14037b000a00040e047b000a000428f70c00062c10207e78d2d2287c07000673dc0900067a02037d2a02000402047d2b02000402057d2c020004020e047d2d0200042a',
        0xb3b:'02283800000a047b540b000416310b047b550b000416fe022b0116203eb2d3d2287c07000628b40a0006047b540b0004047b550b00045a0328c90a00065a0a058e6906fe01205bb2d3d2287c07000628b40a000602037dd309000402047dd409000402027cd40900047b540b0004027bd309000428c90a00065a7dd509000402057dd60900042a',
        0xb3c:'0203040573ee0c00060e04283b0b00062a',
        0xcf7:'027b540b0004037b540b00043312027b550b0004037b550b0004fe0116fe012a172a',
        0x80c:'02283800000a020f0128f10c00067d4b0400042a',
        0x80d:'7efa0800047b4d0500047bc20800046f46060006730c08000680490400041f421f3973ee0c0006730c080006804a0400042a'}
    for rid,body in expected.items():
        if metadata.body(rid)!=bytes.fromhex(body):
            raise ValueError('unsupported current dimension wiring IL')
    return {'status':'exact-il-conditional-managed-wiring-only',
            'metadata_proof':current_dimension_wiring_metadata_audit(metadata),
            'method_il_sha256':{f'{0x06000000|rid:08x}':hashlib.sha256(bytes.fromhex(body)).hexdigest()
                                for rid,body in expected.items()},
            'surface_constructor':{'method':'06000b3b','stored_dimensions':'argument 2 -> 040009d4',
                                   'precondition':'Base constructor and both assertion calls return normally',
                                   'integer_overflow':'Size and stride multiplications are unchecked int32'},
            'surface_dimension_adapter':{'method':'06000b3c','constructor':'06000cee',
                                         'target':'06000b3b'},
            'texture_bundle':{'method':'06000644','dimension_field':'04000a00',
                              'inequality_method':'06000cf7','comparisons':[[1,2],[1,3],[1,4]],
                              'mismatch':'throw after any unequal width or height',
                              'successful_stores':['0400022a','0400022b','0400022c','0400022d']},
            'grid_constructor':{'method':'0600080c','conversion':'06000cf1',
                                'vector_field':'0400044b'},
            'grid_static_initializer':{'method':'0600080d',
                                       'main_grid':'040008fa -> 0400054d -> 040008c2 -> 06000646 -> 0600080c -> 04000449',
                                       'secondary_grid':{'dimensions':[66,57],'index_constructor':'06000cee',
                                                         'grid_constructor':'0600080c','static_field':'0400044a'}},
            'unproved_obligations':['Runtime type resolution, dispatch and external base constructor',
                                    'Assertion behavior and runtime initialization success',
                                    'Native loader dimensions and pixel conversion semantics']}

def current_pixel_execute(body, width, height, stride, pixels):
    """Bounded byte-array IL evaluation; external calls are explicit models only."""
    if (not all(type(value) is int and value >= 0 for value in (width,height,stride))
            or width*height > 4096 or stride*height > 65536
            or stride < width*4 or len(pixels) != stride*height):
        raise ValueError('unsupported pixel fixture dimensions')
    pixels=bytearray(pixels)
    fields={0x040009d5:stride,0x040009d6:pixels}
    receiver=object();stack=[];locals_={};pc=0
    boundaries={offset for offset,_,_ in instructions(body)}
    def pop():
        if not stack:raise ValueError('pixel IL stack underflow')
        return stack.pop()
    def index(array,i):
        if not isinstance(array,bytearray) or not 0<=i<len(array):
            raise ValueError('pixel IL array bounds')
    def int32(value):
        value&=0xffffffff
        return value-0x100000000 if value&0x80000000 else value
    for _ in range(1000000):
        if pc not in boundaries:raise ValueError('invalid pixel IL branch')
        op=body[pc];pc+=1
        if op==2:stack.append(receiver)
        elif 6<=op<=9:
            if op-6 not in locals_:raise ValueError('uninitialized pixel local')
            stack.append(locals_[op-6])
        elif 10<=op<=13:locals_[op-10]=pop()
        elif op in (0x11,0x13):
            slot=body[pc];pc+=1
            if op==0x13:locals_[slot]=pop()
            else:
                if slot not in locals_:raise ValueError('uninitialized pixel local')
                stack.append(locals_[slot])
        elif 0x16<=op<=0x1e:stack.append(op-0x16)
        elif op==0x20:
            stack.append(int.from_bytes(body[pc:pc+4],'little',signed=True));pc+=4
        elif op in (0x58,0x59,0x5a,0x5b):
            right=pop();left=pop()
            if type(left) is not int or type(right) is not int:raise ValueError('pixel IL integer required')
            if op==0x58:value=left+right
            elif op==0x59:value=left-right
            elif op==0x5a:value=left*right
            else:
                if right==0:raise ValueError('pixel IL division by zero')
                value=(abs(left)//abs(right))*(-1 if (left<0)!=(right<0) else 1)
            stack.append(int32(value))
        elif op==0xd2:stack.append(pop()&255)
        elif op in (0x2b,0x32):
            delta=int.from_bytes(body[pc:pc+1],'little',signed=True);pc+=1
            take=True
            if op==0x32:
                right=pop();left=pop();take=left<right
            if take:pc+=delta
        elif op in (0x28,0x6f,0x7b,0x8d):
            token=int.from_bytes(body[pc:pc+4],'little');pc+=4
            if op==0x7b:
                if pop() is not receiver or token not in fields:raise ValueError('unsupported pixel field')
                stack.append(fields[token])
            elif op==0x8d:
                size=pop()
                if token!=0x0100003b or type(size) is not int or not 0<=size<=65536:
                    raise ValueError('unsupported pixel allocation')
                stack.append(bytearray(size))
            elif token in (0x06000b3d,0x06000b3e):
                if pop() is not receiver:raise ValueError('invalid pixel getter receiver')
                stack.append(width if token==0x06000b3d else height)
            elif token==0x0a000369:
                count=pop();target_index=pop();target=pop();source_index=pop();source=pop()
                if (not isinstance(source,bytearray) or not isinstance(target,bytearray)
                        or min(count,source_index,target_index)<0
                        or source_index+count>len(source) or target_index+count>len(target)):
                    raise ValueError('pixel Array.Copy bounds')
                target[target_index:target_index+count]=source[source_index:source_index+count]
            else:raise ValueError('unsupported pixel call')
        elif op in (0x91,0x9c):
            value=pop() if op==0x9c else None
            i=pop();array=pop();index(array,i)
            if op==0x91:stack.append(array[i])
            else:
                if type(value) is not int:raise ValueError('pixel IL byte value required')
                array[i]=value&255
        elif op==0x2a:
            if stack:raise ValueError('invalid pixel return stack')
            return bytes(pixels)
        else:raise ValueError(f'unsupported pixel IL opcode {op:02x}')
    raise ValueError('pixel IL instruction budget exceeded')


def current_pixel_audit(metadata):
    expected={
        0xb40:'027bd50900048d3b0000010a160b2b6002283e0b00061759075907027bd50900045a0c027bd50900045a0d027bd6090004080616027bd5090004286903000a027bd609000409027bd609000408027bd5090004286903000a0616027bd609000409027bd5090004286903000a0717580b0702283e0b0006185b32952a',
        0xed5:'026f3d0b00060a026f3e0b00060b027bd60900040c160d2b72081a095a911304081a095a1758911305081a095a1858911306081a095a1958911307110411075a20ff0000005bd21304110511075a20ff0000005bd21305110611075a20ff0000005bd21306081a095a11049c081a095a175811059c081a095a185811069c081a095a195811079c0917580d0906075a32882a'}
    bodies={rid:metadata.body(rid) for rid in expected}
    if any(bodies[rid].hex()!=body for rid,body in expected.items()):
        raise ValueError('unsupported current pixel IL')
    cases=0
    for width in (1,2,3):
        for height in (0,1,2,3,4,5):
            for padding in (0,3):
                stride=width*4+padding
                pixels=bytes((i*73+19)%256 for i in range(stride*height))
                flipped=b''.join(pixels[row*stride:(row+1)*stride] for row in reversed(range(height)))
                if current_pixel_execute(bodies[0xb40],width,height,stride,pixels)!=flipped:
                    raise ValueError('current pixel flip fixture failed')
                cases+=1
    # Tight rows are a separate premise: the premultiply loop does not use stride.
    channels=bytes(value for alpha in range(256) for channel in range(256)
                   for value in (channel,255-channel,channel^alpha,alpha))
    for start in range(0,len(channels),4096*4):
        pixels=channels[start:start+4096*4];count=len(pixels)//4
        reference=bytes(value for offset in range(0,len(pixels),4)
                        for value in (*(pixels[offset+c]*pixels[offset+3]//255 for c in range(3)),pixels[offset+3]))
        if current_pixel_execute(bodies[0xed5],count,1,count*4,pixels)!=reference:
            raise ValueError('current pixel premultiply fixture failed')
    return {'status':'exact-il-bounded-byte-array-fixtures-only',
            'method_il_sha256':{f'{0x06000000|rid:08x}':hashlib.sha256(body).hexdigest() for rid,body in bodies.items()},
            'flip_fixture_count':cases,'premultiply_alpha_channel_pairs':65536,
            'assumptions':['Width and height getters return the supplied nonnegative dimensions',
                           'Array.Copy has byte-array memmove semantics and calls return normally',
                           'Premultiply pixels are tightly packed four-byte elements'],
            'scope':'Finite modeled inputs only; not a proof of all dimensions, metadata dispatch or native pixel layout'}


def current_grid_asset_audit(metadata,content):
    assembly_hash=hashlib.sha256(metadata.data).hexdigest()
    if assembly_hash!='ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5':
        raise ValueError('unsupported current grid assembly hash')
    payload_offset=0x8295c
    length=metadata.number(payload_offset-4,4)
    payload=metadata.data[payload_offset:payload_offset+length]
    if length!=161228 or hashlib.sha256(payload).hexdigest()!='eb30da8c127cda59067d17dc3f396465f50a1cd4275b92db5ccbb264190addc3':
        raise ValueError('unexpected current string resource')
    if struct.unpack_from('<HH',payload)!=(0x8a92,0xcb6a):
        raise ValueError('unsupported current string resource header')
    loader_strings=[current_grid_string(payload,arg) for arg in (0xd2d30604,0xd2d356b3,0xd2d21f0f)]
    if [item['text'] for item in loader_strings]!=['Content','.png','.psd']:
        raise ValueError('unexpected current texture lookup rules')
    expected_assets={
        0xd2d24077:'2db084044cd64132361e20b6eb0caabcb7196010440b3a04c3adadfdbd5bb96b',
        0xd2d25f9b:'7c1fe2c11cd89e36dd1a9c2a88bfaf7983543c987665e9cdc974d43295a8657b',
        0xd2d25fa8:'53ce58d8a918cbb0f30542916c2f16e716bfcd9da6f471432f77d6617b8dca02',
        0xd2d25ffe:'34897c68cd2091a6e5292b742750854a29809ac390e07490b950d97d0ac0b8e4'}
    textures=[]
    for argument,expected_hash in expected_assets.items():
        record=current_grid_string(payload,argument)
        relative=Path(record['text'])
        if not record['text'] or relative.is_absolute() or '..' in relative.parts:
            raise ValueError('invalid current texture path')
        png=Path(content)/(str(relative)+'.png')
        psd=Path(content)/(str(relative)+'.psd')
        if psd.exists():
            raise ValueError('current PSD override needs separate loader evidence')
        data=png.read_bytes()
        if (len(data)<33 or data[:8]!=b'\x89PNG\r\n\x1a\n' or data[12:16]!=b'IHDR'
                or int.from_bytes(data[8:12],'big')!=13):
            raise ValueError('invalid current PNG header')
        width,height=struct.unpack_from('>II',data,16)
        if (width,height)!=(82,71):
            raise ValueError('current main-grid texture dimensions changed')
        asset_hash=hashlib.sha256(data).hexdigest()
        if asset_hash!=expected_hash:
            raise ValueError('unexpected current PNG hash')
        textures.append({**record,'asset':record['text']+'.png',
                         'sha256':asset_hash,
                         'width':width,'height':height,'psd_override_present':False})
    methods=(0x77a,0x77c,0x7ea,0x90c,0x913,0x914,0x97b,0x702,0xed1,0xed2,
             0xed3,0xed4,0xed5,0xe1c,0xb3b,0xb3c,0xb3d,0xb3e,0xb40,
             0x644,0x646,0x80c,0x80d,0xcf1)
    imports=[row for row in metadata.rows.get(28,[]) if row[1]==(0x9f4<<1|1)]
    if (len(imports)!=1 or metadata.string(imports[0][2])!='IMG_Load'
            or metadata.string(metadata.rows[26][imports[0][3]-1][0])!='SDL2_image.dll'):
        raise ValueError('unexpected current native image loader')
    lighting=current_grid_string(payload,0xd2d21e4c)
    if lighting['text']!='atom.lighting':
        raise ValueError('unexpected current premultiply selector')
    return {'schema':1,'assembly_sha256':assembly_hash,'certification_eligible':False,
            'status':'conditional-static-asset-evidence-only',
            'scope':'Read-only resource decoding and PNG IHDR dimensions; no assembly load or game execution',
            'resource':{'payload_offset':payload_offset,'length':length,
                        'sha256':hashlib.sha256(payload).hexdigest()},
            'decoder':{'header_masks':['8a92','cb69'],'record_key_length':3,
                       'conditional_metadata_hash':'76af7af6e310bfc0',
                       'conditional_normal_state':'703c6bb8','key':'aa688a0f',
                       'argument_xor':'d2d32f94','flags_xor':'cfc9ad3a',
                       'normal_first_state_update':'(0 & 0x0fffff72) ^ 0x1a84',
                       'assumptions':['Same-assembly normal caller and stack guards succeed',
                                      'Runtime cache initialization uses zero before decoding',
                                      'No alternate initialization or anti-tamper branch']},
            'cache_integer_proof':current_cache_audit(metadata),
            'dimension_leaf_proof':current_dimension_leaf_audit(metadata),
            'dimension_wiring_proof':current_dimension_wiring_audit(metadata),
            'pixel_fixture_proof':current_pixel_audit(metadata),
            'loader_strings':loader_strings,'textures':textures,
            'dimension_flow':{
                'native_import':{'method_token':'060009f4','library':'SDL2_image.dll','entry_point':'IMG_Load'},
                'premultiply_selector':lighting,
                'managed_chain':['06000ed4: surface w/h -> 06000b3c',
                                 '06000b3c: Index2(w,h) -> 06000b3b',
                                 '06000b3b: Index2 -> 040009d4',
                                 '06000b40: vertical row swap only',
                                 '06000ed5: RGB premultiply only',
                                 '06000ed2: 06000b3d/06000b3e -> texture dimensions',
                                 '06000644: equal dimensions -> 0400022a',
                                 '0600080d: 06000646 -> 0600080c -> 04000449'],
                'external_assumption':'IMG_Load and SDL_ConvertSurfaceFormat preserve PNG IHDR dimensions'},
            'method_il_sha256':{f'{0x06000000|rid:08x}':hashlib.sha256(metadata.body(rid)).hexdigest()
                                for rid in methods},
            'unproved_obligations':['Actual runtime guard stack and cache initialization',
                                    'Native image loading and conversion dimension semantics',
                                    'Collision, continuous coverage, raster and metric equivalence']}

def production_bounds_audit(metadata):
    def method(index):
        return {'method_token':f'{0x06000000|index:08x}',
            'method_name':metadata.token_name(0x06000000|index),
            'method_il_sha256':hashlib.sha256(metadata.body(index)).hexdigest(),
            'references':metadata.references(index)}
    def region(index,start,stop):
        body=metadata.body(index);decoded=list(instructions(body));result=[]
        for n,(offset,opcode,token) in enumerate(decoded):
            if not start<=offset<stop:continue
            end=decoded[n+1][0] if n+1<len(decoded) else len(body)
            entry={'offset':offset,'opcode':hex(opcode),'bytes':body[offset:end].hex()}
            if token is not None:entry['token']=f'{token:08x}'
            if opcode==0x22:entry['float32']=struct.unpack('<f',body[offset+1:end])[0]
            if opcode in (0x2b,0x2c,0x2d):entry['branch_target']=end+struct.unpack('<b',body[offset+1:end])[0]
            result.append(entry)
        return result
    consumers=[]
    for index in range(1,len(metadata.rows[6])+1):
        if any(ref['token']=='0600117f' for ref in metadata.references(index)):
            consumers.append(method(index))
    return {'method':'version-locked static IL data flow; not a game replay',
        'computation':method(0xdff),
        'left_branch':region(0xdff,0x125,0x141),
        'right_branch':region(0xdff,0x14c,0x168),
        'derived_bounds_write':region(0xdff,0x174,0x179),
        'solution_bounds':method(0x117f),'solution_bounds_direct_consumers':consumers,
        'simulation_references':[
            {'method_token':f'{0x06000000|index:08x}','references':refs}
            for index in range(1,len(metadata.rows[6])+1)
            if metadata.type_name(metadata.owner[index])=='.Sim'
            and (refs:=[ref for ref in metadata.references(index)
                       if ref['token'] in {'04000415','04000416','04000417','06000dff','0600117f'}])]}

def production_audit(metadata):
    expected='fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d'
    digest=hashlib.sha256(metadata.data).hexdigest()
    if digest!=expected:raise ValueError('game build differs; production evidence must be re-established')
    watched={'04000414':'vial_records','0400069e':'vial_position','0400069f':'vial_style',
             '040006a0':'vial_visual_variants','04000415':'derived_production_bounds',
             '04000416':'shrink_left','04000417':'shrink_right'}
    evidence=[]
    for index in range(1,len(metadata.rows.get(6,[]))+1):
        references=[ref for ref in metadata.references(index) if ref['token'] in watched]
        if references:
            evidence.append({'method_token':f'{0x06000000|index:08x}',
                'method_name':metadata.token_name(0x06000000|index),
                'method_il_sha256':hashlib.sha256(metadata.body(index)).hexdigest(),
                'references':[{**reference,'field_role':watched[reference['token']]} for reference in references]})
    simulation=[index for index in range(1,len(metadata.rows[6])+1)
                if metadata.type_name(metadata.owner[index])=='.Sim']
    return {'schema':2,'assembly_sha256':digest,'method_definitions_inspected':len(metadata.rows[6]),
        'method_bodies_inspected':sum(row[0]!=0 for row in metadata.rows[6]),
        'simulation_methods':len(simulation),'simulation_methods_with_il':sum(metadata.rows[6][i-1][0]!=0 for i in simulation),
        'method': 'PE/CLI byte-only inspection; no game or external inspector execution',
        'scope':'static vial and production-display bounds data flow; not a game replay or proof of every production rule',
        'fields':watched,'references':evidence,'production_bounds':production_bounds_audit(metadata)}

def numerical_audit(metadata):
    expected='fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d'
    digest=hashlib.sha256(metadata.data).hexdigest()
    if digest!=expected:raise ValueError('game build differs; numerical evidence must be re-established')
    def method(index):
        body=metadata.body(index)
        return {'method_token':f'{0x06000000|index:08x}',
            'method_name':metadata.token_name(0x06000000|index),
            'method_il_sha256':hashlib.sha256(body).hexdigest(),
            'il_bytes':len(body),'references':metadata.references(index)}
    def region(index,start,stop):
        body=metadata.body(index);decoded=list(instructions(body));result=[]
        for n,(offset,opcode,token) in enumerate(decoded):
            if not start<=offset<stop:continue
            end=decoded[n+1][0] if n+1<len(decoded) else len(body)
            entry={'offset':offset,'opcode':hex(opcode)}
            if token is not None:entry['token']=f'{token:08x}'
            if opcode==0x22:
                entry['float32']=struct.unpack('<f',body[offset+1:end])[0]
                entry['operand_bytes']=body[offset+1:end].hex()
            if opcode==0x23:
                entry['float64']=struct.unpack('<d',body[offset+1:end])[0]
                entry['operand_bytes']=body[offset+1:end].hex()
            if opcode in (0x1f,0x20):entry['integer']=int.from_bytes(body[offset+1:end],'little',signed=True)
            result.append(entry)
        return result
    body=metadata.body(0x10d4);decoded=list(instructions(body));assignments=[]
    for n,(offset,opcode,token) in enumerate(decoded[:-1]):
        following=decoded[n+1]
        if opcode==0x22 and following[1]==0x80 and following[2] in range(0x04001139,0x0400113e):
            assignments.append({'literal_offset':offset,'operand_bytes':body[offset+1:offset+5].hex(),
                'float32':struct.unpack('<f',body[offset+1:offset+5])[0],
                'write_offset':following[0],'field_token':f'{following[2]:08x}'})
    return {'schema':1,'assembly_sha256':digest,
        'method':'Hash-locked PE/CLI byte-only inspection; no assembly load, game execution or native build',
        'scope':'Numerical constants and selected static IL data flow, not a game replay or full collision/area equivalence proof; no complete method bodies are bundled',
        'publication_evidence_level':'model-only; evidence below does not establish the existing fixed clearance or raster uncertainty envelopes',
        'reference_source':{
            'repository':'https://github.com/ianh/omsim',
            'revision':'a88d12d8d100784aa525ee90259255b869bcba38',
            'file':'collision.c','git_blob_sha1':'22a4555d82ff3bd38bc52473e1d05ba7310d4571',
            'sha256':'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5',
            'facts':{'hex_size_x':82,'hex_size_y':71,'atom_radius':29,'produced_atom_radius':15,'arm_base_radius':20},
            'evidence_kind':'Pinned upstream source, not original-game execution'},
        'original_game_static_il':{
            'simulation_initializer':method(0x10d4),'simulation_float_assignments':assignments,
            'collider_radius_selector':method(0x110f),
            'collider_radius_selector_instructions':region(0x110f,14,45),
            'collider_radius_semantics':'Constructor categories 0/1 select field 04001139; other categories select 0400113b. Full category meanings and integration coverage remain unproven here.',
            'collider_pair_filter_instructions':region(0x10e8,0x3fa,0x515),
            'collider_pair_filter_semantics':{
                'distance_gate':'Vector2.Distance(positionA,positionB) < radiusA+radiusB; category dispatch follows only below this strict threshold',
                'unordered_error_category_pairs':[[0,0],[1,1],[0,1],[0,3],[2,3]],
                'other_pairs':'No collision error emitted by this dispatch; area observation still follows the pair loop',
                'scope':'Static dispatch for constructed collider records only; category meanings, insertion completeness and float-distance equivalence are separate obligations'},
            'manual_radius_overrides':region(0x10e8,720,746)+region(0x10e8,891,930),
            'hex_mapper':method(0x793),
            'hex_mapper_selected_instructions':[entry for entry in region(0x793,0,60)
                if entry['opcode'] in ('0x7b','0x6b','0x22','0x5a','0x58')],
            'hex_mapper_semantics':'float32 scale.X*(float32 Q + 0.5f*float32 R), scale.Y*float32 R',
            'hex_mapper_scale_initializer':method(0x790),
            'main_grid_asset_write':{
                'method_token':'0600006b',
                'method_il_sha256':hashlib.sha256(metadata.body(0x6b)).hexdigest(),
                'instructions':region(0x6b,0xeaf,0xef7)},
            'four_texture_wrapper':method(0x8fa),
            'four_texture_size_getter':method(0x8fc),
            'main_grid_motion_consumer':region(0x10e8,0xdb,0xf9),
            'main_grid_area_consumer':region(0x10ed,0,0x39),
            'hex_mapper_scale_provenance':'Main singleton 040006bf reads dimensions through asset field 04000411 and method 060008fc. MainGridAssetEvidence.json separately closes the locked installation 82x71 binding via static resource decoding and PNG dimensions; reproduce with --audit-grid-assets. A second singleton 040006c0 is explicitly initialized from 66,57.',
            'angle_conversion':method(0xc9c),
            'angle_conversion_constants':[entry for entry in region(0xc9c,0,17) if 'float32'in entry or 'integer'in entry],
            'vector_rotation':method(0x1309),
            'vector_rotation_rounding':[entry for entry in region(0x1309,0,58)
                if entry['opcode'] in ('0x6b','0x6c') or entry.get('token') in ('0a00036f','0a0000ea')],
            'collision_step_computation':method(0x10e4),
            'collision_step_instructions':region(0x10e4,0x369,0x3ae),
            'collision_step_semantics':'Stores float32(min(0.125,0.25/(2**round(log(maximumHexRadius,2))))). 0.125 is a timestep cap in this method, not evidence for a 0.125-pixel clearance margin.',
            'collision_motion_consumer':method(0x10e8),
            'area_neighbour_consumer':method(0x10ed)},
        'mathematical_ledger':[
            {'claim':'The interval kernel encloses supplied finite double trajectory parameters using outward long-double operations and a Taylor remainder.',
             'kind':'Implementation/mathematical obligation','conditions':['IEEE arithmetic and no fast-math','Each floating operation enclosure audited','The caller-supplied trajectory is the intended model'],
             'does_not_prove':'Correct game trajectory construction, semantic radii, raster definition or original-game floating-point behavior'},
            {'claim':'Pixel mapping and ideal 60-degree hex rotation do not commute when y scale is 71 rather than 82*sqrt(3)/2.',
             'kind':'Mathematical fact',
             'example':{'hex_vector':[10,0],'angle_degrees':60,'pixel_then_euclidean_rotate_y':710.1408311032396,'hex_rotate_then_pixel_y':710.0,'absolute_y_difference_pixels':0.1408311032396},
             'does_not_prove':'That the game uses the other representation at the same execution phase; that data flow still needs tracing'}],
        'kernel_review':{
            'soundness_scope':'Supplied double trajectory parameters, not caller construction or original-game mapping',
            'positive_controls':['Outward nextafter around primitive long-double operations','Taylor remainder for sin/cos with absolute angle at most 8','Analytic relative-linear minimum','Same-center/angle/translation rigid-distance invariance','Uncertain on invalid input, fast-math or exhausted subdivision'],
            'subdivision_enclosure':'Rounded midpoint reach uses upward-rounded max(mid-low,high-mid); degenerate midpoints return Uncertain before interval-wide separation. Runtime certification requires nearest rounding and rejects x86 DAZ/FTZ. This does not close caller/game mapping obligations.',
            'confirmed_false_separated_counterexample':'Not established by this static audit; no runtime native/game equivalence claim follows'},
        'unproved_obligations':[
            'Meaning and insertion completeness of all collider categories, including explicit 24.0 and 20.0 overrides; pair dispatch is recorded separately but integration coverage remains unproved',
            'Complete original-game trajectory interpolation/snap data flow across half cycles and multiple rotations',
            'A uniform or per-trajectory error envelope justifying +/-0.125 pixels for the complete supported domain',
            'A proved envelope for .05+.03*radius+16*float_epsilon*coordinate_sum used by area rasterization',
            'Full game area/cost/instruction rules, including raster-phase membership and platform Math.Sin/Cos behavior',
            'Actual game outcomes for near-contact, generated atoms, wheel/base/gripper/wall and raster-boundary fixtures'],
        'minimal_game_fixture_contract':{
            'required_record':['game executable/build and asset hashes','puzzle and solution hashes','exact expected condition and evidence source','observed completion/collision result and cycle','observation/replay artifact provenance'],
            'cases':['translation versus rotation at equal nominal clearance','long off-axis bonded fragment through rotation and next-cycle snap','piston extension in all six directions','generated versus ordinary atom outlet near a body','wheel/arm-base/gripper and wall boundary contacts','area raster inclusion immediately below/at/above a threshold'],
            'status':'Not executed; original-game observation artifacts are still required'}}

def current_report_dispatch_audit(metadata):
    base=0x4b6
    signature=bytes.fromhex('2003010c0e1d119388')
    methods=(0x11e8,0x8c7,0x12a9)
    owners=(base,0x2b2,0x4c8)
    flags=(0x5c6,0xc6,0xc6)
    layouts=('', '02177dd70400042a',
             '032200000000320803220000803f3710203b66d2d2287c070006735404000a7a02287c1200060a120028b106000a2d2c02731f06000625037d0a02000425057d0b02000425047d0c02000428bf06000a287d12000602177d291200042a')
    descendants={base}
    while True:
        found={i for i,row in enumerate(metadata.rows[2],1)
               if len(row)>3 and row[3]&3==0 and row[3]>>2 in descendants}
        if found<=descendants:break
        descendants.update(found)
    if descendants!=set(owners):raise ValueError('report dispatch inheritance mismatch')
    name=metadata.string(metadata.rows[6][methods[0]-1][3])
    records=[]
    for method,owner,flag,layout in zip(methods,owners,flags,layouts):
        row=metadata.rows[6][method-1]
        start=metadata.rows[2][owner-1][5]
        end=metadata.rows[2][owner][5]
        if not start<=method<end or row[2]!=flag or row[1]!=0:
            raise ValueError('report dispatch owner or flags mismatch')
        if metadata.string(row[3])!=name or metadata.blob(row[4])!=signature:
            raise ValueError('report dispatch signature mismatch')
        body=metadata.body(method)
        if body.hex()!=layout or (method==methods[0] and row[0]!=0):
            raise ValueError('report dispatch body mismatch')
        matches=[rid for rid in range(start,end)
                 if metadata.string(metadata.rows[6][rid-1][3])==name
                 and metadata.blob(metadata.rows[6][rid-1][4])==signature]
        if matches!=[method]:raise ValueError('report dispatch ambiguous slot')
        records.append({'method_token':f'{0x06000000|method:08x}',
                        'owner_token':f'{0x02000000|owner:08x}','flags':f'{flag:04x}',
                        'body_sha256':hashlib.sha256(body).hexdigest(),'il':layout})
    for owner,body,declaration in metadata.rows.get(25,[]):
        if owner in descendants and any(v&1 or v>>1 in methods for v in (body,declaration)):
            raise ValueError('report dispatch explicit override requires audit')
    return {'signature':signature.hex(),'methods':records,
            'slot':'Base abstract virtual newslot; derived virtual reuseslot; no explicit target override',
            'scope':'Closed metadata hierarchy only; reflection, runtime dispatch and report state effects remain unproved'}

def current_report_optional_audit(metadata):
    owner=0x3de
    row=metadata.rows[2][owner-1]
    following=metadata.rows[2][owner]
    if (row[0],row[3],row[4],following[4],row[5],following[5])!=(1048841,913,2994,2996,3423,3441):
        raise ValueError('report optional type layout mismatch')
    parameters=[(rid,r) for rid,r in enumerate(metadata.rows.get(42,[]),1) if r[2]==owner<<1]
    if (len(parameters)!=1 or parameters[0][1][:3]!=[0,0,owner<<1]
            or any(r[0]==parameters[0][0] for r in metadata.rows.get(44,[]))):
        raise ValueError('report optional generic parameter mismatch')
    for spec,signature in ((0x38,'15118f78011300'),(0x39,'1300'),(0x70,'15118f7801118f04'),(0x1fe,'15118f78011287f8')):
        if metadata.blob(metadata.rows[27][spec-1][0]).hex()!=signature:
            raise ValueError('report optional type specification mismatch')
    methods=((0xd5f,6278,'200201021300','0203287d04000a02047d7e04000a2a'),
             (0xd60,134,'200002','027b7f04000a2a'),
             (0xd61,129,'20010102','02037d7f04000a2a'),
             (0xd64,150,'000115118f780113001300','028c3900001b2d102024f0d3d2287c07000673d002000a7a1702738004000a2a'),
             (0xd65,2198,'000115118f780113001300','02288104000a2a'),
             (0xd66,2198,'000115118f78011300118cf0','161200fe153900001b06738004000a2a'))
    for method,flags,signature,body in methods:
        item=metadata.rows[6][method-1]
        if (not row[5]<=method<following[5] or metadata.owner.get(method)!=owner
                or item[1:3]!=[0,flags] or metadata.blob(item[4]).hex()!=signature
                or metadata.body(method).hex()!=body
                or any(r[2]==(method<<1|1) for r in metadata.rows.get(42,[]))):
            raise ValueError('report optional method mismatch')
    for method,header in ((0xd5f,'3e'),(0xd60,'1e'),(0xd61,'22'),(0xd64,'82'),(0xd65,'1e')):
        if metadata.tiny_header(method).hex()!=header:
            raise ValueError('report optional header or locals mismatch')
    if (metadata.fat_header(0xd66).hex()!='133002001000000060010011'
            or metadata.blob(metadata.rows[17][0x160-1][0]).hex()!='07011300'):
        raise ValueError('report optional header or locals mismatch')
    for field,signature in ((0xbb2,'0602'),(0xbb3,'061300')):
        item=metadata.rows[4][field-1]
        if (metadata.field_owner.get(field)!=owner or item[0]!=1
                or metadata.blob(item[2]).hex()!=signature):
            raise ValueError('report optional field mismatch')
    for member,parent,table,target in ((0x47d,452,6,0xd61),(0x47e,452,4,0xbb3),
            (0x47f,452,4,0xbb2),(0x480,452,6,0xd5f),(0x481,452,6,0xd64),
            (0x377,900,6,0xd65),(0x6b1,4084,6,0xd60),(0x6bf,4084,6,0xd65),(0x6ba,4084,6,0xd66)):
        item=metadata.rows[10][member-1]
        target_row=metadata.rows[table][target-1]
        name_index,signature_index=(3,4) if table==6 else (1,2)
        if (item[0]!=parent or metadata.string(item[1])!=metadata.string(target_row[name_index])
                or metadata.blob(item[2])!=metadata.blob(target_row[signature_index])):
            raise ValueError('report optional member binding mismatch')
    consumer=metadata.rows[6][0xb54-1]
    if (not metadata.rows[2][0x379-1][5]<=0xb54<metadata.rows[2][0x379][5]
            or metadata.owner.get(0xb54)!=0x379
            or consumer[1:3]!=[0,134]
            or metadata.blob(consumer[4]).hex()!='200115118f7801118f0408'
            or metadata.body(0xb54).hex()!='027be709000403a3c1030002287703000a2a'
            or metadata.tiny_header(0xb54).hex()!='4a'
            or any(r[2]==(0xb54<<1|1) for r in metadata.rows.get(42,[]))):
        raise ValueError('report optional consumer binding or body mismatch')
    for method in [m[0] for m in methods]+[0xb54]:
        if metadata.exception_sections(method):
            raise ValueError('report optional exception sections mismatch')
    return {'type_token':'020003de','method_tokens':[f'{0x06000000|m[0]:08x}' for m in methods],
            'consumer_method_token':'06000b54','consumer_conversion_member_token':'0a000377',
            'field_tokens':['04000bb2','04000bb3'],
            'scope':'Optional storage and conversion reference bytes only; conversion exception dependencies, consumers and collision failure semantics remain unproved'}

def current_area_candidates_audit(metadata):
    methods=(
        (0x1124,0x495,129,'200101119388','d73ba063621c36c3be245d05297ab37fe59622571528db3eea88b0b2fdc517cf'),
        (0x810,0x285,134,'2001119388118ef8','1e96ba9802445202adbd442d0d9d6afcda8e53559f84ef7f9e3458e40d9a5bce'),
        (0x80f,0x285,134,'2002119388118ef8119388','9360eb019d79a65f616ae9e42a85d517e689ac5d6468a3eb28c4db0c87af4281'),
        (0x811,0x285,134,'2002118ef8119388119388','c602d73224f53539723fe2affe54ccc3d109960c5f4af4f456aa1e86772f41a2'),
        (0xca6,0x3be,6278,'2002010808','5a2d143fff288e8d86dc477c9b3336926cf54e103a4daeb620fd63da68666181'),
        (0xcaf,0x3be,2198,'0002118ef8118ef8118ef8','752b6cababcaf0f93d6132a72c84ae370ca14eece5c342e077bae6481c28a67c'),
        (0xca7,0x3be,6289,'000001','8f9c770c3768da86b895144f8c8eb5e7ddeef74e507db6c47d79612166f7a4bb'),
        (0x80c,0x285,6278,'200101118f24','88369f793fd6881e3016cd68c233e416d2af817d75d798a0ff912ecad73d274c'),
        (0xcf1,0x3c9,134,'2000119388','aacfe16228c07ba62829de8848132b88bc9619f9c2a89af68364ccf69a57b2db'))
    records=[]
    for method,owner,flags,signature,digest in methods:
        row=metadata.rows[6][method-1]
        if (not metadata.rows[2][owner-1][5]<=method<metadata.rows[2][owner][5]
                or row[1]!=0 or row[2]!=flags
                or metadata.blob(row[4]).hex()!=signature
                or hashlib.sha256(metadata.body(method)).hexdigest()!=digest):
            raise ValueError('area candidates method binding or body mismatch')
        records.append({'method_token':f'{0x06000000|method:08x}',
                        'owner_token':f'{0x02000000|owner:08x}',
                        'signature':signature,'il_sha256':digest})
    if (metadata.fat_header(0x1124).hex()!='133003006d000000d2010011'
            or metadata.blob(metadata.rows[17][0x1d2-1][0]).hex()!='0706118ef81d118ef808118ef8118ef8119388'):
        raise ValueError('area candidates header or locals mismatch')
    fields=((0x449,0x285,22,'06128a14'),(0x44b,0x285,38,'06119388'),
            (0xb23,0x3be,38,'0608'),(0xb24,0x3be,38,'0608'),
            (0xb26,0x3be,54,'061d118ef8'),(0x1135,0x495,6,'061512811101118ef8'),
            (0xb54,0x3c9,6,'0608'),(0xb55,0x3c9,6,'0608'),
            (0x12b5,0x4e2,22,'06119388'),(0x113d,0x495,49,'060c'))
    for field,owner,flags,signature in fields:
        row=metadata.rows[4][field-1]
        if (not metadata.rows[2][owner-1][4]<=field<metadata.rows[2][owner][4]
                or row[0]!=flags or metadata.blob(row[2]).hex()!=signature):
            raise ValueError('area candidates field binding mismatch')
    if metadata.blob(metadata.rows[27][0x3d-1][0]).hex()!='1512811101118ef8':
        raise ValueError('area candidates HashSet type mismatch')
    for member,parent,name,signature in ((0x133,492,'Add','2001021300'),
            (0x15b,1225,'Round','00010d0d'),(0x20a,1225,'Abs','00010c0c')):
        row=metadata.rows[10][member-1]
        if row[0]!=parent or metadata.string(row[1])!=name or metadata.blob(row[2]).hex()!=signature:
            raise ValueError('area candidates member binding mismatch')
    row=metadata.rows[1][0x44-1]
    scope=metadata.rows[35][2]
    if (row[0]!=14 or metadata.string(row[1])!='HashSet`1'
            or metadata.string(row[2])!='System.Collections.Generic'
            or scope[:5]!=[4,0,0,0,0] or metadata.string(scope[6])!='System.Core'
            or metadata.blob(scope[5]).hex()!='b77a5c561934e089'
            or metadata.string(scope[7])!='' or metadata.blob(scope[8])!=b''):
        raise ValueError('area candidates HashSet assembly mismatch')
    return {'methods':records,'candidate_offsets':[[0,0],[1,0],[0,1],[-1,1],[-1,0],[0,-1],[1,-1]],
            'selection':'Distance(input, mapped candidate) < 2 * field 0400113d; bge.un.s skips greater, equal and unordered',
            'mapping_storage':'0600080c stores 06000cf1 integer-to-binary32 vector conversion in instance field 0400044b',
            'scope':'Local static bytes only; mapping asset values, runtime rounding, seven-cell completeness, continuous coverage and metric equivalence remain unproved'}

def current_report_state_audit(metadata):
    methods=((0x127c,0x4c8,0x86,'200015118f78011287f8','027b281200042a'),
             (0x127d,0x4c8,0x81,'20010115118f78011287f8','02037d281200042a'),
             (0x61f,0x1fe,0x1886,'200001','02283800000a2a'),
             (0x8ba,0x2b2,0x8c6,'200002','027bd70400042a'),
             (0x1283,0x4c8,0x8c6,'200002','02287c1200060a120028b106000a2a'))
    records=[]
    for method,owner,flags,signature,body in methods:
        row=metadata.rows[6][method-1]
        start=metadata.rows[2][owner-1][5]
        end=metadata.rows[2][owner][5]
        if not start<=method<end or row[1]!=0 or row[2]!=flags:
            raise ValueError('report state method owner or flags mismatch')
        if metadata.blob(row[4]).hex()!=signature or metadata.body(method).hex()!=body:
            raise ValueError('report state method signature or body mismatch')
        records.append({'method_token':f'{0x06000000|method:08x}',
                        'owner_token':f'{0x02000000|owner:08x}',
                        'signature':signature,'il':body})
    if (metadata.fat_header(0x1283).hex()!='133001000f00000033020011'
            or metadata.blob(metadata.rows[17][0x233-1][0]).hex()!='070115118f78011287f8'):
        raise ValueError('report state header or locals mismatch')
    fields=((0x1228,0x4c8,1,'0615118f78011287f8'),
            (0x1229,0x4c8,1,'0602'),(0x4d7,0x2b2,1,'0602'),
            (0x20a,0x1fe,6,'060c'),(0x20b,0x1fe,6,'061d119388'),
            (0x20c,0x1fe,6,'060e'))
    field_records=[]
    for field,owner,flags,signature in fields:
        row=metadata.rows[4][field-1]
        start=metadata.rows[2][owner-1][4]
        end=metadata.rows[2][owner][4]
        if not start<=field<end or row[0]!=flags or metadata.blob(row[2]).hex()!=signature:
            raise ValueError('report state field owner or signature mismatch')
        field_records.append({'field_token':f'{0x04000000|field:08x}',
                              'owner_token':f'{0x02000000|owner:08x}',
                              'signature':signature})
    return {'methods':records,'fields':field_records,
            'scope':'Direct accessor and storage bytes only; optional conversion, consumers and collision failure semantics remain unproved'}

def current_report_wrapper_audit(metadata):
    owner=metadata.rows[2][0x21f-1]
    following=metadata.rows[2][0x21f]
    if (owner[0]!=1048832 or owner[3:]!=[641,623,1676]
            or following[4:]!=[624,1681]
            or [row[:3] for row in metadata.rows[42] if row[2]==0x21f<<1]!=[[0,0,0x21f<<1]]):
        raise ValueError('report wrapper owner or generic parameter mismatch')
    for rid,signature in ((0x134,'1512887c01128f30'),(0x5d,'1512813d011300'),
                          (0x5e,'1512887c011300')):
        if metadata.blob(metadata.rows[27][rid-1][0]).hex()!=signature:
            raise ValueError('report wrapper type specification mismatch')
    for rid,name,namespace in ((0x4f,'List`1','System.Collections.Generic'),
                               (0x7d,'InvalidOperationException','System')):
        row=metadata.rows[1][rid-1]
        if row[0]!=6 or metadata.string(row[1])!=name or metadata.string(row[2])!=namespace:
            raise ValueError('report wrapper external type mismatch')
    scope=metadata.rows[35][0]
    if (scope[:5]!=[4,0,0,0,0] or metadata.string(scope[6])!='mscorlib'
            or metadata.blob(scope[5]).hex()!='b77a5c561934e089'
            or metadata.string(scope[7])!='' or metadata.blob(scope[8])!=b''):
        raise ValueError('report wrapper assembly identity mismatch')
    field=metadata.rows[4][0x26f-1]
    screen_field=metadata.rows[4][0xafc-1]
    if not metadata.rows[2][0x3b7-1][4]<=0xafc<metadata.rows[2][0x3b7][4]:
        raise ValueError('report wrapper screen field owner mismatch')
    if (field[0]!=38 or metadata.blob(field[2]).hex()!='061512813d011300'
            or screen_field[0]!=1 or metadata.blob(screen_field[2]).hex()!='061512887c01128f30'):
        raise ValueError('report wrapper field mismatch')
    members=((0xdc,748,'.ctor','200001'),
             (0xdd,756,metadata.string(field[1]),'061512813d011300'),
             (0xde,748,'get_Count','200008'),(0xdf,748,'Add','2001011300'),
             (0xe0,1001,'.ctor','2001010e'),(0xe1,748,'get_Item','2001130008'),
             (0xe2,748,'RemoveAt','20010108'),
             (0x40f,2468,metadata.string(metadata.rows[6][0x690-1][3]),'20001300'))
    member_records=[]
    for rid,parent,name,signature in members:
        row=metadata.rows[10][rid-1]
        if row[0]!=parent or metadata.string(row[1])!=name or metadata.blob(row[2]).hex()!=signature:
            raise ValueError('report wrapper member binding mismatch')
        member_records.append({'member_token':f'{0x0a000000|rid:08x}',
                               'parent_coded_index':parent,'signature':signature})
    methods=((0x68c,6278,'200001','0273dc00000a7ddd00000a02283800000a2a'),
             (0x68d,134,'200008','027bdd00000a6fde00000a2a'),
             (0x68e,134,'2001011300','027bdd00000a036fdf00000a2a'),
             (0x68f,134,'20001300','027bdd00000a6fde00000a2d1020d178d2d2287c07000673e000000a7a027bdd00000a6fde00000a17590a027bdd00000a066fe100000a027bdd00000a066fe200000a2a'),
             (0x690,134,'20001300','027bdd00000a6fde00000a2d1020d178d2d2287c07000673e000000a7a027bdd00000a027bdd00000a6fde00000a17596fe100000a2a'))
    records=[]
    for rid,flags,signature,body in methods:
        row=metadata.rows[6][rid-1]
        if (row[1]!=0 or row[2]!=flags or metadata.blob(row[4]).hex()!=signature
                or metadata.body(rid).hex()!=body):
            raise ValueError('report wrapper method metadata or body mismatch')
        records.append({'method_token':f'{0x06000000|rid:08x}','signature':signature,'il':body})
    return {'owner_token':'0200021f','screen_field_token':'04000afc',
            'storage_field_token':'0400026f','members':member_records,'methods':records,
            'scope':'Generic wrapper binding and method bytes only; external library behavior, initialization, runtime dispatch and exception semantics remain unproved'}

def current_report_reset_audit(metadata):
    methods=((0x1293,0x4c8,0x86,'200001',197,'006d0d97993b6c8bd9899f93418059fea3123dcd085efb1b944afaae2d49d607'),
             (0x128e,0x4c8,0x81,'200002',17,'285943e445b9cdeba41b24e10466201e7069a0290f3fb50e527785ba2fe3b2cd'),
             (0x11e1,0x4b6,0x5c6,'20001189c4',0,'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'),
             (0x1295,0x4c8,0x81,'2001011189c4',50,'6af9d46117c516593d3844aca1799972c275301c6c3842710d119eca744c4eb6'))
    records=[]
    for method,owner,flags,signature,size,digest in methods:
        row=metadata.rows[6][method-1]
        if (not metadata.rows[2][owner-1][5]<=method<metadata.rows[2][owner][5]
                or row[1]!=0 or row[2]!=flags
                or metadata.blob(row[4]).hex()!=signature):
            raise ValueError('report reset method metadata mismatch')
        body=metadata.body(method)
        if (len(body)!=size or hashlib.sha256(body).hexdigest()!=digest
                or (method==0x11e1 and row[0]!=0)):
            raise ValueError('report reset method body mismatch')
        records.append({'method_token':f'{0x06000000|method:08x}',
                        'owner_token':f'{0x02000000|owner:08x}',
                        'signature':signature,'il_size':size,'il_sha256':digest})
    if metadata.fat_header(0x1293).hex()!='03300200c500000000000000':
        raise ValueError('report reset header mismatch')
    helpers=((0xc6b,0x3b7,0x86,'2000128f30','027bfc0a00046f0f04000a2a'),
             (0xae7,0x369,0x96,'00020e0e1d1c','282d03000a0203282e03000a2a'),
             (0x9dc,0x337,0x1886,'2001010e','0203282b02000a2a'))
    helper_records=[]
    for method,owner,flags,signature,body in helpers:
        row=metadata.rows[6][method-1]
        if (not metadata.rows[2][owner-1][5]<=method<metadata.rows[2][owner][5]
                or row[1]!=0 or row[2]!=flags
                or metadata.blob(row[4]).hex()!=signature
                or metadata.body(method).hex()!=body):
            raise ValueError('report reset helper metadata or body mismatch')
        helper_records.append({'method_token':f'{0x06000000|method:08x}',
                               'owner_token':f'{0x02000000|owner:08x}',
                               'signature':signature,'il':body})
    return {'methods':records,'helpers':helper_records,
            'early_return':{'predicate_call_offset':1,'branch_offset':6,'return_offset':8},
            'abstract_precondition':{'call_offset':10,'method_token':'060011e1',
                                     'nonzero_branch_target':24,'failure_call_offset':19},
            'empty_report_assignment':{'load_offset':75,'conversion_offset':80,'setter_offset':85,
                                       'setter_token':'0600127d'},
            'failure_helper':{'method_token':'06001295','newobj_offset':44,'throw_offset':49},
            'scope':'Conditional reset bytes only, not unconditional clearing. Runtime dispatch, preceding calls, exception construction and downstream consumers remain unproved'}

def collider_sampling_loop(body, update_offset, condition_offset):
    entry=bytes.fromhex('281a1300060a027b381100040b38')+struct.pack('<i',condition_offset-18)
    tail=(bytes.fromhex('07027b38110004580b07220000803f3e')+
          struct.pack('<i',18-(condition_offset+11)))
    if body[:18]!=entry or body[update_offset:condition_offset+11]!=tail:
        raise ValueError('unsupported collider sampling loop bytes')
    branches=branch_targets(body)
    expected=[(13,condition_offset),(condition_offset+6,18)]
    for offset,target in expected:
        if not any(branch['offset']==offset and branch['targets']==[target]
                   for branch in branches):
            raise ValueError('unsupported collider sampling loop branch')
    return {'entry_branch_offset':13,'body_entry_offset':18,
            'update_offset':update_offset,'condition_offset':condition_offset,
            'backedge_offset':condition_offset+6,'step_field_token':'04001138',
            'entry_il':entry.hex(),'update_and_condition_il':tail.hex(),
            'normal_expression':'t = field; while (t <= 1.0f) { body; t = t + field; }',
            'scope':'Exact entry, increment and ordered ble condition only; positive finite step, progress, body effects, exceptions and update timing remain unproved'}

def local_control_flow_evidence(body, local, initializer):
    """Inspect local accesses and dominance in the explicit, normal IL graph only."""
    decoded=list(instructions(body))
    branches={item['offset']:item['targets'] for item in branch_targets(body)}
    successors={};accesses=[]
    for index,(offset,opcode,_) in enumerate(decoded):
        end=decoded[index+1][0] if index+1<len(decoded) else len(body)
        edges=list(branches.get(offset,[]))
        if opcode not in (0x27,0x2a,0x2b,0x38,0x7a,0xdc,0xdd,0xde,0xfe11,0xfe1a) and end<len(body):
            edges.append(end)
        successors[offset]=edges
        slot=None;kind=None
        if 0x06<=opcode<=0x09:slot=opcode-0x06;kind='read'
        elif 0x0a<=opcode<=0x0d:slot=opcode-0x0a;kind='write'
        elif opcode in (0x11,0x12,0x13):
            slot=body[offset+1];kind={0x11:'read',0x12:'address',0x13:'write'}[opcode]
        elif opcode in (0xfe0c,0xfe0d,0xfe0e):
            slot=int.from_bytes(body[offset+2:offset+4],'little')
            kind={0xfe0c:'read',0xfe0d:'address',0xfe0e:'write'}[opcode]
        if slot==local:accesses.append({'offset':offset,'kind':kind})
    if not decoded or initializer not in successors:
        raise ValueError('invalid local initializer offset')
    if {'offset':initializer,'kind':'write'} not in accesses:
        raise ValueError('local initializer is not a matching store')
    def reachable(blocked=None):
        pending=[decoded[0][0]];seen=set()
        while pending:
            offset=pending.pop()
            if offset==blocked or offset in seen:continue
            seen.add(offset);pending.extend(successors[offset])
        return seen
    reached=reachable();without_initializer=reachable(initializer)
    return {'local':local,'initializer_offset':initializer,'accesses':accesses,
            'initializer_reachable':initializer in reached,
            'reads_or_addresses_not_dominated':[
                item['offset'] for item in accesses if item['kind']!='write'
                and item['offset'] in reached and item['offset'] in without_initializer],
            'unreachable_accesses':[item['offset'] for item in accesses if item['offset'] not in reached],
            'scope':'Explicit normal-control-flow graph only; leave edges bypass finally handlers. No EH, stack/type validation, value range, call effects, field lifecycle or termination proof'}


def field_store_return_dominance(body, sections, store, window_start=None):
    """Check an EH-free constructor's explicit normal paths to return."""
    if sections:
        raise ValueError('field initialization constructor has exception sections')
    decoded=list(instructions(body))
    branches={item['offset']:item['targets'] for item in branch_targets(body)}
    if not decoded or not any(offset==store and opcode==0x7d for offset,opcode,_ in decoded):
        raise ValueError('field initialization store is not an instruction')
    if window_start is not None:
        window=[offset for offset,_,_ in decoded if window_start<=offset<=store]
        if not window or window[0]!=window_start or window[-1]!=store:
            raise ValueError('field initialization window is not instruction aligned')
        for source,targets in branches.items():
            if window_start<=source<store or any(window_start<target<=store for target in targets):
                raise ValueError('field initialization branch crosses value window')
    successors={};returns=[]
    for index,(offset,opcode,_) in enumerate(decoded):
        if opcode in (0x27,0xdc,0xdd,0xde,0xfe11,0xfe1a,
                      0xfe12,0xfe13,0xfe14,0xfe16,0xfe19,0xfe1e):
            raise ValueError('unsupported field initialization control flow')
        end=decoded[index+1][0] if index+1<len(decoded) else len(body)
        edges=list(branches.get(offset,[]))
        if opcode not in (0x2a,0x2b,0x38,0x7a):
            if end==len(body):
                raise ValueError('field initialization control flow falls off method')
            edges.append(end)
        if opcode==0x2a:returns.append(offset)
        successors[offset]=edges
    def reachable(blocked=None):
        pending=[decoded[0][0]];seen=set()
        while pending:
            offset=pending.pop()
            if offset==blocked or offset in seen:continue
            seen.add(offset);pending.extend(successors[offset])
        return seen
    reached=reachable();without_store=reachable(store)
    reached_returns=[offset for offset in returns if offset in reached]
    if store not in reached or not reached_returns or any(offset in without_store for offset in reached_returns):
        raise ValueError('field initialization does not dominate normal returns')
    return {'store_offset':store,'reachable_return_count':len(reached_returns),
            'scope':'EH-free explicit normal control flow only; calls may throw or have side effects. No stack/type, receiver identity, publication, reentrancy, termination or lifecycle proof'}


def local_finally_control_flow_evidence(body, sections, local, initializer):
    """Overapproximate finally-only IL, except the nonthrowing initializer store."""
    normal=local_control_flow_evidence(body,local,initializer)
    decoded=list(instructions(body))
    offsets={offset for offset,_,_ in decoded}
    clauses=[clause for section in sections for clause in section['clauses']]
    for clause in clauses:
        if clause['flags']!=2 or clause['extra']!=0:
            raise ValueError('only finally clauses supported')
        for prefix in ('try','handler'):
            start=clause[prefix+'_offset'];end=start+clause[prefix+'_length']
            if start not in offsets or end not in offsets|{len(body)} or end<=start:
                raise ValueError('invalid finally region boundary')
    regions=[(item['try_offset'],item['try_offset']+item['try_length']) for item in clauses]
    for index,(start,end) in enumerate(regions):
        for other_start,other_end in regions[index+1:]:
            if max(start,other_start)<min(end,other_end) and not (
                    start<=other_start and other_end<=end or other_start<=start and end<=other_end):
                raise ValueError('overlapping finally try regions')
            if (start,end)==(other_start,other_end):
                raise ValueError('ambiguous finally try regions')
    branches={item['offset']:item['targets'] for item in branch_targets(body)}
    operations={offset:(opcode,decoded[index+1][0] if index+1<len(decoded) else len(body))
                for index,(offset,opcode,_) in enumerate(decoded)}
    def membership(offset):
        return frozenset((index,prefix) for index,item in enumerate(clauses)
                         for prefix in ('try','handler')
                         if item[prefix+'_offset']<=offset<item[prefix+'_offset']+item[prefix+'_length'])
    handlers=[(item['handler_offset'],item['handler_offset']+item['handler_length']) for item in clauses]
    for index,(start,end) in enumerate(handlers):
        own_start,own_end=regions[index]
        if max(start,own_start)<min(end,own_end):
            raise ValueError('finally handler overlaps its own try')
        for other_index,(outer_start,outer_end) in enumerate(regions):
            if other_index!=index and outer_start<=own_start<own_end<=outer_end:
                if not outer_start<=start<end<=outer_end:
                    raise ValueError('nested finally handler outside enclosing try')
        for other_start,other_end in handlers[index+1:]:
            if max(start,other_start)<min(end,other_end):
                raise ValueError('nested or overlapping finally handlers unsupported')
        for try_start,try_end in regions:
            if max(start,try_start)<min(end,try_end) and not try_start<=start<end<=try_end:
                raise ValueError('try inside handler or partial overlap unsupported')
    for offset,(opcode,end) in operations.items():
        targets=list(branches.get(offset,[]))
        if opcode in (0xdd,0xde):
            if any(kind=='handler' for _,kind in membership(offset)):
                raise ValueError('leave inside finally unsupported')
            if not membership(targets[0])<=membership(offset):
                raise ValueError('leave enters protected region')
        else:
            for target in targets:
                if membership(offset)!=membership(target):raise ValueError('branch crosses protected region')
            if opcode not in (0x27,0x2a,0x2b,0x38,0x7a,0xdc,0xfe11,0xfe1a) and end<len(body):
                if not membership(offset)<=membership(end) or any(
                        kind=='handler' for _,kind in membership(end)-membership(offset)):
                    raise ValueError('fallthrough crosses protected region')
        if opcode==0xdc and not any(kind=='handler' for _,kind in membership(offset)):
            raise ValueError('endfinally outside handler')
        if opcode in (0x27,0x2a,0xfe11,0xfe1a) and membership(offset):
            raise ValueError('unsupported protected-region terminator')
    def unwind(offset,target):
        active=[item for item in clauses
                if item['try_offset']<=offset<item['try_offset']+item['try_length']
                and (target is None or not item['try_offset']<=target<item['try_offset']+item['try_length'])]
        active.sort(key=lambda item:item['try_length'])
        return tuple(item['handler_offset'] for item in active)+(target,)
    def enter(chain):
        return [] if chain[0] is None else [(chain[0],chain[1:])]
    def reachable(blocked=None):
        pending: list[tuple[int, tuple[int | None, ...]]] = [(decoded[0][0],())]
        seen=set()
        while pending:
            offset,continuation=pending.pop()
            state=(offset,continuation)
            if state in seen:continue
            if len(seen)>=100000:raise ValueError('finally control-flow state budget exceeded')
            if offset not in operations:raise ValueError('invalid finally control-flow target')
            if offset!=blocked:seen.add(state)
            opcode,end=operations[offset]
            # Only the initializer store is treated as non-throwing in this model.
            if offset==blocked:continue
            # Other instructions conservatively may throw, superseding continuations.
            if offset!=initializer:pending.extend(enter(unwind(offset,None)))
            if opcode in (0xdd,0xde):
                pending.extend(enter(unwind(offset,branches[offset][0])))
            elif opcode==0xdc:
                if not continuation:raise ValueError('endfinally without continuation')
                pending.extend(enter(continuation))
            elif opcode not in (0x27,0x2a,0x7a,0xfe11,0xfe1a):
                targets=list(branches.get(offset,[]))
                if opcode not in (0x2b,0x38) and end<len(body):targets.append(end)
                pending.extend((target,continuation) for target in targets)
        return {offset for offset,_ in seen},len(seen)
    reached,state_count=reachable()
    without_initializer,_=reachable(initializer)
    return {'local':local,'initializer_offset':initializer,'state_count':state_count,
            'initializer_reachable':initializer in reached,
            'reads_or_addresses_not_dominated':[
                item['offset'] for item in normal['accesses'] if item['kind']!='write'
                and item['offset'] in reached and item['offset'] in without_initializer],
            'unreachable_accesses':[item['offset'] for item in normal['accesses'] if item['offset'] not in reached],
            'scope':'Finally-only diagnostic overapproximation: every instruction except the initializing local store may throw; leave and exceptional unwind carry separate continuations. Nested handlers and tries inside handlers are rejected; bounded to 100000 states. No catch/filter/fault, IL validity, stack/type, callee effects, value, lifecycle or termination proof'}


def collider_distance_calculation(metadata):
    layouts={0xcb4:('000208118ef8118ef8',
                    '027b230b0004037b230b000459285c02000a027b240b0004037b240b000459285c02000a580f0028a80c00060f0128a80c000659285c02000a58185b2a'),
             0xca8:('200008','027b230b000465027b240b0004592a')}
    for method,(signature,layout) in layouts.items():
        row=metadata.rows[6][method-1]
        expected_flags=150 if method==0xcb4 else 2182
        if (row[1:3]!=[0,expected_flags] or metadata.blob(row[4]).hex()!=signature or
                metadata.body(method).hex()!=layout or metadata.exception_sections(method)):
            raise ValueError('current collider distance method mismatch')
    owner=metadata.rows[2][0x3be-1]
    successor=metadata.rows[2][0x3be]
    if (owner[4:6]!=[0xb23,0xca6] or successor[4:6]!=[0xb27,0xcbb]):
        raise ValueError('current collider distance ownership mismatch')
    for field in (0xb23,0xb24):
        row=metadata.rows[4][field-1]
        if row[0]!=38 or metadata.blob(row[2]).hex()!='0608':
            raise ValueError('current collider distance field mismatch')
    absolute=metadata.rows[10][0x25c-1]
    if (absolute[0]!=1225 or metadata.string(absolute[1])!='Abs' or
            metadata.blob(absolute[2]).hex()!='00010808'):
        raise ValueError('current collider distance Abs mismatch')
    return {'method_token':'06000cb4','implied_s_method_token':'06000ca8',
            'coordinate_field_tokens':['04000b23','04000b24'],
            'expression':'(Abs(a.Q-b.Q) + Abs(a.R-b.R) + Abs((-a.Q-a.R)-(-b.Q-b.R))) / 2',
            'arithmetic':'Int32 unchecked neg/sub/add and signed div; Abs(Int32.MinValue) throws under the standard mscorlib contract',
            'range_warning':'Unchecked differences and sums can wrap; this method alone does not establish a nonnegative mathematical hex distance',
            'conditional_coordinate_bound':{'maximum_absolute_q_or_r':268435455,
                'proof':'For |Q|,|R|<=B, implied S is bounded by 2B, Q/R differences by 2B, S difference by 4B, and the sum of absolute differences by 8B<=Int32.MaxValue. All intermediates are representable and the even numerator gives the mathematical distance.',
                'caller_domain_established':False},
            'overflow_counterexample':{'a':[1073741824,0],'b':[0,0],
                'unchecked_result':-1073741824,'mathematical_distance':1073741824,
                'reachable_in_game_established':False},
            'scope':'Exact method bodies, signatures, field/method ownership and Abs member checked; enclosing Math identity, input bounds, runtime binding and callers remain separate obligations'}

def collider_step_calculation(body):
    tail=bytes.fromhex('230000000000000040086c230000000000000040285d02000a285b01000a282802000a0d0223000000000000c03f23000000000000d03f095b28ca05000a6b7d381100042a')
    if len(body)!=942 or body[873:]!=tail:
        raise ValueError('unsupported collider step calculation bytes')
    list(instructions(body))
    return {'method_token':'0600111b','expression_offset':873,'store_offset':936,
            'step_field_token':'04001138','tail_il':tail.hex(),
            'normal_expression':'field = (float)Min(0.125, 0.25 / Pow(2, Round(Log((double)local2, 2))))',
            'conditional_binary32_progress':{
                'assumptions':'local2 is a positive Int32; Log/Round/Pow return the expected nearest-power exponent k in [0,31]; binary32 nearest-even addition; unchanged field',
                'step':'2^-max(3,k+2)',
                'progress_through_one_if':'k <= 21',
                'stagnation_if':'k >= 22: at t = 2^(22-k), t + step rounds back to t',
                'counterexample':{'local2':1073741824,'k':30,'step':2.0**-32,
                                  't':2.0**-8,'rounded_next_t':2.0**-8}},
            'scope':'This helper checks exact tail bytes only; the enclosing audit separately checks Math metadata identity. Local2 range/EH control flow, runtime Math accuracy, field lifecycle and bounded termination remain unproved; positive finite step alone does not imply progress'}

def collider_step_receivers(metadata):
    windows={0x110a:((55,66),),0x111b:((909,941),),
             0x111f:((6,12),(1447,1453)),0x1122:((6,12),(295,301))}
    methods={}
    for method,ranges in windows.items():
        row=metadata.rows[6][method-1]
        if (metadata.owner.get(method)!=0x495 or row[1]!=0 or
                row[2]!=(6278 if method==0x110a else 129) or
                metadata.blob(row[4])!=bytes.fromhex('200001')):
            raise ValueError('current collider step receiver metadata mismatch')
        body=metadata.body(method)
        decoded=list(instructions(body))
        # Without argument address-taking or assignment, ldarg.0 retains entry this.
        if any(op in (0x0f,0x10,0xfe0a,0xfe0b) for _,op,_ in decoded):
            raise ValueError('current collider step receiver argument escape or assignment')
        branches=branch_targets(body)
        boundaries={offset for offset,_,_ in decoded}|{len(body)}
        entries=[clause[key]
                 for section in metadata.exception_sections(method)
                 for clause in section['clauses']
                 for key in (('handler_offset','extra') if clause['flags']==1
                             else ('handler_offset',))]
        for start,end in ranges:
            if (start not in boundaries or end not in boundaries or
                    body[start]!=0x02 or any(start<entry<end for entry in entries) or any(
                    start<target<end for branch in branches for target in branch['targets'])):
                raise ValueError('current collider step receiver window ingress')
        methods[f'{0x06000000|method:08x}']={
            'receiver_offsets':[start for start,_ in ranges],
            'window_ends':[end for _,end in ranges]}
    return {'methods':methods,
            'scope':'Same-owner managed instance parameterless methods; no argument address-taking or assignment; encoded ldarg.0 windows start and end on instruction boundaries and have no interior branch targets or EH handler/filter entries. Entry this identity only, not stack verification, call effects, cross-invocation object identity, publication, reentrancy or lifecycle proof'}

STEP_LIFECYCLE_LAYOUTS={
    0x1116:(0x495,134,'200001','0228111100062c10209a15d2d2287c07000673dc0900067a022818110006021728191100060217281c1100060216281911000602281b11000602281f110006022820110006021728121100062a'),
    0x1117:(0x495,134,'200001','0228111100062d1020c615d2d2287c07000673dc0900067a0216281c1100060228221100060228231100060202280d1100061758280e110006021628121100062a'),
    0x8b4:(0x2b2,6278,'2003011292c4080c','0228da11000602037dd604000402047dda04000402057ddb0400040228141100060a1200288502000a206e6dd2d2287c07000628b40a0006021200288602000a28b60800060228b50800066f161100062a'),
    0x8b5:(0x2b2,134,'2000129254','027bd80400042a'),
    0x8b6:(0x2b2,129,'200101129254','02037dd80400042a'),
    0x8bd:(0x2b2,134,'20010102','026fe91100062c012a032c2602027bd9040004027bda0400046c2300000000000000405b28d901000a69587dd90400042b0e02027bd904000417587dd9040004027bd9040004027bda040004321d0228b50800066f1711000602167dd90400040228b50800066f161100062a')}

STEP_IDENTITY_METHODS = {3423: (6278, '200201021300', '0203287d04000a02047d7e04000a2a', '3e'), 3424: (134, '200002', '027b7f04000a2a', '1e'), 3425: (129, '20010102', '02037d7f04000a2a', '22'), 3426: (134, '20001300', '02287000000a2d10204ef0d3d2287c07000673e000000a7a027b7e04000a2a', '7e'), 3428: (150, '000115118f780113001300', '028c3900001b2d102024f0d3d2287c07000673d002000a7a1702738004000a2a', '82'), 3429: (2198, '000115118f780113001300', '02288104000a2a', '1e')}
STEP_IDENTITY_FACTORY_IL = '027bbc1100042c1802026fdc11000628db0b00067dbb11000402167dbc110004027cbb11000428a305000a2d0b7e0709000428a405000a2a730a1100060a06027d2f11000406166f0e11000606027cbb11000428a505000a6f10110006026fdc1100066fa41100066f7c00000a0b2b391201287d00000a0c067b3211000408086fdb0d00066fa605000a086fc80d00067bea0900042c12067b33110004080873401100066fa705000a1201287f00000a2dbede0e1201fe163e00001b6f0600000adc067b321100046fa805000a6fa905000a0d2b23120328aa05000a1304110411047c180c000428cf0c00061c5d73cc0c00067d180c0004120328ab05000a2dd4de0e1203fe16a901001b6f0600000adc066f27110006026fdc1100067e07090004288900000a166fae1100066f3e01000a13052b171205283f01000a1306067b3511000411066f3301000a261205284401000a2de0de0e1205fe167600001b6f0600000adc0628ac05000a2a'
STEP_IDENTITY_FACTORY_EH = [{'bytes': '0128000002006e0046b4000e000000000200d3003003010e00000000020034012458010e00000000', 'clauses': [{'flags': 2, 'try_offset': 110, 'try_length': 70, 'handler_offset': 180, 'handler_length': 14, 'extra': 0}, {'flags': 2, 'try_offset': 211, 'try_length': 48, 'handler_offset': 259, 'handler_length': 14, 'extra': 0}, {'flags': 2, 'try_offset': 308, 'try_length': 36, 'handler_offset': 344, 'handler_length': 14, 'extra': 0}]}]

def collider_step_identity(metadata):
    def require(condition):
        if not condition:
            raise ValueError('current collider step lifecycle wrapper identity mismatch')
    owner=0x3de
    row=metadata.rows[2][owner-1]
    following=metadata.rows[2][owner]
    require((row[0],row[3],row[4],following[4],row[5],following[5])==
            (1048841,913,2994,2996,3423,3441))
    base=metadata.rows[1][(913>>2)-1]
    require(metadata.string(base[1])=='ValueType' and metadata.string(base[2])=='System')
    parameters=[(i,r) for i,r in enumerate(metadata.rows.get(42,[]),1) if r[2]==owner<<1]
    require(len(parameters)==1 and parameters[0][1][:3]==[0,0,owner<<1])
    require(not any(r[0]==parameters[0][0] for r in metadata.rows.get(44,[])))
    for spec,sig in ((0x38,'15118f78011300'),(0x39,'1300'),(0xcb,'15118f7801129254')):
        require(metadata.blob(metadata.rows[27][spec-1][0]).hex()==sig)
    for method,(flags,sig,il,header) in STEP_IDENTITY_METHODS.items():
        item=metadata.rows[6][method-1]
        require(item[0]!=0 and item[1:3]==[0,flags] and metadata.owner.get(method)==owner)
        require(metadata.blob(item[4]).hex()==sig and metadata.body(method).hex()==il)
        require(metadata.tiny_header(method).hex()==header and not metadata.exception_sections(method))
        require(not any(r[2]==(method<<1|1) for r in metadata.rows.get(42,[])))
    for field,sig in ((0xbb2,'0602'),(0xbb3,'061300')):
        item=metadata.rows[4][field-1]
        require(metadata.field_owner.get(field)==owner and item[0]==1 and metadata.blob(item[2]).hex()==sig)
    bindings=((0x5ac,1628,6,0xd65),(0x286,1628,6,0xd62),(0x285,1628,6,0xd60),
              (0x70,452,6,0xd60),(0x47d,452,6,0xd61),(0x47e,452,4,0xbb3),
              (0x47f,452,4,0xbb2),(0x480,452,6,0xd5f),(0x481,452,6,0xd64))
    for member,parent,table,target in bindings:
        item=metadata.rows[10][member-1]
        name_col,sig_col=(3,4) if table==6 else (1,2)
        candidates=[i for i in range(row[5] if table==6 else row[4],
                                    following[5] if table==6 else following[4])
                    if metadata.string(metadata.rows[table][i-1][name_col])==metadata.string(item[1])
                    and metadata.blob(metadata.rows[table][i-1][sig_col])==metadata.blob(item[2])]
        require(item[0]==parent and candidates==[target])
    require(metadata.body(0x1114).hex()==STEP_IDENTITY_FACTORY_IL)
    require(metadata.fat_header(0x1114).hex()=='1b3003006d010000c7010011')
    require(metadata.exception_sections(0x1114)==STEP_IDENTITY_FACTORY_EH)
    for method,rid,header,sig in (
            (0x1114,0x1c7,'1b3003006d010000c7010011',
             '07071292541511814101128fbc128fbc1511810d02128fbc128fd8128fd81511811501118ef8118ef8'),
            (0x8b4,0xb2,'1330020051000000b2000011','070115118f7801129254')):
        require(metadata.fat_header(method).hex()==header)
        require(metadata.blob(metadata.rows[17][rid-1][0]).hex()==sig)
    # These intervening static calls consume bool/string, not the wrapper address.
    for method,sig in ((0x77c,'00010e08'),(0xab4,'000201020e')):
        item=metadata.rows[6][method-1]
        require(item[1]==(8 if method==0x77c else 0) and item[2]&0x10 and metadata.blob(item[4]).hex()==sig)
    return {'status':'conditional-reference-identity-proved',
            'wrapper_type':'020003de', 'instantiation':'1b0000cb',
            'methods':[{'token':f'{0x06000000|m:08x}','flags':f,'signature':sig,'il':il,'header':h}
                       for m,(f,sig,il,h) in STEP_IDENTITY_METHODS.items()],
            'member_bindings':[{'member':f'{0x0a000000|m:08x}','target':f'{table<<24|t:08x}'}
                               for m,_,table,t in bindings],
            'factory_il':STEP_IDENTITY_FACTORY_IL,
            'factory_exception_sections':STEP_IDENTITY_FACTORY_EH,
            'premises':['Standard managed CLI value/reference and call semantics; no unsafe/external stack mutation',
                        'Normal path through factory newobj@56 and return@364, then 08b4 extraction@59'],
            'local_reasoning':[
                'Factory local0 has exactly one store at61, no address-taking; every path reaching358 after56 retains that reference, including all three finally regions. Calls may mutate/escape the object but cannot replace this unaddressed caller local',
                'd65 calls d64; its normal path passes true and unchanged arg0 to d5f. d5f stores true via d61 and unchanged arg2 in bb3. Null-rejecting throw path gives no result',
                '08b4 stores returned value-type wrapper at33; its sole earlier address use calls d60 at36, which only reads bb2. Calls46/51 receive bool/string only; extraction d62 tests d60 and returns bb3 on its normal branch'],
            'scope':'Identity of factory local0 and extraction result only; no termination, non-escape, callee purity, cross-getter stability or complete lifecycle claim'}

def collider_step_lifecycle(metadata):
    methods={}
    for method,(owner,flags,signature,layout) in STEP_LIFECYCLE_LAYOUTS.items():
        row=metadata.rows[6][method-1]
        if (not row[0] or row[1]!=0 or row[2]!=flags or
                metadata.owner.get(method)!=owner or
                metadata.blob(row[4]).hex()!=signature or
                metadata.body(method).hex()!=layout or metadata.exception_sections(method)):
            raise ValueError('current collider step lifecycle method mismatch')
        methods[f'{0x06000000|method:08x}']={
            'owner':f'{0x02000000|owner:08x}','flags':flags,
            'signature':signature,'il':layout}
    row=metadata.rows[6][0x1114-1]
    if (not row[0] or row[1:3]!=[0,150] or metadata.owner.get(0x1114)!=0x495 or
            metadata.blob(row[4]).hex()!='000115118f78011292541292d8'):
        raise ValueError('current collider step lifecycle factory metadata mismatch')
    identity=collider_step_identity(metadata)
    factory=metadata.body(0x1114)
    windows=((56,69,'730a1100060a06027d2f110004'),
             (358,365,'0628ac05000a2a'))
    for start,end,layout in windows:
        if factory[start:end].hex()!=layout:
            raise ValueError('current collider step lifecycle factory window mismatch')
    boundaries={offset for offset,_,_ in instructions(factory)}|{len(factory)}
    entries=[clause[key] for section in metadata.exception_sections(0x1114)
             for clause in section['clauses']
             for key in (('handler_offset','extra') if clause['flags']==1 else ('handler_offset',))]
    for start,end,_ in windows:
        if (start not in boundaries or end not in boundaries or
                any(start<entry<end for entry in entries) or any(
                    start<target<end for branch in branch_targets(factory)
                    for target in branch['targets'])):
            raise ValueError('current collider step lifecycle factory window ingress')
    field=metadata.rows[4][0x4d8-1]
    if (metadata.field_owner.get(0x4d8)!=0x2b2 or field[0]!=1 or
            metadata.blob(field[2]).hex()!='06129254'):
        raise ValueError('current collider step lifecycle publication field mismatch')
    return {
        'identity':identity,
        'methods':methods,
        'factory':{'method_token':'06001114','signature':'000115118f78011292541292d8',
                   'windows':[{'start':start,'end':end,'il':layout} for start,end,layout in windows],
                   'scope':'On fallthrough through 56..69, newobj 110a result becomes local0 and receives factory arg0 in field 0400112f (not publication of local0). At 358 the unchanged local0 is passed to MemberRef 0a0005ac; conditional identity through extraction is proved by identity, not non-escape or purity'},
        'primary_order':{'caller':'06001116','update_call':52,'sample_call':58,
                         'scope':'On normal arrival at offset 51, unchanged entry this is passed to 111b, then after its normal return to 111f. No intervening caller instruction can change the receiver; callee effects and reentrancy are not proved'},
        'secondary_entry':{'caller':'06001117','sample_call':32,
                           'scope':'Entry this is passed to 1122; this method contains no direct 111b call. A prior update on that object is not proved'},
        'publication_candidate':{'field_token':'040004d8','setter':'060008b6','getter':'060008b5',
                                 'constructor_calls':[28,59,64,70,75],
                                 'loop_calls':[79,84,97,102],
                                 'scope':'Setter stores arg1; getter returns the field. 08b4 passes wrapper extraction result (0a000286) to setter then getter result to 1116. 08bd independently reloads getter before 1117 and 1116; equality across those calls is not proved'},
        'unproved_edges':[
            {'edge':'08bd@79 getter result -> 1117@84 effects -> 08bd@97 getter result',
             'classification':'offline-alias-and-effects','missing':'Field 040004d8 stability through callees, reentrancy and aliases; no closed call/effect domain'},
            {'edge':'1117@32 receiver -> prior 111b update / 110a initialization',
             'classification':'offline-cross-entry-lifecycle','missing':'All entry paths and object provenance, including callers outside 08b4/08bd'},
            {'edge':'111b store -> all 111f/1122 reads during execution',
             'classification':'external-execution-assumptions','missing':'Concurrency, reflection/external mutation and runtime resolution assumptions; observations of fixtures cannot prove all inputs'}],
        'scope':'Bounded byte-bound local call ordering and publication candidate only; not a closed call graph, CLI verifier, stable-field, fresh-value, non-escape or complete lifecycle proof'}

CATEGORY12_WINDOW = bytes.fromhex(
    '027b2f1100046fdc1100067ba31100047e62110004252d17267e58110004fe065211000673a900000a258062110004'
    '28e001002b284302002b1310161311389f000000111011119a1318027b2f11000411187eb5120004076ff21100061319'
    '081711197b82030004733d1100066fcf05000a11187bf70b0004131a16131b2b54111a111b9a131c027b2f110004'
    '111c7eb5120004076ff2110006131908121dfe1599040002121d187d49110004121d11197b820300047d4a110004'
    '121d7e401100047d4b110004111d6fcf05000a111b1758131b111b111a8e6932a4111117581311111111108e693f56ffffff')


def category12_local_rule(body, sections=()):
    """Recognize one closed indexed-loop template, not arbitrary IL or callee effects."""
    start,end=547,784
    if body[start:end]!=CATEGORY12_WINDOW:
        raise ValueError('category12 local generation window mismatch')
    decoded={offset:(opcode,token) for offset,opcode,token in instructions(body)}
    branches=branch_targets(body)
    if any(start<=target<end and not start<=branch['offset']<end
           for branch in branches for target in branch['targets']):
        raise ValueError('category12 external interior entry')
    for section in sections:
        for clause in section['clauses']:
            for offset,length in (('try_offset','try_length'),('handler_offset','handler_length')):
                if clause[offset]<end and clause[offset]+clause[length]>start:
                    raise ValueError('category12 exception region overlap')
            if clause['flags']==1 and start<=clause['extra']<end:
                raise ValueError('category12 filter entry')
    loops=[]
    for init,update,condition,back,entry,array_local,index_local in (
            (606,769,773,779,614,16,17),(670,755,759,765,675,26,27)):
        branch=next(item for item in branches if item['offset']==back)
        loops.append({'array_local':array_local,'index_local':index_local,
                      'initial':decoded[init][0]-0x16,'increment':decoded[update][0]-0x16,
                      'condition_offset':condition,'backedge_offset':back,
                      'body_offset':branch['targets'][0],
                      'comparison':'signed index < conv.i4(array.Length)'})
        if branch['targets']!=[entry]:raise ValueError('category12 backedge mismatch')
    return {'window':[start,end],'loops':loops,
            'category1':{'category':decoded[643][0]-0x16,'add_offset':656,
                         'position':'return event 060011f2@635(candidate occurrence i).04000382',
                         'radius':20,'radius_source':'0600113d category==1 branch literal 20'},
            'category2':{'category':decoded[714][0]-0x16,'add_offset':748,
                         'position':'return event 060011f2@696(captured 04000bf7[i][j]).04000382',
                         'radius':24,'radius_source':'04001140'},
            'count_rule':'category1=N; category2=sum(length(captured bf7 for occurrence i))',
            'preconditions':[
                'Normal entry at 547, exit at 784; every call/allocation/Add returns normally; no reentry',
                'Where/ToArray return the finite ordered qualifying occurrence array; cached delegate is 1152',
                '1152 uses separate getter events: first e7 length != 0 OR second f1 count > 0, short circuit',
                'Non-null captured arrays, lengths <= Int32.MaxValue; no mutation of not-yet-read members',
                'No callee changes loop locals, captured references or output list except the two explicit Adds',
                'Each Add appends exactly one value to the captured local2 list; radius fields retain audited values'],
            'scope':'Conditional local numbered generation only; no game execution, entity identity, callee effects or global totals proof'}


def category12_synthetic_filter(filter_body, first_e7_length, second_f1_count):
    """Synthetic predicate model; getters are separate supplied observations."""
    if filter_body!=bytes.fromhex('036ff20d00062d07036ff30d00062a172a'):
        raise ValueError('category12 filter template mismatch')
    ins=list(instructions(filter_body))
    branch=next(item for item in branch_targets(filter_body) if item['offset']==6)
    truth=dict((offset,opcode) for offset,opcode,_ in ins)[branch['targets'][0]]-0x16
    return bool(truth) if first_e7_length!=0 else second_f1_count>0


def category12_synthetic_trace(body, candidates):
    """Apply the recognized loop recurrence to synthetic already-filtered occurrences.

    Inputs supply per-occurrence bf7 snapshots and per-call returned positions; this
    is not an interpreter, a filter implementation, or a game-behavior oracle.
    """
    rule=category12_local_rule(body)
    outer,inner=rule['loops'];result=[];i=outer['initial']
    while i<len(candidates):
        candidate=candidates[i]
        result.append(dict(rule['category1'],occurrence=[i],source=candidate['identity'],
                           returned_position=candidate['position']))
        j=inner['initial'];members=candidate['members']
        while j<len(members):
            member=members[j]
            result.append(dict(rule['category2'],occurrence=[i,j],source=member['identity'],
                               returned_position=member['position']))
            j+=inner['increment']
        i+=outer['increment']
    return result


def current_collider_audit(metadata):
    assembly_sha='ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5'
    if hashlib.sha256(metadata.data).hexdigest()!=assembly_sha:
        raise ValueError('unsupported current collider assembly hash')
    expected={0x110b:'0b745a6f3fd2e3e2d45f06ddf8eb39c43e108f21215b2390aa6c7eabeab85055',
              0x113d:'1417de379d0d23770268213b12b75b0cd4240e873ac36070732805aa280c9bfd',
              0x111c:'730e6a613e97e21a78bdf453c1f7b10a687e4fad1b4c225de666a85d8c004b57',
              0x111f:'cdf648043cb42967de598182228416c68e6138223a70128137ef0f7b4b64416e',
              0x111b:'f40ab827db2d48409d70f645d7be36721179c118503afa517d99528d14b13406',
              0x1122:'fbb45a6b06347b39c01e81953cc306acb4ced20f8bbd6bf4afe6de4acff2b0a8'}
    bodies={}
    for method,digest in expected.items():
        body=metadata.body(method)
        if hashlib.sha256(body).hexdigest()!=digest:raise ValueError('current collider method hash mismatch')
        list(instructions(body))
        bodies[method]=body
    step_finally_clauses=[{'flags':2,'try_offset':start,'try_length':length,
                          'handler_offset':handler,'handler_length':handler_length,'extra':0}
                         for start,length,handler,handler_length in (
                             (35,82,117,14),(201,152,353,14),(389,185,574,14),
                             (139,463,602,14),(770,47,817,12),(642,217,859,14))]
    step_sections=metadata.exception_sections(0x111b)
    if (len(step_sections)!=1 or
            step_sections[0].get('clauses')!=step_finally_clauses):
        raise ValueError('current collider step exception layout mismatch')
    constants=[]
    for offset,field in zip(range(304,345,10),range(0x113d,0x1142)):
        body=bodies[0x110b]
        if body[offset]!=0x22 or body[offset+5]!=0x80 or int.from_bytes(body[offset+6:offset+10],'little')!=0x04000000|field:
            raise ValueError('current collider initializer mismatch')
        signature=metadata.blob(metadata.rows[4][field-1][2])
        if signature!=b'\x06\x0c':raise ValueError('current collider field is not R4')
        constants.append({'field_token':f'{0x04000000|field:08x}', 'signature':signature.hex(),
                          'load_offset':offset,'value':struct.unpack('<f',body[offset+1:offset+5])[0]})
    constructor=bytes.fromhex('02037d4911000402047d4a110004032c0403172e0c027e3d1100047d4b1100042a027e3f1100047d4b1100042a')
    if bodies[0x113d]!=constructor:raise ValueError('current collider constructor layout mismatch')
    signature=metadata.blob(metadata.rows[6][0x113d-1][4])
    if signature!=bytes.fromhex('200201119274119388'):
        raise ValueError('current collider constructor signature mismatch')
    parameter_types=[]
    for coded in (0x1274,0x1388):
        index=coded>>2
        row=metadata.rows[2][index-1]
        parameter_types.append({'token':f'{0x02000000|index:08x}',
                                'namespace':metadata.string(row[2]),'name':metadata.string(row[1])})
    category_type=metadata.rows[2][0x49d-1]
    if category_type[4]!=0x1155 or metadata.rows[2][0x49e-1][4]!=0x1156:
        raise ValueError('current collider category field ownership mismatch')
    collider_type=metadata.rows[2][0x499-1]
    if collider_type[4]!=0x1149 or metadata.rows[2][0x49a-1][4]!=0x114c:
        raise ValueError('current collider field ownership mismatch')
    for field,expected_signature in ((0x1149,'06119274'),(0x114a,'06119388'),(0x114b,'060c')):
        if metadata.blob(metadata.rows[4][field-1][2]).hex()!=expected_signature:
            raise ValueError('current collider instance field signature mismatch')
    category_field=metadata.rows[4][0x1155-1]
    if metadata.string(category_field[1])!='value__' or metadata.blob(category_field[2])!=b'\x06\x08':
        raise ValueError('current collider category backing field mismatch')
    insertion=bytes.fromhex('08121dfe1599040002121d187d49110004121d11197b820300047d4a110004121d7e401100047d4b110004111d6fcf05000a')
    if bodies[0x111f][703:753]!=insertion:
        raise ValueError('current collider explicit insertion layout mismatch')
    second_insertion=bytes.fromhex('08121dfe1599040002121d197d49110004121d7e4904000411226f100800067d4a110004121d7e411100047d4b110004111d6fcf05000a')
    if bodies[0x111f][881:936]!=second_insertion:
        raise ValueError('current collider second insertion layout mismatch')
    add=metadata.rows[10][0x5cf-1]
    if (add[0]!=0xd3c or metadata.string(add[1])!='Add' or
            metadata.blob(add[2]).hex()!='2001011300' or
            metadata.blob(metadata.rows[27][0x1a7-1][0]).hex()!='1512813d01119264'):
        raise ValueError('current collider Add signature mismatch')
    constructor_calls=[]
    for offset,category,layout in (
            (256,0,'0816110e733d1100066fcf05000a'),
            (491,0,'08161117733d1100066fcf05000a'),
            (642,1,'081711197b82030004733d1100066fcf05000a')):
        encoded=bytes.fromhex(layout)
        if bodies[0x111f][offset:offset+len(encoded)]!=encoded:
            raise ValueError('current collider constructor call layout mismatch')
        constructor_calls.append({'offset':offset,'category':category})
    radius15_insertions=[]
    for offset,position_local in ((2197,31),(2729,43),(3352,53),(3861,63),
                                  (4726,68),(5260,74),(6254,86)):
        layout=bytes.fromhex('1220fe15990400021220167d49110004122011')+bytes([position_local])+bytes.fromhex(
            '7d4a11000412207e3e1100047d4b11000411206fcf05000a')
        if bodies[0x111c][offset:offset+len(layout)]!=layout:
            raise ValueError('current collider radius15 insertion layout mismatch')
        radius15_insertions.append({'offset':offset,'category':0,'position_local':position_local,
                                    'radius_field_token':'0400113e','radius':15})
    list_type=metadata.rows[1][0x4f-1]
    scope=metadata.rows[35][0]
    if (list_type[0]!=6 or metadata.string(list_type[1])!='List`1' or
            metadata.string(list_type[2])!='System.Collections.Generic' or
            metadata.string(scope[6])!='mscorlib'):
        raise ValueError('current collider List type identity mismatch')
    pair_layout=bytes.fromhex(
        '132611247b4a11000411267b4a110004284713000611247b4b11000411267b4b110004584153010000'
        '11247b491100042c0c11267b4911000416fe012b0117132711247b49110004172e0c11267b4911000417fe012b01171328'
        '11247b49110004182e0c11267b4911000418fe012b0117132911247b49110004192e0c11267b4911000419fe012b0117132a'
        '17132b11247b491100042d1a11267b491100042d117e23110004283f0d0006132c3867000000'
        '11247b4911000417331811267b4911000417330e7e24110004283f0d0006132c2b45'
        '112711285f2c0e7e25110004283f0d0006132c2b30112a11275f2c0e7e26110004283f0d0006132c2b1b'
        '112a11295f2c0e7e27110004283f0d0006132c2b0616132b14132c112b2c5f'
        '0722000000002277be7f3f28b60a0006132d73df05000a132e'
        '11247b49110004192e0e112e11247b4a1100046fe005000a'
        '11267b49110004192e0e112e11267b4a1100046fe005000a'
        '027b2f110004112d112c112e6fe105000a6fe8110006')
    if bodies[0x111f][1015:1395]!=pair_layout:
        raise ValueError('current collider pair report layout mismatch')
    pair_loop_prefix=bytes.fromhex(
        '16132338bf0100000811236fde05000a1324112317581325388a0100000811256fde05000a')
    pair_loop_suffix=bytes.fromhex(
        '1125175813251125086fe205000a3f69feffff0211247b4a1100042824110006'
        '1123175813231123086fe205000a3f34feffff')
    if (bodies[0x111f][978:1015]!=pair_loop_prefix or
            bodies[0x111f][1395:1446]!=pair_loop_suffix):
        raise ValueError('current collider pair loop layout mismatch')
    for member,name,sig in ((0x5de,'get_Item','2001130008'),
                            (0x5e2,'get_Count','200008')):
        row=metadata.rows[10][member-1]
        if (row[0]!=3388 or metadata.string(row[1])!=name or
                metadata.blob(row[2]).hex()!=sig):
            raise ValueError('current collider pair loop member identity mismatch')
    pair_signatures={0x06001347:'00020c119388119388',0x06000d3f:'00010e128f68',
                     0x06000ab6:'00030c0c0c0c',0x0a0005e1:'20001d1300',
                     0x060011e8:'2003010c0e1d119388'}
    for token,expected_signature in pair_signatures.items():
        row=metadata.rows[token>>24][(token&0xffffff)-1]
        if metadata.blob(row[4] if token>>24==6 else row[2]).hex()!=expected_signature:
            raise ValueError('current collider pair report signature mismatch')
    numerical_chain={
        0x1347:('00020c119388119388','020328501300060a120028401300062a'),
        0x1350:('0002119388119388119388','027bb3120004037bb312000459027bb4120004037bb412000459733b1300062a'),
        0x1340:('20000c','027bb3120004027bb31200045a027bb4120004027bb41200045a586c280607000a6b2a'),
        0x133b:('2002010c0c','02037db312000402047db41200042a'),
        0xab6:('00030c0c0c0c','020328e901000a04284a00000a2a')}
    for method,(method_signature,layout) in numerical_chain.items():
        if (metadata.blob(metadata.rows[6][method-1][4]).hex()!=method_signature or
                metadata.body(method).hex()!=layout):
            raise ValueError('current collider numerical chain mismatch')
    if (metadata.rows[2][0x4e2-1][4]!=0x12b3 or
            metadata.rows[2][0x4e3-1][4]!=0x12b6):
        raise ValueError('current collider vector field ownership mismatch')
    if (metadata.rows[2][0x4e2-1][5]!=0x133b or
            metadata.rows[2][0x4e3-1][5]!=0x1359):
        raise ValueError('current collider vector method ownership mismatch')
    if metadata.blob(metadata.rows[27][0x1bb-1][0]).hex()!='1512813d01119388':
        raise ValueError('current collider vector list identity mismatch')
    for member,name,sig in ((0x5df,'.ctor','200001'),(0x5e0,'Add','2001011300'),
                            (0x5e1,'ToArray','20001d1300')):
        row=metadata.rows[10][member-1]
        if (row[0]!=3548 or metadata.string(row[1])!=name or
                metadata.blob(row[2]).hex()!=sig):
            raise ValueError('current collider vector list member identity mismatch')
    for field in (0x12b3,0x12b4):
        if metadata.blob(metadata.rows[4][field-1][2]).hex()!='060c':
            raise ValueError('current collider vector field signature mismatch')
    math_type=metadata.rows[1][0x99-1]
    if (math_type[0]!=6 or metadata.string(math_type[1])!='Math' or
            metadata.string(math_type[2])!='System' or scope[:5]!=[4,0,0,0,0] or
            metadata.blob(scope[5]).hex()!='b77a5c561934e089' or
            metadata.string(scope[7])!='' or metadata.blob(scope[8])!=b''):
        raise ValueError('current collider Math assembly identity mismatch')
    for member,name,member_signature in ((0x706,'Sqrt','00010d0d'),
                                         (0x1e9,'Max','00020c0c0c'),
                                         (0x4a,'Min','00020c0c0c'),
                                         (0x25d,'Log','00020d0d0d'),
                                         (0x15b,'Round','00010d0d'),
                                         (0x228,'Pow','00020d0d0d'),
                                         (0x5ca,'Min','00020d0d0d'),
                                         (0x131,'Max','0002080808')):
        row=metadata.rows[10][member-1]
        if (row[0]!=1225 or metadata.string(row[1])!=name or
                metadata.blob(row[2]).hex()!=member_signature):
            raise ValueError('current collider Math member identity mismatch')
    filter_methods={
        0xdc8:('2000128de4','027bf20b00042a'),
        0xdf2:('200002','0228c80d00067be70900048e16fe032a'),
        0xdf3:('200002','0228c80d00067bf10900046f4202000a16fe022a'),
        0x1151:('200102128fbc','036ff30d00062a'),
        0x1152:('200102128fbc','036ff20d00062d07036ff30d00062a172a')}
    if metadata.rows.get(3) or metadata.rows.get(5):
        raise ValueError('current collider pointer tables unsupported')
    for method,(sig,layout) in filter_methods.items():
        if (metadata.blob(metadata.rows[6][method-1][4]).hex()!=sig or
                metadata.body(method).hex()!=layout):
            raise ValueError('current collider filter method mismatch')
    for owner,fields,methods in ((0x379,(2529,2556),None),
                                  (0x3ef,(3055,3073),(3526,3576)),
                                  (0x49f,None,(4423,4437))):
        for column,bounds in ((4,fields),(5,methods)):
            if bounds is not None and (metadata.rows[2][owner-1][column],
                                       metadata.rows[2][owner][column])!=bounds:
                raise ValueError('current collider filter ownership mismatch')
    for field,sig in ((0xbf2,'06128de4'),(0x9e7,'061d118f04'),
                      (0x9f1,'06151280f902118ef8128e1c')):
        if metadata.blob(metadata.rows[4][field-1][2]).hex()!=sig:
            raise ValueError('current collider filter field mismatch')
    accessor_methods={
        0xdc9:('200101128de4','02037df20b00042a'),
        0xdd4:('200002','027bfc0b00042a'),
        0xdd5:('20010102','02037dfc0b00042a'),
        0xdcb:('200101118ef8','02037df30b00042a'),
        0xdcd:('200101118f04','02037df40b00042a')}
    for method,(sig,layout) in accessor_methods.items():
        row=metadata.rows[6][method-1]
        if (row[1:3]!=[0,134 if method==0xdd4 else 129] or
                metadata.tiny_header(method)!=bytes([(len(bytes.fromhex(layout))<<2)|2]) or
                metadata.blob(row[4]).hex()!=sig or
                metadata.body(method).hex()!=layout):
            raise ValueError('current collider initialization accessor mismatch')
    for field,sig in ((0xbf2,'06128de4'),(0xbf3,'06118ef8'),
                      (0xbf4,'06118f04'),(0xbfc,'0602')):
        row=metadata.rows[4][field-1]
        if row[0]!=1 or metadata.blob(row[2]).hex()!=sig:
            raise ValueError('current collider initialization field mismatch')
    initialization_constructors={
        0xb51:('200001','033002005300000000000000',
               '0ac268ddfcf7de1344355b886bd1cdc0f24baa33128245911080954c00722e13'),
        0xdc6:('200201128de402','133004000501000077010011',
               '64788d6df2c2d2e916641009e32c240abbe9723f612c00b8e994502362af6c65')}
    for method,(sig,header,digest) in initialization_constructors.items():
        row=metadata.rows[6][method-1]
        if (row[1:3]!=[0,6278] or metadata.blob(row[4]).hex()!=sig or
                metadata.fat_header(method).hex()!=header or
                hashlib.sha256(metadata.body(method)).hexdigest()!=digest):
            raise ValueError('current collider initialization constructor mismatch')
    if metadata.blob(metadata.rows[17][0x177-1][0]).hex()!='07030808128fbc':
        raise ValueError('current collider initialization constructor locals mismatch')
    static_initializer=metadata.rows[6][0xa48-1]
    static_body=metadata.body(0xa48)
    static_window='73510b0006252073c6d3d2287c0700067de109000425206cc6d3d2287c07000628590700067de209000425177de509000425177dea09000425177df209000425167df80900048062090004'
    if (static_initializer[1:3]!=[0,150] or
            metadata.blob(static_initializer[4]).hex()!='000001' or
            metadata.fat_header(0xa48).hex()!='13300800901a0000dc000011' or
            hashlib.sha256(static_body).hexdigest()!='2977b87c820b4c753cb90c5ffbb3de7d88b7bdaa29be75e5c3e46dc4c0ca41a5' or
            static_body[1448:1523].hex()!=static_window or
            metadata.blob(metadata.rows[17][0xdc-1][0]).hex()!='0701128de4' or
            metadata.rows[4][0x962-1][0]!=22 or
            metadata.blob(metadata.rows[4][0x962-1][2]).hex()!='06128de4' or
            (metadata.rows[2][0x35b-1][5],metadata.rows[2][0x35b][5])!=(2632,2635)):
        raise ValueError('current collider static initialization mismatch')
    count=metadata.rows[10][0x242-1]
    dictionary=metadata.rows[1][0x3e-1]
    if (count[0]!=1492 or metadata.string(count[1])!='get_Count' or
            metadata.blob(count[2]).hex()!='200008' or
            metadata.blob(metadata.rows[27][0xba-1][0]).hex()!='151280f902118ef8128e1c' or
            dictionary[0]!=6 or metadata.string(dictionary[1])!='Dictionary`2' or
            metadata.string(dictionary[2])!='System.Collections.Generic'):
        raise ValueError('current collider filter collection identity mismatch')
    direct_adds={}
    for method,offsets in ((0x111c,[2236,2768,3391,3900,4765,5299,6293]),
                           (0x111f,[265,500,656,748,931])):
        found=[offset for offset,op,token in instructions(bodies[method])
               if op in (0x28,0x6f) and token==0x0a0005cf]
        if found!=offsets:raise ValueError('current collider direct Add inventory mismatch')
        direct_adds[f'{0x06000000|method:08x}']=found
    sampling_loop=collider_sampling_loop(bodies[0x111f],1446,1455)
    secondary_sampling_loop=collider_sampling_loop(bodies[0x1122],294,303)
    report_dispatch=current_report_dispatch_audit(metadata)
    step_calculation=collider_step_calculation(bodies[0x111b])
    step_field_references=field_reference_inventory(metadata,[0x04001138])
    expected_step_references=[
        {'method_token':f'{0x06000000|method:08x}','offset':offset,
         'opcode':f'{opcode:04x}','field_token':'04001138'}
        for method,offset,opcode in ((0x110a,61,0x7d),(0x111b,936,0x7d),
                                    (0x111f,7,0x7b),(0x111f,1448,0x7b),
                                    (0x1122,7,0x7b),(0x1122,296,0x7b))]
    if (not step_field_references['complete'] or
            step_field_references['references']!=expected_step_references):
        raise ValueError('current collider step field reference inventory mismatch')
    step_field=metadata.rows[4][0x1138-1]
    if step_field[0]!=1 or metadata.blob(step_field[2])!=b'\x06\x0c':
        raise ValueError('current collider step field metadata mismatch')
    step_constructor_row=metadata.rows[6][0x110a-1]
    if (metadata.field_owner.get(0x1138)!=0x495 or
            metadata.owner.get(0x110a)!=0x495 or
            step_constructor_row[0]==0 or step_constructor_row[1]!=0 or
            step_constructor_row[2]!=6278 or
            metadata.string(step_constructor_row[3])!='.ctor' or
            metadata.blob(step_constructor_row[4])!=bytes.fromhex('200001')):
        raise ValueError('current collider step constructor metadata mismatch')
    step_constructor=metadata.body(0x110a)
    if step_constructor[55:66]!=bytes.fromhex('02220000803f7d38110004'):
        raise ValueError('current collider step constructor initialization mismatch')
    step_constructor_flow=field_store_return_dominance(
        step_constructor,metadata.exception_sections(0x110a),61,55)
    for body in bodies.values():
        branch_targets(body)
    return {'assembly_sha256':assembly_sha,'status':'partial-byte-evidence-only',
            'step_entry_receivers':collider_step_receivers(metadata),
            'step_lifecycle':collider_step_lifecycle(metadata),
            'step_constructor_normal_flow':step_constructor_flow,
            'step_field_initialization':{
                'field_token':'04001138','flags':step_field[0],'signature':'060c',
                'constructor_token':'0600110a','receiver_offset':55,
                'constant_offset':56,'store_offset':61,'value':1.0,
                'scope':'Private instance R4 field, same-owner managed instance .ctor metadata and exact this/ldc.r4 1/stfld window; normal-return dominance is checked separately. No publication, call ordering, alias, reflection, external-write or lifecycle proof'},
            'step_field_references':step_field_references,
            'category12_local_generation':category12_local_rule(
                bodies[0x111f],metadata.exception_sections(0x111f)),
            'generation_filters':{
                'methods':{f'{0x06000000|method:08x}':{'signature':sig,'il':layout}
                           for method,(sig,layout) in filter_methods.items()},
                'normal_expressions':{'06001151':'Count(field 040009f1 of getter 06000dc8(arg1)) > 0',
                                      '06001152':'Length(field 040009e7 of getter 06000dc8(arg1)) != 0 OR Count(field 040009f1 of getter 06000dc8(arg1)) > 0 (short circuit)'},
                'getter_field_token':'04000bf2',
                'scope':'Exact method bodies and metadata only; non-null normal inputs, no exception or full generation proof'},
            'initialization_accessors':{
                'methods':{f'{0x06000000|method:08x}':{'signature':sig,'il':layout}
                           for method,(sig,layout) in accessor_methods.items()},
                'field_signatures':{'04000bf2':'06128de4','04000bf3':'06118ef8',
                                    '04000bf4':'06118f04','04000bfc':'0602'},
                'method_layouts':{f'{0x06000000|method:08x}':{
                    'impl_flags':0,'flags':134 if method==0xdd4 else 129,
                    'tiny_header':bytes([(len(bytes.fromhex(layout))<<2)|2]).hex()}
                    for method,(_,layout) in accessor_methods.items()},
                'scope':'Direct nonvirtual accessor bodies, tiny headers and instance field signatures only; constructor recursion, initialization and aliases remain unproved'},
            'initialization_constructors':{
                'methods':{f'{0x06000000|method:08x}':{'signature':sig,
                    'impl_flags':0,'flags':6278,'fat_header':header,'il_sha256':digest}
                    for method,(sig,header,digest) in initialization_constructors.items()},
                'locals':{'11000177':'07030808128fbc'},
                'scope':'Exact constructor bodies, headers without extra EH sections and locals only; recursive termination, initialization order and aliases remain unproved'},
            'static_initialization':{
                'method_token':'06000a48','impl_flags':0,'flags':150,
                'signature':'000001','fat_header':'13300800901a0000dc000011',
                'il_sha256':'2977b87c820b4c753cb90c5ffbb3de7d88b7bdaa29be75e5c3e46dc4c0ca41a5',
                'locals':{'110000dc':'0701128de4'},
                'publication':{'offset':1518,'field_token':'04000962',
                               'construction_offset':1448,'window_il':static_window},
                'scope':'Exact ordinary static method and publication window only; not a type initializer, no call-order, alias or recursive termination proof'},
            'direct_add_inventory':{'sites':direct_adds,'member_token':'0a0005cf',
                                    'scope':'Direct call sites only; not alias, reachability or initialization proof'},
            'sampling_loop':sampling_loop,
            'distance_calculation':collider_distance_calculation(metadata),
            'step_calculation':step_calculation,
            'step_local_normal_control_flow':local_control_flow_evidence(bodies[0x111b],2,623),
            'step_local_finally_control_flow':local_finally_control_flow_evidence(bodies[0x111b],step_sections,2,623),
            'secondary_sampling_loop':secondary_sampling_loop,
            'report_dispatch':report_dispatch,
            'report_optional':current_report_optional_audit(metadata),
            'area_candidates':current_area_candidates_audit(metadata),
            'report_state':current_report_state_audit(metadata),
            'report_wrapper':current_report_wrapper_audit(metadata),
            'report_reset':current_report_reset_audit(metadata),
            'numerical_chain':{f'{0x06000000|method:08x}':{'signature':sig,'il':layout}
                               for method,(sig,layout) in numerical_chain.items()},
            'numerical_scope':'Static binary32 arithmetic and binary64 Sqrt call chain only; runtime equivalence unproved',
            'certification_eligible':False,'constants':constants,
            'method_il_sha256':{f'{0x06000000|method:08x}':digest for method,digest in expected.items()},
            'constructor':{'token':'0600113d','signature':signature.hex(),'parameter_types':parameter_types,
                           'zero_branch_target':21,'one_branch_target':33,
                           'radius_rule':'category == 1 ? 20 : 29'},
            'category_backing_field':{'token':'04001155','name':'value__','signature':'0608'},
            'explicit_insertion':{'method_token':'0600111f','offset':703,'category':2,
                                  'radius_field_token':'04001140','radius':24,
                                  'position_source_field_token':'04000382','add_token':'0a0005cf'},
            'second_explicit_insertion':{'method_token':'0600111f','offset':881,'category':3,
                                         'radius_field_token':'04001141','radius':20,
                                         'position_call_token':'06000810','add_token':'0a0005cf'},
            'constructor_calls':{'method_token':'0600111f','sites':constructor_calls},
            'radius15_insertions':{'method_token':'0600111c','sites':radius15_insertions},
            'insertion_target':{'token':'0a0005cf','name':'Add','signature':'2001011300',
                                'parent_typespec_signature':'1512813d01119264',
                                'generic_type':'System.Collections.Generic.List`1',
                                'resolution_scope':'AssemblyRef 23000001: mscorlib'},
            'pair_report':{'method_token':'0600111f','offset':1015,'end_offset_exclusive':1395,
                           'distance_call_token':'06001347','skip_opcode':'bge.un','skip_target':1395,
                           'category_presence_locals':[39,40,41,42],
                           'report_category_pairs':[[0,0],[1,1],[0,1],[0,3],[2,3]],
                           'pairs_are_symmetric':True,'omitted_report_position_category':3,
                           'report_argument_r4_hex':['00000000','77be7f3f'],
                           'call_signatures':{f'{token:08x}':value for token,value in pair_signatures.items()},
                           'scope':'Local report selection only; not complete collision semantics'},
            'unproved_obligations':['Category meanings and all insertion paths',
                                   'Pair filtering and explicit radius overrides',
                                   'Trajectory, continuous coverage and metric equivalence']}

def business_identity_slice(metadata):
    """Lock the finite identity/effect slice; do not infer a sampling binding."""
    windows=(
        (0xa48,1448,'73510b0006252073c6d3d2287c0700067de109000425206cc6d3d2287c07000628590700067de209000425177de509000425177dea09000425177df209000425167df80900048062090004'),
        (0xb51,0,'027e9e0b00047de2090004027e9e0b00047de309000402168dc10300027de709000402168d820300027deb09000402168dbe0300027ded0900040273e702000a7df109000402177df309000402283800000a2a'),
        (0xa4a,0,'734b0a00060a06027d770900047e7609000406fe064c0a000673ec02000a28af01002b2a'),
        (0xa4c,0,'037be1090004027b7709000428d500000a2a'),
        (0x11bb,305,'066f0605000a284a0a0006120f285202002b2d117e07090004281b05000a1309dd24020000110f1673c60d00061310066f0805000a066f2502000a1311066f2502000a13122c14110511101111111273a60c00066fa6110006'),
        (0x11a6,0,'020328a711000603046fec0d0006027ba3110004036fd704000a2a'),
        (0x11a7,0,'036fda0d00062c0e027ba3110004036f4306000a262a20476fd2d2287c07000673dc0900067a'),
        (0xdec,0,'020328cb0d00060228f10d00062a'),
        (0xdc6,55,'020328c90d0006020428d50d0006037be70900048e690a02068def0300027df70b0004160b2b58'),
        (0xdc9,0,'02037df20b00042a'),
        (0xdcb,0,'02037df30b00042a'),
        (0xdf1,0,'160a2b1d027bf70b0004069a7e3a110004020628d70600066fcb0d00060617580a06027bf70b00048e6932d82a'),
        (0xdf2,0,'0228c80d00067be70900048e16fe032a'),
        (0xdf3,0,'0228c80d00067bf10900046f4202000a16fe022a'),
        (0x1152,0,'036ff20d00062d07036ff30d00062a172a'))
    evidence=[]
    for method,start,encoded in windows:
        body=metadata.body(method);expected=bytes.fromhex(encoded)
        if body[start:start+len(expected)]!=expected:
            raise ValueError('unsupported business identity slice')
        evidence.append({'method_token':f'{0x06000000|method:08x}',
                         'window':[start,start+len(expected)],'bytes':encoded})
    return evidence


def current_business_identity_audit(metadata):
    digest=hashlib.sha256(metadata.data).hexdigest()
    if digest!='ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5':
        raise ValueError('unsupported business identity assembly hash')
    payload=metadata.data[0x8295c:0x8295c+161228]
    if (metadata.number(0x82958,4)!=161228 or
            hashlib.sha256(payload).hexdigest()!='eb30da8c127cda59067d17dc3f396465f50a1cd4275b92db5ccbb264190addc3'):
        raise ValueError('unsupported business identity resource')
    record=current_grid_string(payload,(-757873037)&0xffffffff)
    if record['text']!='claw-pivot':
        raise ValueError('unsupported business identity string')
    windows=business_identity_slice(metadata)
    methods=(0x77c,0xa48,0xb51,0xa4a,0xa4b,0xa4c,0x11bb,0xdc6,
             0xdc9,0xdc8,0xdd5,0xdd4,0x11a6,0x11a7,0xdda,0xdec,0xdcb,
             0xdf1,0xdf2,0xdf3,0x1152,0x111f)
    return {'schema':1,'assembly_sha256':digest,'certification_eligible':False,
            'status':'bounded-fragments-with-explicit-missing-edges',
            'sampling_identity_bound':False,
            'scope':'Offline PE/resource bytes only; no managed method execution, save input or game launch',
            'business_id':record,
            'resource':{'payload_offset':0x8295c,'length':len(payload),
                        'sha256':hashlib.sha256(payload).hexdigest()},
            'windows':windows,
            'methods':[{'token':f'{0x06000000|method:08x}',
                        'owner_token':f'{0x02000000|metadata.owner[method]:08x}',
                        'flags':metadata.rows[6][method-1][1:3],
                        'signature':metadata.blob(metadata.rows[6][method-1][4]).hex(),
                        'body_size':len(metadata.body(method)),
                        'il_sha256':hashlib.sha256(metadata.body(method)).hexdigest(),
                        'exception_sections':metadata.exception_sections(method)}
                       for method in methods],
            'registration':initialization_array_publication(metadata.body(0xa48)),
            'query_bindings':initialization_query_bindings(metadata),
            'verified_fragments':[
                'Resource record for uint32(-757873037) is claw-pivot; a48@1454/1459/1464 targets 962.e1',
                'a4c compares descriptor e1 with query 977; this does not prove unique lookup',
                '11bb@342 loads local15 into dc6(false), stores result local16; @378 passes the same local16 to 11a6 on the nonzero branch',
                'dc9 stores descriptor arg1 in instance bf2; dc6@69 reads arg1.e7 to allocate bf7',
                '11a6 forwards its original arg1 through 11a7 and dec before Add to this.11a3',
                'dec first stores bf3 via dcb, then df1 loops over bf7 and calls 6d7 and child.dcb',
                '1152 short-circuits separate df2/df3 getter events: e7 nonempty OR f1.Count positive'],
            'missing_edges':[
                {'edge':'resource text -> runtime 77c return',
                 'reason':'Reused resource decoder is not a proof of all wrapper cache/guard effects, termination or exceptional paths'},
                {'edge':'ReadString -> unique local15 descriptor 962',
                 'reason':'No serialized input supplied; all 976 entry IDs, initialization timing and Maybe success/uniqueness are not proved'},
                {'edge':'new dc6 -> normal return and descriptor preservation',
                 'reason':'Before dc9: dc6@39 new c42 and @50 Object ctor; after dc9: dd5 and descriptor-driven e7/ef/e6 branches. Normal return and no interference are not proved'},
                {'edge':'11a6 pre-effects -> Add of unchanged instance to unchanged owner list',
                 'reason':'11a7@1 dda reads bf6; Contains/throw branch plus dec->df1@19 6d7/child setter remain effect obligations, not assumed pure'},
                {'edge':'inserted claw-pivot -> same 111f category1/2 occurrence',
                 'reason':'Initial 962.e7 is empty, so it alone cannot satisfy 1152. Need same-descriptor e7/f1 observations, list lifetime and captured bf7 member identity; no category/pair assignment inferred'},
                {'edge':'A same-owner noninterference',
                 'reason':'111c/1122/1123 normal/exception/finally edges remain independently open'}],
            'category12_local_rule':category12_local_rule(metadata.body(0x111f),metadata.exception_sections(0x111f))}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('assembly');parser.add_argument('--method',type=int);parser.add_argument('--calls',default='')
    parser.add_argument('--audit-production',action='store_true')
    parser.add_argument('--audit-numerical',action='store_true')
    parser.add_argument('--audit-current-colliders',action='store_true')
    parser.add_argument('--audit-current-initialization-inventory',action='store_true')
    parser.add_argument('--audit-grid-assets',action='store_true')
    parser.add_argument('--audit-current-grid-assets',action='store_true')
    parser.add_argument('--audit-current-business-identity',action='store_true')
    args=parser.parse_args();metadata=Metadata(args.assembly)
    if args.audit_current_business_identity:
        print(json.dumps(current_business_identity_audit(metadata),ensure_ascii=False,indent=2))
    elif args.audit_current_grid_assets:
        print(json.dumps(current_grid_asset_audit(metadata,Path(args.assembly).parent/'Content'),ensure_ascii=False,indent=2))
    elif args.audit_current_initialization_inventory:
        print(json.dumps(current_initialization_inventory(metadata),ensure_ascii=False,indent=2))
    elif args.audit_current_colliders:
        print(json.dumps(current_collider_audit(metadata),ensure_ascii=False,indent=2))
    elif args.audit_grid_assets:
        print(json.dumps(main_grid_asset_audit(metadata,Path(args.assembly).parent/'Content'),ensure_ascii=False,indent=2))
    elif args.audit_numerical:
        print(json.dumps(numerical_audit(metadata),ensure_ascii=False,indent=2))
    elif args.audit_production:
        print(json.dumps(production_audit(metadata),ensure_ascii=False,indent=2))
    elif args.method:
        print(json.dumps({'method':args.method,'name':metadata.token_name(0x06000000|args.method),
            'bytes':metadata.body(args.method).hex(),'references':metadata.references(args.method)},ensure_ascii=False,indent=2))
    else:
        print(json.dumps({'streams':metadata.streams,'counts':metadata.counts},indent=2))
        for index in range(1,len(metadata.rows.get(6,[]))+1):
            refs=metadata.references(index)
            matches=[ref for ref in refs if args.calls and args.calls.lower() in ref['name'].lower()]
            if matches:print(json.dumps({'method':index,'name':metadata.token_name(0x06000000|index),'matches':matches},ensure_ascii=False))
if __name__=='__main__':main()
