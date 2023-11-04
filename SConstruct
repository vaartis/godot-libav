#!/usr/bin/env python

import os

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

env.Append(LIBS=["libavcodec", "libavformat", "libavutil", "avfilter", "avdevice", "avutil", "swresample", "swscale"])

library = env.SharedLibrary(
    library_filename,
    source=sources
)

def copy_bin_to_projectdir(target, source, env):
    import shutil
    shutil.copyfile(library_filename, "demo/" + library_filename)

copy = env.Command("demo/" + library_filename, library_filename, copy_bin_to_projectdir)
Default([library, compiledb, copy])
