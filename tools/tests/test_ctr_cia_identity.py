#!/usr/bin/env python3
"""Reject mixed or legacy CIA IDs across ticket, TMD, NCCH and exheaders."""
from pathlib import Path
import importlib.util,struct
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('packages',ROOT/'tools/verify_3ds_packages.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
b=bytearray(0x2000)
struct.pack_into('<IHHIII',b,0,0x40,0,0,0,0x300,0x300)
struct.pack_into('>I',b,0x40,0x10004);struct.pack_into('>I',b,0x340,0x10004)
expected=0x000400000D3D5000
fields=[('>Q',0x40+0x140+0x9c),('>Q',0x340+0x140+0x4c)]+[('<Q',0x640+i) for i in [0x108,0x118,0x400,0x800]]
for fmt,p in fields:struct.pack_into(fmt,b,p,expected)
m.verify_cia_identity(b,expected)
for fmt,p in fields:
 c=bytearray(b);struct.pack_into(fmt,c,p,0x0004000003F39500)
 try:m.verify_cia_identity(c,expected)
 except ValueError:pass
 else:raise AssertionError('Accepted a mixed/legacy identity')
for fmt,p in fields:struct.pack_into(fmt,b,p,0x0004000003F39500)
try:m.verify_cia_identity(b,0x0004000003F39500)
except ValueError:pass
else:raise AssertionError('Allowed the old DevilutionX identity')
print('PASS: all six CIA identity fields agree; legacy and mixed packages are rejected')
