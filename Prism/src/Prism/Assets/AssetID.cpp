#include "AssetID.h"

#include <random>
#include <mutex>

namespace Prism {

    static AssetID::RandomSource s_TestSource = nullptr;

    void AssetID::SetRandomSourceForTesting(RandomSource source) {
        s_TestSource = source;
    }

    AssetID AssetID::Generate() {
        // Um unico gerador por processo, protegido por mutex: std::mt19937_64
        // nao e thread-safe, e o AssetRegistry pode ser chamado de uma thread
        // de importacao no futuro. std::random_device so semeia (pode ser
        // lento/bloqueante em algumas plataformas - por isso nao e usado
        // diretamente a cada chamada).
        static std::mutex s_Mutex;
        static std::mt19937_64 s_Engine{ std::random_device{}() ^ ((uint64_t)std::random_device{}() << 32) };

        std::lock_guard<std::mutex> lock(s_Mutex);

        // Laco: 0 e reservado ("sem asset"), entao um sorteio 0 e descartado
        // e refeito. O laco NAO e paranoia - e ele que garante a regra
        // mesmo se a fonte devolver 0 (ver SetRandomSourceForTesting).
        for (;;) {
            uint64_t v = s_TestSource ? s_TestSource() : s_Engine();
            if (v != 0)
                return AssetID(v);
        }
    }

    std::string AssetID::ToString() const {
        static const char* kHex = "0123456789abcdef";
        std::string s(16, '0');
        uint64_t v = m_Value;
        for (int i = 15; i >= 0; i--) {
            s[i] = kHex[v & 0xF];
            v >>= 4;
        }
        return s;
    }

    bool AssetID::TryParse(const std::string& text, AssetID& out) {
        if (text.size() != 16)
            return false;

        uint64_t v = 0;
        for (char c : text) {
            uint64_t digit;
            if (c >= '0' && c <= '9')      digit = (uint64_t)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (uint64_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = (uint64_t)(c - 'A' + 10);
            else return false;
            v = (v << 4) | digit;
        }

        if (v == 0)
            return false;

        out = AssetID(v);
        return true;
    }

}
