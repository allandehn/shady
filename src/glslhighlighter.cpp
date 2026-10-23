#include "glslhighlighter.h"
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <QTextLayout>

namespace {
enum Kind { Keyword, Type, Number, Builtin, Preproc, Comment, KindCount };

const QSet<QString> &words(Kind k)
{
    static const QSet<QString> kw = {
        "if", "else", "for", "while", "do", "return", "break", "continue", "discard", "switch", "case", "default",
        "in", "out", "inout", "uniform", "const", "struct", "precision", "highp", "mediump", "lowp", "attribute",
        "varying", "layout", "flat", "smooth", "invariant", "true", "false"};
    static const QSet<QString> ty = {"void", "bool", "int", "uint", "float", "double", "sampler2D", "sampler3D",
                                     "samplerCube", "sampler2DArray", "isampler2D", "usampler2D"};
    static const QSet<QString> bi = {
        // functions
        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "pow", "exp",
        "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign", "floor", "ceil", "trunc", "round", "fract",
        "mod", "modf", "min", "max", "clamp", "mix", "step", "smoothstep", "isnan", "isinf", "length", "distance",
        "dot", "cross", "normalize", "faceforward", "reflect", "refract", "matrixCompMult", "transpose",
        "inverse", "determinant", "outerProduct", "lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual",
        "equal", "notEqual", "any", "all", "not", "texture", "textureLod", "texelFetch", "textureSize",
        "textureGrad", "texture2D", "textureCube", "dFdx", "dFdy", "fwidth", "floatBitsToInt", "intBitsToFloat",
        "packHalf2x16", "unpackHalf2x16",
        // Shadertoy
        "iTime", "iTimeDelta", "iFrame", "iFrameRate", "iResolution", "iMouse", "iDate", "iChannel0", "iChannel1",
        "iChannel2", "iChannel3", "iChannelResolution", "iChannelTime", "iSampleRate", "fragCoord", "fragColor",
        "mainImage", "gl_FragCoord", "gl_FragColor"};
    return k == Keyword ? kw : k == Type ? ty : bi;
}
} // namespace

class GlslHighlighter::Impl : public QSyntaxHighlighter
{
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;
    QTextCharFormat fmt[KindCount];

protected:
    void highlightBlock(const QString &text) override
    {
        static const QRegularExpression token(QStringLiteral(
            R"((//.*)|(/\*)|(\b0[xX][0-9a-fA-F]+[uU]?\b|(?:\b\d+\.\d*|\.\d+|\b\d+)(?:[eE][+-]?\d+)?(?:[fF]|[uU]|lf|LF)?)|([A-Za-z_]\w*))"));
        static const QRegularExpression typeRe(QStringLiteral(R"(^(?:[ibud]?vec[234]|d?mat[234](?:x[234])?)$)"));
        static const QRegularExpression close(QStringLiteral(R"(\*/)"));
        setCurrentBlockState(0);
        int from = 0;
        if (previousBlockState() == 1) {           // inside /* */ from an earlier line
            const int e = text.indexOf(close);
            if (e < 0) { setFormat(0, text.size(), fmt[Comment]); setCurrentBlockState(1); return; }
            setFormat(0, e + 2, fmt[Comment]);
            from = e + 2;
        }
        const int lead = text.indexOf(QRegularExpression(QStringLiteral("\\S")), from);
        if (from == 0 && lead >= 0 && text.at(lead) == u'#') {
            // Preprocessor line; a trailing // comment stays muted.
            const int c = text.indexOf(QStringLiteral("//"), lead);
            setFormat(lead, (c < 0 ? text.size() : c) - lead, fmt[Preproc]);
            if (c >= 0) setFormat(c, text.size() - c, fmt[Comment]);
            return;
        }
        auto it = token.globalMatch(text, from);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedStart(1) >= 0) { setFormat(m.capturedStart(1), m.capturedLength(1), fmt[Comment]); return; }
            if (m.capturedStart(2) >= 0) {
                const int s = m.capturedStart(2), e = text.indexOf(close, s + 2);
                if (e < 0) { setFormat(s, text.size() - s, fmt[Comment]); setCurrentBlockState(1); return; }
                setFormat(s, e + 2 - s, fmt[Comment]);
                it = token.globalMatch(text, e + 2);
                continue;
            }
            if (m.capturedStart(3) >= 0) { setFormat(m.capturedStart(3), m.capturedLength(3), fmt[Number]); continue; }
            const QString w = m.captured(4);
            const Kind k = words(Keyword).contains(w) ? Keyword
                         : words(Type).contains(w) || typeRe.match(w).hasMatch() ? Type
                         : words(Builtin).contains(w) ? Builtin : KindCount;
            if (k != KindCount) setFormat(m.capturedStart(4), w.size(), fmt[k]);
        }
    }
};

GlslHighlighter::GlslHighlighter(QObject *parent) : QObject(parent), m_hl(new Impl(this))
{
    recolour();
}

void GlslHighlighter::setDocument(QQuickTextDocument *d)
{
    if (d == m_qdoc) return;
    m_qdoc = d;
    setTextDocument(d ? d->textDocument() : nullptr);
    emit documentChanged();
}

void GlslHighlighter::setTextDocument(QTextDocument *d)
{
    m_hl->setDocument(d);
    if (d) m_hl->rehighlight(); // now, not on the next event loop pass (no flash of plain text)
}

void GlslHighlighter::setTheme(QObject *t)
{
    if (t == m_theme) return;
    if (m_theme) disconnect(m_theme, nullptr, this, nullptr);
    m_theme = t;
    if (t) connect(t, SIGNAL(changed()), this, SLOT(recolour())); // live theme switches
    recolour();
    emit themeChanged();
}

void GlslHighlighter::recolour()
{
    auto c = [&](const char *p, const char *fallback) {
        const QColor v = m_theme ? m_theme->property(p).value<QColor>() : QColor();
        return v.isValid() ? v : QColor(QLatin1String(fallback));
    };
    const QColor col[KindCount] = {c("magenta", "#bb9af7"), c("yellow", "#e0af68"), c("orange", "#ff9e64"),
                                   c("blue", "#7aa2f7"), c("cyan", "#7dcfff"), c("muted", "#737aa2")};
    bool same = true;
    for (int k = 0; k < KindCount; ++k) {
        same &= m_hl->fmt[k].foreground().color() == col[k];
        m_hl->fmt[k].setForeground(col[k]);
    }
    if (!same && m_hl->document()) m_hl->rehighlight();
}

QColor GlslHighlighter::colorAt(int line, int col) const
{
    const QTextDocument *d = m_hl->document();
    if (!d) return {};
    const QTextBlock b = d->findBlockByNumber(line);
    if (!b.isValid() || !b.layout()) return {};
    for (const QTextLayout::FormatRange &r : b.layout()->formats())
        if (col >= r.start && col < r.start + r.length) return r.format.foreground().color();
    return {};
}
