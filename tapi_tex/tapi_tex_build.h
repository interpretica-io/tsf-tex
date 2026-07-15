/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Compiling a XeLaTeX document on an agent
 *
 * @defgroup tapi_tex_build Compiling XeLaTeX (tapi_tex_build)
 * @ingroup tapi_tex
 * @{
 *
 * Running @c xelatex (or @c lualatex) on a Test Agent to turn a @c .tex
 * source into a PDF, through @ref tapi_devtool_run. The source is
 * written to a working directory on the agent, the engine is run the
 * requested number of passes (two by default, so a table of contents
 * and cross-references settle), and the result says whether a PDF came
 * out, with the final pass's log kept for a verdict. The PDF stays on
 * the agent and, when asked, is copied to the engine as an artifact.
 */

#ifndef __TAPI_TEX_BUILD_H__
#define __TAPI_TEX_BUILD_H__

#include "te_errno.h"
#include "te_defs.h"
#include "te_string.h"
#include "tapi_job.h"
#include "tapi_tex.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Options for a compile. All-zero (@ref TAPI_TEX_BUILD_OPTS_INIT) is fine. */
typedef struct tapi_tex_build_opts {
    const char *engine;     /**< Engine program (@c NULL => @c "xelatex"). */
    unsigned int passes;    /**< Compile passes (0 => 2). */
    int timeout_ms;         /**< Timeout per pass (0 => 120000). */
    const char *workdir;    /**< Working dir on the agent (@c NULL => a unique one under @c /tmp). */
    bool keep_workdir;      /**< Keep the working dir after the compile (default: remove it). */
    const char *copy_pdf_to;/**< Engine path to copy the PDF to, or @c NULL. */
} tapi_tex_build_opts;

/** Initializer for #tapi_tex_build_opts (xelatex, two passes, defaults). */
#define TAPI_TEX_BUILD_OPTS_INIT { 0 }

/** What a compile produced. Release with tapi_tex_build_result_free(). */
typedef struct tapi_tex_build_result {
    bool ok;                /**< @c true iff the final pass exited 0 and a PDF exists. */
    te_string log;          /**< Combined stdout+stderr of the final pass. */
    char *pdf_ta_path;      /**< PDF path on the agent, or @c NULL. */
    char *pdf_local_path;   /**< PDF path on the engine if copied, or @c NULL. */
    unsigned int passes_run;/**< How many passes actually ran. */
} tapi_tex_build_result;

/** Release a result's buffers. Safe on a zeroed result. */
extern void tapi_tex_build_result_free(tapi_tex_build_result *result);

/**
 * Is a LaTeX engine present on the agent behind @p factory?
 *
 * Runs @c "<engine> --version" and checks it exits 0. Use it in a
 * prologue to @c TEST_SKIP rather than fail when the agent has no TeX.
 *
 * @param factory     Job factory bound to the agent.
 * @param engine      Engine program (@c NULL => @c "xelatex").
 * @param timeout_ms  Timeout (0 => 10000).
 *
 * @return @c true if the engine ran.
 */
extern bool tapi_tex_available(tapi_job_factory_t *factory,
                               const char *engine, int timeout_ms);

/**
 * Compile @p tex (a complete @c .tex source) to PDF on the agent.
 *
 * @param[in]  factory  Job factory bound to the agent.
 * @param[in]  tex      Full @c .tex source.
 * @param[in]  opts     Options (@c NULL for all defaults).
 * @param[out] result   Filled in on return; free with
 *                       tapi_tex_build_result_free(). @a ok tells
 *                       whether the PDF was produced.
 *
 * @return Status code. A clean compile that nonetheless failed to make
 *         a PDF returns 0 with @a result->ok @c false (a LaTeX error is
 *         a result, not a transport failure); a non-zero code means the
 *         job could not be run at all.
 */
extern te_errno tapi_tex_compile(tapi_job_factory_t *factory,
                                 const char *tex,
                                 const tapi_tex_build_opts *opts,
                                 tapi_tex_build_result *result);

/**
 * Render @p doc and compile it, as tapi_tex_render() followed by
 * tapi_tex_compile().
 */
extern te_errno tapi_tex_doc_compile(tapi_job_factory_t *factory,
                                     const tapi_tex_doc *doc,
                                     const tapi_tex_build_opts *opts,
                                     tapi_tex_build_result *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

/**@} <!-- END tapi_tex_build --> */
#endif /* __TAPI_TEX_BUILD_H__ */
