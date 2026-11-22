"""Offline binding to the exact experimental C effect chain, fixed 44.1 kHz."""
import ctypes as ct
import os
from host import ROOT,build

class Chain:
    _library=None
    @classmethod
    def library(cls):
        if cls._library is None:
            # Independent CLI processes must not rebuild a library already loaded
            # by another process (locked on Windows, unsafe to replace on Linux).
            suffix='.dll' if os.name=='nt' else '.so'
            output=ROOT/('build/chain-host-'+str(os.getpid())+suffix)
            cls._library=ct.CDLL(str(build(output,ROOT/'tests/chain_host.c')))
        return cls._library
    def __init__(self):
        # This diagnostic harness has one static instance; new objects reset it.
        self.lib=self.library()
        self.lib.chain_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_float]
        self.lib.chain_enable.argtypes=[ct.c_uint,ct.c_uint]
        self.lib.chain_bypass_policy.argtypes=[ct.c_uint,ct.c_uint]
        self.lib.chain_process.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        self.lib.chain_load_ir.argtypes=[ct.POINTER(ct.c_float),ct.c_uint]
        self.lib.chain_configure.argtypes=[ct.POINTER(ct.c_uint),ct.c_uint]
        self.lib.chain_slot_ir.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        self.lib.chain_init()
    def configure(self,types):
        values=(ct.c_uint*len(types))(*types)
        if not self.lib.chain_configure(values,len(values)):raise ValueError('C engine rejected cold route')
    def load_ir(self,coefficients,slot=3):
        values=(ct.c_float*len(coefficients))(*coefficients)
        if not self.lib.chain_slot_ir(slot,values,len(values)):raise ValueError('C engine rejected IR')
    def bypass_policy(self,slot,policy):
        if not self.lib.chain_bypass_policy(slot,policy):raise ValueError('C engine rejected cold bypass policy')
    def process(self,samples):
        x=(ct.c_float*len(samples))(*samples);y=(ct.c_float*len(samples))()
        self.lib.chain_process(x,y,len(samples));return list(y)
