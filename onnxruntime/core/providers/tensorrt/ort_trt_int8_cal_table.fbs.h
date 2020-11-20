// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_ORTTRTINT8CALTABLE_CALTABLEFLATBUFFERS_H_
#define FLATBUFFERS_GENERATED_ORTTRTINT8CALTABLE_CALTABLEFLATBUFFERS_H_

#include "flatbuffers/flatbuffers.h"

namespace CalTableFlatBuffers {

struct KeyValue;
struct KeyValueBuilder;

struct TrtTable;
struct TrtTableBuilder;

struct KeyValue FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef KeyValueBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_KEY = 4,
    VT_VALUE = 6
  };
  const flatbuffers::String *key() const {
    return GetPointer<const flatbuffers::String *>(VT_KEY);
  }
  bool KeyCompareLessThan(const KeyValue *o) const {
    return *key() < *o->key();
  }
  int KeyCompareWithValue(const char *val) const {
    return strcmp(key()->c_str(), val);
  }
  const flatbuffers::String *value() const {
    return GetPointer<const flatbuffers::String *>(VT_VALUE);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffsetRequired(verifier, VT_KEY) &&
           verifier.VerifyString(key()) &&
           VerifyOffset(verifier, VT_VALUE) &&
           verifier.VerifyString(value()) &&
           verifier.EndTable();
  }
};

struct KeyValueBuilder {
  typedef KeyValue Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_key(flatbuffers::Offset<flatbuffers::String> key) {
    fbb_.AddOffset(KeyValue::VT_KEY, key);
  }
  void add_value(flatbuffers::Offset<flatbuffers::String> value) {
    fbb_.AddOffset(KeyValue::VT_VALUE, value);
  }
  explicit KeyValueBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  KeyValueBuilder &operator=(const KeyValueBuilder &);
  flatbuffers::Offset<KeyValue> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<KeyValue>(end);
    fbb_.Required(o, KeyValue::VT_KEY);
    return o;
  }
};

inline flatbuffers::Offset<KeyValue> CreateKeyValue(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<flatbuffers::String> key = 0,
    flatbuffers::Offset<flatbuffers::String> value = 0) {
  KeyValueBuilder builder_(_fbb);
  builder_.add_value(value);
  builder_.add_key(key);
  return builder_.Finish();
}

inline flatbuffers::Offset<KeyValue> CreateKeyValueDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    const char *key = nullptr,
    const char *value = nullptr) {
  auto key__ = key ? _fbb.CreateString(key) : 0;
  auto value__ = value ? _fbb.CreateString(value) : 0;
  return CalTableFlatBuffers::CreateKeyValue(
      _fbb,
      key__,
      value__);
}

struct TrtTable FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef TrtTableBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_DICT = 4
  };
  const flatbuffers::Vector<flatbuffers::Offset<CalTableFlatBuffers::KeyValue>> *dict() const {
    return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<CalTableFlatBuffers::KeyValue>> *>(VT_DICT);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_DICT) &&
           verifier.VerifyVector(dict()) &&
           verifier.VerifyVectorOfTables(dict()) &&
           verifier.EndTable();
  }
};

struct TrtTableBuilder {
  typedef TrtTable Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_dict(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<CalTableFlatBuffers::KeyValue>>> dict) {
    fbb_.AddOffset(TrtTable::VT_DICT, dict);
  }
  explicit TrtTableBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  TrtTableBuilder &operator=(const TrtTableBuilder &);
  flatbuffers::Offset<TrtTable> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<TrtTable>(end);
    return o;
  }
};

inline flatbuffers::Offset<TrtTable> CreateTrtTable(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<CalTableFlatBuffers::KeyValue>>> dict = 0) {
  TrtTableBuilder builder_(_fbb);
  builder_.add_dict(dict);
  return builder_.Finish();
}

inline flatbuffers::Offset<TrtTable> CreateTrtTableDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    std::vector<flatbuffers::Offset<CalTableFlatBuffers::KeyValue>> *dict = nullptr) {
  auto dict__ = dict ? _fbb.CreateVectorOfSortedTables<CalTableFlatBuffers::KeyValue>(dict) : 0;
  return CalTableFlatBuffers::CreateTrtTable(
      _fbb,
      dict__);
}

}  // namespace CalTableFlatBuffers

#endif  // FLATBUFFERS_GENERATED_ORTTRTINT8CALTABLE_CALTABLEFLATBUFFERS_H_
