#include "Resource.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

// Valor, formato binario e entrada/saida de arquivo. O formato texto fica em ResourceText.cpp.

namespace Prism {

    // ------------------------------------------------------------------------
    // ResourceValue
    // ------------------------------------------------------------------------
    int64_t ResourceValue::AsInt(int64_t fallback) const {
        if (m_Kind == Kind::Int)
            return m_Int;
        if (m_Kind == Kind::Float && std::isfinite(m_Float) && m_Float == std::floor(m_Float)
            && m_Float >= -9.2e18 && m_Float <= 9.2e18)
            return (int64_t)m_Float;
        return fallback;
    }

    ResourceValue ResourceValue::MakeFloat32(float v) {
        if (!std::isfinite(v))
            return MakeFloat((double)v);
        char buffer[64];
        auto written = std::to_chars(buffer, buffer + sizeof(buffer), v); // menor texto que volta ao mesmo float
        double d = (double)v;
        std::from_chars(buffer, written.ptr, d);                          // o double mais proximo desse texto
        return MakeFloat(d);
    }

    const ResourceValue* ResourceValue::Find(std::string_view key) const {
        if (!HasFields())
            return nullptr;
        for (const auto& field : m_Fields)
            if (field.first == key)
                return &field.second;
        return nullptr;
    }

    ResourceValue* ResourceValue::Find(std::string_view key) {
        return const_cast<ResourceValue*>(static_cast<const ResourceValue*>(this)->Find(key));
    }

    ResourceValue& ResourceValue::Set(std::string key, ResourceValue v) {
        if (ResourceValue* existing = Find(key)) {
            *existing = std::move(v);
            return *existing;
        }
        m_Fields.emplace_back(std::move(key), std::move(v));
        return m_Fields.back().second;
    }

    bool ResourceValue::GetBool(std::string_view key, bool& out) const {
        const ResourceValue* v = Find(key);
        if (!v || v->m_Kind != Kind::Bool)
            return false;
        out = v->m_Int != 0;
        return true;
    }

    bool ResourceValue::GetInt(std::string_view key, int64_t& out) const {
        const ResourceValue* v = Find(key);
        if (!v || !v->IsNumber())
            return false;
        if (v->m_Kind == Kind::Float && v->AsInt(INT64_MIN) == INT64_MIN)
            return false; // decimal nao inteiro (ou nao finito) nao vira inteiro em silencio
        out = v->AsInt();
        return true;
    }

    bool ResourceValue::GetFloat(std::string_view key, float& out) const {
        const ResourceValue* v = Find(key);
        if (!v || !v->IsNumber())
            return false;
        out = (float)v->AsDouble();
        return true;
    }

    bool ResourceValue::GetString(std::string_view key, std::string& out) const {
        const ResourceValue* v = Find(key);
        if (!v || v->m_Kind != Kind::String)
            return false;
        out = v->m_Text;
        return true;
    }

    bool ResourceValue::GetAssetID(std::string_view key, AssetID& out) const {
        const ResourceValue* v = Find(key);
        if (!v || v->m_Kind != Kind::AssetRef)
            return false;
        out = v->m_Id;
        return true;
    }

    bool ResourceValue::GetFloatArray(std::string_view key, float* out, size_t count) const {
        const ResourceValue* v = Find(key);
        if (!v || v->m_Kind != Kind::Array || v->m_Items.size() != count)
            return false;
        for (const auto& item : v->m_Items)
            if (!item.IsNumber())
                return false;
        for (size_t i = 0; i < count; i++)
            out[i] = (float)v->m_Items[i].AsDouble();
        return true;
    }

    bool ResourceValue::operator==(const ResourceValue& o) const {
        if (m_Kind != o.m_Kind)
            return false;
        switch (m_Kind) {
        case Kind::Null: return true;
        case Kind::Bool:
        case Kind::Int: return m_Int == o.m_Int;
        case Kind::Float: return std::memcmp(&m_Float, &o.m_Float, sizeof(double)) == 0;
        case Kind::String: return m_Text == o.m_Text;
        case Kind::AssetRef: return m_Id == o.m_Id && m_Text == o.m_Text;
        case Kind::Array: return m_Items == o.m_Items;
        case Kind::Object:
            if (m_Text != o.m_Text)
                return false;
            [[fallthrough]];
        case Kind::Map:
            return m_Fields == o.m_Fields;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Binario
    // ------------------------------------------------------------------------
    namespace {

        enum Tag : uint8_t {
            kTagNull = 0, kTagFalse = 1, kTagTrue = 2, kTagInt = 3, kTagFloat = 4,
            kTagString = 5, kTagArray = 6, kTagMap = 7, kTagObject = 8, kTagAssetRef = 9
        };

        constexpr char kMagic[4] = { 'P', 'R', 'E', 'S' };

        void PutVarint(std::string& out, uint64_t v) {
            while (v >= 0x80) {
                out.push_back((char)((v & 0x7F) | 0x80));
                v >>= 7;
            }
            out.push_back((char)v);
        }

        void PutU32(std::string& out, uint32_t v) {
            for (int i = 0; i < 4; i++)
                out.push_back((char)((v >> (8 * i)) & 0xFF));
        }

        void PutU64(std::string& out, uint64_t v) {
            for (int i = 0; i < 8; i++)
                out.push_back((char)((v >> (8 * i)) & 0xFF));
        }

        void PutString(std::string& out, const std::string& s) {
            PutVarint(out, s.size());
            out.append(s);
        }

        void WriteBinaryValue(std::string& out, const ResourceValue& v) {
            switch (v.GetKind()) {
            case ResourceValue::Kind::Null: out.push_back((char)kTagNull); break;
            case ResourceValue::Kind::Bool: out.push_back((char)(v.AsBool() ? kTagTrue : kTagFalse)); break;
            case ResourceValue::Kind::Int: {
                out.push_back((char)kTagInt);
                const int64_t n = v.AsInt();
                PutVarint(out, ((uint64_t)n << 1) ^ (uint64_t)(n >> 63)); // zigzag
                break;
            }
            case ResourceValue::Kind::Float: {
                out.push_back((char)kTagFloat);
                const double d = v.AsDouble();
                uint64_t bits;
                std::memcpy(&bits, &d, sizeof(bits));
                PutU64(out, bits);
                break;
            }
            case ResourceValue::Kind::String:
                out.push_back((char)kTagString);
                PutString(out, v.AsString());
                break;
            case ResourceValue::Kind::Array:
                out.push_back((char)kTagArray);
                PutVarint(out, v.Items().size());
                for (const auto& item : v.Items())
                    WriteBinaryValue(out, item);
                break;
            case ResourceValue::Kind::Map:
            case ResourceValue::Kind::Object:
                if (v.GetKind() == ResourceValue::Kind::Object) {
                    out.push_back((char)kTagObject);
                    PutString(out, v.TypeName());
                }
                else {
                    out.push_back((char)kTagMap);
                }
                PutVarint(out, v.Fields().size());
                for (const auto& field : v.Fields()) {
                    PutString(out, field.first);
                    WriteBinaryValue(out, field.second);
                }
                break;
            case ResourceValue::Kind::AssetRef:
                out.push_back((char)kTagAssetRef);
                PutU64(out, v.AsAssetID().Value());
                PutString(out, v.PathHint());
                break;
            }
        }

        // Leitor com limites: nunca le alem do fim e nunca reserva memoria proporcional a um
        // numero que o arquivo apenas AFIRMA (so ao que ainda cabe nos bytes restantes).
        struct BinaryReader {
            const unsigned char* Data;
            size_t Size;
            size_t Pos = 0;
            std::string Error;

            BinaryReader(std::string_view bytes) : Data((const unsigned char*)bytes.data()), Size(bytes.size()) {}

            size_t Remaining() const { return Size - Pos; }

            bool Fail(const std::string& message) {
                if (Error.empty())
                    Error = "posicao " + std::to_string(Pos) + ": " + message;
                return false;
            }

            bool U8(uint8_t& v) {
                if (Remaining() < 1) return Fail("fim inesperado do arquivo");
                v = Data[Pos++];
                return true;
            }

            bool U32(uint32_t& v) {
                if (Remaining() < 4) return Fail("fim inesperado do arquivo");
                v = 0;
                for (int i = 0; i < 4; i++) v |= (uint32_t)Data[Pos + i] << (8 * i);
                Pos += 4;
                return true;
            }

            bool U64(uint64_t& v) {
                if (Remaining() < 8) return Fail("fim inesperado do arquivo");
                v = 0;
                for (int i = 0; i < 8; i++) v |= (uint64_t)Data[Pos + i] << (8 * i);
                Pos += 8;
                return true;
            }

            bool Varint(uint64_t& v) {
                v = 0;
                for (int shift = 0; shift < 70; shift += 7) {
                    uint8_t b;
                    if (!U8(b)) return false;
                    if (shift == 63 && (b & 0x7E)) return Fail("numero grande demais");
                    v |= (uint64_t)(b & 0x7F) << shift;
                    if (!(b & 0x80)) return true;
                }
                return Fail("numero grande demais");
            }

            bool String(std::string& s) {
                uint64_t length;
                if (!Varint(length)) return false;
                if (length > Remaining()) return Fail("texto maior que o arquivo");
                s.assign((const char*)Data + Pos, (size_t)length);
                Pos += (size_t)length;
                return true;
            }
        };

        bool ReadBinaryValue(BinaryReader& r, ResourceValue& out, int depth) {
            if (depth > ResourceIO::kMaxDepth)
                return r.Fail("aninhamento profundo demais");
            uint8_t tag;
            if (!r.U8(tag))
                return false;
            switch (tag) {
            case kTagNull: out = ResourceValue::MakeNull(); return true;
            case kTagFalse: out = ResourceValue::MakeBool(false); return true;
            case kTagTrue: out = ResourceValue::MakeBool(true); return true;
            case kTagInt: {
                uint64_t z;
                if (!r.Varint(z)) return false;
                out = ResourceValue::MakeInt((int64_t)(z >> 1) ^ -(int64_t)(z & 1));
                return true;
            }
            case kTagFloat: {
                uint64_t bits;
                if (!r.U64(bits)) return false;
                double d;
                std::memcpy(&d, &bits, sizeof(d));
                out = ResourceValue::MakeFloat(d);
                return true;
            }
            case kTagString: {
                std::string s;
                if (!r.String(s)) return false;
                out = ResourceValue::MakeString(std::move(s));
                return true;
            }
            case kTagArray: {
                uint64_t count;
                if (!r.Varint(count)) return false;
                if (count > r.Remaining()) return r.Fail("lista maior que o arquivo"); // cada item ocupa >= 1 byte
                out = ResourceValue::MakeArray();
                for (uint64_t i = 0; i < count; i++) {
                    ResourceValue item;
                    if (!ReadBinaryValue(r, item, depth + 1)) return false;
                    out.Add(std::move(item));
                }
                return true;
            }
            case kTagMap:
            case kTagObject: {
                std::string typeName;
                if (tag == kTagObject && !r.String(typeName)) return false;
                uint64_t count;
                if (!r.Varint(count)) return false;
                if (count > r.Remaining() / 2) return r.Fail("mapa maior que o arquivo"); // chave + valor >= 2 bytes
                out = tag == kTagObject ? ResourceValue::MakeObject(std::move(typeName)) : ResourceValue::MakeMap();
                for (uint64_t i = 0; i < count; i++) {
                    std::string key;
                    ResourceValue value;
                    if (!r.String(key) || !ReadBinaryValue(r, value, depth + 1)) return false;
                    out.AppendField(std::move(key), std::move(value));
                }
                return true;
            }
            case kTagAssetRef: {
                uint64_t id;
                std::string hint;
                if (!r.U64(id) || !r.String(hint)) return false;
                if (id == 0) return r.Fail("referencia de asset com ID 0");
                out = ResourceValue::MakeAssetRef(AssetID(id), std::move(hint));
                return true;
            }
            default:
                return r.Fail("tipo de valor desconhecido (" + std::to_string((int)tag) + ")");
            }
        }

    }

    namespace ResourceIO {

        std::string WriteBinary(const ResourceDocument& doc) {
            std::string out;
            out.append(kMagic, sizeof(kMagic));
            PutU32(out, kContainerVersion);
            PutString(out, doc.Type);
            PutU32(out, doc.Version);
            WriteBinaryValue(out, doc.Root);
            return out;
        }

        bool LooksBinary(std::string_view bytes) {
            return bytes.size() >= sizeof(kMagic) && std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) == 0;
        }

        bool ParseBinary(std::string_view bytes, ResourceDocument& out, std::string* error) {
            auto fail = [&](const std::string& message) {
                if (error) *error = message;
                return false;
            };
            if (!LooksBinary(bytes))
                return fail("nao e um recurso binario da Prism (assinatura PRES ausente)");

            BinaryReader r(bytes);
            r.Pos = sizeof(kMagic);
            uint32_t container = 0;
            ResourceDocument doc;
            if (!r.U32(container))
                return fail(r.Error);
            if (container == 0 || container > kContainerVersion)
                return fail("versao do formato binario nao suportada (" + std::to_string(container) + "); atualize a engine");
            if (!r.String(doc.Type) || !r.U32(doc.Version) || !ReadBinaryValue(r, doc.Root, 0))
                return fail(r.Error);
            if (doc.Root.GetKind() != ResourceValue::Kind::Map)
                return fail("a raiz do recurso precisa ser um mapa");
            if (r.Remaining() != 0)
                return fail("posicao " + std::to_string(r.Pos) + ": bytes sobrando depois do recurso");
            out = std::move(doc);
            return true;
        }

        // ParseText/WriteText estao em ResourceText.cpp.

        bool Load(const std::filesystem::path& file, ResourceDocument& out, std::string* error) {
            std::ifstream in(file, std::ios::binary);
            if (!in.is_open()) {
                if (error) *error = "nao foi possivel abrir " + file.string();
                return false;
            }
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (in.bad()) {
                if (error) *error = "erro ao ler " + file.string();
                return false;
            }
            return LooksBinary(bytes) ? ParseBinary(bytes, out, error) : ParseText(bytes, out, error);
        }

        bool Save(const std::filesystem::path& file, const ResourceDocument& doc, ResourceFormat format, std::string* error) {
            auto fail = [&](const std::string& message) {
                if (error) *error = message;
                return false;
            };
            std::error_code ec;
            if (file.has_parent_path())
                std::filesystem::create_directories(file.parent_path(), ec);

            const std::string bytes = format == ResourceFormat::Binary ? WriteBinary(doc) : WriteText(doc);

            std::filesystem::path temp = file;
            temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                    return fail("nao foi possivel criar " + temp.string());
                out.write(bytes.data(), (std::streamsize)bytes.size());
                out.flush();
                if (!out.good()) {
                    out.close();
                    std::filesystem::remove(temp, ec);
                    return fail("erro ao gravar " + temp.string());
                }
            }
            std::filesystem::rename(temp, file, ec);
            if (ec) {
                std::error_code ignored;
                std::filesystem::remove(temp, ignored);
                return fail("nao foi possivel substituir " + file.string() + ": " + ec.message());
            }
            return true;
        }

    }


    namespace ResourceIO {
        bool FormatFromPath(const std::filesystem::path& file, ResourceFormat& out) {
            std::string ext = file.extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == BinaryExtension()) { out = ResourceFormat::Binary; return true; }
            if (ext == TextExtension()) { out = ResourceFormat::Text; return true; }
            return false;
        }
        bool SaveByExtension(const std::filesystem::path& file, const ResourceDocument& doc, std::string* error) {
            ResourceFormat format = ResourceFormat::Text;
            FormatFromPath(file, format);
            return Save(file, doc, format, error);
        }
    }

}
