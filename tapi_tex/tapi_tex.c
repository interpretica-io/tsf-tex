/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Building and rendering a XeLaTeX document
 *
 * The document model (tapi_tex.h): content builders append LaTeX to a
 * body buffer, and tapi_tex_render() wraps that body in a styled
 * preamble. Pure engine-side - no agent, no process; compiling the
 * result is tapi_tex_build.c.
 */

#define TE_LGR_USER     "TAPI TeX"

#include "te_config.h"

#include <stdarg.h>
#include <string.h>

#include "te_defs.h"
#include "te_str.h"
#include "te_string.h"
#include "te_kvpair.h"
#include "logger_api.h"

#include "tapi_tex.h"

/* Defaults for anything the caller left unset. */
#define TEX_DEF_FONTSIZE    11
#define TEX_DEF_MAIN_FONT   "TeX Gyre Pagella"
#define TEX_DEF_SANS_FONT   "TeX Gyre Heros"
#define TEX_DEF_MONO_FONT   "TeX Gyre Cursor"
#define TEX_DEF_GEOMETRY    "a4paper,margin=2.5cm"
#define TEX_DEF_ACCENT      "2A6EBB"

/* See description in tapi_tex.h */
void
tapi_tex_doc_init(tapi_tex_doc *doc, const tapi_tex_opts *opts)
{
    static const tapi_tex_opts defaults = TAPI_TEX_OPTS_INIT;

    memset(doc, 0, sizeof(*doc));
    doc->opts = opts != NULL ? *opts : defaults;
    doc->body = (te_string)TE_STRING_INIT;
    doc->preamble = (te_string)TE_STRING_INIT;
}

/* See description in tapi_tex.h */
void
tapi_tex_doc_free(tapi_tex_doc *doc)
{
    if (doc == NULL)
        return;
    te_string_free(&doc->body);
    te_string_free(&doc->preamble);
}

/* See description in tapi_tex.h */
void
tapi_tex_escape(te_string *dst, const char *src)
{
    const char *p;

    if (src == NULL)
        return;

    for (p = src; *p != '\0'; p++)
    {
        switch (*p)
        {
            case '\\': te_string_append(dst, "%s", "\\textbackslash{}"); break;
            case '&':  te_string_append(dst, "%s", "\\&"); break;
            case '%':  te_string_append(dst, "%s", "\\%"); break;
            case '$':  te_string_append(dst, "%s", "\\$"); break;
            case '#':  te_string_append(dst, "%s", "\\#"); break;
            case '_':  te_string_append(dst, "%s", "\\_"); break;
            case '{':  te_string_append(dst, "%s", "\\{"); break;
            case '}':  te_string_append(dst, "%s", "\\}"); break;
            case '~':  te_string_append(dst, "%s", "\\textasciitilde{}"); break;
            case '^':  te_string_append(dst, "%s", "\\textasciicircum{}"); break;
            default:   te_string_append_buf(dst, p, 1); break;
        }
    }
}

/** Append fmt/ap to a scratch string and escape it into @p dst. */
static void
tex_append_escaped_va(te_string *dst, const char *fmt, va_list ap)
{
    te_string tmp = TE_STRING_INIT;

    te_string_append_va(&tmp, fmt, ap);
    tapi_tex_escape(dst, tmp.ptr != NULL ? tmp.ptr : "");
    te_string_free(&tmp);
}

/* See description in tapi_tex.h */
void
tapi_tex_use(tapi_tex_doc *doc, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    te_string_append_va(&doc->preamble, fmt, ap);
    va_end(ap);
    te_string_append(&doc->preamble, "%s", "\n");
}

/**
 * The sectioning command for @p level in this document's class, deepest
 * clamped to the last entry.
 */
static const char *
tex_section_cmd(const tapi_tex_doc *doc, unsigned int level)
{
    static const char *const article[] = {
        "section", "subsection", "subsubsection", "paragraph", "subparagraph"
    };
    static const char *const report[] = {
        "chapter", "section", "subsection", "subsubsection", "paragraph"
    };
    const char *const *tbl = doc->opts.doc_class == TAPI_TEX_REPORT ?
                             report : article;
    unsigned int max = TE_ARRAY_LEN(article) - 1;

    return tbl[level <= max ? level : max];
}

/* See description in tapi_tex.h */
void
tapi_tex_section(tapi_tex_doc *doc, unsigned int level, const char *title)
{
    te_string_append(&doc->body, "\n\\%s{", tex_section_cmd(doc, level));
    tapi_tex_escape(&doc->body, title);
    te_string_append(&doc->body, "%s", "}\n");
}

/* See description in tapi_tex.h */
void
tapi_tex_paragraph(tapi_tex_doc *doc, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    tex_append_escaped_va(&doc->body, fmt, ap);
    va_end(ap);
    te_string_append(&doc->body, "%s", "\n\n");
}

/* See description in tapi_tex.h */
void
tapi_tex_raw(tapi_tex_doc *doc, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    te_string_append_va(&doc->body, fmt, ap);
    va_end(ap);
}

/* See description in tapi_tex.h */
void
tapi_tex_verbatim(tapi_tex_doc *doc, const char *caption, const char *text)
{
    if (caption != NULL)
    {
        te_string_append(&doc->body, "%s", "\n\\par\\noindent{\\sffamily\\bfseries ");
        tapi_tex_escape(&doc->body, caption);
        te_string_append(&doc->body, "%s", "}\\par\\nobreak\\vspace{2pt}\n");
    }
    te_string_append(&doc->body, "%s", "\\begin{Verbatim}\n");
    if (text != NULL)
        te_string_append(&doc->body, "%s", text);
    if (text == NULL || text[0] == '\0' || text[strlen(text) - 1] != '\n')
        te_string_append(&doc->body, "%s", "\n");
    te_string_append(&doc->body, "%s", "\\end{Verbatim}\n");
}

/** Shared body of the two list builders. */
static void
tex_list(tapi_tex_doc *doc, const char *env,
         const char *const *items, size_t n_items)
{
    size_t i;

    te_string_append(&doc->body, "\n\\begin{%s}\n", env);
    for (i = 0; i < n_items; i++)
    {
        te_string_append(&doc->body, "%s", "  \\item ");
        tapi_tex_escape(&doc->body, items[i]);
        te_string_append(&doc->body, "%s", "\n");
    }
    te_string_append(&doc->body, "\\end{%s}\n", env);
}

/* See description in tapi_tex.h */
void
tapi_tex_itemize(tapi_tex_doc *doc, const char *const *items, size_t n_items)
{
    tex_list(doc, "itemize", items, n_items);
}

/* See description in tapi_tex.h */
void
tapi_tex_enumerate(tapi_tex_doc *doc, const char *const *items, size_t n_items)
{
    tex_list(doc, "enumerate", items, n_items);
}

/** Emit the bold sans caption line used above tables. */
static void
tex_caption_line(tapi_tex_doc *doc, const char *caption)
{
    if (caption == NULL)
        return;
    te_string_append(&doc->body, "%s", "\n\\par\\noindent{\\sffamily\\bfseries ");
    tapi_tex_escape(&doc->body, caption);
    te_string_append(&doc->body, "%s", "}\\par\\nobreak\\vspace{3pt}\n");
}

/* See description in tapi_tex.h */
void
tapi_tex_keyval(tapi_tex_doc *doc, const char *caption, const te_kvpair_h *kv)
{
    const te_kvpair *p;

    tex_caption_line(doc, caption);
    te_string_append(&doc->body, "%s",
        "\\begin{longtable}{@{}>{\\bfseries}l p{0.62\\linewidth}@{}}\n"
        "\\toprule\n");
    if (kv != NULL)
    {
        TAILQ_FOREACH(p, kv, links)
        {
            tapi_tex_escape(&doc->body, p->key);
            te_string_append(&doc->body, "%s", " & ");
            tapi_tex_escape(&doc->body, p->value);
            te_string_append(&doc->body, "%s", " \\\\\n");
        }
    }
    te_string_append(&doc->body, "%s", "\\bottomrule\n\\end{longtable}\n");
}

/* See description in tapi_tex.h */
void
tapi_tex_table(tapi_tex_doc *doc, const char *caption,
               size_t ncols, const char *const *headers,
               size_t nrows, const char *const *cells)
{
    size_t r;
    size_t c;

    if (ncols == 0)
        return;

    tex_caption_line(doc, caption);

    te_string_append(&doc->body, "%s", "\\begin{longtable}{@{}");
    for (c = 0; c < ncols; c++)
        te_string_append(&doc->body, "%s", "l");
    te_string_append(&doc->body, "%s", "@{}}\n\\toprule\n");

    if (headers != NULL)
    {
        for (c = 0; c < ncols; c++)
        {
            te_string_append(&doc->body, "%s", "{\\sffamily\\bfseries ");
            tapi_tex_escape(&doc->body, headers[c]);
            te_string_append(&doc->body, "%s", c + 1 < ncols ? "} & " : "} \\\\\n");
        }
        te_string_append(&doc->body, "%s", "\\midrule\n\\endhead\n");
    }

    for (r = 0; r < nrows; r++)
    {
        for (c = 0; c < ncols; c++)
        {
            tapi_tex_escape(&doc->body, cells[r * ncols + c]);
            te_string_append(&doc->body, "%s", c + 1 < ncols ? " & " : " \\\\\n");
        }
    }

    te_string_append(&doc->body, "%s", "\\bottomrule\n\\end{longtable}\n");
}

/* See description in tapi_tex.h */
void
tapi_tex_image(tapi_tex_doc *doc, const char *path,
               const char *width, const char *caption)
{
    const char *w = width != NULL ? width : "\\linewidth";

    if (path == NULL)
        return;

    if (caption != NULL)
    {
        te_string_append(&doc->body,
            "\n\\begin{figure}[htbp]\\centering\n"
            "\\includegraphics[width=%s,keepaspectratio]{%s}\n"
            "\\caption{", w, path);
        tapi_tex_escape(&doc->body, caption);
        te_string_append(&doc->body, "%s", "}\n\\end{figure}\n");
    }
    else
    {
        te_string_append(&doc->body,
            "\n\\begin{center}\n"
            "\\includegraphics[width=%s,keepaspectratio]{%s}\n"
            "\\end{center}\n", w, path);
    }
}

/* See description in tapi_tex.h */
void
tapi_tex_pagebreak(tapi_tex_doc *doc)
{
    te_string_append(&doc->body, "%s", "\n\\clearpage\n");
}

/** Emit the \title{...} content, folding a subtitle under the title. */
static void
tex_title_value(const tapi_tex_opts *o, te_string *out)
{
    tapi_tex_escape(out, o->title);
    if (o->subtitle != NULL)
    {
        te_string_append(out, "%s", "\\\\[0.35em]\\large ");
        tapi_tex_escape(out, o->subtitle);
    }
}

/* See description in tapi_tex.h */
void
tapi_tex_render(const tapi_tex_doc *doc, te_string *out)
{
    const tapi_tex_opts *o = &doc->opts;
    unsigned int fontsize = o->fontsize_pt != 0 ? o->fontsize_pt : TEX_DEF_FONTSIZE;
    const char *cls = o->doc_class == TAPI_TEX_REPORT ? "report" : "article";
    const char *geometry = o->geometry != NULL ? o->geometry : TEX_DEF_GEOMETRY;
    const char *accent = o->accent_rgb != NULL ? o->accent_rgb : TEX_DEF_ACCENT;
    bool have_title = o->title != NULL;
    bool titlepage = o->titlepage || (o->doc_class == TAPI_TEX_REPORT && have_title);

    te_string_reset(out);

    /* Document class and packages. */
    te_string_append(out, "\\documentclass[%upt]{%s}\n", fontsize, cls);
    te_string_append(out, "\\usepackage[%s%s]{geometry}\n",
                     geometry, o->landscape ? ",landscape" : "");
    te_string_append(out, "%s",
        "\\usepackage{fontspec}\n"
        "\\usepackage{xcolor}\n"
        "\\usepackage{booktabs}\n"
        "\\usepackage{longtable}\n"
        "\\usepackage{graphicx}\n"
        "\\usepackage{enumitem}\n"
        "\\usepackage{titlesec}\n"
        "\\usepackage{fancyhdr}\n"
        "\\usepackage{fvextra}\n"       /* fancyvrb + breaklines/breakanywhere */
        "\\usepackage{microtype}\n"
        "\\usepackage[hidelinks]{hyperref}\n");

    /* Colour, fonts. */
    te_string_append(out, "\\definecolor{accent}{HTML}{%s}\n", accent);
    te_string_append(out, "\\setmainfont{%s}\n",
                     o->main_font != NULL ? o->main_font : TEX_DEF_MAIN_FONT);
    te_string_append(out, "\\setsansfont{%s}\n",
                     o->sans_font != NULL ? o->sans_font : TEX_DEF_SANS_FONT);
    te_string_append(out, "\\setmonofont{%s}\n",
                     o->mono_font != NULL ? o->mono_font : TEX_DEF_MONO_FONT);
    te_string_append(out, "%s",
        "\\hypersetup{colorlinks=true,linkcolor=accent,urlcolor=accent,"
        "citecolor=accent}\n");

    /* Headings in the accent colour, sans serif. */
    te_string_append(out, "%s",
        "\\titleformat{\\section}{\\Large\\sffamily\\bfseries\\color{accent}}"
        "{\\thesection}{0.75em}{}\n"
        "\\titleformat{\\subsection}{\\large\\sffamily\\bfseries\\color{accent}}"
        "{\\thesubsection}{0.75em}{}\n"
        "\\titleformat{\\subsubsection}"
        "{\\normalsize\\sffamily\\bfseries\\color{accent}}"
        "{\\thesubsubsection}{0.75em}{}\n");

    /* Running header/footer. */
    te_string_append(out, "%s",
        "\\pagestyle{fancy}\n\\fancyhf{}\n"
        "\\renewcommand{\\headrulewidth}{0.4pt}\n"
        "\\fancyfoot[C]{\\small\\sffamily\\thepage}\n");
    if (have_title || o->author != NULL)
    {
        te_string_append(out, "%s", "\\fancyhead[L]{\\small\\sffamily\\color{accent}");
        tapi_tex_escape(out, have_title ? o->title : o->author);
        te_string_append(out, "%s", "}\n");
    }

    /* Verbatim defaults and section numbering depth. */
    te_string_append(out, "%s",
        "\\fvset{fontsize=\\small,frame=single,framesep=4pt,"
        "breaklines=true,breakanywhere=true}\n");
    te_string_append(out, "\\setcounter{secnumdepth}{%d}\n",
                     o->number_sections ? 3 : -2);
    te_string_append(out, "\\setcounter{tocdepth}{%d}\n", 2);

    /* Title metadata. */
    if (have_title)
    {
        te_string_append(out, "%s", "\\title{");
        tex_title_value(o, out);
        te_string_append(out, "%s", "}\n");
        te_string_append(out, "%s", "\\author{");
        tapi_tex_escape(out, o->author != NULL ? o->author : "");
        te_string_append(out, "%s", "}\n");
        if (o->date != NULL)
        {
            te_string_append(out, "%s", "\\date{");
            tapi_tex_escape(out, o->date);
            te_string_append(out, "%s", "}\n");
        }
    }

    /* Caller's extra preamble, then the document. */
    if (doc->preamble.ptr != NULL && doc->preamble.len != 0)
        te_string_append(out, "%s", doc->preamble.ptr);

    te_string_append(out, "%s", "\\begin{document}\n");

    if (have_title)
    {
        if (titlepage)
            te_string_append(out, "%s", "\\begin{titlepage}\n\\maketitle\n"
                                        "\\thispagestyle{empty}\n\\end{titlepage}\n");
        else
            te_string_append(out, "%s", "\\maketitle\n");
    }
    if (o->toc)
        te_string_append(out, "%s", "\\tableofcontents\n\\clearpage\n");

    if (doc->body.ptr != NULL)
        te_string_append(out, "%s", doc->body.ptr);

    te_string_append(out, "%s", "\n\\end{document}\n");
}
