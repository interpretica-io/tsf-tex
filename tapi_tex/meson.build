# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica Unipessoal Lda
#
# External TE library (see TE_EXT_REPO): engine-side builder for
# XeLaTeX reports. It assembles a .tex document from a small content
# model and compiles it to PDF by running xelatex on an agent through
# tapi_devtool_run. Built as a shared library on the engine platform
# only, like tapi_ai: it calls tapi and tapi_job, engine libraries with
# no static variant, so a static build of it would fail the configure.

if get_option('engine')
    build_lib_shared = true
endif
build_lib_static = false
install_lib = install_dev

headers += files(
    'tapi_tex.h',
    'tapi_tex_build.h',
)

sources += files(
    'tapi_tex.c',
    'tapi_tex_build.c',
)

te_libs += [
    'tapi',
    'tapi_job',
    'tapi_devtool',
]
