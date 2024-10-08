# automatically generated by the FlatBuffers compiler, do not modify

# namespace: fbs

import flatbuffers
from flatbuffers.compat import import_numpy
np = import_numpy()

# a single runtime optimization
# see corresponding type in onnxruntime/core/graph/runtime_optimization_record.h
class RuntimeOptimizationRecord(object):
    __slots__ = ['_tab']

    @classmethod
    def GetRootAs(cls, buf, offset=0):
        n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, offset)
        x = RuntimeOptimizationRecord()
        x.Init(buf, n + offset)
        return x

    @classmethod
    def GetRootAsRuntimeOptimizationRecord(cls, buf, offset=0):
        """This method is deprecated. Please switch to GetRootAs."""
        return cls.GetRootAs(buf, offset)
    @classmethod
    def RuntimeOptimizationRecordBufferHasIdentifier(cls, buf, offset, size_prefixed=False):
        return flatbuffers.util.BufferHasIdentifier(buf, offset, b"\x4F\x52\x54\x4D", size_prefixed=size_prefixed)

    # RuntimeOptimizationRecord
    def Init(self, buf, pos):
        self._tab = flatbuffers.table.Table(buf, pos)

    # RuntimeOptimizationRecord
    def ActionId(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(4))
        if o != 0:
            return self._tab.String(o + self._tab.Pos)
        return None

    # RuntimeOptimizationRecord
    def NodesToOptimizeIndices(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            x = self._tab.Indirect(o + self._tab.Pos)
            from ort_flatbuffers_py.fbs.NodesToOptimizeIndices import NodesToOptimizeIndices
            obj = NodesToOptimizeIndices()
            obj.Init(self._tab.Bytes, x)
            return obj
        return None

    # RuntimeOptimizationRecord
    def ProducedOpIds(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(10))
        if o != 0:
            a = self._tab.Vector(o)
            return self._tab.String(a + flatbuffers.number_types.UOffsetTFlags.py_type(j * 4))
        return ""

    # RuntimeOptimizationRecord
    def ProducedOpIdsLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(10))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # RuntimeOptimizationRecord
    def ProducedOpIdsIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(10))
        return o == 0

def RuntimeOptimizationRecordStart(builder):
    builder.StartObject(4)

def Start(builder):
    RuntimeOptimizationRecordStart(builder)

def RuntimeOptimizationRecordAddActionId(builder, actionId):
    builder.PrependUOffsetTRelativeSlot(0, flatbuffers.number_types.UOffsetTFlags.py_type(actionId), 0)

def AddActionId(builder, actionId):
    RuntimeOptimizationRecordAddActionId(builder, actionId)

def RuntimeOptimizationRecordAddNodesToOptimizeIndices(builder, nodesToOptimizeIndices):
    builder.PrependUOffsetTRelativeSlot(1, flatbuffers.number_types.UOffsetTFlags.py_type(nodesToOptimizeIndices), 0)

def AddNodesToOptimizeIndices(builder, nodesToOptimizeIndices):
    RuntimeOptimizationRecordAddNodesToOptimizeIndices(builder, nodesToOptimizeIndices)

def RuntimeOptimizationRecordAddProducedOpIds(builder, producedOpIds):
    builder.PrependUOffsetTRelativeSlot(3, flatbuffers.number_types.UOffsetTFlags.py_type(producedOpIds), 0)

def AddProducedOpIds(builder, producedOpIds):
    RuntimeOptimizationRecordAddProducedOpIds(builder, producedOpIds)

def RuntimeOptimizationRecordStartProducedOpIdsVector(builder, numElems):
    return builder.StartVector(4, numElems, 4)

def StartProducedOpIdsVector(builder, numElems):
    return RuntimeOptimizationRecordStartProducedOpIdsVector(builder, numElems)

def RuntimeOptimizationRecordEnd(builder):
    return builder.EndObject()

def End(builder):
    return RuntimeOptimizationRecordEnd(builder)
