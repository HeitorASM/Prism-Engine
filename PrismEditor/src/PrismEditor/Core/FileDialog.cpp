#include "FileDialog.h"

#include <Prism/Core/Log.h>
#include <nfd.h>

#include <system_error>

namespace PrismEditor::FileDialog {

    std::string ToUtf8(const std::filesystem::path& path) {
        std::u8string text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    std::filesystem::path FromUtf8(const std::string& text) {
        std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
        return std::filesystem::path(u8);
    }

    namespace {

        // Mantem NFD inicializado so durante a chamada (init/quit sao baratos,
        // e assim nao ha estado global para lembrar de desfazer no shutdown).
        class NfdSession {
        public:
            NfdSession() : m_Ok(NFD_Init() == NFD_OKAY) {
                if (!m_Ok)
                    PRISM_ERROR("Nao foi possivel abrir o dialogo de arquivos: ", NFD_GetError());
            }
            ~NfdSession() {
                if (m_Ok)
                    NFD_Quit();
            }
            bool Ok() const { return m_Ok; }
        private:
            bool m_Ok;
        };

        // Texto de 'startIn' como o NFD espera (nullptr = sem pasta inicial).
        // Devolve true e preenche 'out' so se o caminho existir.
        bool StartPath(const std::filesystem::path& startIn, std::string& out) {
            if (startIn.empty())
                return false;
            std::error_code ec;
            if (!std::filesystem::exists(startIn, ec))
                return false;
            std::filesystem::path preferred = startIn;
            preferred.make_preferred();
            out = ToUtf8(preferred);
            return true;
        }

    }

    std::filesystem::path PickFolder(const std::filesystem::path& startIn) {
        NfdSession session;
        if (!session.Ok())
            return {};

        std::string start;
        bool hasStart = StartPath(startIn, start);

        nfdu8char_t* picked = nullptr;
        nfdresult_t result = NFD_PickFolderU8(&picked, hasStart ? start.c_str() : nullptr);
        if (result == NFD_OKAY) {
            std::filesystem::path path = FromUtf8(picked);
            NFD_FreePathU8(picked);
            return path;
        }
        if (result == NFD_ERROR)
            PRISM_ERROR("Erro no dialogo de pasta: ", NFD_GetError());
        return {}; // NFD_CANCEL
    }

    std::filesystem::path OpenFile(const char* filterName, const char* filterSpec,
                                   const std::filesystem::path& startIn) {
        NfdSession session;
        if (!session.Ok())
            return {};

        std::string start;
        bool hasStart = StartPath(startIn, start);

        nfdu8filteritem_t filters[1] = { { filterName, filterSpec } };
        nfdu8char_t* picked = nullptr;
        nfdresult_t result = NFD_OpenDialogU8(&picked, filters, 1, hasStart ? start.c_str() : nullptr);
        if (result == NFD_OKAY) {
            std::filesystem::path path = FromUtf8(picked);
            NFD_FreePathU8(picked);
            return path;
        }
        if (result == NFD_ERROR)
            PRISM_ERROR("Erro no dialogo de arquivo: ", NFD_GetError());
        return {}; // NFD_CANCEL
    }

}
