// ============================================================================
// UITests.cpp
// Testes do nucleo da UI em texto (.prismui): parser (UIMarkup), layout
// (UILayout) e hover/clique (UIInput). Sem framework externo (mesmo CHECK dos
// outros testes) e sem OpenGL/GLFW/EnTT/GLM: so std.
// ============================================================================
#include "Prism/UI/UIDocument.h"
#include "Prism/UI/UIInput.h"
#include "Prism/UI/UILayout.h"
#include "Prism/UI/UIMarkup.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>

using namespace Prism::Ui;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }
#define CHECK_NEAR(a, b) do { g_Checks++; if (!Near((a), (b))) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s == %s  (%f vs %f)\n", __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); } } while (0)

// Fonte de mentira: cada caractere (codepoint) mede fontPx/2 de largura. Com fontPx = 20: 10 px por letra, linha de 24 px.
struct FakeMeasure : Measure {
    float TextWidth(const std::string& text, float fontPx) const override {
        size_t codepoints = 0;
        for (unsigned char c : text)
            if ((c & 0xC0) != 0x80) codepoints++;
        return (float)codepoints * fontPx * 0.5f;
    }
    Size ImageSize(const std::string& path) const override {
        if (path == "coin.png") return { 64, 32 };
        return {};
    }
};

static ParseResult MustParse(const std::string& xml) {
    ParseResult r = ParseDocument(xml);
    if (!r.Ok) {
        std::printf("  parse falhou inesperadamente:\n");
        for (const auto& d : r.Diagnostics) std::printf("    %d:%d %s\n", d.Line, d.Column, d.Message.c_str());
    }
    return r;
}

static bool HasMessage(const ParseResult& r, const char* fragment) {
    for (const auto& d : r.Diagnostics)
        if (d.Message.find(fragment) != std::string::npos)
            return true;
    return false;
}

static const Node& ById(const ParseResult& r, const char* id) { return r.Doc.Nodes[(size_t)r.Doc.Find(id)]; }

// Layout em 1000x500 com a fonte de mentira.
static ParseResult Laid(const std::string& xml, float w = 1000, float h = 500) {
    ParseResult r = MustParse(xml);
    FakeMeasure measure;
    LayoutDocument(r.Doc, w, h, measure);
    return r;
}

// ------------------------------------------------------------------ helpers
static void TestHelpers() {
    std::puts("[Helpers: cor, tamanho, margens, numeros]");
    Color c;
    CHECK(ParseColor("#fff", c) && c == Color(1, 1, 1, 1));
    CHECK(ParseColor("#F008", c) && Near(c.R, 1) && Near(c.G, 0) && Near(c.A, 136.0f / 255.0f));
    CHECK(ParseColor("#ff000080", c) && Near(c.R, 1) && Near(c.A, 128.0f / 255.0f));
    CHECK(ParseColor("#00ff00", c) && Near(c.G, 1) && Near(c.A, 1));
    CHECK(ParseColor("Transparent", c) && Near(c.A, 0));
    CHECK(ParseColor("white", c) && c == Color(1, 1, 1, 1));
    CHECK(!ParseColor("#12", c));
    CHECK(!ParseColor("#12345", c));
    CHECK(!ParseColor("#gg0000", c));
    CHECK(!ParseColor("azulzinho", c));
    CHECK(!ParseColor("", c));

    Length l;
    CHECK(ParseLength("auto", l) && l.K == Length::Kind::Auto);
    CHECK(ParseLength("120", l) && l.K == Length::Kind::Px && Near(l.V, 120));
    CHECK(ParseLength("120px", l) && l.K == Length::Kind::Px);
    CHECK(ParseLength("50%", l) && l.K == Length::Kind::Percent && Near(l.V, 50));
    CHECK(ParseLength("fill", l) && l.K == Length::Kind::Fill && Near(l.V, 1));
    CHECK(ParseLength("2fr", l) && l.K == Length::Kind::Fill && Near(l.V, 2));
    CHECK(ParseLength("1.5", l) && Near(l.V, 1.5f));
    CHECK(!ParseLength("", l));
    CHECK(!ParseLength("-5", l));
    CHECK(!ParseLength("0fr", l));
    CHECK(!ParseLength("10em", l));
    CHECK(!ParseLength("abc", l));

    Insets i;
    CHECK(ParseInsets("8", i) && Near(i.Left, 8) && Near(i.Top, 8) && Near(i.Right, 8) && Near(i.Bottom, 8));
    CHECK(ParseInsets("4 10", i) && Near(i.Top, 4) && Near(i.Bottom, 4) && Near(i.Left, 10) && Near(i.Right, 10));
    CHECK(ParseInsets("1 2 3 4", i) && Near(i.Top, 1) && Near(i.Right, 2) && Near(i.Bottom, 3) && Near(i.Left, 4)); // CSS
    CHECK(ParseInsets("1, 2, 3, 4", i) && Near(i.Left, 4));
    CHECK(ParseInsets("8px 16px", i) && Near(i.Left, 16));
    CHECK(!ParseInsets("1 2 3", i));
    CHECK(!ParseInsets("1 2 3 4 5", i));
    CHECK(!ParseInsets("", i));
    CHECK(!ParseInsets("-1", i));
    CHECK(!ParseInsets("a b", i));

    float f = 0;
    CHECK(ParseFloatStrict("1.5", f) && Near(f, 1.5f));
    CHECK(ParseFloatStrict("  -2 ", f) && Near(f, -2));
    CHECK(ParseFloatStrict("1e2", f) && Near(f, 100));
    CHECK(!ParseFloatStrict("1,5", f));      // virgula decimal nao e aceita (e nao depende do locale)
    CHECK(!ParseFloatStrict("1.5x", f));
    CHECK(!ParseFloatStrict(".", f));

    // O parser nao pode depender do locale do processo.
    const char* old = std::setlocale(LC_ALL, nullptr);
    std::string saved = old ? old : "C";
    std::setlocale(LC_ALL, "pt_BR.UTF-8"); // pode nao existir na maquina: tudo bem
    CHECK(ParseFloatStrict("2.25", f) && Near(f, 2.25f));
    ParseResult r = MustParse("<UI><Text id='t' size='12.5'>x</Text></UI>");
    CHECK(Near(ById(r, "t").FontSize, 12.5f));
    std::setlocale(LC_ALL, saved.c_str());
}

// ------------------------------------------------------------------ parser
static void TestParserBasics() {
    std::puts("[Parser: estrutura]");
    ParseResult r = MustParse(R"(<?xml version="1.0" encoding="utf-8"?>
<!-- comentario -->
<UI scale="height" reference="1280x720">
  <Panel id="hud" anchor="top-right" margin="16" width="220" layout="column" gap="8" padding="12" color="#00000099">
    <Text id="score" size="24" color="#FFCC00">Moedas: 0</Text>
    <Button id="pause" onClick="OnPause" width="160">Pausar</Button>
    <Image id="icone" src="Assets/Textures/coin.png" width="32" height="32"/>
    <Spacer height="fill"/>
  </Panel>
  <Column id="col"><Text>a</Text></Column>
  <Row id="row"/>
</UI>)");
    CHECK(r.Ok);
    CHECK(r.Diagnostics.empty());
    CHECK(r.Doc.Scale == ScaleMode::Height);
    CHECK_NEAR(r.Doc.ReferenceWidth, 1280);
    CHECK_NEAR(r.Doc.ReferenceHeight, 720);
    CHECK(r.Doc.Nodes.size() == 8);
    CHECK(r.Doc.Roots.size() == 3);

    const Node& hud = ById(r, "hud");
    CHECK(hud.Type == NodeType::Panel && hud.Layout == LayoutMode::Column);
    CHECK(hud.Anchoring == Anchor::TopRight);
    CHECK(hud.Width.K == Length::Kind::Px && Near(hud.Width.V, 220));
    CHECK_NEAR(hud.Gap, 8);
    CHECK_NEAR(hud.Padding.Left, 12);
    CHECK_NEAR(hud.Margin.Top, 16);
    CHECK_NEAR(hud.Tint.A, 0x99 / 255.0f);
    CHECK(hud.BlocksPointer);                       // Panel com cor barra o mouse
    CHECK(hud.Children.size() == 4);
    CHECK(hud.Parent == -1);

    const Node& score = ById(r, "score");
    CHECK(score.Type == NodeType::Text && score.Text == "Moedas: 0");
    CHECK(!score.BlocksPointer);
    CHECK(score.Parent == r.Doc.Find("hud"));
    CHECK_NEAR(score.FontSize, 24);

    const Node& pause = ById(r, "pause");
    CHECK(pause.Type == NodeType::Button && pause.OnClick == "OnPause" && pause.Text == "Pausar");
    CHECK(pause.BlocksPointer);
    CHECK_NEAR(pause.Padding.Left, 16);             // padding padrao do botao
    CHECK(pause.Align_ == TextAlign::Center);

    CHECK(ById(r, "icone").Src == "Assets/Textures/coin.png");
    CHECK(ById(r, "col").Layout == LayoutMode::Column);
    CHECK(ById(r, "row").Layout == LayoutMode::Row);

    // Ordem de documento = ordem de desenho: pai antes dos filhos.
    for (size_t i = 0; i < r.Doc.Nodes.size(); i++)
        CHECK(r.Doc.Nodes[i].Parent < (int)i);

    // Panel transparente (contêiner) nao barra o mouse.
    ParseResult t = MustParse("<UI><Panel id='p' width='fill' height='fill'/><Panel id='q' blocksPointer='true'/><Panel id='s' src='a.png'/></UI>");
    CHECK(!ById(t, "p").BlocksPointer);
    CHECK(ById(t, "q").BlocksPointer);
    CHECK(ById(t, "s").BlocksPointer);

    // anchor="stretch" vira fill nos dois eixos (sem sobrescrever o que foi dito).
    ParseResult st = MustParse("<UI><Panel id='a' anchor='stretch'/><Panel id='b' anchor='stretch' width='100'/></UI>");
    CHECK(ById(st, "a").Width.K == Length::Kind::Fill && ById(st, "a").Height.K == Length::Kind::Fill);
    CHECK(ById(st, "b").Width.K == Length::Kind::Px && ById(st, "b").Height.K == Length::Kind::Fill);

    // Variacoes de escrita do anchor.
    ParseResult an = MustParse("<UI><Panel id='a' anchor='Bottom_Right'/><Panel id='b' anchor='topLeft'/><Panel id='c' anchor='bottom-left'/></UI>");
    CHECK(ById(an, "a").Anchoring == Anchor::BottomRight);
    CHECK(ById(an, "b").Anchoring == Anchor::TopLeft);
    CHECK(ById(an, "c").Anchoring == Anchor::BottomLeft);
    CHECK(an.Diagnostics.empty());
}

static void TestParserText() {
    std::puts("[Parser: texto e entidades]");
    ParseResult r = MustParse(
        "<UI>\n"
        "  <Text id='a'>\n     Ola,\n     mundo   !  \n  </Text>\n"
        "  <Text id='b'>1 &lt; 2 &amp;&amp; 3 &gt; 2 &quot;ok&quot; &apos;x&apos;</Text>\n"
        "  <Text id='c'>linha1&#10;linha2</Text>\n"
        "  <Text id='d'>&#x41;&#233;&#8364;</Text>\n"
        "  <Text id='e' text='do atributo'/>\n"
        "  <Text id='f'><![CDATA[<tag> & cru]]></Text>\n"
        "  <Text id='g'>antes<!-- tira -->depois</Text>\n"
        "  <Button id='h' text='Rotulo'/>\n"
        "  <Text id='i'>Acao: coracao \xC3\xA7\xC3\xA3o</Text>\n"
        "</UI>");
    CHECK(r.Ok && r.Diagnostics.empty());
    CHECK(ById(r, "a").Text == "Ola, mundo !");     // espacos/quebras colapsados e aparados
    CHECK(ById(r, "b").Text == "1 < 2 && 3 > 2 \"ok\" 'x'");
    CHECK(ById(r, "c").Text == "linha1\nlinha2");   // &#10; vira quebra de linha de verdade
    CHECK(ById(r, "d").Text == "A\xC3\xA9\xE2\x82\xAC");
    CHECK(ById(r, "e").Text == "do atributo");
    CHECK(ById(r, "f").Text == "<tag> & cru");
    CHECK(ById(r, "g").Text == "antesdepois");
    CHECK(ById(r, "h").Text == "Rotulo");
    CHECK(ById(r, "i").Text == "Acao: coracao \xC3\xA7\xC3\xA3o");

    // BOM no inicio e aspas simples.
    ParseResult bom = MustParse(std::string("\xEF\xBB\xBF") + "<UI><Text id='x'>ok</Text></UI>");
    CHECK(bom.Ok);
}

static void TestParserErrors() {
    std::puts("[Parser: erros de XML]");
    struct Case { const char* Xml; int Line; const char* Fragment; };
    const Case cases[] = {
        { "<UI>\n  <Panel>\n</UI>", 3, "esperava </Panel>" },                        // fechou o pai antes do filho
        { "<UI><Text>oi</Panel></UI>", 1, "esperava </Text>" },
        { "<UI>\n<Panel id=hud/></UI>", 2, "entre aspas" },
        { "<UI><Panel id='a/>", 1, "aspas do atributo nunca fechadas" },
        { "<UI><Panel id='a/></UI>", 1, "'<'" },
        { "<UI><Panel id='a' id='b'/></UI>", 1, "repetido" },
        { "<UI><Text>a & b</Text></UI>", 1, "&amp;" },
        { "<UI><Text>&foo;</Text></UI>", 1, "entidade desconhecida" },
        { "<UI><Text>&#0;</Text></UI>", 1, "entidade numerica invalida" },
        { "<UI><Text>&#xZZ;</Text></UI>", 1, "invalida" },
        { "<UI></UI><UI></UI>", 1, "depois do elemento raiz" },
        { "", 1, "vazio" },
        { "   \n  ", 2, "vazio" },
        { "texto solto", 1, "esperava '<'" },
        { "<UI>", 1, "nunca foi fechado" },
        { "<UI><Panel>", 1, "nunca foi fechado" },
        { "<UI><!-- sem fim </UI>", 1, "comentario" },
        { "<UI><Text><![CDATA[x</Text></UI>", 1, "CDATA" },
        { "<UI><Panel width=\"1<2\"/></UI>", 1, "'<'" },
        { "<!DOCTYPE UI><UI/>", 1, "DOCTYPE" },
        { "<Panel/>", 1, "raiz precisa ser <UI>" },
        { "<UI><Panel width/></UI>", 1, "esperava '='" },
        { "<UI><Panel/ ></UI>", 1, "'>'" },
        { "<UI>\n\n   <3d/></UI>", 3, "inesperado" },
    };
    for (const Case& c : cases) {
        ParseResult r = ParseDocument(c.Xml);
        CHECK(!r.Ok);
        CHECK(r.HasErrors());
        CHECK(r.Doc.Nodes.empty());                                  // documento invalido nao entrega meia arvore
        bool matched = false;
        for (const auto& d : r.Diagnostics)
            if (d.Level == Diagnostic::Severity::Error && d.Message.find(c.Fragment) != std::string::npos && d.Line == c.Line)
                matched = true;
        if (!matched) {
            std::printf("  caso sem a mensagem/linha esperada: %s\n", c.Xml);
            for (const auto& d : r.Diagnostics) std::printf("    -> %d:%d %s\n", d.Line, d.Column, d.Message.c_str());
        }
        CHECK(matched);
    }

    // Coluna certa e formato da mensagem.
    ParseResult r = ParseDocument("<UI>\n  <Panel>\n    <Text>oi</Panel>\n</UI>");
    CHECK(!r.Ok && r.Diagnostics.size() == 1);
    CHECK(r.Diagnostics[0].Line == 3 && r.Diagnostics[0].Column == 13);
    CHECK(FormatDiagnostic("hud.prismui", r.Diagnostics[0]).rfind("hud.prismui:3:13: error: ", 0) == 0);

    // Profundidade absurda nao estoura a pilha.
    std::string deep = "<UI>";
    for (int i = 0; i < 200; i++) deep += "<Panel>";
    for (int i = 0; i < 200; i++) deep += "</Panel>";
    deep += "</UI>";
    ParseResult d = ParseDocument(deep);
    CHECK(!d.Ok && HasMessage(d, "fundo demais"));
}

static void TestParserWarnings() {
    std::puts("[Parser: avisos (o documento ainda carrega)]");
    ParseResult r = MustParse(
        "<UI colour='x'>\n"
        "  <Pannel id='a'/>\n"
        "  <Panel id='b' colour='#fff' wdth='10' width='abc' margin='1 2 3' anchor='meio' layout='grid'/>\n"
        "  <Text id='c' src='a.png' onClick='Foo'>t</Text>\n"
        "  <Text id='c'>duplicado</Text>\n"
        "  <Text id='d'><Panel/></Text>\n"
        "  <Panel id='e'>texto solto</Panel>\n"
        "  <Panel id='f g'/>\n"
        "  <Button id='h' onClick='faz algo'>b</Button>\n"
        "  <Panel id='i' visible='talvez' gap='-1'/>\n"
        "</UI>");
    CHECK(r.Ok);                                                      // so avisos
    CHECK(!r.HasErrors());
    CHECK(HasMessage(r, "atributo 'colour' desconhecido em <UI>"));
    CHECK(HasMessage(r, "elemento <Pannel> desconhecido foi ignorado (quis dizer 'Panel'?)"));
    CHECK(HasMessage(r, "atributo 'colour' desconhecido em <Panel> (quis dizer 'color'?)"));
    CHECK(HasMessage(r, "atributo 'wdth' desconhecido em <Panel> (quis dizer 'width'?)"));
    CHECK(HasMessage(r, "width='abc' invalido"));
    CHECK(HasMessage(r, "margin='1 2 3' invalido"));
    CHECK(HasMessage(r, "anchor='meio' invalido"));
    CHECK(HasMessage(r, "layout='grid' invalido"));
    CHECK(HasMessage(r, "o atributo 'src' nao se aplica a <Text>"));
    CHECK(HasMessage(r, "o atributo 'onClick' nao se aplica a <Text>"));
    CHECK(HasMessage(r, "id 'c' repetido"));
    CHECK(HasMessage(r, "<Text> nao pode ter filhos"));
    CHECK(HasMessage(r, "texto solto dentro de <Panel>"));
    CHECK(HasMessage(r, "id='f g' invalido"));
    CHECK(HasMessage(r, "onClick='faz algo' invalido"));
    CHECK(HasMessage(r, "visible='talvez' invalido"));
    CHECK(HasMessage(r, "gap='-1' invalido"));

    // Valores invalidos caem no padrao; o elemento ainda existe.
    CHECK(r.Doc.Find("b") >= 0);
    CHECK(ById(r, "b").Width.K == Length::Kind::Auto);
    CHECK(ById(r, "b").Anchoring == Anchor::TopLeft);
    CHECK(ById(r, "b").Layout == LayoutMode::Stack);
    CHECK(r.Doc.Find("a") < 0);                                       // o <Pannel> foi ignorado
    CHECK(r.Doc.Find("c") >= 0 && ById(r, "c").Text == "t");          // o primeiro 'c' vale
    CHECK(r.Doc.Find("f g") < 0 && r.Doc.Find("f") < 0);
    CHECK(ById(r, "h").OnClick.empty());

    // Posicao do aviso.
    for (const auto& d : r.Diagnostics)
        if (d.Message.find("'wdth'") != std::string::npos)
            CHECK(d.Line == 3 && d.Column > 1);

    // Documento sem elementos: aviso, nao erro.
    ParseResult empty = MustParse("<UI/>");
    CHECK(empty.Ok && HasMessage(empty, "nenhum elemento"));
    ParseResult scale = MustParse("<UI scale='diagonal' reference='grande'/>");
    CHECK(scale.Ok && HasMessage(scale, "scale='diagonal' invalido") && HasMessage(scale, "reference='grande' invalido"));
}

// ------------------------------------------------------------------ layout
static void TestLayoutStack() {
    std::puts("[Layout: stack e ancoras]");
    ParseResult r = Laid(
        "<UI>"
        "<Panel id='tl' width='200' height='100' margin='10'/>"
        "<Panel id='tr' anchor='top-right' width='200' height='100' margin='10 20'/>"
        "<Panel id='br' anchor='bottom-right' width='200' height='100' margin='16'/>"
        "<Panel id='c' anchor='center' width='200' height='100'/>"
        "<Panel id='co' anchor='center' width='200' height='100' margin='0 0 0 40'/>"
        "<Panel id='fill' anchor='stretch' margin='5'/>"
        "<Panel id='pct' width='50%' height='10%'/>"
        "<Panel id='bl' anchor='bottom-left' width='100' height='50'/>"
        "</UI>");
    CHECK_NEAR(ById(r, "tl").Bounds.X, 10);   CHECK_NEAR(ById(r, "tl").Bounds.Y, 10);
    CHECK_NEAR(ById(r, "tl").Bounds.W, 200);  CHECK_NEAR(ById(r, "tl").Bounds.H, 100);
    // margin "10 20" = vertical 10, horizontal 20: a direita fica a 20 do canto.
    CHECK_NEAR(ById(r, "tr").Bounds.X, 1000 - 20 - 200); CHECK_NEAR(ById(r, "tr").Bounds.Y, 10);
    CHECK_NEAR(ById(r, "br").Bounds.X, 1000 - 16 - 200); CHECK_NEAR(ById(r, "br").Bounds.Y, 500 - 16 - 100);
    CHECK_NEAR(ById(r, "c").Bounds.X, 400); CHECK_NEAR(ById(r, "c").Bounds.Y, 200);
    // margin-left 40 desloca o centralizado 20px para a direita (margens se compensam em volta do centro).
    CHECK_NEAR(ById(r, "co").Bounds.X, 400 + 20);
    CHECK_NEAR(ById(r, "fill").Bounds.X, 5); CHECK_NEAR(ById(r, "fill").Bounds.W, 990); CHECK_NEAR(ById(r, "fill").Bounds.H, 490);
    CHECK_NEAR(ById(r, "pct").Bounds.W, 500); CHECK_NEAR(ById(r, "pct").Bounds.H, 50);
    CHECK_NEAR(ById(r, "bl").Bounds.X, 0); CHECK_NEAR(ById(r, "bl").Bounds.Y, 450);
}

static void TestLayoutIntrinsic() {
    std::puts("[Layout: tamanho natural]");
    ParseResult r = Laid(
        "<UI>"
        "<Text id='t' size='20'>Hello</Text>"
        "<Text id='multi' size='20' margin='100 0 0 0'>ab&#10;abcd</Text>"
        "<Button id='b' size='20' margin='300 0 0 0'>OK</Button>"
        "<Image id='img' src='coin.png' margin='400 0 0 0'/>"
        "<Image id='img2' src='nao-existe.png' margin='450 0 0 0'/>"
        "</UI>");
    CHECK_NEAR(ById(r, "t").Bounds.W, 50);  CHECK_NEAR(ById(r, "t").Bounds.H, 24);
    CHECK(ById(r, "t").Lines.size() == 1 && ById(r, "t").Lines[0] == "Hello");
    CHECK_NEAR(ById(r, "multi").Bounds.W, 40); CHECK_NEAR(ById(r, "multi").Bounds.H, 48);
    CHECK(ById(r, "multi").Lines.size() == 2);
    // Botao: texto + padding padrao (16 / 8).
    CHECK_NEAR(ById(r, "b").Bounds.W, 20 + 32); CHECK_NEAR(ById(r, "b").Bounds.H, 24 + 16);
    CHECK_NEAR(ById(r, "img").Bounds.W, 64); CHECK_NEAR(ById(r, "img").Bounds.H, 32);
    CHECK_NEAR(ById(r, "img2").Bounds.W, 0); CHECK_NEAR(ById(r, "img2").Bounds.H, 0);
}

static void TestLayoutColumnRow() {
    std::puts("[Layout: row e column]");
    {   // Panel auto-dimensionado em volta de uma coluna.
        ParseResult r = Laid(
            "<UI><Column id='col' padding='10' gap='8'>"
            "<Text id='a' size='20'>abcd</Text><Text id='b' size='20'>ab</Text></Column></UI>");
        const Node& col = ById(r, "col");
        CHECK_NEAR(col.Bounds.W, 40 + 20);  CHECK_NEAR(col.Bounds.H, 10 + 24 + 8 + 24 + 10);
        CHECK_NEAR(ById(r, "a").Bounds.X, 10); CHECK_NEAR(ById(r, "a").Bounds.Y, 10);
        CHECK_NEAR(ById(r, "b").Bounds.X, 10); CHECK_NEAR(ById(r, "b").Bounds.Y, 10 + 24 + 8);
    }
    {   // Row com 'fill' em pesos 1 e 2.
        ParseResult r = Laid(
            "<UI><Row id='row' width='300' height='50'>"
            "<Panel id='a' width='fill' height='fill'/><Panel id='b' width='2fr' height='fill'/></Row></UI>");
        CHECK_NEAR(ById(r, "a").Bounds.W, 100); CHECK_NEAR(ById(r, "b").Bounds.W, 200);
        CHECK_NEAR(ById(r, "b").Bounds.X, 100); CHECK_NEAR(ById(r, "a").Bounds.H, 50);
    }
    {   // fill com tamanhos fixos, gap e margem.
        ParseResult r = Laid(
            "<UI><Row width='400' height='50' gap='10'>"
            "<Panel id='a' width='100' height='10'/>"
            "<Panel id='f' width='fill' height='10' margin='0 5'/>"
            "<Panel id='c' width='50' height='10'/></Row></UI>");
        // usado: 100 + 50 + margens 10 + gaps 20 = 180 -> fill = 220
        CHECK_NEAR(ById(r, "f").Bounds.W, 220);
        CHECK_NEAR(ById(r, "f").Bounds.X, 100 + 10 + 5);
        CHECK_NEAR(ById(r, "c").Bounds.X, 100 + 10 + 5 + 220 + 5 + 10);
    }
    {   // Justify.
        const char* tpl = "<UI><Row width='300' height='20' justify='%s'>"
                          "<Panel id='a' width='50' height='10'/><Panel id='b' width='50' height='10'/></Row></UI>";
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), tpl, "center");
        ParseResult c = Laid(buffer);
        CHECK_NEAR(ById(c, "a").Bounds.X, 100); CHECK_NEAR(ById(c, "b").Bounds.X, 150);
        std::snprintf(buffer, sizeof(buffer), tpl, "end");
        ParseResult e = Laid(buffer);
        CHECK_NEAR(ById(e, "a").Bounds.X, 200); CHECK_NEAR(ById(e, "b").Bounds.X, 250);
        std::snprintf(buffer, sizeof(buffer), tpl, "space-between");
        ParseResult s = Laid(buffer);
        CHECK_NEAR(ById(s, "a").Bounds.X, 0); CHECK_NEAR(ById(s, "b").Bounds.X, 250);
    }
    {   // Align no eixo transversal (coluna de 200 de largura).
        const char* tpl = "<UI><Column width='200' height='300' align='%s'>"
                          "<Panel id='a' width='50' height='10'/>"
                          "<Panel id='s' height='10'/></Column></UI>";
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), tpl, "center");
        ParseResult c = Laid(buffer);
        CHECK_NEAR(ById(c, "a").Bounds.X, 75);
        std::snprintf(buffer, sizeof(buffer), tpl, "end");
        ParseResult e = Laid(buffer);
        CHECK_NEAR(ById(e, "a").Bounds.X, 150);
        std::snprintf(buffer, sizeof(buffer), tpl, "stretch");
        ParseResult s = Laid(buffer);
        CHECK_NEAR(ById(s, "s").Bounds.W, 200);                      // auto estica
        CHECK_NEAR(ById(s, "a").Bounds.W, 50);                       // largura explicita nao estica
        std::snprintf(buffer, sizeof(buffer), tpl, "start");
        ParseResult st = Laid(buffer);
        CHECK_NEAR(ById(st, "a").Bounds.X, 0);
        CHECK_NEAR(ById(st, "s").Bounds.W, 0);                       // auto sem conteudo
    }
    {   // Coluna com Spacer 'fill' empurra o ultimo item para baixo.
        ParseResult r = Laid(
            "<UI><Column width='100' height='300'>"
            "<Panel id='top' height='20'/><Spacer height='fill'/><Panel id='bottom' height='20'/></Column></UI>");
        CHECK_NEAR(ById(r, "top").Bounds.Y, 0);
        CHECK_NEAR(ById(r, "bottom").Bounds.Y, 280);
    }
    {   // Elementos invisiveis nao ocupam espaco, nem os filhos deles sao posicionados.
        ParseResult r = Laid(
            "<UI><Column width='100' height='300' gap='10'>"
            "<Panel id='a' height='20'/>"
            "<Panel id='hidden' height='500' visible='false'><Panel id='inner' width='10' height='10'/></Panel>"
            "<Panel id='b' height='20'/></Column></UI>");
        CHECK_NEAR(ById(r, "b").Bounds.Y, 30);
        CHECK_NEAR(ById(r, "hidden").Bounds.H, 0);
        CHECK_NEAR(ById(r, "inner").Bounds.W, 0);
    }
    {   // Centro absoluto de uma coluna dentro da tela, com botoes: o caso do "menu principal".
        ParseResult r = Laid(
            "<UI><Column id='menu' anchor='center' gap='12' align='stretch'>"
            "<Button id='play' size='20' width='fill'>Jogar</Button>"
            "<Button id='quit' size='20'>Sair</Button></Column></UI>");
        const Node& menu = ById(r, "menu");
        // Largura do menu = maior botao (Jogar: 50 + 32).
        CHECK_NEAR(menu.Bounds.W, 82);
        CHECK_NEAR(menu.Bounds.H, 40 + 12 + 40);
        CHECK_NEAR(menu.Bounds.X, (1000 - 82) / 2.0f);
        CHECK_NEAR(ById(r, "quit").Bounds.W, 82);                    // esticado pelo align
        CHECK_NEAR(ById(r, "play").Bounds.W, 82);
    }
}

static void TestLayoutScaleAndWrap() {
    std::puts("[Layout: escala por resolucao e quebra de texto]");
    {   // scale=height, referencia 1000x500, tela 2000x1000 => escala 2.
        ParseResult r = Laid(
            "<UI scale='height' reference='1000x500'>"
            "<Panel id='p' width='100' height='50' margin='10'/>"
            "<Text id='t' size='20' margin='100 0 0 0'>abcd</Text></UI>", 2000, 1000);
        CHECK_NEAR(ById(r, "p").Bounds.W, 200); CHECK_NEAR(ById(r, "p").Bounds.H, 100);
        CHECK_NEAR(ById(r, "p").Bounds.X, 20);
        CHECK_NEAR(ById(r, "t").ScaledFontSize, 40);
        CHECK_NEAR(ById(r, "t").Bounds.W, 80);                       // 4 letras * 20
        CHECK_NEAR(ComputeScale(r.Doc, 2000, 1000), 2.0f);
        CHECK_NEAR(ComputeScale(r.Doc, 500, 250), 0.5f);
        CHECK_NEAR(ComputeScale(r.Doc, 1, 1), 0.1f);                 // limitado
    }
    {
        Document d;
        d.ReferenceWidth = 1000; d.ReferenceHeight = 500;
        d.Scale = ScaleMode::Width;  CHECK_NEAR(ComputeScale(d, 3000, 500), 3.0f);
        d.Scale = ScaleMode::Min;    CHECK_NEAR(ComputeScale(d, 3000, 500), 1.0f);
        d.Scale = ScaleMode::None;   CHECK_NEAR(ComputeScale(d, 3000, 3000), 1.0f);
    }
    {   // Quebra automatica: 10 px por letra.
        ParseResult r = Laid("<UI><Text id='t' size='20' wrap='true' width='70'>aaa bbb ccc ddd</Text></UI>");
        const Node& t = ById(r, "t");
        CHECK(t.Lines.size() == 2);
        CHECK(t.Lines.size() == 2 && t.Lines[0] == "aaa bbb" && t.Lines[1] == "ccc ddd");
        CHECK_NEAR(t.Bounds.H, 48);                                  // altura auto = 2 linhas
    }
    {   // Palavra maior que a largura quebra por caractere; '\n' explicito sempre quebra.
        ParseResult r = Laid("<UI><Text id='t' size='20' wrap='true' width='30'>abcdefgh&#10;x</Text></UI>");
        const Node& t = ById(r, "t");
        CHECK(t.Lines.size() == 4);
        CHECK(t.Lines.size() == 4 && t.Lines[0] == "abc" && t.Lines[1] == "def" && t.Lines[2] == "gh" && t.Lines[3] == "x");
    }
    {   // Sem wrap, nao quebra por palavra.
        ParseResult r = Laid("<UI><Text id='t' size='20' width='30'>aaa bbb ccc</Text></UI>");
        CHECK(ById(r, "t").Lines.size() == 1);
    }
    {   // Texto com wrap dentro de coluna: largura = a do pai, altura cresce com as linhas.
        ParseResult r = Laid(
            "<UI><Column id='c' width='100'><Text id='t' size='20' wrap='true'>aa bb cc dd ee</Text>"
            "<Panel id='after' height='5'/></Column></UI>");
        CHECK(ById(r, "t").Lines.size() == 2);                       // "aa bb cc"(80) / "dd ee"
        CHECK_NEAR(ById(r, "after").Bounds.Y, 48);                   // empurrado pelas 2 linhas
    }
    {   // UTF-8 conta por caractere, nao por byte.
        ParseResult r = Laid("<UI><Text id='t' size='20'>a\xC3\xA7\xC3\xA3o</Text></UI>");
        CHECK_NEAR(ById(r, "t").Bounds.W, 40);
    }
    {   // Mesma largura medida e usada para quebrar (sem quebra espuria por arredondamento).
        ParseResult r = Laid("<UI><Text id='t' size='13.7' wrap='true'>one two three four</Text></UI>", 100, 100);
        CHECK(ById(r, "t").Lines.size() == 1 || ById(r, "t").Lines.size() == 2);
        const size_t before = ById(r, "t").Lines.size();
        FakeMeasure m;
        LayoutDocument(r.Doc, 100, 100, m);
        CHECK(ById(r, "t").Lines.size() == before);                  // layout repetido e estavel
    }
}

// ------------------------------------------------------------------ input
static void TestInteraction() {
    std::puts("[Input: hover, clique e quem barra o mouse]");
    ParseResult r = Laid(
        "<UI>"
        "<Button id='a' width='100' height='40' margin='10' onClick='OnA'>A</Button>"
        "<Button id='b' width='100' height='40' margin='200 0 0 10' interactable='false'>B</Button>"
        "<Text id='label' width='100' height='40' margin='300 0 0 10'>texto</Text>"
        "<Panel id='cover' width='100' height='40' margin='400 0 0 10' color='#000'/>"
        "<Button id='under' width='100' height='40' margin='400 0 0 10'>U</Button>"
        "<Panel id='ghost' width='100' height='40' margin='500 0 0 10'/>"
        "<Button id='hid' width='100' height='40' margin='600 0 0 10' visible='false'>H</Button>"
        "</UI>", 1000, 800);
    const int a = r.Doc.Find("a"), b = r.Doc.Find("b"), under = r.Doc.Find("under");
    Interaction input;

    auto at = [&](float x, float y, bool down, bool free = true) { return input.Update(r.Doc, x, y, down, free); };

    // Hover sem clicar.
    Interaction::Result res = at(50, 30, false);
    CHECK(res.PointerOverUI && res.Hovered == a && res.Pressed == -1 && res.Clicked.empty());
    res = at(500, 700, false);
    CHECK(!res.PointerOverUI && res.Hovered == -1);

    // Apertar e soltar em cima: clique.
    at(50, 30, false);
    res = at(50, 30, true);
    CHECK(res.Pressed == a && res.Clicked.empty());
    res = at(50, 30, true);
    CHECK(res.Pressed == a);                                         // segurando: continua apertado
    res = at(50, 30, false);
    CHECK(res.Clicked.size() == 1 && res.Clicked[0] == a && res.Pressed == -1);
    res = at(50, 30, false);
    CHECK(res.Clicked.empty());                                      // so um clique

    // Apertar em cima e soltar FORA: sem clique.
    at(50, 30, true);
    res = at(500, 700, false);
    CHECK(res.Clicked.empty() && res.Pressed == -1);

    // Apertar FORA e arrastar para cima do botao: sem clique (nunca apertou nele).
    at(500, 700, true);
    at(50, 30, true);
    res = at(50, 30, false);
    CHECK(res.Clicked.empty());

    // Botao nao interagivel barra o mouse mas nao fica em hover nem clica.
    res = at(50, 230, false);
    CHECK(res.PointerOverUI && res.Hovered == -1);
    at(50, 230, true);
    res = at(50, 230, false);
    CHECK(res.Clicked.empty());
    (void)b;

    // Texto nao barra o mouse.
    res = at(50, 330, false);
    CHECK(!res.PointerOverUI);

    // O Panel de cima (desenhado depois? aqui vem ANTES do botao) -> o botao 'under' esta por cima.
    res = at(50, 430, false);
    CHECK(res.Hovered == under);

    // Panel transparente (contêiner) nao barra; botao invisivel tambem nao.
    res = at(50, 530, false);
    CHECK(!res.PointerOverUI);
    res = at(50, 630, false);
    CHECK(!res.PointerOverUI && res.Hovered == -1);

    // Cursor travado: a UI nao recebe mouse.
    res = at(50, 30, false, /*pointerFree=*/false);
    CHECK(!res.PointerOverUI && res.Hovered == -1);

    // Um Panel colorido por cima de um botao barra o hover dele.
    ParseResult cover = Laid(
        "<UI><Button id='btn' width='100' height='40'>x</Button><Panel id='cov' width='100' height='40' color='#fff'/></UI>");
    Interaction input2;
    Interaction::Result covered = input2.Update(cover.Doc, 50, 20, false, true);
    CHECK(covered.PointerOverUI && covered.Hovered == -1);

    // Reset.
    input.Reset();
    CHECK(input.Pressed() == -1);

    // Indice de botao pressionado que deixou de existir (documento recarregado menor) nao quebra.
    Interaction input3;
    input3.Update(r.Doc, 50, 30, false, true);
    input3.Update(r.Doc, 50, 30, true, true);
    Document smaller;
    Interaction::Result afterReload = input3.Update(smaller, 50, 30, false, true);
    CHECK(afterReload.Clicked.empty());
}

static void TestRobustness() {
    std::puts("[Robustez: lixo nao pode quebrar]");
    // Nenhum desses pode travar/estourar; erros de XML sao esperados.
    const char* junk[] = {
        "<UI><Panel width='1e999'/></UI>", "<UI><Text size='0'>x</Text></UI>", "<UI><Text size='99999'>x</Text></UI>",
        "<UI><Panel margin='1e9 1e9'/></UI>", "<UI><Row><Panel width='fill'/></Row></UI>", "<UI><Column><Spacer height='fill'/></Column></UI>",
        "\xFF\xFE<UI/>", "<UI \xC3\xA7='1'/>", "<UI><Text>\xC3</Text></UI>",
    };
    for (const char* xml : junk) {
        ParseResult r = ParseDocument(xml);
        FakeMeasure m;
        LayoutDocument(r.Doc, 800, 600, m);
        CHECK(true);
    }

    // Documento grande: 5000 filhos em linha e em coluna.
    std::string big = "<UI><Column width='500' height='500'>";
    for (int i = 0; i < 5000; i++) big += "<Text size='10'>x</Text>";
    big += "</Column></UI>";
    ParseResult r = MustParse(big);
    CHECK(r.Ok && r.Doc.Nodes.size() == 5001);
    FakeMeasure m;
    LayoutDocument(r.Doc, 800, 600, m);
    CHECK_NEAR(r.Doc.Nodes[5000].Bounds.Y, 4999 * 12.0f);

    // Passou do limite de elementos: erro claro, sem crash.
    std::string huge = "<UI>";
    for (int i = 0; i < 10100; i++) huge += "<Spacer/>";
    huge += "</UI>";
    ParseResult over = ParseDocument(huge);
    CHECK(!over.Ok && HasMessage(over, "elementos demais"));

    // Tamanho de tela zero nao divide por zero.
    ParseResult z = MustParse("<UI scale='height'><Panel width='50%' height='fill'/></UI>");
    LayoutDocument(z.Doc, 0, 0, m);
    CHECK(std::isfinite(z.Doc.Nodes[0].Bounds.W));
}

int main() {
    TestHelpers();
    TestParserBasics();
    TestParserText();
    TestParserErrors();
    TestParserWarnings();
    TestLayoutStack();
    TestLayoutIntrinsic();
    TestLayoutColumnRow();
    TestLayoutScaleAndWrap();
    TestInteraction();
    TestRobustness();

    std::printf("\n%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
