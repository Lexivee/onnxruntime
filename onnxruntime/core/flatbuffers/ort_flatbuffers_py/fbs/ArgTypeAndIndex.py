# automatically generated by the FlatBuffers compiler, do not modify

# namespace: fbs

import flatbuffers
from flatbuffers.compat import import_numpy
np = import_numpy()

class ArgTypeAndIndex(object):
    __slots__ = ['_tab']

    @classmethod
    def GetRootAsArgTypeAndIndex(cls, buf, offset):
        n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, offset)
        x = ArgTypeAndIndex()
        x.Init(buf, n + offset)
        return x

    @classmethod
    def ArgTypeAndIndexBufferHasIdentifier(cls, buf, offset, size_prefixed=False):
        return flatbuffers.util.BufferHasIdentifier(buf, offset, b"\x4F\x52\x54\x4D", size_prefixed=size_prefixed)

    # ArgTypeAndIndex
    def Init(self, buf, pos):
        self._tab = flatbuffers.table.Table(buf, pos)

    # ArgTypeAndIndex
    def ArgType(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(4))
        if o != 0:
            return self._tab.Get(flatbuffers.number_types.Int8Flags, o + self._tab.Pos)
        return 0

    # ArgTypeAndIndex
    def Index(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            return self._tab.Get(flatbuffers.number_types.Uint32Flags, o + self._tab.Pos)
        return 0

def ArgTypeAndIndexStart(builder): builder.StartObject(2)
def ArgTypeAndIndexAddArgType(builder, argType): builder.PrependInt8Slot(0, argType, 0)
def ArgTypeAndIndexAddIndex(builder, index): builder.PrependUint32Slot(1, index, 0)
def ArgTypeAndIndexEnd(builder): return builder.EndObject()
