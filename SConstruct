#!/usr/bin/env python

import os
from subprocess import check_call
import sys


# Gets the standard flags CC, CCX, etc.
env = SConscript("godot-cpp/SConstruct")

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=['src/'])
env.VariantDir('build', 'src', duplicate=0)
sources = Glob('build/*.cpp')

env.Tool("compilation_db")

compiledb = env.CompilationDatabase()
env.Alias("compiledb", compiledb)

bin_prefix = "bin/" + env["platform"]
if env["platform"] == "macos":
    library_filename = "{}/godot-libav.{}.{}.framework/libgodot-libav.{}.{}".format(
        bin_prefix, env["platform"], env["target"], env["platform"], env["target"]
    )
else:
    library_filename = "{}/libgodot-libav{}{}".format(bin_prefix, env["suffix"], env["SHLIBSUFFIX"])

def config_ffmpeg(target, source, env):
    check_call(
        [
            "./configure", r"--extra-ldsoflags=-Wl,-rpath='\$\$\$\$ORIGIN'", "--prefix=../build/ffmpeg/", "--disable-programs", "--disable-avfilter",
            "--disable-avdevice", "--disable-autodetect", "--disable-encoders", "--disable-protocols", "--disable-protocol=http", "--disable-protocol=rtp",
            "--disable-static", "--enable-shared", "--disable-bsfs", "--disable-iconv", "--enable-protocol=file"
        ],
        stdout=sys.stdout,
        cwd="FFmpeg"
    )

def make_ffmpeg(target, source, env):
    jobs = GetOption("num_jobs")

    check_call(f"make --trace -j{jobs} install", stdout=sys.stdout, shell=True, cwd="FFmpeg")

av_libs = []
for lib in ["avcodec", "avformat", "avutil", "swresample", "swscale"]:
    av_libs.append(File(f"build/ffmpeg//lib/lib{lib}{env['SHLIBSUFFIX']}"))

configure_ffmpeg = env.Command("FFMpeg/ffbuild/config.mak", [], config_ffmpeg)
build_ffmpeg = env.Command(av_libs, "FFMpeg/ffbuild/config.mak", make_ffmpeg)

env.Append(LIBPATH=["build/ffmpeg/lib"])
env.Append(CPPPATH=["build/ffmpeg/include"])

env.Append(LIBS=av_libs)

library = env.SharedLibrary(
    library_filename,
    source=sources
)

def copy_bin_to_projectdir(target, source, env):
    import shutil
    shutil.copyfile(library_filename, "demo/" + library_filename)

copy = env.Command("demo/" + library_filename, library_filename, copy_bin_to_projectdir)


Default([library, compiledb, copy])
