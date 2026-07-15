/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Building a XeLaTeX document from a test
 *
 * @defgroup tapi_tex XeLaTeX reports (tapi_tex)
 * @{
 *
 * Generating a good-looking XeLaTeX document and compiling it to PDF on
 * a Test Agent - meant for test reports. The document is built up with
 * a small content model (sections, paragraphs, key/value and general
 * tables, lists, code blocks, images) and rendered to a @c .tex source
 * with a careful preamble (fontspec fonts, @c geometry margins, an
 * accent colour, @c booktabs tables, @c titlesec headings, a
 * @c fancyhdr header/footer, @c hyperref links). The compile step runs
 * @c xelatex on the agent through @ref tapi_devtool_run - nothing is
 * spawned on the engine.
 *
 * - @ref tapi_tex - the document model and rendering to @c .tex (pure
 *   engine-side: building and escaping text, no agent involved);
 * - @ref tapi_tex_build - compiling a @c .tex (or a built document) to
 *   PDF with @c xelatex on an agent behind a job factory.
 *
 * Why XeLaTeX: it reads the @c .tex as UTF-8 and loads system/TeX fonts
 * by name through @c fontspec, so a report with real Unicode and a
 * chosen typeface compiles without the font dance that @c pdflatex
 * needs. @c lualatex works the same way and is accepted as the engine
 * too; @c pdflatex does not and is not the default.
 *
 * @section tapi_tex_example A report in a few calls
 *
 * @code
 * tapi_tex_opts opts = TAPI_TEX_OPTS_INIT;
 * tapi_tex_doc doc;
 * tapi_tex_build_result res;
 *
 * opts.title = "Nightly assurance report";
 * opts.author = "Test Environment";
 * opts.toc = true;
 * tapi_tex_doc_init(&doc, &opts);
 *
 * tapi_tex_section(&doc, 0, "Summary");
 * tapi_tex_paragraph(&doc, "All %u checks passed on %s.", n, host);
 *
 * tapi_tex_section(&doc, 0, "Findings");
 * tapi_tex_keyval(&doc, "Run parameters", &kv);
 *
 * if (!tapi_tex_available(factory, NULL, 10000))
 *     TEST_SKIP("no LaTeX engine on the agent");
 * CHECK_RC(tapi_tex_doc_compile(factory, &doc, NULL, &res));
 * if (!res.ok)
 *     TEST_VERDICT("xelatex failed: %s", res.log.ptr);
 * tapi_tex_build_result_free(&res);
 * tapi_tex_doc_free(&doc);
 * @endcode
 */

#ifndef __TAPI_TEX_H__
#define __TAPI_TEX_H__

#include "te_errno.h"
#include "te_defs.h"
#include "te_string.h"
#include "te_kvpair.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which LaTeX document class the report uses. */
typedef enum tapi_tex_class {
    TAPI_TEX_ARTICLE = 0,   /**< @c article - the default. */
    TAPI_TEX_REPORT,        /**< @c report - chapters and a title page. */
} tapi_tex_class;

/**
 * Styling of the document, used to build the preamble. Every field has
 * a sensible default when left zero/@c NULL, so @ref TAPI_TEX_OPTS_INIT
 * alone yields a clean report.
 */
typedef struct tapi_tex_opts {
    tapi_tex_class doc_class;   /**< Document class (default @c article). */
    const char *title;          /**< Title, or @c NULL for no title block. */
    const char *subtitle;       /**< Subtitle under the title, or @c NULL. */
    const char *author;         /**< Author line, or @c NULL. */
    const char *date;           /**< Date, or @c NULL for @c \\today. */
    unsigned int fontsize_pt;   /**< Base font size (0 => 11). */
    const char *main_font;      /**< Roman font name (@c NULL => a default). */
    const char *sans_font;      /**< Sans font name (@c NULL => a default). */
    const char *mono_font;      /**< Monospace font name (@c NULL => a default). */
    const char *geometry;       /**< @c geometry options (@c NULL => a default). */
    const char *accent_rgb;     /**< Accent colour as @c RRGGBB hex (@c NULL => a default). */
    bool number_sections;       /**< Number the headings (default via INIT). */
    bool toc;                   /**< Emit a table of contents (default @c false). */
    bool titlepage;             /**< Force a dedicated title page. */
    bool landscape;             /**< Landscape page orientation. */
} tapi_tex_opts;

/** Initializer for #tapi_tex_opts: numbered sections, everything else default. */
#define TAPI_TEX_OPTS_INIT { .number_sections = true }

/** A document being built. Treat the fields as read-only. */
typedef struct tapi_tex_doc {
    tapi_tex_opts opts;         /**< A private copy of the styling. */
    te_string body;             /**< Accumulated body LaTeX. */
    te_string preamble;         /**< Extra preamble added with tapi_tex_use(). */
} tapi_tex_doc;

/**
 * Start a document with the given styling.
 *
 * @p opts is copied, so its string fields need only outlive this call.
 *
 * @param[out] doc   Document, zeroed or uninitialized.
 * @param[in]  opts  Styling (@c NULL for all defaults).
 */
extern void tapi_tex_doc_init(tapi_tex_doc *doc, const tapi_tex_opts *opts);

/** Release a document's buffers. Safe on a zeroed or freed document. */
extern void tapi_tex_doc_free(tapi_tex_doc *doc);

/**
 * Append @p src to @p dst with the LaTeX special characters escaped
 * (@c & @c % @c $ @c # @c _ @c { @c } @c ~ @c ^ @c \ and a couple of
 * quoting niceties), so arbitrary text is safe to drop into the body.
 *
 * @param[in,out] dst  Destination string (appended to).
 * @param[in]     src  Plain text (@c NULL is a no-op).
 */
extern void tapi_tex_escape(te_string *dst, const char *src);

/**
 * Add a line to the preamble, e.g. @c "\\usepackage{siunitx}" or a
 * macro definition. The text is literal LaTeX and is emitted after the
 * built-in preamble, before @c \\begin{document}.
 */
extern void tapi_tex_use(tapi_tex_doc *doc, const char *fmt, ...)
    TE_LIKE_PRINTF(2, 3);

/**
 * Open a heading. @p level 0 is the top heading of the class
 * (@c \\section, or @c \\chapter for @c report), 1 the next level down,
 * and so on. @p title is escaped.
 */
extern void tapi_tex_section(tapi_tex_doc *doc, unsigned int level,
                             const char *title);

/** Add a paragraph of text; the format result is escaped. */
extern void tapi_tex_paragraph(tapi_tex_doc *doc, const char *fmt, ...)
    TE_LIKE_PRINTF(2, 3);

/** Append literal LaTeX to the body (an escape hatch; nothing is escaped). */
extern void tapi_tex_raw(tapi_tex_doc *doc, const char *fmt, ...)
    TE_LIKE_PRINTF(2, 3);

/**
 * Add a monospaced, verbatim code block (a @c Verbatim environment with
 * line breaking). @p caption is an optional bold line above it
 * (escaped); @p text is reproduced as-is and is not escaped.
 *
 * @note The one sequence that cannot appear in @p text is a line equal
 *       to @c \\end{Verbatim}.
 */
extern void tapi_tex_verbatim(tapi_tex_doc *doc, const char *caption,
                              const char *text);

/** Add a bulleted list. Each of @p n_items entries is escaped. */
extern void tapi_tex_itemize(tapi_tex_doc *doc,
                             const char *const *items, size_t n_items);

/** Add a numbered list. Each of @p n_items entries is escaped. */
extern void tapi_tex_enumerate(tapi_tex_doc *doc,
                               const char *const *items, size_t n_items);

/**
 * Add a two-column key/value table (a @c booktabs @c longtable), one
 * row per pair in @p kv in insertion order. Keys and values are
 * escaped. @p caption is optional.
 */
extern void tapi_tex_keyval(tapi_tex_doc *doc, const char *caption,
                            const te_kvpair_h *kv);

/**
 * Add a general table (a @c booktabs @c longtable).
 *
 * @param doc      Document.
 * @param caption  Optional caption (escaped).
 * @param ncols    Number of columns.
 * @param headers  @p ncols header cells (escaped), or @c NULL for none.
 * @param nrows    Number of body rows.
 * @param cells    @p ncols * @p nrows cells, row-major, each escaped.
 */
extern void tapi_tex_table(tapi_tex_doc *doc, const char *caption,
                           size_t ncols, const char *const *headers,
                           size_t nrows, const char *const *cells);

/**
 * Include an image. @p path is a file reachable by @c xelatex at compile
 * time - i.e. a path on the agent that runs the compile, usually under
 * the working directory. @p width is a LaTeX length (e.g.
 * @c "0.8\\linewidth"; @c NULL => @c "\\linewidth"); @p caption is
 * optional (escaped) and, when given, makes it a captioned figure.
 */
extern void tapi_tex_image(tapi_tex_doc *doc, const char *path,
                           const char *width, const char *caption);

/** Force a page break in the body. */
extern void tapi_tex_pagebreak(tapi_tex_doc *doc);

/**
 * Render the whole document into @p out as a complete @c .tex source.
 * @p out is reset first.
 */
extern void tapi_tex_render(const tapi_tex_doc *doc, te_string *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

/**@} <!-- END tapi_tex --> */
#endif /* __TAPI_TEX_H__ */
