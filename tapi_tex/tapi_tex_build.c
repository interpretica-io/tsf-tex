/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Compiling a XeLaTeX document on an agent
 *
 * Writes the .tex to a working directory on the agent and runs the
 * LaTeX engine there through tapi_devtool_run, the requested number of
 * passes. The PDF stays on the agent and is copied to the engine on
 * request. See tapi_tex_build.h.
 */

#define TE_LGR_USER     "TAPI TeX build"

#include "te_config.h"

#include <stdarg.h>
#include <string.h>

#include "te_defs.h"
#include "te_str.h"
#include "te_alloc.h"
#include "te_string.h"
#include "te_vec.h"
#include "logger_api.h"

#include "tapi_job.h"
#include "tapi_job_opt.h"
#include "tapi_file.h"
#include "tapi_devtool_run.h"

#include "tapi_tex_build.h"

#define TEX_DEF_ENGINE      "xelatex"
#define TEX_DEF_PASSES      2
#define TEX_DEF_TIMEOUT_MS  120000
#define TEX_AVAIL_TIMEOUT   10000
#define TEX_TEX_NAME        "report.tex"
#define TEX_PDF_NAME        "report.pdf"

/** A plain argument vector for tapi_devtool_run (as tsf-ai does). */
typedef struct tex_args {
    size_t n_args;
    const char **args;
} tex_args;

static const tapi_job_opt_bind tex_arg_binds[] = TAPI_JOB_OPT_SET(
    TAPI_JOB_OPT_ARRAY_PTR(tex_args, n_args, args,
        TAPI_JOB_OPT_CONTENT(TAPI_JOB_OPT_STRING, NULL, false))
);

/** Push a formatted, heap-owned argument onto @p args. */
static void
tex_arg(te_vec *args, const char *fmt, ...)
{
    te_string s = TE_STRING_INIT;
    va_list ap;
    char *p;

    va_start(ap, fmt);
    te_string_append_va(&s, fmt, ap);
    va_end(ap);

    /* Move ownership of the buffer into the vector. */
    p = s.ptr;
    TE_VEC_APPEND(args, p);
}

/**
 * Run @p program with @p args on the agent and wait for it.
 *
 * @param[out] out        Appended with stdout (may be @c NULL).
 * @param[out] err        Appended with stderr (may be @c NULL).
 * @param[out] exit_code  Exit code, or -1 if not a normal exit (may be @c NULL).
 */
static te_errno
tex_run(tapi_job_factory_t *factory, const char *name, const char *program,
        te_vec *args, const char *workdir, int timeout_ms,
        te_string *out, te_string *err, int *exit_code)
{
    tapi_devtool_run run = TAPI_DEVTOOL_RUN_INIT;
    tapi_devtool_output output;
    tex_args opt;
    te_errno rc;

    opt.n_args = te_vec_size(args);
    opt.args = (const char **)te_vec_get(args, 0);

    rc = tapi_devtool_run_init(&run, factory, name, program,
                               tex_arg_binds, &opt, workdir);
    if (rc == 0)
        rc = tapi_devtool_run_start(&run);
    if (rc == 0)
        rc = tapi_devtool_run_wait(&run, timeout_ms);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) == TE_EINPROGRESS)
            rc = TE_RC(TE_TAPI, TE_ETIMEDOUT);
        tapi_devtool_run_fini(&run);
        return rc;
    }

    tapi_devtool_run_get_output(&run, &output);
    if (out != NULL && output.out != NULL)
        te_string_append(out, "%s", output.out);
    if (err != NULL && output.err != NULL)
        te_string_append(err, "%s", output.err);
    if (exit_code != NULL)
    {
        *exit_code = output.status.type == TAPI_JOB_STATUS_EXITED ?
                     output.status.value : -1;
    }

    return tapi_devtool_run_fini(&run);
}

/* See description in tapi_tex_build.h */
bool
tapi_tex_available(tapi_job_factory_t *factory, const char *engine,
                   int timeout_ms)
{
    te_vec args = TE_VEC_INIT(char *);
    int exit_code = -1;
    te_errno rc;

    if (engine == NULL)
        engine = TEX_DEF_ENGINE;
    if (timeout_ms <= 0)
        timeout_ms = TEX_AVAIL_TIMEOUT;

    tex_arg(&args, "--version");
    rc = tex_run(factory, engine, engine, &args, NULL, timeout_ms,
                 NULL, NULL, &exit_code);
    te_vec_deep_free(&args);

    return rc == 0 && exit_code == 0;
}

/* See description in tapi_tex_build.h */
void
tapi_tex_build_result_free(tapi_tex_build_result *result)
{
    if (result == NULL)
        return;
    te_string_free(&result->log);
    free(result->pdf_ta_path);
    free(result->pdf_local_path);
    result->pdf_ta_path = NULL;
    result->pdf_local_path = NULL;
}

/** Run one LaTeX pass in @p dir. */
static te_errno
tex_one_pass(tapi_job_factory_t *factory, const char *engine,
             const char *dir, int timeout_ms, te_string *log, int *exit_code)
{
    te_vec args = TE_VEC_INIT(char *);
    te_errno rc;

    tex_arg(&args, "-interaction=nonstopmode");
    tex_arg(&args, "-halt-on-error");
    tex_arg(&args, "%s", TEX_TEX_NAME);

    rc = tex_run(factory, engine, engine, &args, dir, timeout_ms,
                 log, log, exit_code);
    te_vec_deep_free(&args);

    return rc;
}

/* See description in tapi_tex_build.h */
te_errno
tapi_tex_compile(tapi_job_factory_t *factory, const char *tex,
                 const tapi_tex_build_opts *opts,
                 tapi_tex_build_result *result)
{
    static const tapi_tex_build_opts defaults = TAPI_TEX_BUILD_OPTS_INIT;
    const char *ta = tapi_job_factory_ta(factory);
    const char *engine;
    unsigned int passes;
    int timeout_ms;
    te_string dir = TE_STRING_INIT;
    bool own_dir;
    te_vec mkdir_args = TE_VEC_INIT(char *);
    int exit_code = -1;
    te_errno rc;

    if (opts == NULL)
        opts = &defaults;
    if (result == NULL || tex == NULL)
        return TE_RC(TE_TAPI, TE_EINVAL);
    if (ta == NULL)
        return TE_RC(TE_TAPI, TE_EINVAL);

    memset(result, 0, sizeof(*result));
    result->log = (te_string)TE_STRING_INIT;

    engine = opts->engine != NULL ? opts->engine : TEX_DEF_ENGINE;
    passes = opts->passes != 0 ? opts->passes : TEX_DEF_PASSES;
    timeout_ms = opts->timeout_ms > 0 ? opts->timeout_ms : TEX_DEF_TIMEOUT_MS;

    /* Working directory: the caller's, or a unique one under /tmp. */
    own_dir = opts->workdir == NULL;
    if (own_dir)
    {
        if (tapi_file_make_custom_pathname(&dir, "/tmp", "-tex") == NULL)
        {
            rc = TE_RC(TE_TAPI, TE_ENOMEM);
            goto out;
        }
    }
    else
    {
        te_string_append(&dir, "%s", opts->workdir);
    }

    /* Make sure the directory exists on the agent. */
    tex_arg(&mkdir_args, "-p");
    tex_arg(&mkdir_args, "%s", dir.ptr);
    rc = tex_run(factory, "mkdir", "mkdir", &mkdir_args, NULL, TEX_AVAIL_TIMEOUT,
                 NULL, NULL, &exit_code);
    te_vec_deep_free(&mkdir_args);
    if (rc != 0)
        goto out;
    if (exit_code != 0)
    {
        ERROR("Failed to create working dir %s on %s", dir.ptr, ta);
        rc = TE_RC(TE_TAPI, TE_EFAIL);
        goto out;
    }

    /* Write the source into the working directory. */
    {
        te_string texpath = TE_STRING_INIT;

        te_string_append(&texpath, "%s/%s", dir.ptr, TEX_TEX_NAME);
        rc = tapi_file_create_ta(ta, texpath.ptr, "%s", tex);
        te_string_free(&texpath);
        if (rc != 0)
            goto out;
    }

    /* Compile the requested number of passes; keep the last log. */
    for (result->passes_run = 0; result->passes_run < passes; )
    {
        bool last = result->passes_run + 1 == passes;

        if (last)
            te_string_reset(&result->log);

        rc = tex_one_pass(factory, engine, dir.ptr, timeout_ms,
                          last ? &result->log : NULL, &exit_code);
        result->passes_run++;
        if (rc != 0)
            goto out;

        /* A LaTeX error stops the run early; it is a result, not a
         * transport failure, so report it with ok=false. */
        if (exit_code != 0)
        {
            if (last)
                break;
            /* Re-run the final pass to capture its log for the verdict. */
            te_string_reset(&result->log);
            rc = tex_one_pass(factory, engine, dir.ptr, timeout_ms,
                              &result->log, &exit_code);
            if (rc != 0)
                goto out;
            break;
        }
    }

    result->ok = exit_code == 0;

    if (result->ok)
    {
        te_string pdfpath = TE_STRING_INIT;

        te_string_append(&pdfpath, "%s/%s", dir.ptr, TEX_PDF_NAME);
        result->pdf_ta_path = TE_STRDUP(pdfpath.ptr);
        te_string_free(&pdfpath);

        if (opts->copy_pdf_to != NULL)
        {
            te_errno crc = tapi_file_copy_ta(ta, result->pdf_ta_path,
                                             NULL, opts->copy_pdf_to);
            if (crc == 0)
                result->pdf_local_path = TE_STRDUP(opts->copy_pdf_to);
            else
                WARN("Could not copy PDF to engine path %s: %r",
                     opts->copy_pdf_to, crc);
        }
    }

    rc = 0;

out:
    /* Remove only a directory we created ourselves. */
    if (own_dir && dir.ptr != NULL && !opts->keep_workdir)
    {
        te_vec rm_args = TE_VEC_INIT(char *);

        tex_arg(&rm_args, "-rf");
        tex_arg(&rm_args, "%s", dir.ptr);
        (void)tex_run(factory, "rm", "rm", &rm_args, NULL, TEX_AVAIL_TIMEOUT,
                      NULL, NULL, NULL);
        te_vec_deep_free(&rm_args);
    }
    te_string_free(&dir);

    if (rc != 0)
        tapi_tex_build_result_free(result);

    return rc;
}

/* See description in tapi_tex_build.h */
te_errno
tapi_tex_doc_compile(tapi_job_factory_t *factory, const tapi_tex_doc *doc,
                     const tapi_tex_build_opts *opts,
                     tapi_tex_build_result *result)
{
    te_string tex = TE_STRING_INIT;
    te_errno rc;

    tapi_tex_render(doc, &tex);
    rc = tapi_tex_compile(factory, tex.ptr != NULL ? tex.ptr : "", opts, result);
    te_string_free(&tex);

    return rc;
}
