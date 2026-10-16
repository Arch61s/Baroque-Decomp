#pragma once
// Intrinsic Alchemy .IGB reader for Baroque (PS2, SLUS_217.14).
// Layout: igIGBFile::readFile FUN_002fb270 (work/decompiled/002fb270.c) and helpers; see
// work/reports/IGB_FORMAT.md. Mirrors tools/igb.py: every section end is asserted, every field is
// decoded from the class layouts recorded in the file (type/ordinal/size); field *names* come from the
// ELF registration tables (igb_elf_fields.cpp), exactly like tools/igb.py.
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baroque::alchemy {

constexpr uint32_t kIgbMagic = 0xFADA;  // _magicCookie default (imm at 0x2c6274)

struct MetaField {
    std::string type;      // metafield class name (section 4 list), e.g. "igIntMetaField"
    uint16_t ordinal = 0;  // runtime field ordinal; igObject owns 0..1 (FUN_003184f0)
    uint16_t size = 0;     // serialized byte size
    std::string name;      // ELF name, or "ord<N>_<type>" when the registry has no entry
    std::string elfType;   // metafield type recorded in the ELF (cross-check only)
};

struct MetaObject {
    int index = -1;
    std::string name;
    uint32_t version = 0;          // word 1 (always 1)
    uint32_t w2 = 0;               // word 2 (always 0)
    uint32_t fieldTotal = 0;       // persistent fields incl. inherited
    int parentIndex = -1;
    const MetaObject* parent = nullptr;
    uint32_t firstFreeOrdinal = 0; // total runtime field count of the class
    std::vector<MetaField> fields;
    bool isA(std::string_view cls) const;
};

class IgbFile;

// One decoded persistent field. `raw` always holds the serialized bytes; the accessors decode them
// little-endian (FUN_003164d0 byte copy). Strings/refs are s32 indices (FUN_003271b0 / FUN_0031ffd0).
struct Value {
    const MetaField* field = nullptr;
    std::vector<uint8_t> raw;
    int stringIndex = -1;          // igStringMetaField: string-table index, -1 = null
    int refSlot = -1;              // single reference: directory slot, -1 = null
    std::vector<int> refSlots;     // reference arrays (one s32 per element)

    bool isString() const;
    bool isRef() const;
    bool isRefArray() const;
    int32_t i32() const;
    uint32_t u32() const;
    int64_t i64() const;
    float f32() const;
    bool b() const;
    // Float vectors (igVec2f/igVec3f/igVec4f/igMatrix44f...): number of floats and copy.
    size_t floatCount() const { return raw.size() / 4; }
    void floats(float* out, size_t n) const;
};

struct IgObject {
    const IgbFile* file = nullptr;
    const MetaObject* meta = nullptr;
    int slot = -1;
    std::vector<Value> values;     // meta->fields order

    std::string_view cls() const { return meta->name; }
    bool isA(std::string_view c) const { return meta->isA(c); }
    const Value* find(std::string_view name) const;
    int32_t getInt(std::string_view name, int32_t def = 0) const;
    uint32_t getUInt(std::string_view name, uint32_t def = 0) const;
    int64_t getLong(std::string_view name, int64_t def = 0) const;
    float getFloat(std::string_view name, float def = 0.0f) const;
    bool getBool(std::string_view name, bool def = false) const;
    std::string_view getString(std::string_view name) const;  // empty when absent/null
    int getRef(std::string_view name) const;                    // slot or -1
    bool getFloats(std::string_view name, float* out, size_t n) const;
};

// Raw memory block (vertex streams, images, CLUTs, list storage).
struct IgMemory {
    int slot = -1;
    uint32_t size = 0;
    std::string elemType;        // metafield type of the element (_memTypeIndex), may be empty
    std::string alignmentType;   // section 5 name or empty
    bool refCounted = false;
    size_t fileOffset = 0;
    std::vector<uint8_t> data;
    std::vector<int> refSlots;   // filled when elemType is a reference type: s32 slots (-1 = null)
    bool isRefArray() const { return !refSlots.empty() || (size == 0 && isRefType()); }
    bool isRefType() const;
};

// Directory slot resolved from another .IGB at load time (never consumes object/memory buffers).
struct IgExternal {
    int slot = -1;
    std::string entryClass;      // igExternalInfoEntry / igExternalDirEntry / igExternalIndexedEntry
    std::string key;             // igExternalInfoEntry._key, e.g. "parts_00.tga 0 1 1 1 1 0 0 0"
    std::string infoName;        // igExternalInfoEntry._infoName, e.g. "M00_TEX.IGB"
    int directoryIndex = -1;     // index into IgbFile::externalNames
    std::string directoryName;   // igExternalDirEntry
    std::string entryName;
    int entryIndex = -1;
    std::string keyName() const; // first whitespace-separated token of key (texture image name)
};

enum class SlotKind { Object, Memory, External };

struct Slot {
    SlotKind kind = SlotKind::Object;
    IgObject* object = nullptr;
    IgMemory* memory = nullptr;
    IgExternal* external = nullptr;
};

struct IgbFlags {
    bool hasInfo = false, hasExternal = false, sharedEntries = false, hasMemoryPoolNames = false;
    bool hasVersion1Cruft = false;
};

class IgbFile {
public:
    IgbFile() = default;
    IgbFile(const IgbFile&) = delete;
    IgbFile& operator=(const IgbFile&) = delete;
    IgbFile(IgbFile&&) = default;
    IgbFile& operator=(IgbFile&&) = default;

    // Parses `path`; on failure returns false and fills `error`.
    bool load(const std::filesystem::path& path, std::string* error);
    bool parse(std::vector<uint8_t> bytes, std::string* error);

    const std::filesystem::path& path() const { return path_; }
    uint32_t version() const { return version_; }
    const IgbFlags& flags() const { return flags_; }
    const std::vector<std::string>& strings() const { return strings_; }
    std::string_view string(int index) const;  // "" for -1 / null
    const std::vector<std::string>& metafieldTypes() const { return metafieldTypes_; }
    const std::vector<std::string>& alignmentNames() const { return alignmentNames_; }
    const std::vector<std::string>& externalNames() const { return externalNames_; }
    const std::vector<std::string>& memoryPoolNames() const { return memoryPoolNames_; }
    const std::vector<std::unique_ptr<MetaObject>>& metaobjects() const { return metaobjects_; }
    const std::vector<std::unique_ptr<IgObject>>& entries() const { return entries_; }
    const std::vector<uint16_t>& index() const { return index_; }
    const std::vector<Slot>& slots() const { return slots_; }
    const std::vector<IgObject*>& objects() const { return objects_; }
    const std::vector<IgMemory*>& memories() const { return memories_; }
    const std::vector<IgExternal*>& externals() const { return externals_; }
    int infoListPlace() const { return infoListPlace_; }
    const IgObject* infoList() const { return object(infoListPlace_); }
    size_t trailingBytes() const { return trailing_; }

    const IgObject* object(int slot) const;
    const IgMemory* memory(int slot) const;
    const IgExternal* external(int slot) const;
    const Slot* slot(int index) const;

    // Objects whose class is or derives from `cls`, in slot order.
    std::vector<const IgObject*> find(std::string_view cls) const;
    const MetaObject* metaobject(std::string_view name) const;

    // igObjectList/igDataList helpers: the `_data` memory block's reference slots (empty list ok).
    std::vector<int> listSlots(const IgObject* list) const;
    std::vector<const IgObject*> listObjects(const IgObject* list) const;

    // Per-class object counts (instantiated objects only), like tools/igb.py summary().
    std::map<std::string, int> classCounts() const;

private:
    struct Reader;
    void readRecord(Reader& r, IgObject& obj, bool checkMeta);
    void nameFields();

    std::filesystem::path path_;
    uint32_t version_ = 0;
    IgbFlags flags_;
    uint32_t entryBufSize_ = 0, entryCount_ = 0, metaobjectBufSize_ = 0, metaobjectCount_ = 0;
    uint32_t objectBufSize_ = 0, objectCount_ = 0, memrefBufSize_ = 0, memrefCount_ = 0;
    uint32_t metafieldBufSize_ = 0, metafieldCount_ = 0;
    std::vector<std::string> strings_;
    std::vector<bool> stringNull_;
    std::vector<std::string> metafieldTypes_;
    std::vector<std::string> alignmentNames_;
    std::vector<std::unique_ptr<MetaObject>> metaobjects_;
    std::vector<std::string> externalNames_;
    std::vector<std::string> memoryPoolNames_;
    std::vector<std::unique_ptr<IgObject>> entries_;
    std::vector<uint16_t> index_;
    int infoListPlace_ = -1;
    std::vector<Slot> slots_;
    std::vector<std::unique_ptr<IgObject>> objectStore_;
    std::vector<std::unique_ptr<IgMemory>> memoryStore_;
    std::vector<std::unique_ptr<IgExternal>> externalStore_;
    std::vector<IgObject*> objects_;
    std::vector<IgMemory*> memories_;
    std::vector<IgExternal*> externals_;
    size_t trailing_ = 0;
};

bool isRefMetaType(std::string_view type);

}  // namespace baroque::alchemy
