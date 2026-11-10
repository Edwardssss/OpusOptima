"""Tests for the byte-only evidence reader; no game install or external code required."""
import hashlib
import io
import json
import struct
from pathlib import Path
import runpy
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parent
MODULE=runpy.run_path(str(ROOT/'GameProductionEvidence.py'))

class EvidenceReaderTests(unittest.TestCase):
    def test_business_identity_bounded_windows(self):
        snapshot=json.loads((ROOT/'CurrentBusinessIdentityEvidence.json').read_text())
        bodies={}
        for window in snapshot['windows']:
            method=int(window['method_token'],16)&0xffffff
            start,end=window['window']
            bodies[method]=b'\x00'*start+bytes.fromhex(window['bytes'])
            self.assertEqual(len(bodies[method]),end)
        reader=Mock();reader.body.side_effect=bodies.__getitem__
        self.assertEqual(MODULE['business_identity_slice'](reader),snapshot['windows'])
        for window in snapshot['windows']:
            method=int(window['method_token'],16)&0xffffff
            start,end=window['window']
            for offset in range(start,end):
                changed=bytearray(bodies[method]);changed[offset]^=1
                reader.body.side_effect=lambda rid:bytes(changed) if rid==method else bodies[rid]
                with self.subTest(method=method,offset=offset):
                    with self.assertRaisesRegex(ValueError,'business identity slice'):
                        MODULE['business_identity_slice'](reader)
            reader.body.side_effect=bodies.__getitem__
        self.assertFalse(snapshot['certification_eligible'])
        self.assertFalse(snapshot['sampling_identity_bound'])
        self.assertEqual(snapshot['business_id']['text'],'claw-pivot')
        self.assertEqual(snapshot['registration']['template_index'],9)
        self.assertEqual(len(snapshot['missing_edges']),6)

    def test_business_identity_rejects_unlocked_source(self):
        reader=Mock();reader.data=b'unsupported'
        with self.assertRaisesRegex(ValueError,'business identity assembly hash'):
            MODULE['current_business_identity_audit'](reader)

    def test_collider_distance_exact_methods_and_metadata(self):
        inspect=MODULE['collider_distance_calculation']
        reader=Mock()
        bodies={0xcb4:bytes.fromhex('027b230b0004037b230b000459285c02000a027b240b0004037b240b000459285c02000a580f0028a80c00060f0128a80c000659285c02000a58185b2a'),
                0xca8:bytes.fromhex('027b230b000465027b240b0004592a')}
        signatures={0xcb4:bytes.fromhex('000208118ef8118ef8'),0xca8:bytes.fromhex('200008'),
                    1:bytes.fromhex('0608'),2:bytes.fromhex('00010808')}
        reader.rows={2:[[0]*6 for _ in range(0x3bf)],
                     4:[[0]*3 for _ in range(0xb24)],
                     6:[[0]*6 for _ in range(0xcb4)],
                     10:[[0]*3 for _ in range(0x25c)]}
        reader.rows[2][0x3bd][4:6]=[0xb23,0xca6]
        reader.rows[2][0x3be][4:6]=[0xb27,0xcbb]
        for field in (0xb23,0xb24):reader.rows[4][field-1]=[38,0,1]
        for method in bodies:
            reader.rows[6][method-1][2]=150 if method==0xcb4 else 2182
            reader.rows[6][method-1][4]=method
        reader.rows[10][0x25b]=[1225,0,2]
        reader.body.side_effect=bodies.__getitem__
        reader.blob.side_effect=signatures.__getitem__
        reader.string.return_value='Abs'
        reader.exception_sections.return_value=[]
        self.assertEqual(inspect(reader)['method_token'],'06000cb4')
        for method,body in bodies.items():
            for offset in range(len(body)):
                with self.subTest(method=method,offset=offset):
                    changed=bytearray(body);changed[offset]^=1
                    reader.body.side_effect=lambda index:bytes(changed) if index==method else bodies[index]
                    with self.assertRaisesRegex(ValueError,'distance method'):inspect(reader)
        reader.body.side_effect=bodies.__getitem__
        for table,index,column in ((2,0x3bd,4),(2,0x3bd,5),(2,0x3be,4),(2,0x3be,5),
                                   (4,0xb22,0),(4,0xb23,0),(4,0xb22,2),
                                   (4,0xb23,2),(6,0xcb3,4),(6,0xca7,4),(10,0x25b,0)):
            original=reader.rows[table][index][column]
            reader.rows[table][index][column]=0
            with self.assertRaises((ValueError,KeyError)):inspect(reader)
            reader.rows[table][index][column]=original
        for key in signatures:
            original=signatures[key]
            signatures[key]=original+b'\x00'
            with self.subTest(signature=key):
                with self.assertRaisesRegex(ValueError,'distance (method|field|Abs)'):inspect(reader)
            signatures[key]=original
        reader.string.return_value='abs'
        with self.assertRaisesRegex(ValueError,'distance Abs'):inspect(reader)
        reader.string.return_value='Abs'
        for method in bodies:
            for column in (1,2):
                original=reader.rows[6][method-1][column]
                reader.rows[6][method-1][column]=original^1
                with self.subTest(method_flags=method,column=column):
                    with self.assertRaisesRegex(ValueError,'distance method'):inspect(reader)
                reader.rows[6][method-1][column]=original
            reader.exception_sections.side_effect=lambda index: [{'clauses':[]}] if index==method else []
            with self.subTest(exception_method=method):
                with self.assertRaisesRegex(ValueError,'distance method'):inspect(reader)
        reader.exception_sections.side_effect=None
        reader.exception_sections.return_value=[]

    def test_collider_distance_bounded_integer_arithmetic(self):
        def int32(value):
            return (value+2**31)%2**32-2**31

        def distance(a,b):
            terms=(int32(a[0]-b[0]),int32(a[1]-b[1]),
                   int32(int32(-a[0]-a[1])-int32(-b[0]-b[1])))
            if -2**31 in terms:
                raise OverflowError('standard Int32 Abs contract')
            total=int32(int32(abs(terms[0])+abs(terms[1]))+abs(terms[2]))
            return total//2 if total>=0 else -((-total)//2)

        bound=(2**31-1)//8
        corners=[(q,r) for q in (-bound,0,bound) for r in (-bound,0,bound)]
        for a in corners:
            for b in corners:
                expected=(abs(a[0]-b[0])+abs(a[1]-b[1])+abs(sum(a)-sum(b)))//2
                self.assertEqual(distance(a,b),expected)
        self.assertEqual(distance((2**30,0),(0,0)),-2**30)
        with self.assertRaises(OverflowError):distance((-2**31,0),(0,0))

    def test_collider_sampling_loop_exact_entry_increment_and_condition(self):
        decode=MODULE['collider_sampling_loop']
        for update,condition in ((1446,1455),(294,303)):
            body=bytearray(condition+12)
            body[:18]=bytes.fromhex('281a1300060a027b381100040b38')+struct.pack('<i',condition-18)
            body[update:condition+11]=bytes.fromhex('07027b38110004580b07220000803f3e')+struct.pack('<i',18-(condition+11))
            body[-1]=0x2a
            result=decode(bytes(body),update,condition)
            self.assertEqual(result['condition_offset'],condition)
            self.assertEqual(result['backedge_offset'],condition+6)
            for offset in (*range(18),*range(update,condition+11)):
                with self.subTest(update=update,offset=offset):
                    changed=bytearray(body)
                    changed[offset]^=1
                    with self.assertRaises(ValueError):decode(bytes(changed),update,condition)
            with self.assertRaises(ValueError):decode(bytes(body[:condition+10]),update,condition)

    def test_local_normal_control_flow_dominance_and_bypass(self):
        inspect=MODULE['local_control_flow_evidence']
        result=inspect(bytes.fromhex('17 0c 08 26 2a'),2,1)
        self.assertTrue(result['initializer_reachable'])
        self.assertEqual(result['reads_or_addresses_not_dominated'],[])
        result=inspect(bytes.fromhex('16 2d 02 17 0c 08 26 2a'),2,4)
        self.assertEqual(result['reads_or_addresses_not_dominated'],[5])
        result=inspect(bytes.fromhex('2b 02 17 0c 08 26 2a'),2,3)
        self.assertFalse(result['initializer_reachable'])
        self.assertEqual(result['unreachable_accesses'],[3])
        self.assertEqual(result['reads_or_addresses_not_dominated'],[4])
        with self.assertRaisesRegex(ValueError,'matching store'):
            inspect(bytes.fromhex('17 0c 08 26 2a'),2,2)

    def test_local_normal_control_flow_switch_loop_and_leave_scope(self):
        inspect=MODULE['local_control_flow_evidence']
        # Switch target bypasses the initializer; the default path does not.
        body=bytes.fromhex('16 45 01000000 02000000 17 0c 08 26 2a')
        self.assertEqual(inspect(body,2,11)['reads_or_addresses_not_dominated'],[12])
        body=bytes.fromhex('17 0c 08 26 2b fc 2a')
        self.assertEqual(inspect(body,2,1)['reads_or_addresses_not_dominated'],[])
        body=bytes.fromhex('17 0c de 03 08 26 dc 08 26 2a')
        result=inspect(body,2,1)
        self.assertEqual(result['unreachable_accesses'],[4])
        self.assertEqual(result['reads_or_addresses_not_dominated'],[])
        self.assertIn('bypass finally',result['scope'])
        for body,initializer in ((b'',0),(b'\x17\x0c\x2a',9)):
            with self.assertRaisesRegex(ValueError,'invalid local initializer'):
                inspect(body,2,initializer)

    def test_local_finally_dominance_tracks_leave_and_exception(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        def section(start,length,handler,handler_length):
            return [{'clauses':[{'flags':2,'extra':0,'try_offset':start,
                                'try_length':length,'handler_offset':handler,
                                'handler_length':handler_length}]}]
        body=bytes.fromhex('17 0c de 03 08 26 dc 08 26 2a')
        result=inspect(body,section(0,4,4,3),2,1)
        self.assertEqual(result['reads_or_addresses_not_dominated'],[4])
        self.assertEqual(result['unreachable_accesses'],[])
        result=inspect(body,section(2,2,4,3),2,1)
        self.assertEqual(result['reads_or_addresses_not_dominated'],[])
        # A store in a finally dominates the leave destination, not vice versa.
        body=bytes.fromhex('de 03 17 0c dc 08 26 2a')
        result=inspect(body,section(0,2,2,3),2,3)
        self.assertEqual(result['reads_or_addresses_not_dominated'],[])
        self.assertTrue(result['initializer_reachable'])
        invalid=section(0,2,2,3)
        invalid[0]['clauses'][0]['flags']=0
        with self.assertRaisesRegex(ValueError,'only finally'):
            inspect(body,invalid,2,3)
        invalid=section(0,1,2,3)
        with self.assertRaisesRegex(ValueError,'boundary'):
            inspect(body,invalid,2,3)

    def test_local_finally_nested_unwind_and_handler_exception(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        # Inner handler initializes local2; its exceptional edge reaches the outer read.
        body=bytes.fromhex('de 05 17 0c dc 08 dc 08 26 2a')
        clauses=[{'flags':2,'extra':0,'try_offset':0,'try_length':2,
                  'handler_offset':2,'handler_length':3},
                 {'flags':2,'extra':0,'try_offset':0,'try_length':5,
                  'handler_offset':5,'handler_length':2}]
        result=inspect(body,[{'clauses':clauses}],2,3)
        self.assertTrue(result['initializer_reachable'])
        self.assertEqual(result['reads_or_addresses_not_dominated'],[5])
        self.assertEqual(result['unreachable_accesses'],[])

    def test_local_finally_rejects_invalid_transfers(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        clause={'flags':2,'extra':0,'try_offset':2,'try_length':2,
                'handler_offset':4,'handler_length':1}
        for text,message in (
                ('17 0c 2b 01 dc 2a','branch crosses'),
                ('17 0c 00 00 dc 2a','fallthrough crosses'),
                ('17 0c de 02 de 00 2a','leave inside'),
                ('17 0c de 01 dc dc','endfinally outside'),
                ('17 0c 2a 00 dc 2a','protected-region terminator')):
            current=dict(clause)
            if 'de 00' in text:current['handler_length']=2
            with self.subTest(text=text),self.assertRaisesRegex(ValueError,message):
                inspect(bytes.fromhex(text),[{'clauses':[current]}],2,1)

    def test_local_finally_rejects_unsupported_layouts(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        def clause(start,length,handler,count):
            return {'flags':2,'extra':0,'try_offset':start,'try_length':length,
                    'handler_offset':handler,'handler_length':count}
        for clauses,message in (
                ([clause(2,4,4,2)],'overlaps its own try'),
                ([clause(2,4,8,2),clause(3,4,10,2)],'overlapping finally try'),
                ([clause(2,4,8,2),clause(2,4,10,2)],'ambiguous finally try'),
                ([clause(2,6,10,2),clause(3,2,8,2)],'outside enclosing try'),
                ([clause(2,2,8,3),clause(4,2,9,1)],'overlapping finally handlers'),
                ([clause(2,2,8,4),clause(9,1,13,1)],'try inside handler')):
            with self.subTest(clauses=clauses),self.assertRaisesRegex(ValueError,message):
                inspect(bytes.fromhex('17 0c')+bytes(13)+b'\x2a',
                        [{'clauses':clauses}],2,1)

    def test_local_finally_normal_nested_unwind_order(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        body=bytes.fromhex('de 06 08 26 dc 17 0c dc 08 26 2a')
        inner={'flags':2,'extra':0,'try_offset':0,'try_length':2,
               'handler_offset':2,'handler_length':3}
        outer={'flags':2,'extra':0,'try_offset':0,'try_length':5,
               'handler_offset':5,'handler_length':3}
        for clauses in ([inner,outer],[outer,inner]):
            with self.subTest(clauses=clauses):
                result=inspect(body,[{'clauses':clauses}],2,6)
                self.assertTrue(result['initializer_reachable'])
                self.assertEqual(result['reads_or_addresses_not_dominated'],[2])
                self.assertEqual(result['unreachable_accesses'],[])

    def test_local_finally_rejects_leave_entering_regions(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        clause={'flags':2,'extra':0,'try_offset':4,'try_length':2,
                'handler_offset':6,'handler_length':1}
        for displacement in (0,2):
            with self.subTest(displacement=displacement),self.assertRaisesRegex(
                    ValueError,'leave enters protected region'):
                inspect(bytes.fromhex('17 0c de')+bytes([displacement])+
                        bytes.fromhex('de 01 dc 2a'),[{'clauses':[clause]}],2,1)

    def test_local_finally_state_budget_boundary(self):
        inspect=MODULE['local_finally_control_flow_evidence']
        for size in (99999,100000,100001):
            body=bytes.fromhex('17 0c')+bytes(size-3)+b'\x2a'
            with self.subTest(size=size):
                if size>100000:
                    with self.assertRaisesRegex(ValueError,'state budget exceeded'):
                        inspect(body,[],2,1)
                else:
                    self.assertEqual(inspect(body,[],2,1)['state_count'],size)

    def test_local_normal_control_flow_all_encodings(self):
        inspect=MODULE['local_control_flow_evidence']
        body=bytes.fromhex('17 13 02 11 02 26 12 02 26 fe 0c 02 00 26 fe 0d 02 00 26 17 fe 0e 02 00 2a')
        result=inspect(body,2,1)
        self.assertEqual(result['accesses'],[
            {'offset':1,'kind':'write'},{'offset':3,'kind':'read'},
            {'offset':6,'kind':'address'},{'offset':9,'kind':'read'},
            {'offset':14,'kind':'address'},{'offset':20,'kind':'write'}])
        self.assertEqual(result['reads_or_addresses_not_dominated'],[])

    @staticmethod
    def step_body():
        return bytes(873)+bytes.fromhex('230000000000000040086c230000000000000040285d02000a285b01000a282802000a0d0223000000000000c03f23000000000000d03f095b28ca05000a6b7d381100042a')

    def test_step_tail_and_binary32_stagnation(self):
        body=self.step_body()
        decode=MODULE['collider_step_calculation']
        result=decode(body)
        for offset in range(873,942):
            changed=bytearray(body)
            changed[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):
                decode(bytes(changed))
        for malformed in (body[:-1],body+b'\x00'):
            with self.assertRaises(ValueError):decode(malformed)
        f32=lambda value:struct.unpack('<f',struct.pack('<f',value))[0]
        for k in range(22,32):
            step=2.0**(-k-2)
            t=2.0**(22-k)
            self.assertGreater(step,0)
            self.assertLessEqual(t,1)
            self.assertEqual(f32(t+step),t)
        example=result['conditional_binary32_progress']['counterexample']
        self.assertEqual(f32(example['t']+example['step']),example['rounded_next_t'])
        for k in range(22):
            step=2.0**(-max(3,k+2))
            self.assertGreater(f32(1+step),1)

    def test_branch_targets_signed_offsets_and_instruction_boundaries(self):
        decode=MODULE['branch_targets']
        for opcode in (*range(0x2b,0x38),0xde):
            with self.subTest(opcode=opcode):
                self.assertEqual(decode(bytes((opcode,0xfe,0x2a))),
                                 [{'offset':0,'opcode':f'{opcode:04x}',
                                   'next_offset':2,'targets':[0]}])
                for displacement in (0xff,1,0x80,0x7f):
                    with self.assertRaises(ValueError):
                        decode(bytes((opcode,displacement,0x2a)))
                self.assertEqual(decode(bytes((opcode,1,0,0x2a)))[0]['targets'],[3])
                self.assertEqual(decode(bytes((0,opcode,0xfd,0x2a)))[0]['targets'],[0])
                with self.assertRaises(ValueError):decode(bytes((opcode,)))
        for opcode in (*range(0x38,0x45),0xdd):
            with self.subTest(opcode=opcode):
                body=bytes((opcode,))+struct.pack('<i',-5)+b'\x2a'
                self.assertEqual(decode(body)[0]['targets'],[0])
                self.assertEqual(decode(bytes((opcode,))+struct.pack('<i',1)+b'\x00\x2a')[0]['targets'],[6])
                self.assertEqual(decode(b'\x00'+bytes((opcode,))+struct.pack('<i',-6)+b'\x2a')[0]['targets'],[0])
                for displacement in (-4,1,-6,0x7fffffff,-0x80000000):
                    with self.assertRaises(ValueError):
                        decode(bytes((opcode,))+struct.pack('<i',displacement)+b'\x2a')
                for length in range(1,5):
                    with self.assertRaises(ValueError):decode(body[:length])
        self.assertEqual(decode(b'\x2b\x00\xfe\x1a')[0]['targets'],[2])
        with self.assertRaises(ValueError):decode(b'\x2b\x01\xfe\x1a')

    def test_switch_targets_use_end_of_entire_table(self):
        decode=MODULE['branch_targets']
        body=b'\x45'+struct.pack('<Iiii',3,-17,0,1)+b'\x00\x2a'
        self.assertEqual(decode(body),[{'offset':0,'opcode':'0045',
                                      'next_offset':17,'targets':[0,17,18]}])
        for displacement in (-16,2,-18,0x7fffffff,-0x80000000):
            with self.subTest(displacement=displacement),self.assertRaises(ValueError):
                decode(body[:5]+struct.pack('<i',displacement)+body[9:])
        for length in range(1,17):
            with self.assertRaises(ValueError):decode(body[:length])
        self.assertEqual(decode(b'\x45\x00\x00\x00\x00\x2a')[0]['targets'],[])
        self.assertEqual(decode(b'\x20\x2b\xff\x45\xdd\x2a'),[])
        with self.assertRaises(ValueError):decode(b'\x45\xff\xff\xff\xff')

    @staticmethod
    def binding_reader():
        reader=Mock()
        reader.rows={table:[[0] for _ in range(size)] for table,size in
                     ((10,0x318),(27,243),(1,204),(35,3),(43,0x1af))}
        reader.string.side_effect=lambda value:value
        reader.blob.side_effect=lambda value:bytes.fromhex(value)
        for index,parent,name,signature in (
            (745,1804,'.ctor','200001'),(746,1804,'Contains','2001021300'),
            (747,1804,'Add','2001021300'),(748,1812,'.ctor','2002011c18'),
            (56,1281,'.ctor','200001'),(6,945,'Dispose','200001'),
            (7,721,'MoveNext','200002'),(770,1876,'GetEnumerator','200015128125011300'),
            (771,1884,'get_Current','20001300'),(791,1948,'Invoke','2001021300'),
            (792,388,'op_Implicit','000115118f780113001300'),
            (790,388,'op_Implicit','000115118f78011300118cf0'),
            (213,1633,'op_Equality','0002020e0e')):
            reader.rows[10][index-1]=[parent,name,signature]
        for index,signature in ((225,'15128111010e'),(226,'1512829501128de4'),
                                (234,'15128121011e00'),(235,'15128125011e00'),
                                (243,'15128295011e00'),(48,'15118f78011e00')):
            reader.rows[27][index-1]=[signature]
        for index,scope,name,namespace in (
            (68,14,'HashSet`1','System.Collections.Generic'),(165,6,'Predicate`1','System'),
            (160,6,'Object','System'),(118,6,'IDisposable','System'),
            (90,6,'IEnumerator','System.Collections'),
            (72,6,'IEnumerable`1','System.Collections.Generic'),
            (73,6,'IEnumerator`1','System.Collections.Generic'),(204,6,'String','System')):
            reader.rows[1][index-1]=[scope,name,namespace]
        for index,name in ((1,'mscorlib'),(3,'System.Core')):
            reader.rows[35][index-1]=[4,0,0,0,0,'b77a5c561934e089',name,'','']
        reader.rows[43][430]=[5538,'0a01128de4']
        return reader

    def test_query_bindings_and_mutations(self):
        reader=self.binding_reader()
        result=MODULE['initialization_query_bindings'](reader)
        self.assertEqual(len(result['member_references']),13)
        self.assertEqual(len(result['type_references']),8)
        self.assertEqual(result['method_specification']['method_token'],'06000ad1')
        for table,rows in reader.rows.items():
            for index,row in enumerate(rows):
                if row==[0]:continue
                for column,value in enumerate(row):
                    mutations: list[int | str]=[value+1] if isinstance(value,int) else [value+'00']
                    if isinstance(value,str) and value:mutations.append(value[:-2])
                    for mutated in mutations:
                        with self.subTest(table=table,index=index,column=column,value=mutated):
                            row[column]=mutated
                            try:
                                with self.assertRaises(ValueError):
                                    MODULE['initialization_query_bindings'](reader)
                            finally:row[column]=value

    @staticmethod
    def query_reader():
        reader=Mock()
        reader.rows={6:[[0] for _ in range(0xad1)],17:[[0] for _ in range(0xf4)],
                     42:[[0,0,0,0] for _ in range(37)],44:[]}
        reader.rows[6][0xad0]=[1,0,150,0,1]
        reader.rows[17][0xf3]=[2]
        reader.rows[42][36]=[0,0,0x15a3,3]
        reader.owner={0xad1:0x369};reader.string.return_value='T'
        blobs={1:bytes.fromhex('10010215118f78011e0015128121011e0015128295011e00'),
               2:bytes.fromhex('070315128125011e001e0015118f78011e00')}
        reader.blob.side_effect=blobs.__getitem__
        reader.body.return_value=bytes.fromhex('026f0203000a0a2b19066f0303000a0b03076f1703000a2c0907281803000a0cde1f066f0700000a2ddfde0a062c06066f0600000adc7e07090004281603000a2a082a')
        reader.fat_header.return_value=bytes.fromhex('1b30020043000000f4000011')
        reader.exception_sections.return_value=[{
            'bytes':'0110000002000700252c000a00000000',
            'clauses':[{'flags':2,'try_offset':7,'try_length':37,
                        'handler_offset':44,'handler_length':10,'extra':0}]}]
        return reader,blobs
    def test_query_method_exact_bytes_and_metadata(self):
        reader,blobs=self.query_reader()
        result=MODULE['initialization_query_method'](reader)
        self.assertEqual(result['body_size'],67)
        self.assertEqual(result['exception_sections'][0]['clauses'][0]['handler_offset'],44)
        for source in (reader.body,reader.fat_header):
            original=source.return_value
            for index in range(len(original)):
                changed=bytearray(original);changed[index]^=1;source.return_value=bytes(changed)
                with self.assertRaisesRegex(ValueError,'query method'):
                    MODULE['initialization_query_method'](reader)
            for changed in (original[:-1],original+b'\x00'):
                source.return_value=changed
                with self.assertRaisesRegex(ValueError,'query method'):
                    MODULE['initialization_query_method'](reader)
            source.return_value=original
        for key,original in list(blobs.items()):
            for index in range(len(original)):
                changed=bytearray(original);changed[index]^=1;blobs[key]=bytes(changed)
                with self.assertRaisesRegex(ValueError,'query method'):
                    MODULE['initialization_query_method'](reader)
            for changed in (original[:-1],original+b'\x00'):
                blobs[key]=changed
                with self.assertRaisesRegex(ValueError,'query method'):
                    MODULE['initialization_query_method'](reader)
            blobs[key]=original
        reader.owner[0xad1]=0x368
        with self.assertRaisesRegex(ValueError,'query method'):
            MODULE['initialization_query_method'](reader)
        reader.owner[0xad1]=0x369
        reader.rows[42].append([0,0,0x15a3,3])
        with self.assertRaisesRegex(ValueError,'query method'):
            MODULE['initialization_query_method'](reader)
        reader.rows[42].pop()
        clause=reader.exception_sections.return_value[0]['clauses'][0]
        for key,value in list(clause.items()):
            clause[key]=value+1
            with self.assertRaisesRegex(ValueError,'query method'):
                MODULE['initialization_query_method'](reader)
            clause[key]=value
        for table,row_index,columns in ((6,0xad0,(1,2)),(42,36,(0,1,2))):
            for column in columns:
                reader.rows[table][row_index][column]^=1
                with self.assertRaisesRegex(ValueError,'query method'):
                    MODULE['initialization_query_method'](reader)
                reader.rows[table][row_index][column]^=1
        reader.rows[44]=[[37,1]]
        with self.assertRaisesRegex(ValueError,'query method'):
            MODULE['initialization_query_method'](reader)
        reader.rows[44]=[]
        for value in ([],[{'bytes':'00','clauses':[]}]):
            reader.exception_sections.return_value=value
            with self.assertRaisesRegex(ValueError,'query method'):
                MODULE['initialization_query_method'](reader)
    def test_query_method_missing_and_truncated_metadata(self):
        for table in (6,17,42):
            with self.subTest(table=table,case='missing'):
                reader,_=self.query_reader()
                del reader.rows[table]
                expected=KeyError if table in (6,17) else ValueError
                with self.assertRaises(expected):
                    MODULE['initialization_query_method'](reader)
            with self.subTest(table=table,case='truncated'):
                reader,_=self.query_reader()
                reader.rows[table].pop()
                expected=IndexError if table in (6,17) else ValueError
                with self.assertRaises(expected):
                    MODULE['initialization_query_method'](reader)
        for name in ('','U','TT'):
            with self.subTest(name=name):
                reader,_=self.query_reader()
                reader.string.return_value=name
                with self.assertRaisesRegex(ValueError,'query method'):
                    MODULE['initialization_query_method'](reader)
        reader,_=self.query_reader()
        del reader.owner[0xad1]
        with self.assertRaises(KeyError):
            MODULE['initialization_query_method'](reader)

    def test_query_bindings_missing_and_truncated_metadata(self):
        for table in (10,27,1,35,43):
            for missing in (True,False):
                with self.subTest(table=table,missing=missing):
                    reader=self.binding_reader()
                    if missing:del reader.rows[table]
                    else:reader.rows[table].clear()
                    with self.assertRaises(KeyError if missing else IndexError):
                        MODULE['initialization_query_bindings'](reader)

    @staticmethod
    def consumer_reader():
        declarations=(
            (0xa49,0x35b,145,'000001','13300300aa000000dd000011','73e902000a0a7e760900040b160c388d00000007089a0d092d102096d9d3d2287c07000673dc0900067a097be10900042d2a208bd9d3d2287c070006097be2090004283f0d000620a3d9d3d2287c070006281d01000a73dc0900067a06097be10900046fea02000a2c2520d8d9d3d2287c070006097be109000420d50ad3d2287c070006281d01000a73dc0900067a06097be10900046feb02000a260817580c08078e693f6affffff2a'),
            (0xa4a,0x35b,150,'000115118f7801128de40e','1330030024000000de000011','734b0a00060a06027d770900047e7609000406fe064c0a000673ec02000a28af01002b2a'),
            (0xa4b,0x35c,6278,'200001','1e','02283800000a2a'),
            (0xa4c,0x35c,131,'200102128de4','4a','037be1090004027b7709000428d500000a2a'))
        reader=Mock();reader.rows={6:[[0] for _ in range(0xa4c)]};reader.owner={}
        bodies={};headers={};signatures={}
        for method,owner,flags,signature,header,body in declarations:
            reader.rows[6][method-1]=[1,0,flags,0,method]
            reader.owner[method]=owner
            bodies[method]=bytes.fromhex(body);headers[method]=bytes.fromhex(header)
            signatures[method]=bytes.fromhex(signature)
        reader.body.side_effect=bodies.__getitem__
        reader.fat_header.side_effect=headers.__getitem__;reader.tiny_header.side_effect=headers.__getitem__
        reader.blob.side_effect=signatures.__getitem__
        return reader,bodies,headers,signatures
    def test_array_consumers_reject_body_header_signature_and_declaration_mutations(self):
        reader,bodies,headers,signatures=self.consumer_reader()
        result=MODULE['initialization_array_consumers'](reader)
        self.assertEqual([entry['body_size'] for entry in result['methods']],[170,36,7,18])
        for method in bodies:
            for values in (bodies,headers,signatures):
                original=values[method]
                for index in range(len(original)):
                    with self.subTest(method=method,index=index,kind=id(values)):
                        changed=bytearray(original);changed[index]^=1;values[method]=bytes(changed)
                        with self.assertRaisesRegex(ValueError,'array consumer'):
                            MODULE['initialization_array_consumers'](reader)
                for changed in (original[:-1],original+b'\x00'):
                    values[method]=changed
                    with self.assertRaisesRegex(ValueError,'array consumer'):
                        MODULE['initialization_array_consumers'](reader)
                values[method]=original
            row=reader.rows[6][method-1]
            for index in (1,2):
                row[index]^=1
                with self.assertRaisesRegex(ValueError,'array consumer'):
                    MODULE['initialization_array_consumers'](reader)
                row[index]^=1
            reader.owner[method]^=1
            with self.assertRaisesRegex(ValueError,'array consumer'):
                MODULE['initialization_array_consumers'](reader)
            reader.owner[method]^=1
    @staticmethod
    def publication_tail():
        fields=(0x959,0x95a,0x95b,0x95c,0x95d,0x95e,0x95f,0x960,
                0x961,0x962,0x963,0x964,0x965,0x966,0x967,0x968,
                0x969,0x96a,0x96b,0x96c,0x973,0x96d,0x974,0x975,
                0x96e,0x96f,0x970,0x971,0x972)
        tail=bytes.fromhex('1f1d8d79030002')
        for index,field in enumerate(fields):
            tail+=b'\x25'+(bytes([0x16+index]) if index<9 else bytes([0x1f,index]))
            tail+=b'\x7e'+struct.pack('<I',0x04000000+field)+b'\xa2'
        return tail+bytes.fromhex('807609000428490a00062a')
    def test_publication_tail_rejects_every_changed_byte_and_truncation(self):
        tail=self.publication_tail();prefix=b'\0'*6530
        result=MODULE['initialization_array_publication'](prefix+tail)
        self.assertEqual(len(tail),270)
        self.assertEqual(result['field_tokens'][9],'04000962')
        self.assertEqual(len(result['field_tokens']),29)
        for index in range(len(tail)):
            with self.subTest(index=index):
                changed=bytearray(tail);changed[index]^=1
                with self.assertRaisesRegex(ValueError,'publication bytes'):
                    MODULE['initialization_array_publication'](prefix+changed)
        for changed in (tail[:-1],tail+b'\x00'):
            with self.assertRaisesRegex(ValueError,'publication bytes'):
                MODULE['initialization_array_publication'](prefix+changed)
    def test_field_inventory_counts_rva_and_preserves_all_sites(self):
        reader=Mock()
        reader.rows={6:[[1],[0],[2]]}
        reader.body.side_effect=[bytes.fromhex('027be70900047fe70900042a'),
                                 bytes.fromhex('7e6209000480620900042a')]
        result=MODULE['field_reference_inventory'](reader,(0x040009e7,0x04000962))
        self.assertEqual((result['methods_with_rva'],result['decoded_methods']),(2,2))
        self.assertTrue(result['complete'])
        self.assertEqual(result['decode_failures'],[])
        self.assertEqual([(r['method_token'],r['offset'],r['opcode'],r['field_token'])
                          for r in result['references']],
                         [('06000001',1,'007b','040009e7'),
                          ('06000001',6,'007f','040009e7'),
                          ('06000003',0,'007e','04000962'),
                          ('06000003',5,'0080','04000962')])
        self.assertEqual([c.args for c in reader.body.call_args_list],[(1,),(3,)])
    def test_field_inventory_reports_failures_without_partial_method_sites(self):
        for broken in (bytes.fromhex('7be709000428'),bytes.fromhex('fe'),
                       bytes.fromhex('450200000000000000'),bytes.fromhex('ff')):
            reader=Mock();reader.rows={6:[[1],[2]]}
            reader.body.side_effect=[broken,bytes.fromhex('7e620900042a')]
            result=MODULE['field_reference_inventory'](reader,(0x040009e7,0x04000962))
            self.assertFalse(result['complete'])
            self.assertEqual((result['methods_with_rva'],result['decoded_methods']),(2,1))
            self.assertEqual(result['decode_failures'][0]['method_token'],'06000001')
            self.assertEqual(len(result['references']),1)
            self.assertEqual(result['references'][0]['method_token'],'06000002')
    def test_initialization_inventory_snapshot(self):
        expected=json.loads((ROOT/'CurrentInitializationInventory.json').read_text())
        inventory=expected['field_reference_inventory']
        reader=Mock();reader.data=b'synthetic inventory'
        reader.rows={6:[[0] for _ in range(0x121b)]}
        bodies={}
        for site in inventory['references']:
            method=int(site['method_token'],16)&0xffffff
            body=bodies.setdefault(method,bytearray())
            offset=site['offset']
            body.extend(b'\0'*max(0,offset+5-len(body)))
            body[offset:offset+5]=bytes([int(site['opcode'],16)])+int(site['field_token'],16).to_bytes(4,'little')
        body=bodies[0xa48]
        body.extend(b'\0'*(6800-len(body)))
        body[-1]=0x2a
        body[6530:]=self.publication_tail()
        for method in range(1,len(reader.rows[6])+1):
            if len(bodies)>=inventory['methods_with_rva']:break
            bodies.setdefault(method,bytearray(b'\x2a'))
        for method in bodies:reader.rows[6][method-1]=[1]
        reader.body.side_effect=lambda method:bytes(bodies[method])
        real_sha=hashlib.sha256
        def digest(data):
            if data==reader.data:return Mock(hexdigest=lambda:expected['assembly_sha256'])
            if data==bytes(body):return Mock(hexdigest=lambda:expected['ordinary_static_method']['il_sha256'])
            return real_sha(data)
        with patch.dict(MODULE['current_initialization_inventory'].__globals__,
                        initialization_array_consumers=lambda _:expected['array_consumers'],
                        initialization_query_method=lambda _:expected['query_method'],
                        initialization_query_bindings=lambda _:expected['query_bindings']), \
             patch.object(MODULE['hashlib'],'sha256',side_effect=digest):
            actual=MODULE['current_initialization_inventory'](reader)
        self.assertEqual(actual['field_reference_inventory'],inventory)
        # Synthetic padding preserves offsets, not the production instruction count.
        actual['ordinary_static_method']['instruction_count']=expected['ordinary_static_method']['instruction_count']
        self.assertEqual(actual,expected)
    def test_initialization_inventory_records_encoded_transfers_not_operand_bytes(self):
        body=bytes.fromhex(
            '200000002a'          # ldc.i4 operand contains ret, not an instruction
            '2b00'                # br.s
            '3800000000'          # br
            '45020000000000000000000000'  # switch, two targets
            '2901000011'          # calli
            '2701000006'          # jmp
            '7a'                  # throw
            'dc'                  # endfinally
            'dd00000000'          # leave
            'de00'                # leave.s
            'fe11'                # endfilter
            'fe1a'                # rethrow
            '2801000006'          # ordinary call is deliberately excluded
            '2a2a')
        reader=Mock();reader.data=b'synthetic transfers'
        reader.rows={6:[]};reader.body.return_value=body
        expected_hash='ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5'
        real_sha=hashlib.sha256
        with patch.dict(MODULE['current_initialization_inventory'].__globals__,
                        initialization_array_publication=lambda _: {'synthetic':True},
                        initialization_array_consumers=lambda _: {'synthetic':True},
                        initialization_query_method=lambda _: {'synthetic':True},
                        initialization_query_bindings=lambda _: {'synthetic':True}), \
             patch.object(MODULE['hashlib'],'sha256',side_effect=lambda data:
                          Mock(hexdigest=lambda:expected_hash) if data==reader.data else real_sha(data)):
            result=MODULE['current_initialization_inventory'](reader)
        method=result['ordinary_static_method']
        self.assertEqual(method['explicit_control_transfers'],[
            {'offset':offset,'opcode':opcode} for offset,opcode in
            ((5,'002b'),(7,'0038'),(12,'0045'),(25,'0029'),(30,'0027'),
             (35,'007a'),(36,'00dc'),(37,'00dd'),(42,'00de'),(44,'fe11'),(46,'fe1a'))])
        self.assertEqual(method['return_offsets'],[53,54])
        self.assertEqual(method['instruction_count'],15)
        self.assertEqual(method['body_size'],55)
        self.assertFalse(result['certification_eligible'])
    def test_initialization_inventory_rejects_other_builds(self):
        reader=Mock();reader.data=b'not the supported assembly'
        with self.assertRaisesRegex(ValueError,'unsupported current initialization inventory'):
            MODULE['current_initialization_inventory'](reader)
    def test_metadata_requires_a_table_stream(self):
        data=bytearray(512)
        def number(offset,value,size=4):
            data[offset:offset+size]=value.to_bytes(size,'little')
        number(0x3c,64)
        data[64:68]=b'PE\0\0'
        number(70,1,2)
        number(84,224,2)
        number(88,0x10b,2)
        number(296,400)
        for offset,value in ((320,511),(324,1),(328,511),(332,1),(408,416)):
            number(offset,value)
        data[416:420]=b'BSJB'
        with patch.object(Path,'read_bytes',return_value=bytes(data)):
            with self.assertRaisesRegex(ValueError,'missing metadata table stream'):
                MODULE['Metadata']('synthetic.exe')
    @staticmethod
    def metadata_image(tables,stream='#~'):
        data=bytearray(2048)
        def number(offset,value,size=4):
            data[offset:offset+size]=value.to_bytes(size,'little')
        number(0x3c,64);data[64:68]=b'PE\0\0'
        number(70,1,2);number(84,224,2);number(88,0x10b,2)
        number(296,400)
        for offset,value in ((320,2047),(324,1),(328,2047),(332,1),(408,416)):
            number(offset,value)
        data[416:420]=b'BSJB'
        number(434,1,2)
        number(436,64);number(440,1024)
        data[444:448]=stream.encode('ascii')+b'\0\0'
        number(488,sum(1<<table for table in tables),8)
        position=504
        for table in sorted(tables):
            number(position,len(tables[table]));position+=4
        for table in sorted(tables):
            for row in tables[table]:
                for kind,value in zip(MODULE['TABLES'][table],row):
                    width=4 if kind=='u4' else 2
                    number(position,value,width);position+=width
        return bytes(data)
    def test_metadata_constructor_ownership_and_pointer_tables(self):
        for stream in ('#~','#-'):
            tables={2:[[0,0,0,0,1,1],[0,0,0,0,2,2],[0,0,0,0,2,2]],
                    4:[[0,0,0] for _ in range(3)],
                    6:[[0,0,0,0,0,1] for _ in range(3)]}
            with patch.object(Path,'read_bytes',return_value=self.metadata_image(tables,stream)):
                reader=MODULE['Metadata']('synthetic.exe')
                self.assertEqual(reader.owner,{1:1,2:3,3:3})
                self.assertEqual(reader.field_owner,reader.owner)
            for column in (4,5):
                for starts in ((1,3,2),(0,2,2),(2,2,2),(1,2,5)):
                    malformed={table:[row[:] for row in rows] for table,rows in tables.items()}
                    for row,start in zip(malformed[2],starts):row[column]=start
                    with self.subTest(stream=stream,column=column,starts=starts):
                        with patch.object(Path,'read_bytes',return_value=self.metadata_image(malformed,stream)):
                            with self.assertRaisesRegex(ValueError,'ownership ranges'):
                                MODULE['Metadata']('synthetic.exe')
            for table in (3,5,7,19,22):
                with self.subTest(stream=stream,pointer=table):
                    with patch.object(Path,'read_bytes',return_value=self.metadata_image({table:[[1]]},stream)):
                        with self.assertRaisesRegex(ValueError,'unsupported metadata pointer table'):
                            MODULE['Metadata']('synthetic.exe')
    def blob_reader(self, data, size=None):
        reader=object.__new__(MODULE['Metadata'])
        reader.data=b'\0'+data
        reader.streams={'#Blob':(1,len(data) if size is None else size)}
        return reader
    def test_blob_payload_and_compressed_lengths(self):
        for prefix,length in ((b'\x02',2),(b'\x80\x80',128),(b'\xc0\x00\x40\x00',16384)):
            with self.subTest(prefix=prefix.hex()):
                self.assertEqual(self.blob_reader(b'\0'+prefix+b'x'*length).blob(1),b'x'*length)
        self.assertEqual(self.blob_reader(b'\0').blob(0),b'')
    def test_blob_requires_a_heap(self):
        reader=self.blob_reader(b'\0')
        reader.streams={}
        with self.assertRaisesRegex(ValueError,'missing blob heap'):reader.blob(0)
    def test_blob_rejects_out_of_heap_and_truncated_data(self):
        for data,index,size in ((b'\0',-1,1),(b'\0',1,1),(b'\0\x80',1,2),
                                (b'\0\xc0\x00\x00',1,4),(b'\0\xe0',1,2),
                                (b'\0\x02x',1,3),(b'\0\x02xx',1,3),(b'\0\x02x',1,4)):
            with self.subTest(data=data.hex(),index=index,size=size):
                with self.assertRaises(ValueError):self.blob_reader(data,size).blob(index)
    def test_current_collider_audit_rejects_other_builds(self):
        reader=self.blob_reader(b'\0')
        with self.assertRaisesRegex(ValueError,'unsupported current collider assembly hash'):
            MODULE['current_collider_audit'](reader)
    def test_member_ownership_ranges(self):
        for table,column in ((6,5),(4,4)):
            reader=self.blob_reader(b'\0')
            reader.rows={table:[[] for _ in range(3)],2:[[0,0,0,0,1,1] for _ in range(3)]}
            for row,start in zip(reader.rows[2],(1,2,2)):row[column]=start
            self.assertEqual(reader.member_owners(table,column),{1:1,2:3,3:3})
            for starts in ((1,3,2),(0,2,2),(2,2,2),(1,2,5)):
                for row,start in zip(reader.rows[2],starts):row[column]=start
                with self.subTest(table=table,starts=starts):
                    with self.assertRaisesRegex(ValueError,'ownership ranges'):
                        reader.member_owners(table,column)
            reader.rows[2]=[]
            with self.assertRaisesRegex(ValueError,'ownership ranges'):
                reader.member_owners(table,column)
            reader.rows[table]=[]
            self.assertEqual(reader.member_owners(table,column),{})

    def current_collider_fixture(self):
        reader=self.blob_reader(b'\0')
        initializer=bytearray(354)
        for offset,field,value in zip(range(304,345,10),range(0x113d,0x1142),(29,15,20,24,20)):
            initializer[offset:offset+10]=b'\x22'+struct.pack('<f',value)+b'\x80'+struct.pack('<I',0x04000000|field)
        bodies={0x110b:bytes(initializer),0x113d:bytes.fromhex(
            '02037d4911000402047d4a110004032c0403172e0c027e3d1100047d4b1100042a027e3f1100047d4b1100042a')}
        bodies[0x111f]=b'\x00'*703+bytes.fromhex('08121dfe1599040002121d187d49110004121d11197b820300047d4a110004121d7e401100047d4b110004111d6fcf05000a')
        bodies[0x111f]+=b'\x00'*(881-len(bodies[0x111f]))+bytes.fromhex('08121dfe1599040002121d197d49110004121d7e4904000411226f100800067d4a110004121d7e411100047d4b110004111d6fcf05000a')
        for offset,layout in ((256,'0816110e733d1100066fcf05000a'),
                              (491,'08161117733d1100066fcf05000a'),
                              (642,'081711197b82030004733d1100066fcf05000a')):
            body=bytearray(bodies[0x111f]);encoded=bytes.fromhex(layout)
            body[offset:offset+len(encoded)]=encoded;bodies[0x111f]=bytes(body)
        bodies[0x111f]+=b'\x00'*(1015-len(bodies[0x111f]))+bytes.fromhex(
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
            '027b2f110004112d112c112e6fe105000a6fe8110006')+b'\x00'
        body=bytearray(bodies[0x111f]+b'\x00'*51)
        body[978:1015]=bytes.fromhex(
            '16132338bf0100000811236fde05000a1324112317581325388a0100000811256fde05000a')
        body[1395:1446]=bytes.fromhex(
            '1125175813251125086fe205000a3f69feffff0211247b4a1100042824110006'
            '1123175813231123086fe205000a3f34feffff')
        body.extend(b'\x00'*(1466-len(body)))
        body[:18]=bytes.fromhex('281a1300060a027b381100040b389d050000')
        body[1446:1466]=bytes.fromhex('07027b38110004580b07220000803f3e58faffff')
        body[547:784]=MODULE['CATEGORY12_WINDOW']
        bodies[0x111f]=bytes(body)
        body=bytearray(6298)
        for offset,local in ((2197,31),(2729,43),(3352,53),(3861,63),(4726,68),(5260,74),(6254,86)):
            encoded=bytes.fromhex('1220fe15990400021220167d49110004122011')+bytes([local])+bytes.fromhex('7d4a11000412207e3e1100047d4b11000411206fcf05000a')
            body[offset:offset+len(encoded)]=encoded
        bodies[0x111c]=bytes(body)
        reader.rows={1:[[6,5,6] for _ in range(0x4f)],35:[[4,0,0,0,0,1,7,0,0]],
                     4:[[0,0,1] for _ in range(0x1155)],
                     6:[[0,0,0,0,2] for _ in range(0x113d)],
                     2:[[0,1,2] for _ in range(0x4e2)]}
        for typedef,start in ((0x499,0x1149),(0x49a,0x114c),(0x49d,0x1155),(0x49e,0x1156)):
            reader.rows[2][typedef-1]=[0,1,2,0,start,0]
        for field,blob in ((0x1149,4),(0x114a,5)):
            reader.rows[4][field-1]=[0,0,blob]
        reader.rows[10]=[[0,0,0] for _ in range(0x5cf)]
        reader.rows[10][0x5cf-1]=[0xd3c,4,6]
        reader.rows[27]=[[7] for _ in range(0x1a7)]
        reader.rows[4][0x1155-1]=[0,3,3]
        blobs={1:b'\x06\x0c',2:bytes.fromhex('200201119274119388'),3:b'\x06\x08',
               4:bytes.fromhex('06119274'),5:bytes.fromhex('06119388'),
               6:bytes.fromhex('2001011300'),7:bytes.fromhex('1512813d01119264')}
        reader.blob=lambda index:blobs[index]
        reader.body=lambda method:bodies[method]
        reader.string=lambda index:{1:'synthetic',2:'',3:'value__',4:'Add',
                                    5:'List`1',6:'System.Collections.Generic',7:'mscorlib'}[index]
        reader.rows[6].extend([[0,0,0,0,0,0] for _ in range(0x1347-len(reader.rows[6]))])
        for index,signature in {0x1347:'00020c119388119388',0xd3f:'00010e128f68',
                                0xab6:'00030c0c0c0c',0x11e8:'2003010c0e1d119388'}.items():
            blobs[0x10000+index]=bytes.fromhex(signature)
            reader.rows[6][index-1][4]=0x10000+index
        reader.rows[10].extend([[0,0,0] for _ in range(0x5e1-len(reader.rows[10]))])
        blobs[0x20000]=bytes.fromhex('20001d1300')
        reader.rows[10][0x5e1-1][2]=0x20000
        reader.rows[6].extend([[0,0,0,0,0,0] for _ in range(0x1350-len(reader.rows[6]))])
        reader.rows[4].extend([[0,0,1] for _ in range(0x12b5-len(reader.rows[4]))])
        reader.rows[2].append([0,1,2,0,0x12b6,0x1359])
        reader.rows[2][0x4e2-1]=[0,1,2,0,0x12b3,0x133b]
        reader.rows[27].extend([[7] for _ in range(0x1bb-len(reader.rows[27]))])
        blobs[0x20001]=bytes.fromhex('1512813d01119388')
        reader.rows[27][0x1bb-1]=[0x20001]
        for member,name,sig in ((0x5df,13,'200001'),(0x5e0,4,'2001011300'),
                                (0x5e1,14,'20001d1300')):
            blobs[0x40000+member]=bytes.fromhex(sig)
            reader.rows[10][member-1]=[3548,name,0x40000+member]
        numerical={
            0x1347:('00020c119388119388','020328501300060a120028401300062a'),
            0x1350:('0002119388119388119388','027bb3120004037bb312000459027bb4120004037bb412000459733b1300062a'),
            0x1340:('20000c','027bb3120004027bb31200045a027bb4120004027bb41200045a586c280607000a6b2a'),
            0x133b:('2002010c0c','02037db312000402047db41200042a'),
            0xab6:('00030c0c0c0c','020328e901000a04284a00000a2a')}
        for method,(sig,layout) in numerical.items():
            bodies[method]=bytes.fromhex(layout)
            blobs[0x10000+method]=bytes.fromhex(sig)
            reader.rows[6][method-1][4]=0x10000+method
        reader.rows[1].extend([[6,1,2] for _ in range(0x99-len(reader.rows[1]))])
        reader.rows[1][0x99-1]=[6,8,9]
        reader.rows[35][0]=[4,0,0,0,0,8,7,2,0]
        blobs[8]=bytes.fromhex('b77a5c561934e089');blobs[0]=b''
        reader.rows[10].extend([[0,0,0] for _ in range(0x706-len(reader.rows[10]))])
        for member,name,sig in ((0x706,10,'00010d0d'),(0x1e9,11,'00020c0c0c'),(0x4a,12,'00020c0c0c')):
            blobs[0x30000+member]=bytes.fromhex(sig)
            reader.rows[10][member-1]=[1225,name,0x30000+member]
        reader.string=lambda index:{1:'synthetic',2:'',3:'value__',4:'Add',
                                    5:'List`1',6:'System.Collections.Generic',7:'mscorlib',
                                    8:'Math',9:'System',10:'Sqrt',11:'Max',12:'Min',
                                    13:'.ctor',14:'ToArray'}[index]
        for owner,start,end in ((0x4b6,4570,4628),(0x2b2,2228,2248),(0x4c8,4730,4797)):
            reader.rows[2][owner-1]=[0,1,2,0 if owner==0x4b6 else 0x4b6<<2,0,start]
            reader.rows[2][owner]=[0,1,2,0,0,end]
        for method,flag,layout in ((0x11e8,0x5c6,''),(0x8c7,0xc6,'02177dd70400042a'),
                (0x12a9,0xc6,'032200000000320803220000803f3710203b66d2d2287c070006735404000a7a02287c1200060a120028b106000a2d2c02731f06000625037d0a02000425057d0b02000425047d0c02000428bf06000a287d12000602177d291200042a')):
            reader.rows[6][method-1]=[0,0,flag,15,0x10000+0x11e8,0]
            bodies[method]=bytes.fromhex(layout)
        reader.rows[25]=[[0x4c8,0x1289<<1,0xd08<<1]]
        reader.string=lambda index:{0:'',15:'report',1:'synthetic',2:'',3:'value__',4:'Add',
                                    5:'List`1',6:'System.Collections.Generic',7:'mscorlib',
                                    8:'Math',9:'System',10:'Sqrt',11:'Max',12:'Min',
                                    13:'.ctor',14:'ToArray'}[index]
        for owner,start,end in ((0x4c8,0x1228,0x122a),(0x2b2,0x4d7,0x4d8),(0x1fe,522,525)):
            for index in (owner-1,owner):
                reader.rows[2][index]+=([0]*max(0,6-len(reader.rows[2][index])))
            reader.rows[2][owner-1][4]=start
            reader.rows[2][owner][4]=end
        reader.rows[2][0x1fe-1][5]=1567
        reader.rows[2][0x1fe][5]=1568
        for method,owner,flags,sig,layout in (
                (0x127c,0x4c8,0x86,'200015118f78011287f8','027b281200042a'),
                (0x127d,0x4c8,0x81,'20010115118f78011287f8','02037d281200042a'),
                (0x61f,0x1fe,0x1886,'200001','02283800000a2a'),
                (0x8ba,0x2b2,0x8c6,'200002','027bd70400042a'),
                (0x1283,0x4c8,0x8c6,'200002','02287c1200060a120028b106000a2a')):
            reader.rows[6][method-1]=[1,0,flags,0,0x50000+method,0]
            blobs[0x50000+method]=bytes.fromhex(sig)
            bodies[method]=bytes.fromhex(layout)
        for field,flags,sig in ((0x1228,1,'0615118f78011287f8'),
                (0x1229,1,'0602'),(0x4d7,1,'0602'),(0x20a,6,'060c'),
                (0x20b,6,'061d119388'),(0x20c,6,'060e')):
            reader.rows[4][field-1]=[flags,0,0x60000+field]
            blobs[0x60000+field]=bytes.fromhex(sig)
        reader.rows[2][0x3de-1]=[1048841,0,0,913,2994,3423]
        reader.rows[2][0x3de]=[1048841,0,0,913,2996,3441]
        reader.field_owner={0xbb2:0x3de,0xbb3:0x3de}
        reader.owner={method:0x3de for method in (0xd5f,0xd60,0xd61,0xd64,0xd65,0xd66)}
        reader.owner[0xb54]=0x379
        reader.rows[42]=[[0,0,0x3de<<1,0]]
        reader.rows[27].extend([[0] for _ in range(0x1fe-len(reader.rows[27]))])
        for spec,sig in ((0x38,'15118f78011300'),(0x39,'1300'),(0x70,'15118f7801118f04'),(0x1fe,'15118f78011287f8')):
            reader.rows[27][spec-1]=[0x70000+spec]
            blobs[0x70000+spec]=bytes.fromhex(sig)
        for method,flags,sig,layout in (
                (0xd5f,6278,'200201021300','0203287d04000a02047d7e04000a2a'),
                (0xd60,134,'200002','027b7f04000a2a'),
                (0xd61,129,'20010102','02037d7f04000a2a'),
                (0xd64,150,'000115118f780113001300','028c3900001b2d102024f0d3d2287c07000673d002000a7a1702738004000a2a'),
                (0xd65,2198,'000115118f780113001300','02288104000a2a'),
                (0xd66,2198,'000115118f78011300118cf0','161200fe153900001b06738004000a2a')):
            reader.rows[6][method-1]=[1,0,flags,0,0x80000+method,0]
            blobs[0x80000+method]=bytes.fromhex(sig)
            bodies[method]=bytes.fromhex(layout)
        reader.rows[6][0xb54-1]=[1,0,134,0,0x80000+0xb54,0]
        blobs[0x80000+0xb54]=bytes.fromhex('200115118f7801118f0408')
        bodies[0xb54]=bytes.fromhex('027be709000403a3c1030002287703000a2a')
        for field,sig in ((0xbb2,'0602'),(0xbb3,'061300')):
            reader.rows[4][field-1]=[1,0,0x90000+field]
            blobs[0x90000+field]=bytes.fromhex(sig)
        for member,parent,table,target in ((0x47d,452,6,0xd61),(0x47e,452,4,0xbb3),
                (0x47f,452,4,0xbb2),(0x480,452,6,0xd5f),(0x481,452,6,0xd64),
                (0x377,900,6,0xd65),(0x6b1,4084,6,0xd60),(0x6bf,4084,6,0xd65),(0x6ba,4084,6,0xd66)):
            target_row=reader.rows[table][target-1]
            reader.rows[10][member-1]=[parent,0,target_row[4 if table==6 else 2]]
        for method,flags,sig,layout in (
                (0x1293,0x86,'200001','02288e1200062c012a026fe11100062d070216289512000602027c23120004288602000a6ff4070006286f02002b7d1a120004027e0709000428a405000a7d2312000402167d24120004027e0709000428ba06000a287d12000602167d1c120004027e07090004289f01000a7d1d120004027e07090004289f01000a7d1e1200040222000000007d26120004027bb81100046f7a06000a027bb91100046f7a06000a027bba1100046f7a06000a02167d2b120004027e07090004289f01000a7d2c1200042a'),
                (0x128e,0x81,'200002','7ee80a00046f6b0c000602fe0116fe012a'),
                (0x11e1,0x5c6,'20001189c4',''),
                (0x1295,0x81,'2001011189c4','207767d2d2287c070006188da00000012516026fe11100068c71020002a22517038c71020002a228e70a000673dc0900067a')):
            reader.rows[6][method-1]=[0 if method==0x11e1 else 1,0,flags,0,0xb0000+method,0]
            blobs[0xb0000+method]=bytes.fromhex(sig)
            bodies[method]=bytes.fromhex(layout)
        for method,owner,flags,sig,layout in (
                (0xc6b,0x3b7,0x86,'2000128f30','027bfc0a00046f0f04000a2a'),
                (0xae7,0x369,0x96,'00020e0e1d1c','282d03000a0203282e03000a2a'),
                (0x9dc,0x337,0x1886,'2001010e','0203282b02000a2a')):
            reader.rows[2][owner-1]=[0,1,2,0,0,method]
            reader.rows[2][owner]=[0,1,2,0,0,method+1]
            reader.rows[6][method-1]=[1,0,flags,0,0xb0000+method,0]
            blobs[0xb0000+method]=bytes.fromhex(sig)
            bodies[method]=bytes.fromhex(layout)
        reader.rows[2][0x21f-1]=[1048832,0,0,641,623,1676]
        reader.rows[2][0x21f]=[0,0,0,0,624,1681]
        reader.rows[2][0x3b7-1][4]=2792
        reader.rows[2][0x3b7][4]=2829
        reader.rows[42].append([0,0,1086,0])
        for rid,sig in ((0x134,'1512887c01128f30'),(0x5d,'1512813d011300'),(0x5e,'1512887c011300')):
            reader.rows[27][rid-1]=[0xc0000+rid]
            blobs[0xc0000+rid]=bytes.fromhex(sig)
        names={0xd0000:'InvalidOperationException',0xd0001:'System',0xd0002:'wrapperStorage',0xd0003:'wrapperPeek'}
        reader.rows[10].extend([[0,0,0] for _ in range(0x5e2-len(reader.rows[10]))])
        for rid,name,sig in ((0x5de,'get_Item','2001130008'),(0x5e2,'get_Count','200008')):
            names[0x120000+rid]=name
            reader.rows[10][rid-1]=[3388,0x120000+rid,0x130000+rid]
            blobs[0x130000+rid]=bytes.fromhex(sig)
        previous_string=reader.string
        reader.string=lambda index:names[index] if index in names else previous_string(index)
        reader.rows[1][0x7d-1]=[6,0xd0000,0xd0001]
        for rid,flags,sig,name in ((0x26f,38,'061512813d011300',0xd0002),(0xafc,1,'061512887c01128f30',0)):
            reader.rows[4][rid-1]=[flags,name,0xe0000+rid]
            blobs[0xe0000+rid]=bytes.fromhex(sig)
        for rid,parent,name,sig in ((0xdc,748,'.ctor','200001'),(0xdd,756,'wrapperStorage','061512813d011300'),
                (0xde,748,'get_Count','200008'),(0xdf,748,'Add','2001011300'),(0xe0,1001,'.ctor','2001010e'),
                (0xe1,748,'get_Item','2001130008'),(0xe2,748,'RemoveAt','20010108'),(0x40f,2468,'wrapperPeek','20001300')):
            names[0xf0000+rid]=name
            reader.rows[10][rid-1]=[parent,0xf0000+rid,0x100000+rid]
            blobs[0x100000+rid]=bytes.fromhex(sig)
        for method,flags,sig,layout in (
                (0x68c,6278,'200001','0273dc00000a7ddd00000a02283800000a2a'),
                (0x68d,134,'200008','027bdd00000a6fde00000a2a'),
                (0x68e,134,'2001011300','027bdd00000a036fdf00000a2a'),
                (0x68f,134,'20001300','027bdd00000a6fde00000a2d1020d178d2d2287c07000673e000000a7a027bdd00000a6fde00000a17590a027bdd00000a066fe100000a027bdd00000a066fe200000a2a'),
                (0x690,134,'20001300','027bdd00000a6fde00000a2d1020d178d2d2287c07000673e000000a7a027bdd00000a027bdd00000a6fde00000a17596fe100000a2a')):
            reader.rows[6][method-1]=[1,0,flags,0xd0003 if method==0x690 else 0,0x110000+method,0]
            blobs[0x110000+method]=bytes.fromhex(sig)
            bodies[method]=bytes.fromhex(layout)
        for owner,field_start,method_start,next_field,next_method in (
                (0x285,0x449,0x80c,0x44c,0x812),(0x3be,0xb23,0xca6,0xb27,0xcbb),
                (0x495,0x1121,0x110a,0x1142,0x113b),(0x3c9,0xb54,0xcee,0xb5a,0xd05)):
            reader.rows[2][owner-1]=[0,0,0,0,field_start,method_start]
            reader.rows[2][owner]=[0,0,0,0,next_field,next_method]
        for rid,flags,sig,layout in (
                (0x1124,129,'200101119388','7e49040004037eb51200046f110800060a7e260b00040b160c2b4b0708a3be0300020d060928af0c000613047e4904000411047eb51200046f0f0800061305031105284713000622000000407e3d1100045a340e027b3511000411046f3301000a260817580c08078e6932af2a'),
                (0x810,134,'2001119388118ef8','027b4b0400047bb3120004037b230b00046b220000003f037b240b00046b5a585a027b4b0400047bb4120004037b240b00046b5a0a06733b1300062a'),
                (0x80f,134,'2002119388118ef8119388','0402032810080006284f1300062a'),
                (0x811,134,'2002118ef8119388119388','030428501300060a067bb3120004027b4b0400047bb31200045b220000003f067bb41200045a027b4b0400047bb41200045b59067bb4120004027b4b0400047bb41200045b0b256507590c256c285b01000a690d076c285b01000a691304086c285b01000a691305096b59280a02000a13060711046b59280a02000a13070811056b59280a02000a130811061107360f1106110836091104651105590d2b16110711083609096511055913042b070965110459130509110473a60c00062a'),
                (0xca6,6278,'2002010808','02037d230b000402047d240b00042a'),
                (0xcaf,2198,'0002118ef8118ef8118ef8','027b230b0004037b230b000458027b240b0004037b240b00045873a60c00062a'),
                (0xca7,6289,'000001','1c8dbe0300022516171673a60c0006a4be0300022517161773a60c0006a4be0300022518151773a60c0006a4be0300022519151673a60c0006a4be030002251a161573a60c0006a4be030002251b171573a60c0006a4be03000280250b00041d8dbe0300022516161673a60c0006a4be0300022517171673a60c0006a4be0300022518161773a60c0006a4be0300022519151773a60c0006a4be030002251a151673a60c0006a4be030002251b161573a60c0006a4be030002251c171573a60c0006a4be03000280260b00042a'),
                (0x80c,6278,'200101118f24','02283800000a020f0128f10c00067d4b0400042a'),
                (0xcf1,134,'2000119388','027b540b00046b027b550b00046b733b1300062a')):
            reader.rows[6][rid-1]=[1,0,flags,0,0x120000+rid,0]
            blobs[0x120000+rid]=bytes.fromhex(sig)
            bodies[rid]=bytes.fromhex(layout)
        for rid,flags,sig in ((0x449,22,'06128a14'),(0x44b,38,'06119388'),
                (0xb23,38,'0608'),(0xb24,38,'0608'),(0xb26,54,'061d118ef8'),
                (0x1135,6,'061512811101118ef8'),(0xb54,6,'0608'),(0xb55,6,'0608'),
                (0x12b5,22,'06119388'),(0x113d,49,'060c')):
            reader.rows[4][rid-1]=[flags,0,0x130000+rid]
            blobs[0x130000+rid]=bytes.fromhex(sig)
        reader.rows[27][0x3d-1]=[0x140000]
        blobs[0x140000]=bytes.fromhex('1512811101118ef8')
        names.update({0x150000:'HashSet`1',0x150001:'System.Collections.Generic',0x150002:'System.Core'})
        reader.rows[1][0x44-1]=[14,0x150000,0x150001]
        reader.rows[35].extend([[0]*9 for _ in range(3-len(reader.rows[35]))])
        reader.rows[35][2]=[4,0,0,0,0,0x150003,0x150002,0,0x150004]
        blobs[0x150003]=bytes.fromhex('b77a5c561934e089')
        blobs[0x150004]=b''
        for rid,parent,name,sig in ((0x133,492,'Add','2001021300'),
                (0x15b,1225,'Round','00010d0d'),(0x20a,1225,'Abs','00010c0c')):
            names[0x160000+rid]=name
            reader.rows[10][rid-1]=[parent,0x160000+rid,0x170000+rid]
            blobs[0x170000+rid]=bytes.fromhex(sig)
        for method,sig,layout in (
                (0xcb4,'000208118ef8118ef8','027b230b0004037b230b000459285c02000a027b240b0004037b240b000459285c02000a580f0028a80c00060f0128a80c000659285c02000a58185b2a'),
                (0xca8,'200008','027b230b000465027b240b0004592a')):
            bodies[method]=bytes.fromhex(layout)
            blobs[0x1d0000+method]=bytes.fromhex(sig)
            reader.rows[6][method-1][1:3]=[0,150 if method==0xcb4 else 2182]
            reader.rows[6][method-1][4]=0x1d0000+method
        for index,bounds in ((0x3bd,[0xb23,0xca6]),(0x3be,[0xb27,0xcbb])):
            reader.rows[2][index]+=[0]*(6-len(reader.rows[2][index]))
            reader.rows[2][index][4:6]=bounds
        for field in (0xb23,0xb24):
            reader.rows[4][field-1]=[38,0,0x1e0000]
        blobs[0x1e0000]=bytes.fromhex('0608')
        reader.rows[10][0x25b]=[1225,0x1e0001,0x1e0002]
        names[0x1e0001]='Abs'
        blobs[0x1e0002]=bytes.fromhex('00010808')
        headers={0x1124:bytes.fromhex('133003006d000000d2010011'),
                 0x1293:bytes.fromhex('03300200c500000000000000'),
                 0xd66:bytes.fromhex('133002001000000060010011'),
                 0x1283:bytes.fromhex('133001000f00000033020011')}
        reader.fat_header=lambda method:headers[method]
        reader.rows[17]=[[0] for _ in range(0x233)]
        for rid,signature in ((0x160,'07011300'),(0x1d2,'0706118ef81d118ef808118ef8118ef8119388'),(0x233,'070115118f78011287f8')):
            reader.rows[17][rid-1]=[0xa0000+rid]
            blobs[0xa0000+rid]=bytes.fromhex(signature)
        for owner,fields,methods in ((0x379,(2529,2556),(2897,2901)),
                                      (0x3ef,(3055,3073),(3526,3576)),
                                      (0x49f,None,(4423,4437))):
            for column,bounds in ((4,fields),(5,methods)):
                if bounds is not None:
                    for index in (owner-1,owner):
                        reader.rows[2][index]+=[0]*(6-len(reader.rows[2][index]))
                    reader.rows[2][owner-1][column]=bounds[0]
                    reader.rows[2][owner][column]=bounds[1]
        for method,sig,layout in (
                (0xdc9,'200101128de4','02037df20b00042a'),
                (0xdd4,'200002','027bfc0b00042a'),
                (0xdd5,'20010102','02037dfc0b00042a'),
                (0xdcb,'200101118ef8','02037df30b00042a'),
                (0xdcd,'200101118f04','02037df40b00042a'),
                (0xdc8,'2000128de4','027bf20b00042a'),
                (0xdf2,'200002','0228c80d00067be70900048e16fe032a'),
                (0xdf3,'200002','0228c80d00067bf10900046f4202000a16fe022a'),
                (0x1151,'200102128fbc','036ff30d00062a'),
                (0x1152,'200102128fbc','036ff20d00062d07036ff30d00062a172a')):
            bodies[method]=bytes.fromhex(layout)
            blobs[0x180000+method]=bytes.fromhex(sig)
            reader.rows[6][method-1][4]=0x180000+method
            if method in (0xdc9,0xdd4,0xdd5,0xdcb,0xdcd):
                reader.rows[6][method-1][1:3]=[0,134 if method==0xdd4 else 129]
                headers[method]=bytes([(len(bodies[method])<<2)|2])
        for method,sig,header,layout in (
                (0xb51,'200001','033002005300000000000000',
                 '027e9e0b00047de2090004027e9e0b00047de309000402168dc10300027de709000402168d820300027deb09000402168dbe0300027ded0900040273e702000a7df109000402177df309000402283800000a2a'),
                (0xdc6,'200201128de402','133004000501000077010011',
                 '027e3b0b00047df40b0004027eef0b00047df50b0004027e07090004288900000a7df60b00040273420c00067df80b000402283800000a020328c90d0006020428d50d0006037be70900048e690a02068def0300027df70b0004160b2b587e620900040228d40d000673c60d00060c087e3a110004020728d70600066fcb0d0006080228c80d00067be7090004078fc103000228d40c00066fcd0d0006080228d404000a7df60b0004027bf70b00040708a20717580b070228c80d00067be70900048e693298037bef0900042c1d0273d502000a7dfb0b0004027bfb0b0004161673a60c00066fd602000a037be60900042c1102042d077e420200042b01167dfd0b00042a')):
            bodies[method]=bytes.fromhex(layout)
            headers[method]=bytes.fromhex(header)
            reader.rows[6][method-1][1:3]=[0,6278]
            reader.rows[6][method-1][4]=0x1b0000+method
            blobs[0x1b0000+method]=bytes.fromhex(sig)
        reader.rows[17][0x177-1]=[0x1c0000]
        blobs[0x1c0000]=bytes.fromhex('07030808128fbc')
        headers[0xb54]=bytes.fromhex('4a')
        for method,header in ((0xd5f,'3e'),(0xd60,'1e'),(0xd61,'22'),(0xd64,'82'),(0xd65,'1e')):
            headers[method]=bytes.fromhex(header)
        reader.tiny_header=lambda method:headers[method]
        reader.exception_sections=lambda method:[{'clauses':[
            {'flags':2,'try_offset':start,'try_length':length,'handler_offset':handler,
             'handler_length':handler_length,'extra':0}
            for start,length,handler,handler_length in (
                (35,82,117,14),(201,152,353,14),(389,185,574,14),
                (139,463,602,14),(770,47,817,12),(642,217,859,14))]}] if method==0x111b else []
        for field,sig in ((0xbf2,'06128de4'),(0xbf3,'06118ef8'),
                          (0xbf4,'06118f04'),(0xbfc,'0602'),(0x9e7,'061d118f04'),
                          (0x9f1,'06151280f902118ef8128e1c')):
            if field in (0xbf2,0xbf3,0xbf4,0xbfc):
                reader.rows[4][field-1][0]=1
            blobs[0x190000+field]=bytes.fromhex(sig)
            reader.rows[4][field-1][2]=0x190000+field
        names[0x1a0000]='Dictionary`2';names[0x1a0001]='get_Count'
        reader.rows[1][0x3e-1]=[6,0x1a0000,6]
        blobs[0x1a0002]=bytes.fromhex('200008')
        reader.rows[10][0x242-1]=[1492,0x1a0001,0x1a0002]
        blobs[0x1a0003]=bytes.fromhex('151280f902118ef8128e1c')
        reader.rows[27][0xba-1]=[0x1a0003]
        bodies[0xa48]=bytes(1448)+bytes.fromhex('73510b0006252073c6d3d2287c0700067de109000425206cc6d3d2287c07000628590700067de209000425177de509000425177dea09000425177df209000425167df80900048062090004')+bytes(6800-1523)
        headers[0xa48]=bytes.fromhex('13300800901a0000dc000011')
        reader.rows[6][0xa48-1][1:3]=[0,150]
        reader.rows[6][0xa48-1][4]=0x1d0000
        blobs[0x1d0000]=bytes.fromhex('000001')
        reader.rows[17][0xdc-1]=[0x1d0001]
        blobs[0x1d0001]=bytes.fromhex('0701128de4')
        reader.rows[4][0x962-1]=[22,0,0x1d0002]
        blobs[0x1d0002]=bytes.fromhex('06128de4')
        reader.rows[2][0x35b-1]=[0,0,0,0,0,2632]
        reader.rows[2][0x35b]=[0,0,0,0,0,2635]
        secondary=bytearray(355)
        secondary[:18]=bytes.fromhex('281a1300060a027b381100040b381d010000')
        secondary[294:314]=bytes.fromhex('07027b38110004580b07220000803f3ed8feffff')
        secondary[-1]=0x2a
        bodies[0x1122]=bytes(secondary)
        step_fixture=bytearray(EvidenceReaderTests.step_body())
        step_fixture[622:624]=b'\x17\x0c'
        step_fixture[797]=0x08
        step_fixture[805]=0x0c
        for start,length,handler,handler_length in (
                (35,82,117,14),(201,152,353,14),(389,185,574,14),
                (139,463,602,14),(770,47,817,12),(642,217,859,14)):
            end=start+length
            target=handler+handler_length
            step_fixture[end-2:end]=bytes((0xde,target-end))
            step_fixture[target-1]=0xdc
        bodies[0x111b]=bytes(step_fixture)
        for member,name,sig in ((0x25d,'Log','00020d0d0d'),(0x15b,'Round','00010d0d'),
                                (0x228,'Pow','00020d0d0d'),(0x5ca,'Min','00020d0d0d'),
                                (0x131,'Max','0002080808')):
            names[0x1e0000+member]=name
            blobs[0x1e0000+member]=bytes.fromhex(sig)
            reader.rows[10][member-1]=[1225,0x1e0000+member,0x1e0000+member]
        real_sha256=hashlib.sha256
        assembly_sha='ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5'
        initializer_sha='0b745a6f3fd2e3e2d45f06ddf8eb39c43e108f21215b2390aa6c7eabeab85055'
        static_body=bodies[0xa48]
        def digest(data):
            if data==bodies[0x111b]:
                result=Mock()
                result.hexdigest.return_value='f40ab827db2d48409d70f645d7be36721179c118503afa517d99528d14b13406'
                return result
            if data==bodies[0x1122]:
                result=Mock()
                result.hexdigest.return_value='fbb45a6b06347b39c01e81953cc306acb4ced20f8bbd6bf4afe6de4acff2b0a8'
                return result
            if data==static_body:
                result=Mock()
                result.hexdigest.return_value='2977b87c820b4c753cb90c5ffbb3de7d88b7bdaa29be75e5c3e46dc4c0ca41a5'
                return result
            value=assembly_sha if data==reader.data else initializer_sha if data==bodies[0x110b] else 'cdf648043cb42967de598182228416c68e6138223a70128137ef0f7b4b64416e' if data==bodies[0x111f] else '730e6a613e97e21a78bdf453c1f7b10a687e4fad1b4c225de666a85d8c004b57' if data==bodies[0x111c] else real_sha256(data).hexdigest()
            result=Mock()
            result.hexdigest.return_value=value
            return result
        constructor=bytearray(67)
        constructor[55:66]=bytes.fromhex('02220000803f7d38110004')
        constructor[66]=0x2a
        bodies[0x110a]=bytes(constructor)
        reader.field_owner[0x1138]=0x495
        reader.owner[0x110a]=0x495
        step_previous_string=reader.string
        reader.string=lambda index:'.ctor' if index==0x210001 else step_previous_string(index)
        blobs[0x210002]=bytes.fromhex('200001')
        reader.rows[6][0x110a-1][1:5]=[0,6278,0x210001,0x210002]
        reader.rows[4][0x1138-1][0]=1
        for method in (0x111b,0x111f,0x1122):
            reader.owner[method]=0x495
            reader.rows[6][method-1][1:3]=[0,129]
            reader.rows[6][method-1][4]=0x220000+method
            blobs[0x220000+method]=bytes.fromhex('200001')
        for method,(owner,flags,signature,layout) in MODULE['STEP_LIFECYCLE_LAYOUTS'].items():
            bodies[method]=bytes.fromhex(layout)
            reader.owner[method]=owner
            reader.rows[6][method-1][1:5]=[0,flags,0,0x230000+method]
            blobs[0x230000+method]=bytes.fromhex(signature)
        bodies[0x1114]=bytes.fromhex(MODULE['STEP_IDENTITY_FACTORY_IL'])
        headers[0x1114]=bytes.fromhex('1b3003006d010000c7010011')
        headers[0x8b4]=bytes.fromhex('1330020051000000b2000011')
        for rid,sig in ((0xb2,'070115118f7801129254'),(0x1c7,
                '07071292541511814101128fbc128fbc1511810d02128fbc128fd8128fd81511811501118ef8118ef8')):
            reader.rows[17][rid-1]=[0x240000+rid]
            blobs[0x240000+rid]=bytes.fromhex(sig)
        previous_sections=reader.exception_sections
        reader.exception_sections=lambda m:MODULE['STEP_IDENTITY_FACTORY_EH'] if m==0x1114 else previous_sections(m)
        reader.rows[1].extend([[0,0,0] for _ in range(228-len(reader.rows[1]))])
        names[0x250000]='ValueType'
        reader.rows[1][(913>>2)-1]=[6,0x250000,9]
        reader.rows[27][0xcb-1]=[0x250001]
        blobs[0x250001]=bytes.fromhex('15118f7801129254')
        for method,(flags,sig,il,header) in MODULE['STEP_IDENTITY_METHODS'].items():
            bodies[method]=bytes.fromhex(il)
            headers[method]=bytes.fromhex(header)
            reader.owner[method]=0x3de
            names[0x260000+method]=f'wrapperMethod{method}'
            reader.rows[6][method-1]=[1,0,flags,0x260000+method,0x270000+method,0]
            blobs[0x270000+method]=bytes.fromhex(sig)
        for field in (0xbb2,0xbb3):
            names[0x260000+field]=f'wrapperField{field}'
            reader.rows[4][field-1][1]=0x260000+field
        for member,parent,table,target in ((0x5ac,1628,6,0xd65),(0x286,1628,6,0xd62),
                (0x285,1628,6,0xd60),(0x70,452,6,0xd60),(0x47d,452,6,0xd61),
                (0x47e,452,4,0xbb3),(0x47f,452,4,0xbb2),(0x480,452,6,0xd5f),
                (0x481,452,6,0xd64),(0x377,900,6,0xd65),(0x6b1,4084,6,0xd60),
                (0x6bf,4084,6,0xd65)):
            target_row=reader.rows[table][target-1]
            reader.rows[10][member-1]=[parent,target_row[3 if table==6 else 1],target_row[4 if table==6 else 2]]
        for method,sig in ((0x77c,'00010e08'),(0xab4,'000201020e')):
            reader.rows[6][method-1][1:3]=[8 if method==0x77c else 0,0x10]
            reader.rows[6][method-1][4]=0x280000+method
            blobs[0x280000+method]=bytes.fromhex(sig)
        reader.owner[0x1114]=0x495
        reader.rows[6][0x1114-1][1:5]=[0,150,0,0x231114]
        blobs[0x231114]=bytes.fromhex('000115118f78011292541292d8')
        reader.field_owner[0x4d8]=0x2b2
        reader.rows[4][0x4d8-1]=[1,0,0x2304d8]
        blobs[0x2304d8]=bytes.fromhex('06129254')
        for method,body in bodies.items():
            if body:
                reader.rows[6][method-1][0]=1
        return reader,bodies,blobs,digest

    def test_step_lifecycle_bounded_claims(self):
        reader,_,_,_=self.current_collider_fixture()
        evidence=MODULE['collider_step_lifecycle'](reader)
        self.assertEqual(evidence['primary_order']['update_call'],52)
        self.assertEqual(evidence['primary_order']['sample_call'],58)
        self.assertEqual(evidence['secondary_entry']['sample_call'],32)
        self.assertEqual(len(evidence['unproved_edges']),3)

    def test_step_lifecycle_mutations_rejected(self):
        for method in MODULE['STEP_LIFECYCLE_LAYOUTS']:
            for kind in ('receiver','token','owner','flags','signature','eh'):
                with self.subTest(method=method,kind=kind):
                    reader,bodies,blobs,_=self.current_collider_fixture()
                    if kind=='receiver':bodies[method]=b'\x03'+bodies[method][1:]
                    elif kind=='token':bodies[method]=bodies[method][:-2]+b'\x00\x2a'
                    elif kind=='owner':reader.owner[method]+=1
                    elif kind=='flags':reader.rows[6][method-1][2]^=0x10
                    elif kind=='signature':blobs[0x230000+method]=b'\x00\x00\x01'
                    else:reader.exception_sections=lambda _: [{'clauses':[]}]
                    with self.assertRaisesRegex(ValueError,'step lifecycle'):
                        MODULE['collider_step_lifecycle'](reader)
        for kind in ('factory-store','factory-return','factory-branch','field-owner','field-signature'):
            with self.subTest(kind=kind):
                reader,bodies,blobs,_=self.current_collider_fixture()
                if kind.startswith('factory'):
                    body=bytearray(bodies[0x1114])
                    if kind=='factory-store':body[64]=0x00
                    elif kind=='factory-return':body[360]^=1
                    else:body[0:2]=bytes((0x2b,60))
                    bodies[0x1114]=bytes(body)
                elif kind=='field-owner':reader.field_owner[0x4d8]+=1
                else:blobs[0x2304d8]=b'\x06\x08'
                with self.assertRaisesRegex(ValueError,'step lifecycle'):
                    MODULE['collider_step_lifecycle'](reader)

    def test_step_wrapper_identity_adversarial_bindings(self):
        for kind in ('conversion','helper','constructor','extractor','query','setter',
                     'parent','payload','ambiguous','typespec','generic','local0-store',
                     'local0-address','factory-bypass','factory-eh','factory-header',
                     'factory-locals','wrapper-locals','wrapper-header','intervening-call'):
            with self.subTest(kind=kind):
                reader,bodies,blobs,_=self.current_collider_fixture()
                methods={'conversion':0xd65,'helper':0xd64,'constructor':0xd5f,
                         'extractor':0xd62,'query':0xd60,'setter':0xd61}
                if kind in methods:
                    m=methods[kind]
                    bodies[m]=bodies[m][:-1]+b'\x00'
                elif kind=='parent':reader.rows[10][0x5ac-1][0]+=8
                elif kind=='payload':reader.field_owner[0xbb3]+=1
                elif kind=='ambiguous':reader.rows[6][0xd63-1][3:5]=reader.rows[6][0xd62-1][3:5]
                elif kind=='typespec':blobs[0x250001]=bytes.fromhex('15118f7801129258')
                elif kind=='generic':reader.rows[42][0][1]=4
                elif kind in ('local0-store','local0-address','factory-bypass'):
                    body=bytearray(bodies[0x1114])
                    offset={'local0-store':120,'local0-address':358,'factory-bypass':56}[kind]
                    body[offset]=0x0a if kind=='local0-store' else 0x12 if kind=='local0-address' else 0x00
                    bodies[0x1114]=bytes(body)
                elif kind=='factory-eh':reader.exception_sections=lambda _:[]
                elif kind.endswith('header'):
                    old=reader.fat_header
                    m=0x1114 if kind=='factory-header' else 0x8b4
                    reader.fat_header=lambda target:bytes.fromhex('133000000000000000000000') if target==m else old(target)
                elif kind.endswith('locals'):
                    blobs[0x240000+(0x1c7 if kind=='factory-locals' else 0xb2)]=b'\x07\x00'
                else:blobs[0x280000+0xab4]=bytes.fromhex('0002011013000e')
                with self.assertRaisesRegex(ValueError,'step lifecycle wrapper identity'):
                    MODULE['collider_step_lifecycle'](reader)

    def test_step_exception_layout_mutations_rejected(self):
        for index in range(6):
            for field in ('flags','try_offset','try_length','handler_offset','handler_length','extra'):
                with self.subTest(index=index,field=field):
                    reader,bodies,blobs,digest=self.current_collider_fixture()
                    sections=reader.exception_sections(0x111b)
                    sections[0]['clauses'][index][field]+=1
                    original=reader.exception_sections
                    reader.exception_sections=lambda method:sections if method==0x111b else original(method)
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaisesRegex(ValueError,'step exception layout'):
                            MODULE['current_collider_audit'](reader)

    def test_step_exception_layout_structure_rejected(self):
        for mutation in ('missing','duplicate-section','missing-clause','duplicate-clause','reordered'):
            with self.subTest(mutation=mutation):
                reader,_,_,digest=self.current_collider_fixture()
                sections=reader.exception_sections(0x111b)
                if mutation=='missing':sections=[]
                elif mutation=='duplicate-section':sections+=sections
                elif mutation=='missing-clause':sections[0]['clauses'].pop()
                elif mutation=='duplicate-clause':sections[0]['clauses'].append(sections[0]['clauses'][0])
                else:sections[0]['clauses'].reverse()
                original=reader.exception_sections
                reader.exception_sections=lambda method:sections if method==0x111b else original(method)
                with patch.object(hashlib,'sha256',side_effect=digest):
                    with self.assertRaisesRegex(ValueError,'step exception layout'):
                        MODULE['current_collider_audit'](reader)

    def test_area_candidates_rejects_binding_changes(self):
        methods=(0x1124,0x810,0x80f,0x811,0xca6,0xcaf,0xca7,0x80c,0xcf1)
        mutations=[(6,rid,col) for rid in methods for col in (1,2,4)]
        mutations += [(4,rid,col) for rid in (0x449,0x44b,0xb23,0xb24,0xb26,0x1135,0xb54,0xb55,0x12b5,0x113d) for col in (0,2)]
        mutations += [(10,rid,col) for rid in (0x133,0x15b,0x20a) for col in range(3)]
        mutations += [(1,0x44,col) for col in range(3)]
        mutations += [(35,3,col) for col in range(9)]
        mutations += [(2,owner,col) for owner in (0x285,0x3be,0x495,0x3c9) for col in (4,5)]
        for table,rid,col in mutations:
            with self.subTest(table=table,rid=rid,col=col):
                reader,_,blobs,_=self.current_collider_fixture()
                row=reader.rows[table][rid-1]
                if (table,col) in ((6,4),(4,2),(10,2),(35,5),(35,8)):
                    blobs[row[col]]+=b'\x00'
                elif (table,col) in ((10,1),(1,1),(1,2),(35,6),(35,7)):
                    original=reader.string
                    index=row[col]
                    reader.string=lambda value: 'tampered' if value==index else original(value)
                elif table==2:
                    row[col]=0xffff
                else:
                    row[col]^=1
                with self.assertRaisesRegex(ValueError,'area candidates'):
                    MODULE['current_area_candidates_audit'](reader)
        for rid in methods:
            with self.subTest(body=rid):
                reader,bodies,_,_=self.current_collider_fixture()
                bodies[rid]+=b'\x00'
                with self.assertRaisesRegex(ValueError,'area candidates'):
                    MODULE['current_area_candidates_audit'](reader)
        for table,rid in ((17,0x1d2),(27,0x3d)):
            reader,_,blobs,_=self.current_collider_fixture()
            blobs[reader.rows[table][rid-1][0]]+=b'\x00'
            with self.assertRaisesRegex(ValueError,'area candidates'):
                MODULE['current_area_candidates_audit'](reader)
        reader,_,_,_=self.current_collider_fixture()
        reader.fat_header=lambda _:b'\x00'
        with self.assertRaisesRegex(ValueError,'area candidates'):
            MODULE['current_area_candidates_audit'](reader)

    def test_report_wrapper_rejects_binding_changes(self):
        mutations=[(2,0x21f,column) for column in (0,3,4,5)]
        mutations += [(2,0x220,column) for column in (4,5)]
        mutations += [(2,owner,4) for owner in (0x3b7,0x3b8)]
        mutations += [(1,rid,column) for rid in (0x4f,0x7d) for column in range(3)]
        mutations += [(35,1,column) for column in range(9)]
        mutations += [(4,rid,column) for rid in (0x26f,0xafc) for column in (0,2)]
        mutations += [(10,rid,column) for rid in (0xdc,0xdd,0xde,0xdf,0xe0,0xe1,0xe2,0x40f) for column in range(3)]
        for table,rid,column in mutations:
            with self.subTest(table=table,rid=rid,column=column):
                reader,_,blobs,_=self.current_collider_fixture()
                row=reader.rows[table][rid-1]
                if (table==35 and column in (5,8)) or (table==4 and column==2) or (table==10 and column==2):
                    blobs[row[column]]+=b'\x00'
                elif (table==1 and column in (1,2)) or (table==35 and column in (6,7)) or (table==10 and column==1):
                    original_string=reader.string
                    changed_index=row[column]
                    reader.string=lambda index: 'tampered' if index==changed_index else original_string(index)
                else:row[column]^=1
                if table==2 and rid==0x3b7:reader.rows[table][rid-1][column]=0xafd
                if table==2 and rid==0x3b8:reader.rows[table][rid-1][column]=0xafc
                with self.assertRaisesRegex(ValueError,'report wrapper'):
                    MODULE['current_report_wrapper_audit'](reader)
        for rid in (0x134,0x5d,0x5e):
            reader,_,blobs,_=self.current_collider_fixture()
            blobs[reader.rows[27][rid-1][0]]+=b'\x00'
            with self.assertRaisesRegex(ValueError,'report wrapper type specification'):
                MODULE['current_report_wrapper_audit'](reader)
        reader,_,_,_=self.current_collider_fixture()
        reader.rows[42].append([1,0,1086,0])
        with self.assertRaisesRegex(ValueError,'report wrapper owner or generic'):
            MODULE['current_report_wrapper_audit'](reader)

    def test_report_wrapper_rejects_method_changes(self):
        for method in range(0x68c,0x691):
            for mutation in ('implementation','flags','signature','body'):
                with self.subTest(method=method,mutation=mutation):
                    reader,bodies,blobs,_=self.current_collider_fixture()
                    row=reader.rows[6][method-1]
                    if mutation=='implementation':row[1]=1
                    elif mutation=='flags':row[2]^=1
                    elif mutation=='signature':blobs[row[4]]+=b'\x00'
                    else:bodies[method]+=b'\x00'
                    with self.assertRaisesRegex(ValueError,'report wrapper'):
                        MODULE['current_report_wrapper_audit'](reader)

    def test_report_headers_and_locals_tampering(self):
        for method,rid,audit in ((0xd66,0x160,'current_report_optional_audit'),
                                 (0x1283,0x233,'current_report_state_audit')):
            for offset in range(12):
                with self.subTest(method=method,offset=offset):
                    reader,_,_,_=self.current_collider_fixture()
                    header=bytearray(reader.fat_header(method));header[offset]^=1
                    reader.fat_header=lambda index:bytes(header)
                    with self.assertRaisesRegex(ValueError,'header or locals'):
                        MODULE[audit](reader)
            reader,_,blobs,_=self.current_collider_fixture()
            blobs[0xa0000+rid]=b'\x07\x00'
            with self.assertRaisesRegex(ValueError,'header or locals'):MODULE[audit](reader)

    def test_report_method_metadata_tampering(self):
        for method,owner,audit in ((0xd66,0x3de,'current_report_optional_audit'),
                                    (0x1283,0x4c8,'current_report_state_audit')):
            for mutation in ('implementation','flags','signature','owner_start','owner_end'):
                with self.subTest(method=method,mutation=mutation):
                    reader,_,blobs,_=self.current_collider_fixture()
                    row=reader.rows[6][method-1]
                    if mutation=='implementation':row[1]=1
                    elif mutation=='flags':row[2]^=1
                    elif mutation=='signature':blobs[row[4]]=b'\x00\x00\x01'
                    elif mutation=='owner_start':reader.rows[2][owner-1][5]=method+1
                    else:reader.rows[2][owner][5]=method
                    with self.assertRaisesRegex(ValueError,'report optional|report state'):
                        MODULE[audit](reader)

    def test_report_reset_rejects_metadata_and_body_changes(self):
        for method,owner in ((0x1293,0x4c8),(0x128e,0x4c8),
                              (0x11e1,0x4b6),(0x1295,0x4c8),
                              (0xc6b,0x3b7),(0xae7,0x369),(0x9dc,0x337)):
            for mutation in ('implementation','flags','signature','owner_start','owner_end','body'):
                with self.subTest(method=method,mutation=mutation):
                    reader,bodies,blobs,_=self.current_collider_fixture()
                    row=reader.rows[6][method-1]
                    if mutation=='implementation':row[1]=1
                    elif mutation=='flags':row[2]^=1
                    elif mutation=='signature':blobs[row[4]]=b'\x00\x00\x01'
                    elif mutation=='owner_start':reader.rows[2][owner-1][5]=method+1
                    elif mutation=='owner_end':reader.rows[2][owner][5]=method
                    else:bodies[method]+=b'\x00'
                    with self.assertRaisesRegex(ValueError,'report reset'):
                        MODULE['current_report_reset_audit'](reader)
        reader,_,_,_=self.current_collider_fixture()
        reader.rows[6][0x11e1-1][0]=1
        with self.assertRaisesRegex(ValueError,'report reset method body'):
            MODULE['current_report_reset_audit'](reader)

    def test_report_reset_rejects_each_header_byte_change(self):
        for offset in range(12):
            with self.subTest(offset=offset):
                reader,_,_,_=self.current_collider_fixture()
                header=bytearray(reader.fat_header(0x1293));header[offset]^=1
                reader.fat_header=lambda method:bytes(header)
                with self.assertRaisesRegex(ValueError,'report reset header'):
                    MODULE['current_report_reset_audit'](reader)

    def test_report_locals_row_rebinding(self):
        for rid,other,audit in ((0x160,0x233,'current_report_optional_audit'),
                                (0x233,0x160,'current_report_state_audit')):
            with self.subTest(rid=rid):
                reader,_,_,_=self.current_collider_fixture()
                reader.rows[17][rid-1]=reader.rows[17][other-1][:]
                with self.assertRaisesRegex(ValueError,'header or locals'):
                    MODULE[audit](reader)

    def test_fat_header_reads_and_rejects_invalid_layout(self):
        header=bytes.fromhex('133002001000000060010011')
        reader=MODULE['Metadata'].__new__(MODULE['Metadata'])
        reader.rows={6:[[1]]}
        reader.data=b'\x00'+header
        def offset(rva,size=1):
            if rva+size>len(reader.data):raise ValueError('truncated header')
            return rva
        reader.offset=offset
        self.assertEqual(reader.fat_header(1),header)
        for data in (header[:11],b'\x12'+header[1:],b'\x13\x20'+header[2:],b'\x13\x40'+header[2:]):
            reader.data=b'\x00'+data
            with self.assertRaises(ValueError):reader.fat_header(1)
        reader.rows[6][0][0]=0
        with self.assertRaises(ValueError):reader.fat_header(1)

    def test_tiny_header_reads_and_rejects_invalid_layout(self):
        reader=MODULE['Metadata'].__new__(MODULE['Metadata'])
        reader.rows={6:[[1]]}
        def offset(rva,size=1):
            if rva+size>len(reader.data):raise ValueError('truncated body')
            return rva
        reader.offset=offset
        for body in (b'',b'\x2a',bytes(range(63))):
            header=bytes([(len(body)<<2)|2])
            reader.data=b'\x00'+header+body
            self.assertEqual(reader.tiny_header(1),header)
            reader.data=reader.data[:-1] if body else b'\x00'
            with self.assertRaises(ValueError):reader.tiny_header(1)
        for flag in (0,1,3,255):
            reader.data=bytes([0,flag])+bytes(64)
            with self.assertRaises(ValueError):reader.tiny_header(1)
        reader.rows[6][0][0]=0
        with self.assertRaises(ValueError):reader.tiny_header(1)

    def test_current_static_initialization_tampering(self):
        for mutation in range(111):
            with self.subTest(mutation=mutation):
                reader,bodies,blobs,digest=self.current_collider_fixture()
                row=reader.rows[6][0xa48-1]
                if mutation==0:row[1]^=1
                elif mutation==1:row[2]^=1
                elif mutation==2:blobs[row[4]]+=b'\x00'
                elif mutation==3:bodies[0xa48]=bodies[0xa48][:-1]
                elif mutation==4:blobs[reader.rows[17][0xdc-1][0]]+=b'\x00'
                elif mutation==5:reader.rows[4][0x962-1][0]^=1
                elif mutation==6:blobs[reader.rows[4][0x962-1][2]]+=b'\x00'
                elif mutation==7:reader.rows[2][0x35b-1][5]+=1
                elif mutation==8:reader.rows[2][0x35b][5]+=1
                elif mutation>=14:
                    original=reader.fat_header
                    header=bytearray(original(0xa48))
                    if mutation==110:header=header[:-1]
                    else:header[(mutation-14)//8]^=1<<((mutation-14)%8)
                    reader.fat_header=lambda rid:bytes(header) if rid==0xa48 else original(rid)
                else:
                    body=bytearray(bodies[0xa48])
                    body[(1448,1464,1485,1518,1522)[mutation-9]]^=1
                    bodies[0xa48]=bytes(body)
                with patch.object(hashlib,'sha256',side_effect=digest):
                    with self.assertRaisesRegex(ValueError,'static initialization'):
                        MODULE['current_collider_audit'](reader)

    def test_current_initialization_constructor_tampering(self):
        for method in (0xb51,0xdc6):
            for mutation in range(7+12*8+1):
                with self.subTest(method=method,mutation=mutation):
                    reader,bodies,blobs,digest=self.current_collider_fixture()
                    row=reader.rows[6][method-1]
                    if mutation==0:row[1]^=1
                    elif mutation==1:row[2]^=1
                    elif mutation==2:blobs[row[4]]+=b'\x00'
                    elif mutation==3:bodies[method]=bodies[method][:-1]
                    elif mutation==4:bodies[method]+=b'\x00'
                    elif mutation==5:
                        original=reader.fat_header
                        reader.fat_header=lambda rid:bytes([original(rid)[0]^8])+original(rid)[1:] if rid==method else original(rid)
                    elif mutation==6:blobs[reader.rows[17][0x177-1][0]]+=b'\x00'
                    else:
                        original=reader.fat_header
                        header=bytearray(original(method))
                        if mutation==103:header=header[:-1]
                        else:header[(mutation-7)//8]^=1<<((mutation-7)%8)
                        reader.fat_header=lambda rid:bytes(header) if rid==method else original(rid)
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaisesRegex(ValueError,'initialization constructor'):
                            MODULE['current_collider_audit'](reader)

    def test_report_optional_headers_exceptions_and_generic_constraints(self):
        audit=MODULE['current_report_optional_audit']
        for method in (0xd5f,0xd60,0xd61,0xd64,0xd65):
            reader,*_=self.current_collider_fixture()
            original=reader.tiny_header
            reader.tiny_header=lambda rid:bytes([original(rid)[0]^4]) if rid==method else original(rid)
            with self.subTest(method=method,mutation='header'):
                with self.assertRaisesRegex(ValueError,'header or locals'):audit(reader)
        for method in (0xd5f,0xd60,0xd61,0xd64,0xd65,0xd66,0xb54):
            reader,*_=self.current_collider_fixture()
            reader.exception_sections=lambda rid:[{}] if rid==method else []
            with self.subTest(method=method,mutation='exception'):
                with self.assertRaisesRegex(ValueError,'exception sections'):audit(reader)
        for parameter_rid in (1,18):
            reader,*_=self.current_collider_fixture()
            reader.rows[42]=[[0,0,0,0]]*(parameter_rid-1)+reader.rows[42]
            reader.rows[44]=[[parameter_rid+1,913]]
            audit(reader)
            reader.rows[44].append([parameter_rid,913])
            with self.subTest(parameter_rid=parameter_rid):
                with self.assertRaisesRegex(ValueError,'generic parameter'):audit(reader)
        for method in (0xd5f,0xd60,0xd61,0xd64,0xd65,0xd66):
            reader,*_=self.current_collider_fixture()
            reader.rows[42].append([0,0,method<<1|1,0])
            with self.subTest(method=method,mutation='generic method'):
                with self.assertRaisesRegex(ValueError,'optional method'):audit(reader)

    def test_report_optional_owner_and_duplicate_parameters(self):
        audit=MODULE['current_report_optional_audit']
        for method in (0xd5f,0xd60,0xd61,0xd64,0xd65,0xd66,0xb54):
            for missing in (False,True):
                reader,*_=self.current_collider_fixture()
                if missing:del reader.owner[method]
                else:reader.owner[method]^=1
                with self.subTest(method=method,missing=missing):
                    with self.assertRaisesRegex(ValueError,'optional (method|consumer)'):audit(reader)
        for field in (0xbb2,0xbb3):
            for missing in (False,True):
                reader,*_=self.current_collider_fixture()
                if missing:del reader.field_owner[field]
                else:reader.field_owner[field]^=1
                with self.subTest(field=field,missing=missing):
                    with self.assertRaisesRegex(ValueError,'optional field'):audit(reader)
        reader,*_=self.current_collider_fixture()
        reader.rows[42].append(reader.rows[42][0].copy())
        with self.assertRaisesRegex(ValueError,'generic parameter'):audit(reader)
        reader,*_=self.current_collider_fixture()
        reader.rows[42].append([0,0,0xb54<<1|1,0])
        with self.assertRaisesRegex(ValueError,'consumer'):audit(reader)

    def test_current_report_optional_tampering(self):
        for mutation in range(23):
            with self.subTest(mutation=mutation):
                reader,bodies,blobs,_=self.current_collider_fixture()
                if mutation==0:reader.rows[2][0x3de][4]+=1
                elif mutation==1:reader.rows[42][0][1]=1
                elif mutation==2:blobs[0x70000+0x39]=b'\x02'
                elif mutation==3:reader.rows[6][0xd65-1][2]^=1
                elif mutation==4:bodies[0xd64]=b'\x2a'
                elif mutation==5:blobs[0x90000+0xbb3]=b'\x06\x02'
                elif mutation==6:reader.rows[10][0x6b1-1][0]=0x14b<<3|4
                elif mutation==7:reader.rows[10][0x480-1][1]=15
                elif mutation==8:reader.rows[10][0x47f-1][2]=0x90000+0xbb3
                elif mutation==9:bodies[0xd66]=b'\x2a'
                elif mutation==10:reader.rows[6][0xd66-1][2]^=1
                elif mutation==11:reader.rows[10][0x6ba-1][2]=0x80000+0xd65
                elif mutation==12:blobs[0x70000+0x70]=b'\x02'
                elif mutation==13:reader.rows[10][0x377-1][0]^=8
                elif mutation==14:reader.rows[10][0x377-1][1]=15
                elif mutation==15:reader.rows[10][0x377-1][2]=0x80000+0xd66
                elif mutation==16:reader.rows[6][0xb54-1][1]=1
                elif mutation==17:reader.rows[6][0xb54-1][2]^=1
                elif mutation==18:blobs[0x80000+0xb54]=b'\x00'
                elif mutation==19:bodies[0xb54]=b'\x2a'
                elif mutation==20:reader.rows[2][0x379-1][5]=0xb55
                elif mutation==21:reader.rows[2][0x379][5]=0xb54
                else:
                    original=reader.tiny_header
                    reader.tiny_header=lambda rid:b'\x46' if rid==0xb54 else original(rid)
                with self.assertRaisesRegex(ValueError,'report optional'):
                    MODULE['current_report_optional_audit'](reader)

    def test_current_report_state_tampering(self):
        for mutation in range(10):
            with self.subTest(mutation=mutation):
                reader,bodies,blobs,_=self.current_collider_fixture()
                if mutation==0:reader.rows[2][0x4c8][5]=0x127c
                elif mutation==1:reader.rows[6][0x127d-1][2]^=1
                elif mutation==2:blobs[0x50000+0x127c]=b'\x00'
                elif mutation==3:bodies[0x61f]=b'\x2a'
                elif mutation==4:reader.rows[2][0x1fe][4]=0x20c
                elif mutation==5:reader.rows[4][0x1228-1][0]=6
                elif mutation==6:blobs[0x60000+0x20b]=b'\x06\x0e'
                elif mutation==7:reader.rows[6][0x8ba-1][1]=1
                elif mutation==8:bodies[0x1283]=b'\x2a'
                else:blobs[0x50000+0x1283]=b'\x00'
                with self.assertRaisesRegex(ValueError,'report state'):
                    MODULE['current_report_state_audit'](reader)

    def test_current_report_dispatch_tampering(self):
        for mutation in range(8):
            with self.subTest(mutation=mutation):
                reader,bodies,blobs,_=self.current_collider_fixture()
                if mutation==0:reader.rows[2][0x2b2-1][3]=0
                elif mutation==1:reader.rows[2][0x4c8][5]=0x12a9
                elif mutation==2:reader.rows[6][0x8c7-1][2]|=0x100
                elif mutation==3:reader.rows[6][0x11e8-1][0]=1
                elif mutation==4:reader.rows[6][0x12a9-1][4]=1
                elif mutation==5:bodies[0x8c7]=b'\x2a'
                elif mutation==6:reader.rows[25].append([0x4c8,0x12a9<<1,0x11e8<<1])
                else:reader.rows[6][0x8c6-1]=reader.rows[6][0x8c7-1][:]
                with self.assertRaisesRegex(ValueError,'report dispatch'):
                    MODULE['current_report_dispatch_audit'](reader)

    def test_current_collider_vector_list_tampering(self):
        for mutation in range(12):
            with self.subTest(mutation=mutation):
                reader,_,blobs,digest=self.current_collider_fixture()
                if mutation<2:
                    reader.rows[2][0x4e2-1+mutation][5]+=1
                elif mutation==2:
                    blobs[0x20001]=bytes.fromhex('1512813d01119264')
                else:
                    member=(0x5df,0x5e0,0x5e1)[(mutation-3)//3]
                    column=(mutation-3)%3
                    reader.rows[10][member-1][column]=0 if column!=1 else 1
                with patch.object(hashlib,'sha256',side_effect=digest):
                    with self.assertRaisesRegex(ValueError,'pair report signature|vector (method|list)'):
                        MODULE['current_collider_audit'](reader)

    def test_current_collider_pair_loop_rejects_mutations(self):
        for offset in (*range(978,1015),*range(1395,1446)):
            with self.subTest(offset=offset):
                reader,bodies,_,digest=self.current_collider_fixture()
                body=bytearray(bodies[0x111f])
                body[offset]^=1
                bodies[0x111f]=bytes(body)
                with patch.object(hashlib,'sha256',side_effect=digest):
                    with self.assertRaises(ValueError):
                        MODULE['current_collider_audit'](reader)

    def test_current_collider_pair_loop_member_identity(self):
        for member in (0x5de,0x5e2):
            for column in range(3):
                with self.subTest(member=member,column=column):
                    reader,_,_,digest=self.current_collider_fixture()
                    reader.rows[10][member-1][column]=0 if column!=1 else 1
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaisesRegex(ValueError,'pair loop member identity'):
                            MODULE['current_collider_audit'](reader)

    def test_current_collider_rejects_branch_into_operand(self):
        reader,bodies,_,digest=self.current_collider_fixture()
        bodies[0x111f]+=b'\x2b\xff'
        with patch.object(hashlib,'sha256',side_effect=digest):
            with self.assertRaisesRegex(ValueError,'branch'):
                MODULE['current_collider_audit'](reader)

    def test_current_collider_synthetic_success(self):
        reader,_,_,digest=self.current_collider_fixture()
        with patch.object(hashlib,'sha256',side_effect=digest):
            evidence=MODULE['current_collider_audit'](reader)
        self.assertFalse(evidence['certification_eligible'])
        self.assertEqual(evidence['status'],'partial-byte-evidence-only')
        self.assertEqual([item['value'] for item in evidence['constants']],[29,15,20,24,20])
        self.assertEqual(evidence['constructor']['radius_rule'],'category == 1 ? 20 : 29')
        self.assertEqual(len(evidence['report_state']['methods']),5)
        self.assertEqual(len(evidence['report_state']['fields']),6)
        self.assertIn('remain unproved',evidence['report_state']['scope'])
        self.assertEqual(evidence['report_optional']['type_token'],'020003de')
        self.assertEqual(len(evidence['report_optional']['method_tokens']),6)
        self.assertIn('remain unproved',evidence['report_optional']['scope'])
    def test_current_collider_filter_bodies_reject_mutations(self):
        for method in (0xdc8,0xdf2,0xdf3,0x1151,0x1152):
            reader,bodies,_,digest=self.current_collider_fixture()
            original=bodies[method]
            mutations=[original[:-1],original+b'\x00']
            mutations.extend(original[:i]+bytes([value^1])+original[i+1:]
                             for i,value in enumerate(original))
            for changed in mutations:
                with self.subTest(method=method,changed=changed.hex()):
                    bodies[method]=changed
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaises(ValueError):
                            MODULE['current_collider_audit'](reader)

    def test_current_collider_initialization_accessors_reject_mutations(self):
        for method in (0xdc9,0xdd4,0xdd5,0xdcb,0xdcd):
            reader,bodies,blobs,digest=self.current_collider_fixture()
            original=bodies[method]
            mutations=[original[:-1],original+b'\x00']
            mutations.extend(original[:offset]+bytes([value^1])+original[offset+1:]
                             for offset,value in enumerate(original))
            for changed in mutations:
                with self.subTest(method=method,changed=changed.hex()):
                    bodies[method]=changed
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaisesRegex(ValueError,'initialization accessor'):
                            MODULE['current_collider_audit'](reader)
            bodies[method]=original
            blobs[reader.rows[6][method-1][4]]+=b'\x00'
            with patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,'initialization accessor'):
                    MODULE['current_collider_audit'](reader)
        for field in (0xbf2,0xbf3,0xbf4,0xbfc):
            for column in (0,2):
                with self.subTest(field=field,column=column):
                    reader,_,blobs,digest=self.current_collider_fixture()
                    if column==0:reader.rows[4][field-1][0]|=0x10
                    else:blobs[reader.rows[4][field-1][2]]+=b'\x00'
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaises(ValueError):
                            MODULE['current_collider_audit'](reader)

    def test_current_collider_accessor_layout_rejects_mutations(self):
        for method in (0xdc9,0xdd4,0xdd5,0xdcb,0xdcd):
            for column,width in ((1,16),(2,16)):
                for bit in range(width):
                    with self.subTest(method=method,column=column,bit=bit):
                        reader,_,_,digest=self.current_collider_fixture()
                        reader.rows[6][method-1][column]^=1<<bit
                        with patch.object(hashlib,'sha256',side_effect=digest):
                            with self.assertRaisesRegex(ValueError,'initialization accessor'):
                                MODULE['current_collider_audit'](reader)
            for bit in range(8):
                with self.subTest(method=method,header_bit=bit):
                    reader,_,_,digest=self.current_collider_fixture()
                    original=reader.tiny_header
                    reader.tiny_header=lambda rid:bytes([original(rid)[0]^(1<<bit)]) if rid==method else original(rid)
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaisesRegex(ValueError,'initialization accessor'):
                            MODULE['current_collider_audit'](reader)

    def test_current_collider_filter_metadata_rejects_mutations(self):
        cases=[(2,owner+delta,column) for owner,columns in
               ((0x379,(4,)),(0x3ef,(4,5)),(0x49f,(5,)))
               for column in columns for delta in (0,1)]
        cases += [(4,rid,2) for rid in (0xbf2,0x9e7,0x9f1)]
        cases += [(6,rid,4) for rid in (0xdc8,0xdf2,0xdf3,0x1151,0x1152)]
        cases += [(10,0x242,column) for column in (0,1,2)]
        cases += [(1,0x3e,column) for column in (0,1,2)]+[(27,0xba,0)]
        for table,rid,column in cases:
            with self.subTest(table=table,rid=rid,column=column):
                reader,_,blobs,digest=self.current_collider_fixture()
                value=reader.rows[table][rid-1][column]
                if (table,column) in ((4,2),(6,4),(10,2),(27,0)):
                    blobs[value]=blobs[value]+b'\x00'
                elif (table,column) in ((10,1),(1,1),(1,2)):
                    reader.rows[table][rid-1][column]=0
                else:
                    reader.rows[table][rid-1][column]+=1
                with patch.object(hashlib,'sha256',side_effect=digest):
                    with self.assertRaises(ValueError):
                        MODULE['current_collider_audit'](reader)
        for table in (3,5):
            reader,_,_,digest=self.current_collider_fixture()
            reader.rows[table]=[[1]]
            with patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaises(ValueError):MODULE['current_collider_audit'](reader)

    def test_current_collider_rejects_extra_direct_add(self):
        for method in (0x111c,0x111f):
            reader,bodies,_,digest=self.current_collider_fixture()
            bodies[method]=bytes.fromhex('28cf05000a')+bodies[method][5:]
            with patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,'direct Add inventory'):
                    MODULE['current_collider_audit'](reader)

    def test_current_collider_step_reference_inventory_rejects_mutations(self):
        for kind in ('extra_store','address_escape','missing','invalid_il'):
            reader,bodies,_,digest=self.current_collider_fixture()
            if kind=='extra_store':bodies[0x110a]+=bytes.fromhex('7d38110004')
            elif kind=='address_escape':bodies[0x110a]+=bytes.fromhex('7c38110004')
            elif kind=='missing':bodies[0x110a]=b'\x2a'
            else:bodies[0x110a]+=b'\xfe'
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,'step field reference inventory mismatch'):
                    MODULE['current_collider_audit'](reader)

    def test_current_collider_step_initialization_rejects_mutations(self):
        for kind in ('receiver','constant','flags','signature'):
            reader,bodies,blobs,digest=self.current_collider_fixture()
            if kind in ('receiver','constant'):
                body=bytearray(bodies[0x110a])
                body[55 if kind=='receiver' else 60]=3 if kind=='receiver' else 0x40
                bodies[0x110a]=bytes(body)
                message='step constructor initialization mismatch'
            else:
                field=reader.rows[4][0x1138-1]
                if kind=='flags':field[0]|=0x10
                else:blobs[field[2]]=bytes.fromhex('060d')
                message='step field metadata mismatch' if kind=='flags' else 'current collider field is not R4'
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,message):
                    MODULE['current_collider_audit'](reader)

    def test_current_collider_step_constructor_metadata_rejects_mutations(self):
        for kind in ('field_owner','method_owner','missing_field_owner','missing_method_owner',
                     'rva','impl_flags','flags','name','signature'):
            reader,_,blobs,digest=self.current_collider_fixture()
            row=reader.rows[6][0x110a-1]
            if kind=='field_owner':reader.field_owner[0x1138]=0x496
            elif kind=='method_owner':reader.owner[0x110a]=0x496
            elif kind=='missing_field_owner':del reader.field_owner[0x1138]
            elif kind=='missing_method_owner':del reader.owner[0x110a]
            elif kind=='rva':row[0]=0
            elif kind=='impl_flags':row[1]=1
            elif kind=='flags':row[2]|=0x10
            elif kind=='name':row[3]=1
            else:blobs[row[4]]=bytes.fromhex('000001')
            message=('step field reference inventory mismatch' if kind=='rva'
                     else 'step constructor metadata mismatch')
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,message):
                    MODULE['current_collider_audit'](reader)

    def test_current_collider_step_entry_receiver_windows(self):
        reader,bodies,_,_=self.current_collider_fixture()
        check=MODULE['collider_step_receivers']
        self.assertEqual(len(check(reader)['methods']),4)
        original=bodies[0x111b]
        for prefix,message in ((bytes.fromhex('0f00'),'argument escape'),
                               (bytes.fromhex('1000'),'argument escape'),
                               (bytes.fromhex('fe0a0000'),'argument escape'),
                               (bytes.fromhex('fe0b0000'),'argument escape'),
                               (b'\x38'+struct.pack('<i',910-5),'window ingress')):
            bodies[0x111b]=prefix+original[len(prefix):]
            with self.subTest(prefix=prefix.hex()),self.assertRaisesRegex(ValueError,message):
                check(reader)
        bodies[0x111b]=original
        previous_sections=reader.exception_sections
        for clause in ({'flags':2,'handler_offset':910},
                       {'flags':1,'handler_offset':0,'extra':910}):
            reader.exception_sections=lambda method: [{'clauses':[clause]}] if method==0x111b else []
            with self.subTest(clause=clause),self.assertRaisesRegex(ValueError,'window ingress'):
                check(reader)
        reader.exception_sections=previous_sections
        bodies[0x111b]=original[:909]+b'\x00'+original[910:]
        with self.assertRaisesRegex(ValueError,'window ingress'):
            check(reader)
        bodies[0x111b]=original
        for column,value in ((1,1),(2,145),(4,0)):
            row=reader.rows[6][0x111b-1]
            previous=row[column]
            row[column]=value
            try:
                with self.subTest(column=column),self.assertRaisesRegex(ValueError,'receiver metadata'):
                    check(reader)
            finally:
                row[column]=previous
        for offset in (905,937):
            candidate=bytearray(950)
            candidate[909]=2
            candidate[offset:offset+9]=b'\x21'+b'\x00'*8
            bodies[0x111b]=bytes(candidate)
            with self.subTest(offset=offset),self.assertRaisesRegex(ValueError,'window ingress'):
                check(reader)
        bodies[0x111b]=original
        reader.owner[0x111f]=0x496
        with self.assertRaisesRegex(ValueError,'receiver metadata'):
            check(reader)

    def test_field_store_normal_return_dominance(self):
        check=MODULE['field_store_return_dominance']
        body=bytes.fromhex('02007d381100042a')
        self.assertEqual(check(body,[],2)['reachable_return_count'],1)
        for candidate,sections,store,message in (
                (bytes.fromhex('2b057d381100042a'),[],2,'does not dominate'),
                (bytes.fromhex('022d057d381100042a'),[],3,'does not dominate'),
                (body,[{'flags':2}],2,'exception sections'),
                (bytes.fromhex('02007d3811000400'),[],2,'falls off'),
                (bytes.fromhex('02007d381100042bff'),[],2,'branch'),
                (bytes.fromhex('02007d38110004de002a'),[],2,'unsupported'),
                (bytes.fromhex('02007d381100042bfe'),[],2,'does not dominate')):
            with self.subTest(body=candidate.hex(),sections=sections):
                with self.assertRaisesRegex(ValueError,message):check(candidate,sections,store)

    def test_field_initialization_value_window(self):
        check=MODULE['field_store_return_dominance']
        body=bytes.fromhex('02220000803f7d38110004162d012a2a')
        self.assertEqual(check(body,[],6,0)['reachable_return_count'],2)
        self.assertEqual(check(b'\x2b\x00'+body,[],8,2)['reachable_return_count'],2)
        with self.assertRaisesRegex(ValueError,'value window'):
            check(bytes.fromhex('022b00220000803f7d381100042a'),[],8,0)
        with self.assertRaisesRegex(ValueError,'does not dominate'):
            check(bytes.fromhex('162d0e')+body,[],9,3)
        for target in (3,8):
            candidate=bytes([0x2b,target-2])+body
            with self.subTest(target=target),self.assertRaisesRegex(ValueError,'value window'):
                check(candidate,[],8,2)
        with self.assertRaisesRegex(ValueError,'instruction aligned'):
            check(body,[],6,2)
        for prefix in ('fe1201','fe13','fe14','fe1601000001','fe1901','fe1e'):
            with self.subTest(prefix=prefix),self.assertRaisesRegex(ValueError,'unsupported'):
                check(bytes.fromhex(prefix)+body,[],len(bytes.fromhex(prefix))+6)

    def test_category12_local_counts_and_sources(self):
        reader,_,_,_=self.current_collider_fixture()
        body=reader.body(0x111f)
        trace=MODULE['category12_synthetic_trace']
        member=lambda identity:dict(identity=identity,position=[3,4])
        candidates=[dict(identity='candidate-'+str(n),position=[3,4],
                         members=[member('member-'+str(j)) for j in range(n)])
                    for n in range(3)]
        for n,candidate in enumerate(candidates):
            with self.subTest(length=n):
                result=trace(body,[candidate])
                self.assertEqual([item['category'] for item in result],[1]+[2]*n)
                self.assertEqual([item['occurrence'] for item in result],[[0]]+[[0,j] for j in range(n)])
                self.assertEqual([item['radius'] for item in result],[20]+[24]*n)
                self.assertEqual([item['add_offset'] for item in result],[656]+[748]*n)
        result=trace(body,candidates)
        self.assertEqual([r['source'] for r in result if r['category']==1],
                         [c['identity'] for c in candidates])
        self.assertEqual(len(result),6)
        repeated=member('same-reference')
        candidate=dict(identity='candidate',position=[8,9],members=[repeated,repeated])
        result=trace(body,[candidate,candidate])
        self.assertEqual(len(result),6)
        self.assertEqual([r['occurrence'] for r in result if r['category']==2],
                         [[0,0],[0,1],[1,0],[1,1]])
        candidate['members']=[member('identity-A'),member('identity-B')]
        result=trace(body,[candidate])
        self.assertNotEqual(result[1]['source'],result[2]['source'])
        self.assertEqual(result[1]['returned_position'],result[2]['returned_position'])
        self.assertNotEqual(result[0]['returned_position'],result[1]['returned_position'])
        self.assertEqual(trace(body,[]),[])

    def test_category12_synthetic_filter_observations(self):
        reader,_,_,_=self.current_collider_fixture()
        predicate=MODULE['category12_synthetic_filter'];body=reader.body(0x1152)
        for e7,f1,expected in ((1,0,True),(0,1,True),(0,0,False),(1,1,True)):
            with self.subTest(e7=e7,f1=f1):self.assertEqual(predicate(body,e7,f1),expected)
        # First getter's e7 and second getter's f1 need not describe one object.
        first={'e7':0,'f1':2};second={'e7':2,'f1':0}
        self.assertFalse(predicate(body,first['e7'],second['f1']))
        with self.assertRaisesRegex(ValueError,'filter template'):
            predicate(body[:-1]+b'\x00',1,0)

    def test_category12_local_rule_rejects_mutations(self):
        reader,_,_,_=self.current_collider_fixture()
        body=reader.body(0x111f);check=MODULE['category12_local_rule']
        for offset in (548,577,594,599,607,627,643,646,656,663,683,714,724,736,748,755,769,779):
            changed=bytearray(body);changed[offset]^=1
            with self.subTest(offset=offset),self.assertRaisesRegex(ValueError,'window mismatch'):
                check(bytes(changed))
        changed=bytearray(body);changed[540:545]=b'\x38'+struct.pack('<i',614-545)
        with self.assertRaisesRegex(ValueError,'external interior entry'):check(bytes(changed))
        clause=dict(flags=2,try_offset=547,try_length=1,handler_offset=800,
                    handler_length=1,extra=0)
        with self.assertRaisesRegex(ValueError,'exception region overlap'):
            check(body,[dict(clauses=[clause])])

    def test_persisted_current_collider_evidence_matches_audit(self):
        reader,_,_,digest=self.current_collider_fixture()
        with patch.object(hashlib,'sha256',side_effect=digest):
            generated=MODULE['current_collider_audit'](reader)
        persisted=json.loads((ROOT/'CurrentColliderEvidence.json').read_text())
        self.assertEqual(persisted['assembly_sha256'],
                         'ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5')
        self.assertEqual(persisted['status'],'partial-byte-evidence-only')
        self.assertIs(persisted['certification_eligible'],False)
        self.assertEqual(set(persisted),set(generated))
        for evidence,count in ((generated,69),(persisted,2897)):
            inventory=evidence['step_field_references']
            self.assertEqual(inventory['methods_with_rva'],count)
            self.assertEqual(inventory['decoded_methods'],count)
            del inventory['methods_with_rva']
            del inventory['decoded_methods']
        # Nop-based synthetic IL has more reachable states than the pinned real body.
        self.assertEqual(generated['step_local_finally_control_flow']['state_count'],966)
        self.assertEqual(persisted['step_local_finally_control_flow']['state_count'],309)
        for evidence in (generated,persisted):
            del evidence['step_local_finally_control_flow']['state_count']
        # Synthetic type names differ; all remaining evidence fields must match.
        for evidence in (generated,persisted):
            for parameter in evidence['constructor']['parameter_types']:
                self.assertEqual(set(parameter),{'token','namespace','name'})
                self.assertIsInstance(parameter['namespace'],str)
                self.assertIsInstance(parameter['name'],str)
                del parameter['namespace']
                del parameter['name']
        self.assertEqual(persisted,generated)

    def test_current_collider_numerical_chain_tampering(self):
        for method in (0x1347,0x1350,0x1340,0x133b,0xab6):
            for kind in ('body','signature'):
                reader,bodies,blobs,digest=self.current_collider_fixture()
                if kind=='body':bodies[method]=b'\x2a'
                else:blobs[0x10000+method]=b'\x00\x00\x01'
                message='pair report signature' if kind=='signature' and method in (0x1347,0xab6) else 'numerical chain'
                with self.subTest(method=method,kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                    with self.assertRaisesRegex(ValueError,message):MODULE['current_collider_audit'](reader)
    def test_current_collider_numerical_identity_tampering(self):
        for kind in ('owner','field','math-scope','math-name','version','key','culture','hash','member-parent','member-name','member-signature'):
            reader,_,blobs,digest=self.current_collider_fixture()
            if kind=='owner':reader.rows[2][0x4e3-1][4]-=1
            elif kind=='field':reader.rows[4][0x12b3-1][2]=0
            elif kind=='math-scope':reader.rows[1][0x99-1][0]=10
            elif kind=='math-name':reader.rows[1][0x99-1][1]=1
            elif kind=='version':reader.rows[35][0][0]=5
            elif kind=='key':blobs[8]=b'wrong'
            elif kind=='culture':reader.rows[35][0][7]=1
            elif kind=='hash':blobs[0]=b'wrong'
            elif kind=='member-parent':reader.rows[10][0x706-1][0]=0
            elif kind=='member-name':reader.rows[10][0x706-1][1]=1
            else:blobs[0x30706]=b'wrong'
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,'vector field|Math .* identity'):
                    MODULE['current_collider_audit'](reader)
    def test_current_collider_pair_tampering(self):
        for offset in (1031,1051,1052,1063,1088,1113,1138,1165,1201,1232,1253,1274,1298,1307,1333,1357,1391):
            reader,bodies,_,digest=self.current_collider_fixture()
            body=bytearray(bodies[0x111f]);body[offset]^=1;bodies[0x111f]=bytes(body)
            with self.subTest(offset=offset),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,'pair report layout'):
                    MODULE['current_collider_audit'](reader)
        for index in (0x1347,0xd3f,0xab6,0x11e8):
            reader,_,blobs,digest=self.current_collider_fixture()
            blobs[0x10000+index]=b'\x00\x00\x01'
            with self.subTest(signature=index),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,'pair report signature'):
                    MODULE['current_collider_audit'](reader)
        reader,_,blobs,digest=self.current_collider_fixture()
        blobs[0x40000+0x5e1]=b'\x20\x00\x01'
        with patch.object(hashlib,'sha256',side_effect=digest):
            with self.assertRaisesRegex(ValueError,'pair report signature'):
                MODULE['current_collider_audit'](reader)
    def test_current_collider_call_and_list_tampering(self):
        for kind in ('constructor-call','radius15','list-name','list-namespace','scope','assembly-name'):
            reader,bodies,_,digest=self.current_collider_fixture()
            if kind in ('constructor-call','radius15'):
                method,offset=(0x111f,257) if kind=='constructor-call' else (0x111c,2207)
                body=bytearray(bodies[method]);body[offset]=0x17;bodies[method]=bytes(body)
                message='constructor call layout' if kind=='constructor-call' else 'radius15 insertion layout'
            else:
                if kind=='list-name':reader.rows[1][0x4f-1][1]=1
                elif kind=='list-namespace':reader.rows[1][0x4f-1][2]=1
                elif kind=='scope':reader.rows[1][0x4f-1][0]=10
                else:reader.rows[35][0][6]=1
                message='List type identity'
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,message):MODULE['current_collider_audit'](reader)
    def test_current_collider_rejects_signature_and_initializer_tampering(self):
        for kind,message in (('signature','constructor signature'),('field','field is not R4'),
                             ('initializer','initializer mismatch'),('method','method hash mismatch')):
            reader,bodies,blobs,digest=self.current_collider_fixture()
            if kind=='signature':blobs[2]=b'\x20\x00\x01'
            elif kind=='field':blobs[1]=b'\x06\x08'
            elif kind=='initializer':
                body=bytearray(bodies[0x110b]);body[309]=0;bodies[0x110b]=bytes(body)
            else:bodies[0x113d]=b'\x2a'
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,message):MODULE['current_collider_audit'](reader)
    def test_current_collider_category_and_insertion_tampering(self):
        for kind,message in (('category','category backing field'),('insertion','explicit insertion layout')):
            reader,bodies,blobs,digest=self.current_collider_fixture()
            if kind=='category':blobs[3]=b'\x06\x0c'
            else:
                body=bytearray(bodies[0x111f]);body[714]=0x17;bodies[0x111f]=bytes(body)
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,message):MODULE['current_collider_audit'](reader)
    def test_current_collider_metadata_and_second_insertion_tampering(self):
        cases=(('category-owner','category field ownership'),('collider-owner','collider field ownership'),
               ('instance-signature','instance field signature'),('second-insertion','second insertion layout'),
               ('add-parent','Add signature'),('add-name','Add signature'),
               ('add-signature','Add signature'),('typespec','Add signature'))
        for kind,message in cases:
            reader,bodies,blobs,digest=self.current_collider_fixture()
            if kind=='category-owner':reader.rows[2][0x49e-1][4]+=1
            elif kind=='collider-owner':reader.rows[2][0x49a-1][4]+=1
            elif kind=='instance-signature':blobs[4]=b'\x06\x08'
            elif kind=='second-insertion':
                body=bytearray(bodies[0x111f]);body[892]=0x18;bodies[0x111f]=bytes(body)
            elif kind=='add-parent':reader.rows[10][0x5cf-1][0]+=1
            elif kind=='add-name':reader.rows[10][0x5cf-1][1]=1
            elif kind=='add-signature':blobs[6]=b'\x20\x00\x01'
            else:blobs[7]=b'\x08'
            with self.subTest(kind=kind),patch.object(hashlib,'sha256',side_effect=digest):
                with self.assertRaisesRegex(ValueError,message):MODULE['current_collider_audit'](reader)
    def test_current_collider_layout_gate_survives_hash_stub(self):
        reader,bodies,_,digest=self.current_collider_fixture()
        original=bodies[0x113d]
        bodies[0x113d]=b'\x00'+original[1:]
        def stub(data):
            return digest(original if data==bodies[0x113d] else data)
        with patch.object(hashlib,'sha256',side_effect=stub):
            with self.assertRaisesRegex(ValueError,'constructor layout mismatch'):
                MODULE['current_collider_audit'](reader)
    def test_current_collider_step_math_member_gates(self):
        for member in (0x25d,0x15b,0x228,0x5ca,0x131):
            for column,value in ((0,0),(1,1),(2,0)):
                with self.subTest(member=hex(member),column=column):
                    reader,_,_,digest=self.current_collider_fixture()
                    reader.rows[10][member-1][column]=value
                    with patch.object(hashlib,'sha256',side_effect=digest):
                        with self.assertRaisesRegex(ValueError,'Math member identity mismatch'):
                            MODULE['current_collider_audit'](reader)

    def test_current_collider_step_hash_and_layout_gates(self):
        for offset in (0,622,623,797,805,873,936,941):
            reader,bodies,_,digest=self.current_collider_fixture()
            original=bodies[0x111b]
            mutated=bytearray(original);mutated[offset]^=1
            bodies[0x111b]=bytes(mutated)
            def strict(data):
                return hashlib_original(data) if data==bodies[0x111b] else digest(data)
            hashlib_original=hashlib.sha256
            with self.subTest(offset=offset),patch.object(hashlib,'sha256',side_effect=strict):
                with self.assertRaisesRegex(ValueError,'method hash mismatch'):
                    MODULE['current_collider_audit'](reader)
        reader,bodies,_,digest=self.current_collider_fixture()
        original=bodies[0x111b]
        original_digest=digest(original)
        mutated=bytearray(original);mutated[936]^=1
        bodies[0x111b]=bytes(mutated)
        def stub(data):
            return original_digest if data==bodies[0x111b] else digest(data)
        with patch.object(hashlib,'sha256',side_effect=stub):
            with self.assertRaisesRegex(ValueError,'step calculation bytes'):
                MODULE['current_collider_audit'](reader)
    def test_current_constructor_branch_rule(self):
        # Independently replay only the category dispatch, using signed relative offsets.
        dispatch=bytes.fromhex('03 2c 04 03 17 2e 0c')
        for category in (-2147483648,-1,0,1,2,2147483647):
            pc=0;stack=[]
            while pc<len(dispatch):
                opcode=dispatch[pc];pc+=1
                if opcode==3:stack.append(category)
                elif opcode==0x17:stack.append(1)
                elif opcode in (0x2c,0x2e):
                    delta=int.from_bytes(dispatch[pc:pc+1],'little',signed=True);pc+=1
                    take=stack.pop()==0 if opcode==0x2c else stack.pop()==stack.pop()
                    if take:pc+=delta
                else:self.fail('unexpected dispatch opcode')
            self.assertEqual(pc+14,33 if category==1 else 21)
    def method_reader(self, data, raw_size=None, virtual_size=None):
        reader=object.__new__(MODULE['Metadata'])
        reader.data=b'\0'+data
        reader.rows={6:[[1]]}
        raw_size=len(data) if raw_size is None else raw_size
        reader.sections=[(1,raw_size if virtual_size is None else virtual_size,1,raw_size)]
        return reader
    def exception_reader(self, clauses=None, body=b'\x00\x00\xdc\x2a', sections=None):
        if clauses is None:clauses=[(2,0,2,2,1,0)]
        if sections is None:
            sections=struct.pack('<BBH',1,4+12*len(clauses),0)
            sections+=b''.join(struct.pack('<HHBHBI',*clause) for clause in clauses)
        header=struct.pack('<HHII',0x300b,2,len(body),0)
        padding=b'\xaa'*((-(1+len(header)+len(body)))%4)
        return self.method_reader(header+body+padding+sections)
    def test_exception_sections_without_extra_sections(self):
        for data in (b'\x06\x2a',bytes.fromhex('0330000001000000000000002a')):
            self.assertEqual(self.method_reader(data).exception_sections(1),[])
        reader=self.method_reader(b'')
        reader.rows[6][0][0]=0
        with self.assertRaisesRegex(ValueError,'missing IL method'):reader.exception_sections(1)
    def test_small_exception_sections_align_using_method_rva(self):
        reader=self.exception_reader()
        self.assertEqual(reader.exception_sections(1),[{
            'bytes':'01100000020000000202000100000000',
            'clauses':[{'flags':2,'try_offset':0,'try_length':2,
                        'handler_offset':2,'handler_length':1,'extra':0}]}])
    def test_small_exception_clause_forms_and_chaining(self):
        clauses=[(flags,0,1,2,1,extra) for flags,extra in
                 ((0,0x01000001),(0,0x02000001),(0,0x1b000001),(1,1),(2,0),(4,0))]
        reader=self.exception_reader(clauses)
        self.assertEqual(len(reader.exception_sections(1)[0]['clauses']),6)
        first=bytes.fromhex('81100000020000000202000100000000')
        second=bytes.fromhex('01100000040000000202000100000000')
        reader=self.exception_reader(sections=first+second)
        self.assertEqual([s['clauses'][0]['flags'] for s in reader.exception_sections(1)],[2,4])
    def test_small_exception_section_headers_fail_closed(self):
        valid=bytes.fromhex('01100000020000000202000100000000')
        for kind in (0,2,0x42,0xc2,0xff):
            with self.subTest(kind=kind):
                with self.assertRaisesRegex(ValueError,'unsupported IL exception section kind'):
                    self.exception_reader(sections=bytes([kind])+valid[1:]).exception_sections(1)
        for size,reserved in ((0,0),(4,0),(15,0),(17,0),(16,1)):
            with self.subTest(size=size,reserved=reserved):
                with self.assertRaisesRegex(ValueError,'invalid small IL exception section header'):
                    self.exception_reader(sections=struct.pack('<BBH',1,size,reserved)+valid[4:]).exception_sections(1)
        for length in range(len(valid)):
            with self.subTest(length=length):
                with self.assertRaises(ValueError):
                    self.exception_reader(sections=valid[:length]).exception_sections(1)
        with self.assertRaises(ValueError):
            self.exception_reader(sections=bytes([0x81])+valid[1:]).exception_sections(1)
    def fat_exception_section(self, clauses, more=False):
        size=4+24*len(clauses)
        return bytes([0x41|(0x80 if more else 0)])+size.to_bytes(3,'little')+b''.join(
            struct.pack('<IIIIII',*clause) for clause in clauses)
    def test_fat_exception_clause_forms_and_mixed_chaining(self):
        clauses=[(flags,0,1,2,1,extra) for flags,extra in
                 ((0,0x01000001),(0,0x02000001),(0,0x1b000001),(1,1),(2,0),(4,0))]
        fat=self.fat_exception_section(clauses)
        parsed=self.exception_reader(sections=fat).exception_sections(1)
        self.assertEqual(parsed[0]['bytes'],fat.hex())
        self.assertEqual([tuple(c.values()) for c in parsed[0]['clauses']],clauses)
        small=bytes.fromhex('01100000020000000202000100000000')
        for sections in (self.fat_exception_section(clauses,True)+small,
                         bytes([0x81])+small[1:]+fat):
            self.assertEqual(len(self.exception_reader(sections=sections).exception_sections(1)),2)
    def test_fat_exception_offsets_and_lengths_are_not_truncated(self):
        body=b'\x00'*70000+b'\xdc\x2a'
        clause=(2,256,69744,70000,1,0)
        parsed=self.exception_reader(body=body,sections=self.fat_exception_section([clause])).exception_sections(1)
        self.assertEqual(tuple(parsed[0]['clauses'][0].values()),clause)
        for count in (11,2731):
            with self.subTest(clause_count=count):
                sections=self.fat_exception_section([clause]*count)
                self.assertGreater(len(sections),255 if count==11 else 65535)
                parsed=self.exception_reader(body=body,sections=sections).exception_sections(1)
                self.assertEqual(len(parsed[0]['clauses']),count)
                self.assertEqual(tuple(parsed[0]['clauses'][-1].values()),clause)
    def test_fat_exception_sections_fail_closed(self):
        valid=self.fat_exception_section([(2,0,2,2,1,0)])
        for size in (0,4,16,27,29,0xffffff):
            with self.subTest(size=size):
                with self.assertRaises(ValueError):
                    self.exception_reader(sections=b'\x41'+size.to_bytes(3,'little')+valid[4:]).exception_sections(1)
        for length in range(len(valid)):
            with self.subTest(length=length):
                with self.assertRaises(ValueError):
                    self.exception_reader(sections=valid[:length]).exception_sections(1)
        for clause in ((3,0,2,2,1,0),(0x10002,0,2,2,1,0),(2,0,0,2,1,0),
                       (2,0,2,2,0,0),(2,0xffffffff,2,2,1,0),(2,0,0xffffffff,2,1,0),
                       (2,0,2,0xffffffff,1,0),(2,0,2,2,0xffffffff,0),
                       (2,0,2,2,1,1),(4,0,2,2,1,1),(1,0,1,2,1,2),
                       (0,0,2,2,1,0),(0,0,2,2,1,0x04000001)):
            with self.subTest(clause=clause):
                with self.assertRaisesRegex(ValueError,'invalid fat IL exception clause'):
                    self.exception_reader(sections=self.fat_exception_section([clause])).exception_sections(1)
        for clause in ((2,1,1,3,1,0),(2,0,1,3,1,0),(2,0,2,1,1,0),(1,0,2,3,1,1)):
            with self.subTest(operand_boundary=clause):
                with self.assertRaisesRegex(ValueError,'invalid fat IL exception clause'):
                    self.exception_reader(body=bytes.fromhex('1f0026dc2a'),
                        sections=self.fat_exception_section([clause])).exception_sections(1)
        reader=self.exception_reader(sections=valid)
        rva,virtual,start,raw=reader.sections[0]
        reader.sections=[(rva,virtual,start,raw-1)]
        with self.assertRaises(ValueError):reader.exception_sections(1)
        with self.assertRaises(ValueError):
            self.exception_reader(sections=bytes([0xc1])+valid[1:]).exception_sections(1)
    def test_small_exception_sections_require_raw_backing(self):
        reader=self.exception_reader()
        rva,virtual,start,raw=reader.sections[0]
        reader.sections=[(rva,virtual,start,raw-1)]
        with self.assertRaises(ValueError):reader.exception_sections(1)
    def test_small_exception_clauses_reject_invalid_ranges_and_extras(self):
        invalid=[(3,0,2,2,1,0),(8,0,2,2,1,0),(2,0,0,2,1,0),
                 (2,0,2,2,0,0),(2,4,1,2,1,0),(2,0,5,2,1,0),
                 (2,0,2,4,1,0),(2,0,2,2,3,0),(2,0,2,2,1,1),
                 (4,0,2,2,1,1),(1,0,1,2,1,2),(1,0,1,2,1,4),
                 (0,0,2,2,1,0),(0,0,2,2,1,0x01000000),
                 (0,0,2,2,1,0x04000001)]
        for clause in invalid:
            with self.subTest(clause=clause):
                with self.assertRaisesRegex(ValueError,'invalid small IL exception clause'):
                    self.exception_reader([clause]).exception_sections(1)
        body=bytes.fromhex('1f0026dc2a')
        for clause in ((2,1,1,3,1,0),(2,0,1,3,1,0),(2,0,2,1,1,0),
                       (2,0,2,0,1,0),(1,0,2,3,1,1)):
            with self.subTest(operand_boundary=clause):
                with self.assertRaisesRegex(ValueError,'invalid small IL exception clause'):
                    self.exception_reader([clause],body).exception_sections(1)
    def test_method_bodies_require_complete_file_backing(self):
        for data in (b'\x12\x2a',bytes.fromhex('03 30 00 00 02 00 00 00 00 00 00 00 2a')):
            with self.subTest(data=data.hex()):
                with self.assertRaises(ValueError):self.method_reader(data).body(1)
    def test_method_body_cannot_cross_raw_section_boundary(self):
        reader=self.method_reader(b'\x0a\x00\x2a',raw_size=2,virtual_size=3)
        with self.assertRaises(ValueError):reader.body(1)
        reader=self.method_reader(b'\x2a',raw_size=0,virtual_size=1)
        with self.assertRaises(ValueError):reader.body(1)
        reader=self.method_reader(bytes.fromhex('03 30 00 00 02 00 00 00 00 00 00 00 00 2a'),
                                  raw_size=13,virtual_size=14)
        with self.assertRaises(ValueError):reader.body(1)
    def test_extended_fat_method_header_requires_complete_file_backing(self):
        reader=self.method_reader(bytes.fromhex('03 40 00 00 00 00 00 00 00 00 00 00'))
        with self.assertRaises(ValueError):reader.body(1)
    def test_fat_method_header_requires_at_least_three_words(self):
        for words in (0,1,2):
            data=bytes((3,words<<4))+b'\0'*10
            with self.subTest(words=words):
                with self.assertRaises(ValueError):self.method_reader(data).body(1)
    def test_valid_tiny_and_fat_method_bodies_are_preserved(self):
        for data in (b'\x06\x2a',bytes.fromhex('03 30 00 00 01 00 00 00 00 00 00 00 2a'),
                     bytes.fromhex('03 40 00 00 01 00 00 00 00 00 00 00 00 00 00 00 2a')):
            with self.subTest(data=data.hex()):
                self.assertEqual(self.method_reader(data).body(1),b'\x2a')
    def test_tokens_inside_integer_operands_are_not_instructions(self):
        body=bytes.fromhex('20 7b 14 04 00 2a')
        self.assertEqual(list(MODULE['instructions'](body)),[(0,0x20,None),(5,0x2a,None)])
    def test_actual_field_token_is_decoded_at_instruction_boundary(self):
        self.assertEqual(list(MODULE['instructions'](bytes.fromhex('02 7b 14 04 00 04 2a'))),
                         [(0,2,None),(1,0x7b,0x04000414),(6,0x2a,None)])
    def test_switch_and_prefix_operands_are_sized(self):
        body=bytes.fromhex('45 01 00 00 00 00 00 00 00 fe 06 43 07 00 06 2a')
        self.assertEqual([i[0] for i in MODULE['instructions'](body)],[0,9,15])
        self.assertEqual(list(MODULE['instructions'](body))[1][2],0x06000743)
    def test_reserved_opcodes_fail_closed(self):
        for opcode in (0x24,0x77,0x78,0xa6,0xb2,0xbb,0xc1,0xc4,0xc5,0xc7,0xcf,0xe1,0xff):
            with self.subTest(opcode=opcode):
                with self.assertRaisesRegex(ValueError,'unsupported IL opcode'):
                    list(MODULE['instructions'](bytes((opcode,0x2a))))
        for second in (0x08,0x10,0x1b,0x1f,0xff):
            with self.subTest(prefix=second):
                with self.assertRaisesRegex(ValueError,'unsupported IL opcode'):
                    list(MODULE['instructions'](bytes((0xfe,second,0x2a))))
    def test_no_prefix_flag_is_not_decoded_as_an_instruction(self):
        body=bytes.fromhex('fe 19 7b 2a')
        self.assertEqual(list(MODULE['instructions'](body)),[(0,0xfe19,None),(3,0x2a,None)])
        with self.assertRaisesRegex(ValueError,'truncated IL operand'):
            list(MODULE['instructions'](bytes.fromhex('fe 19')))
    def test_all_supported_opcode_operand_boundaries(self):
        token_ops={0x27,0x28,0x29,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x79,
                   0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x8c,0x8d,0x8f,0xa3,0xa4,0xa5,0xc2,0xc6,0xd0}
        sizes={1:set(range(0x0e,0x14))|{0x1f,0xde}|set(range(0x2b,0x38)),
               4:token_ops|{0x20,0x22,0xdd}|set(range(0x38,0x45)),8:{0x21,0x23}}
        supported=set(range(0x24))|set(range(0x25,0x77))|set(range(0x79,0xa6))|set(range(0xb3,0xbb))|{0xc2,0xc3,0xc6}|set(range(0xd0,0xe1))
        for opcode in range(256):
            if opcode==0xfe:continue
            with self.subTest(opcode=hex(opcode)):
                if opcode not in supported:
                    with self.assertRaises(ValueError):list(MODULE['instructions'](bytes((opcode,))))
                    continue
                size=4 if opcode==0x45 else next((n for n,ops in sizes.items() if opcode in ops),0)
                prefix=bytes((opcode,));operand=b'\0'*size
                decoded=list(MODULE['instructions'](prefix+operand+b'\x2a'))
                self.assertEqual(decoded,[(0,opcode,0 if opcode in token_ops else None),(1+size,0x2a,None)])
                for length in range(size):
                    with self.assertRaises(ValueError):list(MODULE['instructions'](prefix+operand[:length]))
    def test_all_extended_opcode_operand_boundaries(self):
        token_ops={0x06,0x07,0x15,0x16,0x1c}
        supported=set(range(8))|set(range(9,16))|set(range(0x11,0x1b))|set(range(0x1c,0x1f))
        for second in range(256):
            with self.subTest(second=hex(second)):
                prefix=bytes((0xfe,second))
                if second not in supported:
                    with self.assertRaises(ValueError):list(MODULE['instructions'](prefix))
                    continue
                size=4 if second in token_ops else 2 if second in range(9,15) else 1 if second in (0x12,0x19) else 0
                operand=b'\0'*size
                self.assertEqual(list(MODULE['instructions'](prefix+operand+b'\x2a')),
                                 [(0,0xfe00|second,0 if second in token_ops else None),(2+size,0x2a,None)])
                for length in range(size):
                    with self.assertRaises(ValueError):list(MODULE['instructions'](prefix+operand[:length]))
    def test_truncation_fails_closed(self):
        for body in (b'\xfe',bytes.fromhex('7b 14'),bytes.fromhex('45 ff ff ff ff')):
            with self.assertRaises(ValueError):list(MODULE['instructions'](body))
    def cache_fixture(self):
        return {
            0x913:bytes.fromhex('28b901000ad0ce02000228b501000a6fb601000a2e0720f0872c006a2a280d0900062d0720f0872c006a2a1a8d7a0000010a0619208ead036b66656666656566656665669e06172087271d7166656665666566656666659e061820860ac72b66666566656566666566659e06162071fb6bdb6665666665666566659e027b260500040b027b270500040c204886c8616666656566666566650d20e1c8103966666565666665666513041613052b3b08071a62071b6361075811040611041f0b63195f945861590c11040959130407081a62081b636108581104061104195f945861590b11051758130511051f2033bf1613062b0b061106169e11061758130611061a33f0086a1f206213071107076e60130711072a'),
            0x914:bytes.fromhex('28b901000ad0ce02000228b501000a6fb601000a2e012a280d0900062d012a1a8d7a0000010a06172087271d7166656665666665656665669e06162071fb6bdb6666656566666566659e061820810ac72b6665666665656665669e0619208ead036b66656666656566656665669e204886c8616666656566666566650b03690c031f2063690d1613041613052b3b08091a62091b636109581104061104195f945861580c11040758130409081a62081b6361085811040611041f0b63195f945861580d11051758130511051f2033bf1613062b0b061106169e11061758130611061a33f002087d2605000402097d270500042a')}

    def test_current_cache_zero_from_source_il(self):
        bodies=self.cache_fixture()
        metadata=Mock()
        metadata.body.side_effect=lambda rid:bodies[rid]
        evidence=MODULE['current_cache_audit'](metadata)
        self.assertEqual(evidence['encrypted_zero'],{'04000526':'a8d058e7','04000527':'e1b1ded8'})
        self.assertEqual(evidence['decrypted_zero'],0)
        self.assertEqual(len(evidence['assumptions']),2)

    def test_current_cache_rejects_every_source_byte_mutation(self):
        bodies=self.cache_fixture()
        for rid,body in bodies.items():
            for offset in range(len(body)):
                mutated=bytearray(body);mutated[offset]^=1
                metadata=Mock()
                metadata.body.side_effect=lambda index:bytes(mutated) if index==rid else bodies[index]
                with self.subTest(method=hex(rid),offset=offset),self.assertRaises(ValueError):
                    MODULE['current_cache_audit'](metadata)

    def test_cache_integer_vm_semantics_and_rejections(self):
        execute=MODULE['current_cache_execute']
        cases={
            '20ffffff7f17582a':-2147483648,
            '20ffffffff17632a':-1,
            '20ffffffff6e2a':4294967295,
            '20ffffffff6a2a':-1,
            '20ffffffff6a6e2a':-1,
            '201032547665662a':1985229327,
            '031f2063692a':-2147483648}
        for body,result in cases.items():
            with self.subTest(body=body):
                self.assertEqual(execute(bytes.fromhex(body),0,{},0x8000000000000000),result)
        for body in ('002a','2bff2a','2bfe','1a8d7b0000012a','2a20','16172a',
                     '582a','0a2a','062a','11042a','13042a','027b260500042a',
                     '7b260500042a','16169e2a','1616942a'):
            with self.subTest(body=body),self.assertRaises(ValueError):
                execute(bytes.fromhex(body),0,{})

    def dimension_metadata_fixture(self):
        metadata=Mock(counts={},field_owner={},owner={})
        metadata.rows={4:[[0,0,0] for _ in range(0x12b4)],
                       6:[[0,0,0,0,0] for _ in range(0x133b)],
                       2:[[0,0,0,0] for _ in range(0x4e2)],
                       1:[[0,0,0] for _ in range(0xe4)]}
        signatures={}
        fields=((0xb54,0x3c9,6,'0608'),(0xb55,0x3c9,6,'0608'),
                (0x12b3,0x4e2,6,'060c'),(0x12b4,0x4e2,6,'060c'),
                (0x9d4,0x375,0x26,'06118f24'),(0x22a,0x206,0x26,'06128dec'),
                (0xa00,0x37b,6,'06118f24'))
        methods=((0xcee,0x3c9,0x1886,'2002010808'),(0x133b,0x4e2,0x1886,'2002010c0c'),
                 (0xb3d,0x375,0x86,'200008'),(0xb3e,0x375,0x86,'200008'),
                 (0x646,0x206,0x86,'2000118f24'),(0xcf1,0x3c9,0x86,'2000119388'))
        for table,records,owners in ((4,fields,metadata.field_owner),(6,methods,metadata.owner)):
            for rid,owner,flags,signature in records:
                key=(table,rid)
                signatures[key]=bytes.fromhex(signature)
                metadata.rows[table][rid-1]=[flags,0,key] if table==4 else [0,0,flags,0,key]
                owners[rid]=owner
        for rid in (0x3c9,0x4e2):
            metadata.rows[2][rid-1][3]=0x391
        metadata.rows[1][0xe4-1]=[6,1,2]
        metadata.blob.side_effect=lambda index:signatures[index]
        metadata.string.side_effect=lambda index:{1:'ValueType',2:'System'}[index]
        return metadata,signatures

    def test_current_dimension_metadata(self):
        metadata,_=self.dimension_metadata_fixture()
        proof=MODULE['current_dimension_metadata_audit'](metadata)
        self.assertEqual(len(proof['fields']),7)
        self.assertEqual(len(proof['methods']),6)
        self.assertEqual(proof['fields']['040009d4']['signature'],'06118f24')

    def test_current_dimension_metadata_rejects_mutations(self):
        audit=MODULE['current_dimension_metadata_audit']
        metadata,signatures=self.dimension_metadata_fixture()
        for key,signature in list(signatures.items()):
            for changed in [signature[:i]+bytes([signature[i]^1])+signature[i+1:] for i in range(len(signature))]+[signature[:-1],signature+b'\x00']:
                signatures[key]=changed
                with self.subTest(signature=key,changed=changed.hex()),self.assertRaises(ValueError):
                    audit(metadata)
            signatures[key]=signature
        for table,owners,flag_index in ((4,metadata.field_owner,0),(6,metadata.owner,2)):
            for rid,owner in list(owners.items()):
                owners[rid]=owner+1
                with self.subTest(table=table,owner=rid),self.assertRaises(ValueError):
                    audit(metadata)
                owners[rid]=owner
                row=metadata.rows[table][rid-1]
                row[flag_index]^=0x10
                with self.subTest(table=table,flags=rid),self.assertRaises(ValueError):
                    audit(metadata)
                row[flag_index]^=0x10
        for rid in (0x3c9,0x4e2):
            metadata.rows[2][rid-1][3]^=1
            with self.subTest(base=rid),self.assertRaises(ValueError):
                audit(metadata)
            metadata.rows[2][rid-1][3]^=1
        for index in (1,2):
            metadata.string.side_effect=lambda i:'changed' if i==index else {1:'ValueType',2:'System'}[i]
            with self.subTest(identity=index),self.assertRaises(ValueError):
                audit(metadata)
        metadata.string.side_effect=lambda i:{1:'ValueType',2:'System'}[i]
        for table in (3,5):
            metadata.counts={table:1}
            with self.subTest(pointer_table=table),self.assertRaises(ValueError):
                audit(metadata)

    def dimension_leaf_fixture(self):
        return {0xcee:bytes.fromhex('02037d540b000402047d550b00042a'),
                0x133b:bytes.fromhex('02037db312000402047db41200042a'),
                0xb3d:bytes.fromhex('027bd40900047b540b00042a'),
                0xb3e:bytes.fromhex('027bd40900047b550b00042a'),
                0x646:bytes.fromhex('027b2a0200047b000a00042a'),
                0xcf1:bytes.fromhex('027b540b00046b027b550b00046b733b1300062a')}

    def test_current_dimension_leaf_dataflow(self):
        bodies=self.dimension_leaf_fixture()
        metadata,_=self.dimension_metadata_fixture()
        metadata.body.side_effect=lambda rid:bodies[rid]
        evidence=MODULE['current_dimension_leaf_audit'](metadata)
        self.assertEqual(evidence['index2_constructor'],{'x_field':'04000b54','y_field':'04000b55'})
        self.assertEqual(evidence['texture_dimension_getter']['dimension_field'],'04000a00')
        self.assertEqual(evidence['index2_to_vector2']['constructor'],'0600133b')
        self.assertEqual(len(evidence['method_il_sha256']),6)
        for rid,body in bodies.items():
            self.assertEqual(evidence['method_il_sha256'][f'{0x06000000|rid:08x}'],hashlib.sha256(body).hexdigest())

    def test_current_dimension_leaf_rejects_source_mutations(self):
        bodies=self.dimension_leaf_fixture()
        for rid,body in bodies.items():
            for changed in [body[:offset]+bytes([body[offset]^1])+body[offset+1:] for offset in range(len(body))]+[body[:-1],body+b'\x00']:
                metadata=Mock()
                metadata.body.side_effect=lambda index:changed if index==rid else bodies[index]
                with self.subTest(method=hex(rid),body=changed.hex()),self.assertRaises(ValueError):
                    MODULE['current_dimension_leaf_audit'](metadata)

    def dimension_wiring_fixture(self):
        return {0x644:bytes.fromhex('02283800000a037b000a0004047b000a000428f70c00062d27037b000a0004057b000a000428f70c00062d14037b000a00040e047b000a000428f70c00062c10207e78d2d2287c07000673dc0900067a02037d2a02000402047d2b02000402057d2c020004020e047d2d0200042a'),
                0xb3b:bytes.fromhex('02283800000a047b540b000416310b047b550b000416fe022b0116203eb2d3d2287c07000628b40a0006047b540b0004047b550b00045a0328c90a00065a0a058e6906fe01205bb2d3d2287c07000628b40a000602037dd309000402047dd409000402027cd40900047b540b0004027bd309000428c90a00065a7dd509000402057dd60900042a'),
                0xb3c:bytes.fromhex('0203040573ee0c00060e04283b0b00062a'),
                0xcf7:bytes.fromhex('027b540b0004037b540b00043312027b550b0004037b550b0004fe0116fe012a172a'),
                0x80c:bytes.fromhex('02283800000a020f0128f10c00067d4b0400042a'),
                0x80d:bytes.fromhex('7efa0800047b4d0500047bc20800046f46060006730c08000680490400041f421f3973ee0c0006730c080006804a0400042a')}

    def wiring_metadata_fixture(self):
        metadata,signatures=self.dimension_metadata_fixture()
        fields=((0x9d3,0x375,0x26,'061188a4'),(0x9d4,0x375,0x26,'06118f24'),
                (0x9d5,0x375,0x26,'0608'),(0x9d6,0x375,0x26,'061d05'),
                (0xa00,0x37b,6,'06118f24'),(0x22a,0x206,0x26,'06128dec'),
                (0x22b,0x206,0x26,'06128dec'),(0x22c,0x206,0x26,'06128dec'),
                (0x22d,0x206,0x26,'06128dec'),(0x44b,0x285,0x26,'06119388'),
                (0x449,0x285,0x16,'06128a14'),(0x44a,0x285,0x16,'06128a14'),
                (0x8fa,0x335,0x16,'06128b78'),(0x54d,0x2de,6,'06128cc4'),
                (0x8c2,0x331,6,'06128818'))
        methods=((0x644,0x206,0x1886,'200401128dec128dec128dec128dec'),
                 (0xb3b,0x375,0x1886,'2003011188a4118f241d05'),
                 (0xb3c,0x375,0x1886,'2004011188a408081d05'),
                 (0xcf7,0x3c9,0x896,'000202118f24118f24'),
                 (0x80c,0x285,0x1886,'200101118f24'),(0x80d,0x285,0x1891,'000001'))
        for table,records,owners in ((4,fields,metadata.field_owner),(6,methods,metadata.owner)):
            for rid,owner,flags,signature in records:
                key=(table,rid)
                signatures[key]=bytes.fromhex(signature)
                metadata.rows[table][rid-1]=[flags,0,key] if table==4 else [0,0,flags,0,key]
                owners[rid]=owner
        return metadata,signatures,fields,methods

    def test_current_wiring_metadata(self):
        metadata,_,fields,methods=self.wiring_metadata_fixture()
        proof=MODULE['current_dimension_wiring_metadata_audit'](metadata)
        self.assertEqual(len(proof['fields']),len(fields))
        self.assertEqual(len(proof['methods']),len(methods))
        self.assertEqual(proof['methods']['06000cf7']['implementation_flags'],0)

    def test_current_wiring_metadata_rejects_mutations(self):
        metadata,signatures,fields,methods=self.wiring_metadata_fixture()
        audit=MODULE['current_dimension_wiring_metadata_audit']
        for table,records,owners,indices in ((4,fields,metadata.field_owner,(0,)),
                                           (6,methods,metadata.owner,(1,2))):
            for rid,owner,_,_ in records:
                key=(table,rid)
                signature=signatures[key]
                for changed in [signature[:i]+bytes([signature[i]^1])+signature[i+1:] for i in range(len(signature))]+[signature[:-1],signature+b'\x00']:
                    signatures[key]=changed
                    with self.subTest(key=key,signature=changed.hex()),self.assertRaises(ValueError):
                        audit(metadata)
                signatures[key]=signature
                owners[rid]=owner+1
                with self.subTest(key=key,owner=True),self.assertRaises(ValueError):
                    audit(metadata)
                owners[rid]=owner
                for index in indices:
                    metadata.rows[table][rid-1][index]^=1
                    with self.subTest(key=key,column=index),self.assertRaises(ValueError):
                        audit(metadata)
                    metadata.rows[table][rid-1][index]^=1
        for table in (3,5):
            metadata.counts={table:1}
            with self.subTest(pointer_table=table),self.assertRaises(ValueError):
                audit(metadata)
        metadata.counts={}
        for table in (4,6):
            rows=metadata.rows[table]
            metadata.rows[table]=[]
            with self.subTest(truncated=table),self.assertRaises(ValueError):
                audit(metadata)
            metadata.rows[table]=rows

    def test_current_dimension_wiring(self):
        bodies=self.dimension_wiring_fixture()
        metadata,_,_,_=self.wiring_metadata_fixture()
        metadata.body.side_effect=lambda rid:bodies[rid]
        evidence=MODULE['current_dimension_wiring_audit'](metadata)
        self.assertEqual(evidence['grid_constructor']['vector_field'],'0400044b')
        self.assertEqual(evidence['grid_static_initializer']['secondary_grid']['dimensions'],[66,57])
        self.assertEqual(evidence['texture_bundle']['comparisons'],[[1,2],[1,3],[1,4]])
        for rid,body in bodies.items():
            self.assertEqual(evidence['method_il_sha256'][f'{0x06000000|rid:08x}'],hashlib.sha256(body).hexdigest())

    def test_current_dimension_wiring_rejects_mutations(self):
        bodies=self.dimension_wiring_fixture()
        for rid,body in bodies.items():
            for changed in [body[:offset]+bytes([body[offset]^1])+body[offset+1:] for offset in range(len(body))]+[body[:-1],body+b'\x00']:
                metadata=Mock()
                metadata.body.side_effect=lambda index:changed if index==rid else bodies[index]
                with self.subTest(method=hex(rid),body=changed.hex()),self.assertRaises(ValueError):
                    MODULE['current_dimension_wiring_audit'](metadata)

    def pixel_fixture(self):
        return {0xb40:bytes.fromhex('027bd50900048d3b0000010a160b2b6002283e0b00061759075907027bd50900045a0c027bd50900045a0d027bd6090004080616027bd5090004286903000a027bd609000409027bd609000408027bd5090004286903000a0616027bd609000409027bd5090004286903000a0717580b0702283e0b0006185b32952a'),
                0xed5:bytes.fromhex('026f3d0b00060a026f3e0b00060b027bd60900040c160d2b72081a095a911304081a095a1758911305081a095a1858911306081a095a1958911307110411075a20ff0000005bd21304110511075a20ff0000005bd21305110611075a20ff0000005bd21306081a095a11049c081a095a175811059c081a095a185811069c081a095a195811079c0917580d0906075a32882a')}

    def test_current_pixel_fixtures(self):
        metadata=Mock();metadata.body.side_effect=self.pixel_fixture().__getitem__
        proof=MODULE['current_pixel_audit'](metadata)
        self.assertEqual(proof['flip_fixture_count'],36)
        self.assertEqual(proof['premultiply_alpha_channel_pairs'],65536)
        execute=MODULE['current_pixel_execute'];bodies=self.pixel_fixture()
        pixels=bytes((255,128,1,0,255,128,1,255))
        self.assertEqual(execute(bodies[0xed5],2,1,8,pixels),bytes((0,0,0,0,255,128,1,255)))
        self.assertEqual(execute(bodies[0xb40],1,3,4,bytes(range(12))),bytes(range(8,12))+bytes(range(4,8))+bytes(range(4)))
        for dimensions in ((-1,1,4),(1,1,3),(4097,1,16388)):
            with self.assertRaises(ValueError):execute(bodies[0xb40],*dimensions,b'')

    def test_current_pixel_interpreter_rejects_invalid_programs(self):
        execute=MODULE['current_pixel_execute']
        cases=[(b'\x58','stack underflow'),(b'\x06','uninitialized'),
               (b'\x16\x2a','return stack'),(b'\x2b\xff','invalid pixel IL branch'),
               (b'\x00','unsupported pixel IL opcode'),(b'\x2b\xfe','instruction budget'),
               (bytes.fromhex('0228000000062a'),'unsupported pixel call'),
               (bytes.fromhex('027b000000042a'),'unsupported pixel field'),
               (bytes.fromhex('027bd609000416912a'),'array bounds'),
               (bytes.fromhex('16165b2a'),'division by zero')]
        for body,message in cases:
            with self.subTest(body=body.hex()),self.assertRaisesRegex(ValueError,message):
                execute(body,0,0,0,b'')

    def test_current_pixel_mutations_fail_closed(self):
        bodies=self.pixel_fixture()
        for rid,body in bodies.items():
            for changed in [body[:i]+bytes([body[i]^1])+body[i+1:] for i in range(len(body))]+[body[:-1],body+b'\x00']:
                metadata=Mock();metadata.body.side_effect=lambda index:changed if index==rid else bodies[index]
                with self.subTest(method=hex(rid),body=changed.hex()),self.assertRaisesRegex(ValueError,'unsupported current pixel IL'):
                    MODULE['current_pixel_audit'](metadata)

    def current_asset_fixture(self):
        payload=struct.pack('<HH',0x8a92,0xcb6a)+bytes(161224)
        metadata=Mock(data=b'assembly'+bytes(0x8295c-8)+payload)
        metadata.number.return_value=len(payload)
        metadata.body.return_value=b'\x2a'
        metadata.rows={28:[[512,0x9f4<<1|1,1,1]],26:[[2]]}
        metadata.string.side_effect=lambda index:{1:'IMG_Load',2:'SDL2_image.dll'}[index]
        arguments=(0xd2d30604,0xd2d356b3,0xd2d21f0f,0xd2d24077,0xd2d25f9b,0xd2d25fa8,0xd2d25ffe,0xd2d21e4c)
        texts=('Content','.png','.psd','textures/left','textures/right','textures/bottom','textures/top','atom.lighting')
        records: dict[int,str]=dict(zip(arguments,texts))
        png=b'\x89PNG\r\n\x1a\n'+struct.pack('>I',13)+b'IHDR'+struct.pack('>II',82,71)+bytes(9)
        hashes=['2db084044cd64132361e20b6eb0caabcb7196010440b3a04c3adadfdbd5bb96b',
                '7c1fe2c11cd89e36dd1a9c2a88bfaf7983543c987665e9cdc974d43295a8657b',
                '53ce58d8a918cbb0f30542916c2f16e716bfcd9da6f471432f77d6617b8dca02',
                '34897c68cd2091a6e5292b742750854a29809ac390e07490b950d97d0ac0b8e4']
        assets={text+'.png':png+bytes([index]) for index,text in enumerate(texts[3:7])}
        digests={metadata.data:'ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5',
                 payload:'eb30da8c127cda59067d17dc3f396465f50a1cd4275b92db5ccbb264190addc3'}
        digests.update({data:digest for data,digest in zip(assets.values(),hashes)})
        return metadata,records,assets,digests

    def run_current_asset_fixture(self,metadata,records,assets,digests,psd=False):
        real_hash=hashlib.sha256
        def digest(data):
            return Mock(hexdigest=lambda:digests.get(data,real_hash(data).hexdigest()))
        with patch.dict(MODULE['current_grid_asset_audit'].__globals__,
                        current_cache_audit=lambda metadata:{'status':'fixture-only'},
                        current_dimension_leaf_audit=lambda metadata:{'status':'fixture-only'},
                        current_pixel_audit=lambda metadata:{'status':'fixture-only'},
                        current_dimension_wiring_audit=lambda metadata:{'status':'fixture-only'},
                        current_grid_string=lambda payload,arg:{'text':records[arg],'argument':f'{arg:08x}'}), \
                patch.object(hashlib,'sha256',side_effect=digest), \
                patch.object(Path,'exists',return_value=psd), \
                patch.object(Path,'read_bytes',side_effect=lambda path:assets[path.as_posix()],autospec=True):
            return MODULE['current_grid_asset_audit'](metadata,Path('.'))

    def test_current_asset_audit_records_conditional_dimension_chain(self):
        evidence=self.run_current_asset_fixture(*self.current_asset_fixture())
        self.assertFalse(evidence['certification_eligible'])
        self.assertEqual(len(evidence['textures']),4)
        self.assertEqual(evidence['dimension_flow']['native_import']['entry_point'],'IMG_Load')
        self.assertIn('06000ed4',evidence['method_il_sha256'])
        self.assertIn('06000b40',evidence['method_il_sha256'])

    def test_current_asset_mutations_fail_closed(self):
        for mutation in ('assembly-hash','resource-hash','resource','header','loader','empty','absolute','parent','psd',
                         'truncated','signature','ihdr','dimensions','hash','import','import-name','import-library','import-duplicate','selector'):
            with self.subTest(mutation=mutation):
                metadata,records,assets,digests=self.current_asset_fixture()
                first='textures/left.png'
                if mutation=='assembly-hash':digests[metadata.data]='0'*64
                elif mutation=='resource-hash':digests[metadata.data[0x8295c:]]='0'*64
                elif mutation=='resource':metadata.number.return_value=1
                elif mutation=='header':
                    old=metadata.data;metadata.data=old[:0x8295c]+b'\0\0'+old[0x8295e:]
                    digests[metadata.data]=digests[old]
                    digests[metadata.data[0x8295c:]]=digests[old[0x8295c:]]
                elif mutation=='loader':records[0xd2d356b3]='.other'
                elif mutation in ('empty','absolute','parent'):
                    records[0xd2d24077]={'empty':'','absolute':'/outside','parent':'../outside'}[mutation]
                elif mutation=='truncated':assets[first]=b'\x89PNG'
                elif mutation=='signature':assets[first]=b'bad'+assets[first][3:]
                elif mutation=='ihdr':assets[first]=assets[first][:8]+bytes(4)+assets[first][12:]
                elif mutation=='dimensions':assets[first]=assets[first][:16]+struct.pack('>II',81,71)+assets[first][24:]
                elif mutation=='hash':assets[first]+=b'changed'
                elif mutation=='import':metadata.rows[28]=[]
                elif mutation=='import-name':metadata.string.side_effect=lambda index:{1:'Other_Load',2:'SDL2_image.dll'}[index]
                elif mutation=='import-library':metadata.string.side_effect=lambda index:{1:'IMG_Load',2:'other.dll'}[index]
                elif mutation=='import-duplicate':metadata.rows[28]*=2
                elif mutation=='selector':records[0xd2d21e4c]='other'
                with self.assertRaises(ValueError):
                    self.run_current_asset_fixture(metadata,records,assets,digests,mutation=='psd')

    def test_asset_cli_routes_current_and_historical_evidence_separately(self):
        main=MODULE['main']
        for flag,selected,other in (
                ('--audit-current-grid-assets','current_grid_asset_audit','main_grid_asset_audit'),
                ('--audit-grid-assets','main_grid_asset_audit','current_grid_asset_audit')):
            with self.subTest(flag=flag):
                metadata=Mock()
                audit=Mock(return_value={'audit':selected,'certification_eligible':False})
                unused=Mock(side_effect=AssertionError('wrong version audit selected'))
                output=io.StringIO()
                with patch.dict(main.__globals__,Metadata=Mock(return_value=metadata),
                                **{selected:audit,other:unused}), \
                        patch('sys.argv',['GameProductionEvidence.py','installation/Lightning.exe',flag]), \
                        patch('sys.stdout',output):
                    main()
                audit.assert_called_once_with(metadata,Path('installation/Content'))
                unused.assert_not_called()
                self.assertEqual(json.loads(output.getvalue()),audit.return_value)

    def test_current_string_known_encoded_records(self):
        for argument,encoded,key,text in (
                (0xd2d30604,'b20360a20ea125bc70b359e01254','bd8908','Content'),
                (0xd2d21e4c,'0731fde016a025152ae56099adf3c4989c295b34','08bb95','atom.lighting')):
            with self.subTest(argument=argument):
                position=argument^0xd2d32f94
                payload=bytes(position)+bytes.fromhex(encoded)
                decoded=MODULE['current_grid_string'](payload,argument)
                self.assertEqual(decoded,{'argument':f'{argument:08x}','payload_offset':position,
                                         'record_key':key,'flags':f'{0x40000000|len(text):08x}','text':text})
                with self.assertRaises(ValueError):MODULE['current_grid_string'](payload[:-1],argument)

    def test_current_string_records_reject_invalid_bounds_and_flags(self):
        decode=MODULE['current_grid_string']
        for position in (0,3,9,100):
            with self.assertRaises(ValueError):decode(bytes(10),position^0xd2d32f94)
        for flags in (0,0x80000001,0x60000001,0x40001001,0x40000002):
            payload=bytes(7)+struct.pack('<I',flags^4^0xcfc9ad3a^0xaa688a0f)+b'X'
            with self.assertRaises(ValueError):decode(payload,4^0xd2d32f94)

    def test_changed_game_build_requires_new_evidence(self):
        class Foreign:
            data=b'not the audited game'
        with self.assertRaises(ValueError):MODULE['production_audit'](Foreign())
        with self.assertRaises(ValueError):MODULE['main_grid_asset_audit'](Foreign(),ROOT)
        with self.assertRaises(ValueError):MODULE['current_grid_asset_audit'](Foreign(),ROOT)

    def test_recorded_asset_versions_remain_separate(self):
        current=json.loads((ROOT/'CurrentGridAssetEvidence.json').read_text())
        historical=json.loads((ROOT/'MainGridAssetEvidence.json').read_text())
        self.assertEqual(current['assembly_sha256'],
                         'ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5')
        self.assertNotEqual(current['assembly_sha256'],historical['assembly_sha256'])
        self.assertFalse(current['certification_eligible'])

    def test_recorded_evidence_is_version_pinned_and_has_no_simulation_vial_read(self):
        evidence=json.loads((ROOT/'ProductionGameEvidence.json').read_text())
        self.assertEqual(evidence['assembly_sha256'],
                         'fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d')
        self.assertEqual(evidence['method_definitions_inspected'],4995)
        self.assertEqual(evidence['method_bodies_inspected'],2881)
        self.assertEqual(evidence['simulation_methods'],49)
        self.assertEqual(evidence['simulation_methods_with_il'],49)
        vial_readers=[]
        for method in evidence['references']:
            for reference in method['references']:
                if reference['field_role'].startswith('vial_') and reference['opcode'] in ('0x7b','0x7c','0x7e','0x7f','0xd0'):
                    vial_readers.append(method['method_token'])
                    self.assertFalse(method['method_name'].startswith('.Sim.'))
        self.assertEqual(set(vial_readers),{'06000dff','06000e00','06000e01','060011b3'})

    def test_recorded_left_right_flags_adjust_display_x_by_33(self):
        evidence=json.loads((ROOT/'ProductionGameEvidence.json').read_text())
        self.assertEqual(evidence['schema'],2)
        bounds=evidence['production_bounds']
        self.assertEqual(bounds['computation']['method_token'],'06000dff')
        self.assertEqual(bounds['computation']['method_il_sha256'],
                         'daa3b737c70d7929dc35309ce2af7a87abd2bd052b9de42ca050c9fcaf7801ec')
        for side,start,target,field,corner,operation in (
                ('left',293,321,'04000416','04000a38','58'),
                ('right',332,360,'04000417','04000a39','59')):
            branch=bounds[side+'_branch']
            self.assertEqual([entry['bytes'] for entry in branch],
                ['7b'+bytes.fromhex(field)[::-1].hex(),'2c15','1200',
                 '7c'+bytes.fromhex(corner)[::-1].hex(),'7cae120004','25','4e','2200000442',operation,'56'])
            self.assertEqual(branch[0]['offset'],start)
            self.assertEqual(branch[0]['token'],field)
            self.assertEqual(branch[1]['branch_target'],target)
            self.assertEqual(branch[3]['token'],corner)
            self.assertEqual(branch[4]['token'],'040012ae')
            self.assertEqual(branch[7]['float32'],33.0)
            decoded=list(MODULE['instructions'](bytes.fromhex(''.join(entry['bytes'] for entry in branch))))
            self.assertEqual([offset+start for offset,_,_ in decoded],[entry['offset'] for entry in branch])
        self.assertEqual(bounds['derived_bounds_write'],[
            {'offset':372,'opcode':'0x7d','bytes':'7d15040004','token':'04000415'}])
    def test_recorded_display_bounds_have_exact_non_simulation_consumers(self):
        bounds=json.loads((ROOT/'ProductionGameEvidence.json').read_text())['production_bounds']
        solution=bounds['solution_bounds']
        self.assertEqual(solution['method_token'],'0600117f')
        self.assertEqual(solution['method_il_sha256'],
                         '476aedb1ed52c64bdab3b2457a22b0861b5adbb889fe36c0760ec6a609ee4344')
        self.assertEqual([(r['offset'],r['opcode']) for r in solution['references'] if r['token']=='04000415'],[(34,'0x7c')])
        consumers=bounds['solution_bounds_direct_consumers']
        self.assertEqual([(r['method_token'],r['method_il_sha256']) for r in consumers],[
            ('060007a6','30e4ca544af4232a42b22c3764e8f4b41d55b71102ab64425478a61ffe8ddf3f'),
            ('0600129e','2b77f5b56672c6968a4cba3fa4d9f53205df680f051d5c19b145143dbcd60889')])
        self.assertEqual([[(r['offset'],r['opcode']) for r in consumer['references']
                          if r['token']=='0600117f'] for consumer in consumers],[[(107,'0x6f')],[(95,'0x6f')]])
        self.assertTrue(consumers[1]['method_name'].startswith('.SolutionRecorderScreen.'))
        self.assertTrue(any(r['name']=='.Bounds2.get_Center' for r in consumers[0]['references']))
        self.assertTrue(any(r['name']=='.Bounds2.get_Width' for r in consumers[1]['references']))
        self.assertEqual(bounds['simulation_references'],[])

    def test_changed_game_build_requires_new_numerical_evidence(self):
        class Foreign:
            data=b'not the audited game'
        with self.assertRaises(ValueError):MODULE['numerical_audit'](Foreign())

    def test_numerical_ledger_pins_original_simulation_radius_assignments(self):
        evidence=json.loads((ROOT/'VerifierNumericalEvidence.json').read_text())
        self.assertEqual(evidence['assembly_sha256'],
            'fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d')
        game=evidence['original_game_static_il']
        self.assertEqual(game['simulation_initializer']['method_token'],'060010d4')
        assignments=game['simulation_float_assignments']
        self.assertEqual([(a['literal_offset'],a['write_offset'],a['field_token'],a['float32']) for a in assignments],
            [(304,309,'04001139',29.0),(314,319,'0400113a',15.0),(324,329,'0400113b',20.0),
             (334,339,'0400113c',24.0),(344,349,'0400113d',20.0)])
        self.assertEqual([a['operand_bytes'] for a in assignments],
            ['0000e841','00007041','0000a041','0000c041','0000a041'])
        self.assertTrue(any(a.get('token')=='0400113c' for a in game['manual_radius_overrides']))

    def test_game_mapper_records_float_arithmetic_without_inventing_asset_scale(self):
        game=json.loads((ROOT/'VerifierNumericalEvidence.json').read_text())['original_game_static_il']
        self.assertEqual(game['hex_mapper']['method_token'],'06000793')
        self.assertEqual(game['hex_mapper']['method_il_sha256'],
            '64850c3c5c69a193267efbafed86b6f30ef6cebd1352da9708f1fedddaae3c97')
        selected=game['hex_mapper_selected_instructions']
        self.assertEqual([(x['offset'],x['float32']) for x in selected if 'float32'in x],[(18,0.5)])
        self.assertEqual([x['offset'] for x in selected if x['opcode']=='0x6b'],[17,29,50])
        self.assertTrue(any(r['token']=='04000411' for r in game['hex_mapper_scale_initializer']['references']))
        self.assertIn('MainGridAssetEvidence.json separately closes',game['hex_mapper_scale_provenance'])

    def test_game_rotation_converts_double_sin_cos_results_to_float(self):
        game=json.loads((ROOT/'VerifierNumericalEvidence.json').read_text())['original_game_static_il']
        self.assertEqual(game['vector_rotation']['method_token'],'06001309')
        self.assertEqual(game['vector_rotation']['method_il_sha256'],
            '692975db8ca56d0dd64c934fe1a6c8d0f449aa87932fc20dbd930ab89ef29e27')
        self.assertEqual([(x['offset'],x['opcode'])for x in game['vector_rotation_rounding']],
            [(1,'0x6c'),(2,'0x28'),(7,'0x6b'),(10,'0x6c'),(11,'0x28'),(16,'0x6b')])
        self.assertEqual(game['angle_conversion_constants'][0]['integer'],60)
        self.assertEqual(game['angle_conversion_constants'][1]['operand_bytes'],'35fa8e3c')

    def test_game_point125_is_a_timestep_cap_not_a_clearance_proof(self):
        evidence=json.loads((ROOT/'VerifierNumericalEvidence.json').read_text())
        game=evidence['original_game_static_il']
        self.assertEqual(game['collision_step_computation']['method_token'],'060010e4')
        self.assertEqual(game['collision_step_computation']['method_il_sha256'],
            'baf76b151851ff8beb15e2733b7063a3c08a0af37350fa05d380866eddad1810')
        literals=[(x['offset'],x['float64']) for x in game['collision_step_instructions']if 'float64'in x]
        self.assertEqual(literals,[(873,2.0),(884,2.0),(910,0.125),(919,0.25)])
        self.assertIn('timestep cap',game['collision_step_semantics'])
        self.assertIn('not evidence',game['collision_step_semantics'])
        self.assertTrue(evidence['publication_evidence_level'].startswith('model-only'))
        self.assertEqual(evidence['minimal_game_fixture_contract']['status'],
            'Not executed; original-game observation artifacts are still required')

    def test_main_grid_asset_consumers_do_not_claim_decoded_asset_paths(self):
        evidence=json.loads((ROOT/'VerifierNumericalEvidence.json').read_text())
        il=evidence['original_game_static_il']
        writes=il['main_grid_asset_write']['instructions']
        self.assertEqual(writes[-1]['token'],'04000411')
        self.assertEqual([entry['integer'] for entry in writes if 'integer' in entry],
            [0x5af75435,0x5af754e1,0x5af754dc,0x5af7548a])
        self.assertEqual(sum(entry.get('token')=='06000aab' for entry in writes),4)
        self.assertEqual([entry['token'] for entry in il['four_texture_size_getter']['references']],
            ['04000867','04000627'])
        self.assertTrue(any(entry.get('token')=='040006bf' for entry in il['main_grid_motion_consumer']))
        self.assertIn('separately closes',il['hex_mapper_scale_provenance'])
        assets=json.loads((ROOT/'MainGridAssetEvidence.json').read_text())
        self.assertEqual(assets['assembly_sha256'],evidence['assembly_sha256'])
        self.assertEqual([entry['argument'] for entry in assets['textures']],
            ['5af75435','5af754e1','5af754dc','5af7548a'])
        self.assertEqual([entry['text'].rsplit('/',1)[1] for entry in assets['textures']],
            ['left','right','bottom','top'])
        self.assertTrue(all((entry['width'],entry['height'])==(82,71) and not entry['psd_override_present']
            for entry in assets['textures']))
        self.assertIn('no assembly load or game execution',assets['method'])

    def test_reference_source_and_mapping_obligations_remain_separate(self):
        import math
        evidence=json.loads((ROOT/'VerifierNumericalEvidence.json').read_text())
        self.assertEqual(evidence['reference_source']['revision'],'a88d12d8d100784aa525ee90259255b869bcba38')
        self.assertEqual(evidence['reference_source']['sha256'],
            'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5')
        example=evidence['mathematical_ledger'][1]['example']
        self.assertAlmostEqual(example['pixel_then_euclidean_rotate_y'],820*math.sin(math.pi/3))
        self.assertGreater(example['absolute_y_difference_pixels'],0.125)
        self.assertGreater(len(evidence['unproved_obligations']),5)
        self.assertIn('max(mid-low,high-mid)',evidence['kernel_review']['subdivision_enclosure'])
        self.assertIn('DAZ/FTZ',evidence['kernel_review']['subdivision_enclosure'])

if __name__=='__main__':unittest.main()
