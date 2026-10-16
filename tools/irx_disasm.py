"""Minimal IRX (IOP MIPS R3000 relocatable ELF) loader + rabbitizer disassembler.

Usage: irx_disasm.py MODULE.IRX [START END]   (hex offsets within .text; default: all)
Prints import/export tables, then the disassembly with labels for branch/jal targets and
import stubs resolved to "module:index" (with names for the SDK modules we know).
Addresses are section-relative (the IRX is linked at 0, so unrelocated lui/addiu pairs are
already correct offsets into the file image).
"""
import os
import struct
import sys

if os.environ.get('BAROQUE_TOOLCHAIN_PY'):  # optional extra module path (e.g. a local Python toolchain with rabbitizer)
    sys.path.insert(0, os.environ['BAROQUE_TOOLCHAIN_PY'])
import rabbitizer  # noqa: E402

KNOWN = {
    'libsd': {2: 'sceSdQuit', 4: 'sceSdInit', 5: 'sceSdSetParam', 6: 'sceSdGetParam', 7: 'sceSdSetSwitch',
              8: 'sceSdGetSwitch', 9: 'sceSdSetAddr', 10: 'sceSdGetAddr', 11: 'sceSdSetCoreAttr',
              12: 'sceSdGetCoreAttr', 13: 'sceSdNote2Pitch', 14: 'sceSdPitch2Note', 15: 'sceSdProcBatch',
              16: 'sceSdProcBatchEx', 17: 'sceSdVoiceTrans', 18: 'sceSdBlockTrans', 19: 'sceSdVoiceTransStatus',
              20: 'sceSdBlockTransStatus', 21: 'sceSdSetTransCallback', 22: 'sceSdSetIRQCallback',
              23: 'sceSdSetEffectAttr', 24: 'sceSdGetEffectAttr', 25: 'sceSdClearEffectWorkArea',
              26: 'sceSdSetTransIntrHandler', 27: 'sceSdSetSpu2IntrHandler', 28: 'sceSdGetTransIntrHandlerArgument',
              29: 'sceSdGetSpu2IntrHandlerArgument', 30: 'sceSdStopTrans', 31: 'sceSdCleanEffectWorkArea',
              32: 'sceSdSetEffectAttr2', 33: 'sceSdSetEffectMode', 34: 'sceSdSetEffectModeParams'},
    'sifcmd': {4: 'sceSifInitCmd', 5: 'sceSifExitCmd', 6: 'sceSifGetSreg', 7: 'sceSifSetSreg', 8: 'sceSifSetCmdBuffer',
               9: 'sceSifSetSysCmdBuffer', 10: 'sceSifAddCmdHandler', 11: 'sceSifRemoveCmdHandler',
               12: 'sceSifSendCmd', 13: 'isceSifSendCmd', 14: 'sceSifInitRpc', 15: 'sceSifBindRpc',
               16: 'sceSifCallRpc', 17: 'sceSifRegisterRpc', 18: 'sceSifCheckStatRpc', 19: 'sceSifSetRpcQueue',
               20: 'sceSifGetNextRequest', 21: 'sceSifExecRequest', 22: 'sceSifRpcLoop', 23: 'sceSifGetOtherData',
               24: 'sceSifRemoveRpc', 25: 'sceSifRemoveRpcQueue'},
    'sifman': {4: 'sceSifInit', 5: 'sceSifSetDChain', 6: 'sceSifSetDma', 7: 'sceSifDmaStat', 8: 'sceSifSend',
               9: 'sceSifSendSync', 10: 'sceSifIsSending', 11: 'sceSifSetSIF0DMA', 12: 'sceSifSendSync0',
               13: 'sceSifIsSending0', 14: 'sceSifSetSIF1DMA', 15: 'sceSifSendSync1', 16: 'sceSifIsSending1',
               17: 'sceSifSetSIF2DMA', 18: 'sceSifSendSync2', 19: 'sceSifIsSending2', 20: 'sceSifGetEEIOPflags',
               21: 'sceSifSetEEIOPflags', 22: 'sceSifGetIOPEEflags', 23: 'sceSifSetIOPEEflags', 24: 'sceSifGetEErcvaddr',
               25: 'sceSifGetIOPrcvaddr', 26: 'sceSifSetIOPrcvaddr', 27: 'sceSifSet1450_2', 28: 'sceSifCheckInit',
               29: 'sceSifSet0CB', 30: 'sceSifReset0CB', 31: 'sceSifSetDmaIntr', 32: 'sceSifDmaStat'},
    'ioman': {4: 'open', 5: 'close', 6: 'read', 7: 'write', 8: 'lseek', 9: 'ioctl', 10: 'remove', 11: 'mkdir',
              12: 'rmdir', 13: 'dopen', 14: 'dclose', 15: 'dread', 16: 'getstat', 17: 'chstat', 18: 'format',
              20: 'AddDrv', 21: 'DelDrv'},
    'sysclib': {4: 'setjmp', 5: 'longjmp', 6: 'toupper', 7: 'tolower', 8: 'look_ctype_table', 9: 'get_ctype_table',
                10: 'memchr', 11: 'memcmp', 12: 'memcpy', 13: 'memmove', 14: 'memset', 15: 'bcmp', 16: 'bcopy',
                17: 'bzero', 18: 'prnt', 19: 'sprintf', 20: 'strcat', 21: 'strchr', 22: 'strcmp', 23: 'strcpy',
                24: 'strcspn', 25: 'index', 26: 'rindex', 27: 'strlen', 28: 'strncat', 29: 'strncmp', 30: 'strncpy',
                31: 'strpbrk', 32: 'strrchr', 33: 'strspn', 34: 'strstr', 35: 'strtok', 36: 'strtol', 37: 'atob',
                38: 'strtoul', 39: 'wmemcopy', 40: 'wmemset', 41: 'vsprintf'},
    'thbase': {4: 'CreateThread', 5: 'DeleteThread', 6: 'StartThread', 7: 'StartThreadArgs', 8: 'ExitThread',
               9: 'ExitDeleteThread', 10: 'TerminateThread', 11: 'iTerminateThread', 12: 'DisableDispatchThread',
               13: 'EnableDispatchThread', 14: 'ChangeThreadPriority', 15: 'iChangeThreadPriority',
               16: 'RotateThreadReadyQueue', 17: 'iRotateThreadReadyQueue', 18: 'ReleaseWaitThread',
               19: 'iReleaseWaitThread', 20: 'GetThreadId', 21: 'CheckThreadStack', 22: 'ReferThreadStatus',
               23: 'iReferThreadStatus', 24: 'SleepThread', 25: 'WakeupThread', 26: 'iWakeupThread',
               27: 'CancelWakeupThread', 28: 'iCancelWakeupThread', 29: 'SuspendThread', 30: 'iSuspendThread',
               31: 'ResumeThread', 32: 'iResumeThread', 33: 'DelayThread', 34: 'GetSystemTime', 35: 'SetAlarm',
               36: 'iSetAlarm', 37: 'CancelAlarm', 38: 'iCancelAlarm', 39: 'USec2SysClock', 40: 'SysClock2USec',
               41: 'GetSystemStatusFlag'},
    'thsemap': {4: 'CreateSema', 5: 'DeleteSema', 6: 'SignalSema', 7: 'iSignalSema', 8: 'WaitSema', 9: 'PollSema',
                10: 'ReferSemaStatus', 11: 'iReferSemaStatus'},
    'intrman': {4: 'RegisterIntrHandler', 5: 'ReleaseIntrHandler', 6: 'EnableIntr', 7: 'DisableIntr',
                8: 'CpuDisableIntr', 9: 'CpuEnableIntr', 10: 'CpuGetICTRL', 11: 'CpuEnableICTRL',
                12: 'CpuSuspendIntr', 13: 'CpuResumeIntr', 14: 'CpuSuspendIntr', 15: 'CpuResumeIntr',
                16: 'CpuDisableIntr', 17: 'CpuSuspendIntr', 18: 'CpuResumeIntr', 19: 'CpuEnableIntr'},
    'timrman': {4: 'AllocHardTimer', 5: 'ReferHardTimer', 6: 'FreeHardTimer', 7: 'SetTimerMode', 8: 'GetTimerStatus',
                9: 'GetTimerCounter', 10: 'SetTimerCounter', 11: 'GetTimerCompare', 12: 'SetTimerCompare',
                13: 'GetHardTimerIntrCode', 14: 'SetTimerCallback', 15: 'SetTimerHandler', 16: 'SetOverflowHandler',
                17: 'SetCompareHandler', 18: 'SetupHardTimer', 19: 'StartHardTimer', 20: 'StopHardTimer'},
    'sysmem': {4: 'AllocSysMemory', 5: 'FreeSysMemory', 6: 'QueryMemSize', 7: 'QueryMaxFreeMemSize',
               8: 'QueryTotalFreeMemSize', 9: 'QueryBlockTopAddress', 10: 'QueryBlockSize', 14: 'Kprintf'},
    'stdio': {4: 'printf', 5: 'getchar', 6: 'putchar', 7: 'puts', 8: 'gets', 9: 'fdprintf', 10: 'fdgetc',
              11: 'fdputc', 12: 'fdputs', 13: 'fdgets'},
    'loadcore': {4: 'FlushIcache', 5: 'FlushDcache', 6: 'RegisterLibraryEntries', 7: 'ReleaseLibraryEntries',
                 8: 'FindImageInfo', 9: 'RegisterModule', 10: 'GetLoadcoreInternalData', 11: 'ReadModuleHeader',
                 12: 'LoadModuleImage', 13: 'LoadModuleImage2'},
}


class Irx:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        b = self.data
        e_shoff = struct.unpack_from('<I', b, 32)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', b, 46)
        shstr = struct.unpack_from('<10I', b, e_shoff + e_shstrndx * 40)
        self.sections = {}
        for i in range(e_shnum):
            s = struct.unpack_from('<10I', b, e_shoff + i * 40)
            name = b[shstr[4] + s[0]:].split(b'\0')[0].decode()
            self.sections[name] = {'addr': s[3], 'off': s[4], 'size': s[5], 'type': s[1]}
        # Build a flat image: sections are placed at their link address (0-based) by the loader.
        top = max(s['addr'] + s['size'] for s in self.sections.values() if s['addr'] or s['size'])
        img = bytearray(top)
        for s in self.sections.values():
            if s['type'] == 1:  # PROGBITS
                img[s['addr']:s['addr'] + s['size']] = b[s['off']:s['off'] + s['size']]
        self.img = bytes(img)
        self.imports = {}  # stub addr -> (module, index)
        self.exports = []
        self._parse_tables()

    def word(self, a):
        return struct.unpack_from('<I', self.img, a)[0]

    def cstring(self, a, n=256):
        return self.img[a:a + n].split(b'\0')[0].decode('latin1')

    def _parse_tables(self):
        t = self.sections['.text']
        a = t['addr']
        end = a + t['size']
        while a + 16 <= end:
            if self.word(a) == 0x41e00000 and self.word(a + 4) == 0:
                mod = self.img[a + 12:a + 20].split(b'\0')[0].decode('latin1')
                p = a + 20
                while self.word(p) != 0 and (self.word(p) >> 26) == 0 and (self.word(p) & 0x3f) == 8:  # jr ra
                    idx = self.word(p + 4) & 0xffff
                    self.imports[p] = (mod, idx)
                    p += 8
                a = p + 8
                continue
            if self.word(a) == 0x41c00000 and self.word(a + 4) == 0:
                mod = self.img[a + 12:a + 20].split(b'\0')[0].decode('latin1')
                p = a + 20
                funcs = []
                while self.word(p) != 0:
                    funcs.append(self.word(p))
                    p += 4
                self.exports.append((mod, funcs))
                a = p + 4
                continue
            a += 4

    def import_name(self, addr):
        mod, idx = self.imports[addr]
        return '%s:%d(%s)' % (mod, idx, KNOWN.get(mod, {}).get(idx, '?'))

    def disasm(self, start, end):
        rabbitizer.config.regNames_gprAbiNames = rabbitizer.Abi.O32
        insns = {}
        targets = set()
        for a in range(start, end, 4):
            w = self.word(a)
            insn = rabbitizer.Instruction(w, a, category=rabbitizer.InstrCategory.R3000GTE)
            insns[a] = insn
            if insn.isJumpWithAddress():
                targets.add(insn.getInstrIndexAsVram())
            elif insn.isBranch():
                targets.add(a + 4 + insn.getBranchOffset())
        out = []
        for a in range(start, end, 4):
            insn = insns[a]
            if a in self.imports:
                out.append('%05x  <import %s>' % (a, self.import_name(a)))
            if a in targets or any(a == f for _, fs in self.exports for f in fs):
                out.append('L_%05x:' % a)
            txt = insn.disassemble()
            comment = ''
            if insn.isJumpWithAddress():
                tgt = insn.getInstrIndexAsVram()
                if tgt in self.imports:
                    comment = '  ; ' + self.import_name(tgt)
                txt = txt.replace('func_%08X' % tgt, 'L_%05x' % tgt).replace('.L%08X' % tgt, 'L_%05x' % tgt)
            elif insn.isBranch():
                tgt = a + 4 + insn.getBranchOffset()
                txt = txt.replace('.L%08X' % tgt, 'L_%05x' % tgt)
            out.append('%05x  %08x  %s%s' % (a, self.word(a), txt, comment))
        return out


def main():
    irx = Irx(sys.argv[1])
    for n, s in irx.sections.items():
        print('%-12s addr %06x size %06x' % (n, s['addr'], s['size']))
    print('imports:')
    for a, (m, i) in sorted(irx.imports.items()):
        print('  %05x %s' % (a, irx.import_name(a)))
    print('exports:', [(m, ['%05x' % f for f in fs]) for m, fs in irx.exports])
    t = irx.sections['.text']
    s, e = t['addr'], t['addr'] + t['size']
    if len(sys.argv) > 3:
        s, e = int(sys.argv[2], 16), int(sys.argv[3], 16)
    print('\n'.join(irx.disasm(s, e)))


if __name__ == '__main__':
    main()
