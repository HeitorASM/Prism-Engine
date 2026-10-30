#include <Prism.h>
#include <PrismVersion.h>
#include <Prism/Core/EntryPoint.h> // define main() - incluir apenas aqui, uma vez

#include "RuntimeLayer.h"

namespace Prism {

    class PrismRuntimeApplication : public Application {
    public:
        explicit PrismRuntimeApplication(const ApplicationSpecification& spec)
            : Application(spec) {
            m_Layer = new PrismRuntime::RuntimeLayer();
            PushLayer(m_Layer);
        }

        ~PrismRuntimeApplication() override {
            // Este destrutor roda ANTES do da base (que desliga Lua/audio/
            // renderer), entao e aqui que os scripts do jogo ainda podem parar
            // com seguranca. A Layer em si e destruida depois, pela LayerStack.
            m_Layer->StopGame();
        }

    private:
        PrismRuntime::RuntimeLayer* m_Layer = nullptr;
    };

    Application* CreateApplication() {
        ApplicationSpecification spec;
        spec.Name = std::string(Version::Name) + " Runtime " + Version::Number;
        spec.WindowWidth = 1280;
        spec.WindowHeight = 720;
        return new PrismRuntimeApplication(spec);
    }

}
