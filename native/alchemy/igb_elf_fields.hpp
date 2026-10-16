#pragma once
// Field-name registry recovered from the ELF (see gen_elf_fields.py). The IGB file supplies the layout
// (type/ordinal/size per class); this table only supplies the names, exactly like tools/igb.py.
#include <cstddef>
#include <string_view>
namespace baroque::alchemy::elf {
struct FieldDef {
    const char* name;
    const char* type;    // metafield class name, e.g. "igIntMetaField"
    int offset;          // runtime struct offset (documentation only)
};
struct ClassDef {
    const char* name;
    const char* parent;  // nullptr for igObject
    int size;            // runtime object size
    const FieldDef* fields;
    int fieldCount;
};
const ClassDef* findClass(std::string_view name);
std::size_t classCount();
}
