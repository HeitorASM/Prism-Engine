#pragma once

// ============================================================================
// Resource.h
// O formato GENERICO de recurso da Prism (o equivalente ao .tres/.res da Godot):
// um arquivo guarda UM recurso de um TIPO nomeado ("ParticleEmitter", e no futuro
// qualquer outro) como uma arvore de valores nomeados. O formato NAO conhece
// nenhum tipo: quem sabe converter um tipo para/de ResourceValue e o codigo daquele
// tipo (ex.: Resources/ParticleResource.h). Adicionar um tipo novo = escrever so
// esse par de funcoes; o arquivo, o leitor e o editor de arquivos nao mudam.
//
// Dois formatos de arquivo para a MESMA arvore (extensoes DIFERENTES: ".prismtres" = texto,
// ".prismres" = binario; ao LER, o formato e identificado pelo conteudo, entao um arquivo
// renomeado continua abrindo):
//   - TEXTO  (padrao): legivel, com diff e merge bons no controle de versao.
//   - BINARIO: compacto e rapido, para recursos grandes ou builds finais.
// Converter entre eles nao perde nada (os testes comparam as duas ida-e-volta).
//
// ----------------------------------------------------------------------------
// FORMATO TEXTO
//
//     # comentario (de '#' ate o fim da linha)
//     prism_resource type="ParticleEmitter" version=1
//
//     Drag = 0.5
//     Shape = "Sphere"
//     ShapeBox = [1.0, 2.0, 3.0]
//     SizeOverLife = [[0.0, 1.0], [0.5, 2.0], [1.0, 0.0]]
//     ColorOverLife = Gradient {
//         Stops = [[0.0, 1.0, 1.0, 1.0, 1.0], [1.0, 1.0, 0.5, 0.0, 0.0]]
//     }
//     Texture = @asset(8f3a1c5d2b7e9041, "Textures/smoke.png")
//
// Valores: null  true  false  inteiro  decimal (inf, -inf, nan)  "texto"
//          [ lista ]  { chave = valor ... } (mapa)  Tipo { ... } (objeto tipado)
//          @asset(<id 16 hex>[, "dica de caminho"]) (referencia a outro asset).
// Virgulas entre itens sao opcionais; chaves sao identificadores
// ([A-Za-z_][A-Za-z0-9_.-]*) ou "texto entre aspas". Um numero com '.' ou expoente
// e decimal; sem eles, inteiro (64 bits). O escritor sempre grava decimais com '.'
// (ou expoente), entao um decimal nunca volta como inteiro.
//
// REFERENCIA A OUTRO ASSET: a identidade e o AssetID (estavel ao mover/renomear,
// ver Assets/AssetID.h); o caminho entre aspas e so uma DICA para humanos e para
// recuperar a referencia se o .meta se perder. Quem le deve resolver pelo ID.
//
// COMPATIBILIDADE: o leitor ignora atributos de cabecalho desconhecidos, e quem
// converte um tipo deve ignorar chaves desconhecidas e usar o padrao para chaves
// ausentes (uma versao nova pode acrescentar campos sem quebrar a anterior).
//
// ----------------------------------------------------------------------------
// FORMATO BINARIO (little-endian)
//   "PRES"  u32 versao-do-contêiner(=1)  string Tipo  u32 VersaoDoTipo  valor-raiz
//   valor = tag(u8) + carga:
//     0 null | 1 false | 2 true | 3 inteiro (varint zigzag) | 4 decimal (8 bytes)
//     5 texto (varint tamanho + bytes UTF-8) | 6 lista (varint n + n valores)
//     7 mapa (varint n + n x [texto chave, valor]) | 8 objeto (texto tipo + como mapa)
//     9 referencia (8 bytes AssetID + texto dica)
// O leitor valida tudo (limites de tamanho/profundidade): um arquivo truncado ou
// malicioso devolve erro, nunca estoura memoria nem pilha.
//
// So std (e AssetID.h): compila e e testado sozinho (tests/ResourceTests.cpp).
// ============================================================================

#include "../Assets/AssetID.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Prism {

    class ResourceValue {
    public:
        enum class Kind : uint8_t { Null, Bool, Int, Float, String, Array, Map, Object, AssetRef };

        // --- construtores nomeados ---
        static ResourceValue MakeNull() { return ResourceValue(); }
        static ResourceValue MakeBool(bool v) { ResourceValue r; r.m_Kind = Kind::Bool; r.m_Int = v ? 1 : 0; return r; }
        static ResourceValue MakeInt(int64_t v) { ResourceValue r; r.m_Kind = Kind::Int; r.m_Int = v; return r; }
        static ResourceValue MakeFloat(double v) { ResourceValue r; r.m_Kind = Kind::Float; r.m_Float = v; return r; }
        static ResourceValue MakeString(std::string v) { ResourceValue r; r.m_Kind = Kind::String; r.m_Text = std::move(v); return r; }
        static ResourceValue MakeArray() { ResourceValue r; r.m_Kind = Kind::Array; return r; }
        static ResourceValue MakeMap() { ResourceValue r; r.m_Kind = Kind::Map; return r; }
        static ResourceValue MakeObject(std::string typeName) { ResourceValue r; r.m_Kind = Kind::Object; r.m_Text = std::move(typeName); return r; }
        static ResourceValue MakeAssetRef(AssetID id, std::string pathHint = {}) { ResourceValue r; r.m_Kind = Kind::AssetRef; r.m_Id = id; r.m_Text = std::move(pathHint); return r; }

        // Decimal vindo de um 'float' da engine: guarda o MENOR texto decimal que volta ao mesmo
        // float (0.1f vira 0.1, e nao 0.10000000149011612). Ler e converter para float devolve
        // exatamente o valor original.
        static ResourceValue MakeFloat32(float v);

        // Lista de decimais (vetores, cores, curvas planas...), cada um como MakeFloat32.
        static ResourceValue MakeFloatArray(const float* values, size_t count) {
            ResourceValue r = MakeArray();
            r.m_Items.reserve(count);
            for (size_t i = 0; i < count; i++)
                r.m_Items.push_back(MakeFloat32(values[i]));
            return r;
        }

        Kind GetKind() const { return m_Kind; }
        bool IsNull() const { return m_Kind == Kind::Null; }
        bool IsNumber() const { return m_Kind == Kind::Int || m_Kind == Kind::Float; }
        bool IsContainer() const { return m_Kind == Kind::Array || m_Kind == Kind::Map || m_Kind == Kind::Object; }
        bool HasFields() const { return m_Kind == Kind::Map || m_Kind == Kind::Object; }

        // --- leitura tolerante: devolve 'fallback' se o tipo nao serve (int serve para decimal) ---
        bool AsBool(bool fallback = false) const { return m_Kind == Kind::Bool ? m_Int != 0 : fallback; }
        int64_t AsInt(int64_t fallback = 0) const;     // decimal exato e inteiro tambem serve
        double AsDouble(double fallback = 0.0) const { return m_Kind == Kind::Float ? m_Float : (m_Kind == Kind::Int ? (double)m_Int : fallback); }
        const std::string& AsString() const { return m_Text; } // vazio se nao for texto/objeto/referencia
        const std::string& TypeName() const { return m_Text; } // Object: nome do tipo
        const std::string& PathHint() const { return m_Text; } // AssetRef: dica de caminho
        AssetID AsAssetID() const { return m_Kind == Kind::AssetRef ? m_Id : AssetID{}; }

        // --- lista ---
        const std::vector<ResourceValue>& Items() const { return m_Items; }
        ResourceValue& Add(ResourceValue v) { m_Items.push_back(std::move(v)); return m_Items.back(); }

        // --- mapa/objeto (ordem de insercao preservada) ---
        const std::vector<std::pair<std::string, ResourceValue>>& Fields() const { return m_Fields; }
        const ResourceValue* Find(std::string_view key) const;
        ResourceValue* Find(std::string_view key);
        // Cria ou SUBSTITUI a chave (mantem a posicao original ao substituir).
        ResourceValue& Set(std::string key, ResourceValue v);
        // Adiciona sem checar duplicata (uso interno do leitor binario).
        ResourceValue& AppendField(std::string key, ResourceValue v) { m_Fields.emplace_back(std::move(key), std::move(v)); return m_Fields.back().second; }

        // --- atalhos de leitura em mapas: devolvem false (sem tocar em 'out') se a chave nao existe/nao serve ---
        bool GetBool(std::string_view key, bool& out) const;
        bool GetInt(std::string_view key, int64_t& out) const;
        bool GetFloat(std::string_view key, float& out) const;
        bool GetString(std::string_view key, std::string& out) const;
        bool GetAssetID(std::string_view key, AssetID& out) const;
        // Lista de ate 'count' decimais; false se nao for lista de EXATAMENTE 'count' numeros.
        bool GetFloatArray(std::string_view key, float* out, size_t count) const;

        // Comparacao estrutural (decimais comparados bit a bit, entao nan == nan; so para testes e sincronizacao).
        bool operator==(const ResourceValue& other) const;
        bool operator!=(const ResourceValue& other) const { return !(*this == other); }

    private:
        Kind m_Kind = Kind::Null;
        int64_t m_Int = 0;        // Bool e Int
        double m_Float = 0.0;
        std::string m_Text;       // String, nome do tipo do Object, dica do AssetRef
        AssetID m_Id;             // AssetRef
        std::vector<ResourceValue> m_Items;                              // Array
        std::vector<std::pair<std::string, ResourceValue>> m_Fields;     // Map e Object
    };

    // Um arquivo de recurso: tipo + versao DO TIPO (cada tipo versiona seus proprios campos)
    // + um mapa de propriedades.
    struct ResourceDocument {
        std::string Type;            // "ParticleEmitter", ...
        uint32_t Version = 1;        // versao do esquema do TIPO (nao do formato do arquivo)
        ResourceValue Root = ResourceValue::MakeMap();

        bool operator==(const ResourceDocument& o) const { return Type == o.Type && Version == o.Version && Root == o.Root; }
    };

    enum class ResourceFormat { Text, Binary };

    namespace ResourceIO {

        constexpr int kMaxDepth = 64;                 // aninhamento maximo aceito na leitura
        constexpr uint32_t kContainerVersion = 1;     // versao do formato BINARIO do contêiner

        std::string WriteText(const ResourceDocument& doc);
        std::string WriteBinary(const ResourceDocument& doc);

        // Em erro devolvem false, preenchem 'error' (se nao nulo, com "linha N: ..." no texto
        // e "posicao N: ..." no binario) e deixam 'out' INTACTO.
        bool ParseText(std::string_view text, ResourceDocument& out, std::string* error = nullptr);
        bool ParseBinary(std::string_view bytes, ResourceDocument& out, std::string* error = nullptr);

        // true se o conteudo comeca com a assinatura binaria "PRES".
        bool LooksBinary(std::string_view bytes);

        // Le o arquivo e identifica o formato pelo conteudo. Tolera BOM UTF-8 e CRLF no texto.
        bool Load(const std::filesystem::path& file, ResourceDocument& out, std::string* error = nullptr);

        // Grava de forma ATOMICA (arquivo temporario ao lado + rename): uma queda no meio
        // nunca deixa o recurso pela metade. Cria a pasta de destino se preciso.
        bool Save(const std::filesystem::path& file, const ResourceDocument& doc, ResourceFormat format = ResourceFormat::Text, std::string* error = nullptr);

        // Extensoes: ".prismres" = BINARIO, ".prismtres" = TEXTO (o "t" de text, como .res/.tres da Godot).
        inline const char* BinaryExtension() { return ".prismres"; }
        inline const char* TextExtension() { return ".prismtres"; }
        inline const char* ExtensionFor(ResourceFormat format) { return format == ResourceFormat::Binary ? BinaryExtension() : TextExtension(); }
        // true se a extensao e de recurso (qualquer caixa) e devolve o formato que ela indica.
        bool FormatFromPath(const std::filesystem::path& file, ResourceFormat& out);
        inline bool IsResourcePath(const std::filesystem::path& file) { ResourceFormat f; return FormatFromPath(file, f); }
        // Grava no formato indicado pela extensao do arquivo (.prismres binario, .prismtres texto; outra = texto).
        bool SaveByExtension(const std::filesystem::path& file, const ResourceDocument& doc, std::string* error = nullptr);
    }

}
