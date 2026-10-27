#include <PrismEditor/Core/BuildOutputAnalysis.h>
#include <cstdio>

using namespace PrismEditor::BuildAnalysis;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {

    CHECK(IsFileLockError("LINK : fatal error LNK1104: cannot open file 'C:\\\\Proj\\\\Binaries\\\\NativeScripts.dll'"));
    CHECK(IsFileLockError("LINK : fatal error LNK1168: cannot open C:\\\\Proj\\\\Binaries\\\\NativeScripts.dll for writing"));
    CHECK(IsFileLockError("NativeScripts.vcxproj : fatal error LNK1201: error writing to program database 'x.pdb'; check for insufficient disk space, invalid path, or insufficient privilege"));
    CHECK(IsFileLockError("fatal error C1041: cannot open program database 'vc143.pdb'; if multiple CL.EXE write to the same .PDB file, please use /FS"));
    CHECK(IsFileLockError("/usr/bin/ld: cannot open output file libNativeScripts.so: Text file busy"));
    CHECK(IsFileLockError("The process cannot access the file because it is being used by another process."));
    CHECK(IsFileLockError("Permission denied"));
    CHECK(IsFileLockError("LINK : erro fatal LNK1104: arquivo 'x.dll' ... Acesso negado"));
    CHECK(IsFileLockError("lnk1104: cannot open file")); // maiusculas/minusculas


    CHECK(!IsFileLockError(""));
    CHECK(!IsFileLockError("[1/3] Building CXX object Spinner.cpp.o"));
    CHECK(!IsFileLockError("Spinner.cpp(12): error C2065: 'x': undeclared identifier"));
    CHECK(!IsFileLockError("Spinner.cpp:12:5: error: 'foo' was not declared in this scope"));
    CHECK(!IsFileLockError("-- Build files have been written to: C:/Proj/Cache/NativeBuild"));
    CHECK(!IsFileLockError("cannot open file 'readme'")); // sem extensao de saida nem "permission denied"


    CHECK(IsUnresolvedSymbolError("Spinner.obj : error LNK2019: unresolved external symbol \"public: void __cdecl Prism::Scene::Foo(void)\""));
    CHECK(IsUnresolvedSymbolError("Spinner.cpp:(.text+0x12): undefined reference to `Prism::Scene::Foo()'"));
    CHECK(IsUnresolvedSymbolError("error LNK2001: unresolved external symbol"));
    CHECK(!IsUnresolvedSymbolError("error C2065: undeclared identifier"));
    CHECK(!IsUnresolvedSymbolError("LNK1104: cannot open file"));

    // classificacao do conjunto
    CHECK(Classify({}) == Failure::None);
    CHECK(Classify({ "Spinner.cpp(1): error C2065: x" }) == Failure::None); // erro de codigo: sem dica
    CHECK(Classify({ "building", "LINK : fatal error LNK1104: cannot open file 'a.dll'" }) == Failure::FileLocked);
    CHECK(Classify({ "error LNK2019: unresolved external symbol foo" }) == Failure::UnresolvedSymbol);
    CHECK(Classify({ "LNK1104: cannot open file 'a.dll'", "error LNK2019: unresolved external symbol foo" }) == Failure::UnresolvedSymbol);

    // dicas
    CHECK(Hint(Failure::None).empty());
    CHECK(!Hint(Failure::FileLocked).empty() && !Hint(Failure::UnresolvedSymbol).empty());
    CHECK(Hint(Failure::FileLocked) != Hint(Failure::UnresolvedSymbol));

    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
