# tsf-tex

Generating a good-looking XeLaTeX document and compiling it to PDF from
a Test Agent, packaged as an external Test Environment (TE) repository
(consumed with the `TE_EXT_REPO` builder directive). It is meant for
**test reports**: a run assembles a report with a small content model
and compiles it with `xelatex` on the agent — **nothing is spawned on
the engine**, the LaTeX engine runs where the agent is.

One library:

- `tapi_tex` — engine side. Two headers:
  - `tapi_tex.h` — the **document model**: start a document with a
    styling (`tapi_tex_doc_init`), append sections, paragraphs,
    key/value and general tables (`booktabs` `longtable`), bullet and
    numbered lists, verbatim code blocks, and images, then render the
    whole thing to a `.tex` string (`tapi_tex_render`). This half is
    pure engine-side text building — no agent, no process — and takes
    care of escaping arbitrary text (`tapi_tex_escape`).
  - `tapi_tex_build.h` — **compiling** a `.tex` (or a built document) to
    PDF on an agent behind a `tapi_job_factory_t` via
    `tapi_devtool_run`: write the source to a working directory on the
    agent, run the engine the requested number of passes (two by
    default, so a table of contents and cross-references settle), and
    report whether a PDF came out, keeping the final pass's log for a
    verdict. `tapi_tex_available()` says whether the agent has a LaTeX
    engine at all, for a clean `TEST_SKIP`.

## Why XeLaTeX

XeLaTeX reads the `.tex` as UTF-8 and loads fonts by name through
`fontspec`, so a report with real Unicode and a chosen typeface compiles
without the font setup `pdflatex` needs. `lualatex` works the same way
and is accepted as the engine too (`tapi_tex_build_opts.engine`);
`pdflatex` does not and is not the default.

The built-in preamble aims for a report that looks finished out of the
box: `geometry` margins, an accent colour, `fontspec` fonts (TeX Gyre by
default, which ship with TeX Live), `titlesec` headings in the accent
colour, a `fancyhdr` header/footer, `booktabs` tables, `fancyvrb` code
blocks with line breaking, `microtype`, and `hyperref` links. Override
the fonts, colour, geometry, class and title through `tapi_tex_opts`, or
add packages and macros with `tapi_tex_use()`.

## Example

```c
tapi_tex_opts opts = TAPI_TEX_OPTS_INIT;
tapi_tex_doc doc;
tapi_tex_build_result res;

opts.title = "Nightly assurance report";
opts.author = "Test Environment";
opts.toc = true;
tapi_tex_doc_init(&doc, &opts);

tapi_tex_section(&doc, 0, "Summary");
tapi_tex_paragraph(&doc, "All %u checks passed on %s.", n_checks, host);
tapi_tex_keyval(&doc, "Run parameters", &kv);

if (!tapi_tex_available(factory, NULL, 10000))
    TEST_SKIP("no LaTeX engine on the agent");

CHECK_RC(tapi_tex_doc_compile(factory, &doc, NULL, &res));
if (!res.ok)
    TEST_VERDICT("xelatex failed:\n%s", res.log.ptr);
RING("report compiled: %s", res.pdf_ta_path);

tapi_tex_build_result_free(&res);
tapi_tex_doc_free(&doc);
```

## On the agent

The agent needs a TeX distribution with the XeLaTeX engine and the
packages above — on Debian/Ubuntu that is `texlive-xetex`,
`texlive-latex-recommended`, `texlive-fonts-recommended` and
`texlive-latex-extra` (for `fancyvrb`/`enumitem`/`titlesec`). Where it is
missing, `tapi_tex_available()` returns `false` so a test skips rather
than fails.

## Build

Consumed as a `TE_EXT_REPO`. Declare it in a suite's `conf/external.yml`
and bind `tapi_tex` on the engine platform; it builds on `tsf-devtool`
(`tapi_devtool`), so declare that too. See the suite `tsf-tex-ts`.

## License

MIT — see [LICENSE](LICENSE).
