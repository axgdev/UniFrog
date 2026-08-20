#!/bin/sh
set -eu

source_root=${CORE_SOURCE_ROOT:?CORE_SOURCE_ROOT is required}
build_root=${BUILD_ROOT:?BUILD_ROOT is required}
output_root=${OUTPUT_ROOT:?OUTPUT_ROOT is required}
core_ids=${CORE_IDS:?CORE_IDS is required}
jobs=${JOBS:-1}
host_cc=${HOST_CC:-cc}
host_cxx=${HOST_CXX:-c++}
host_nm=${HOST_NM:-nm}
verbose=${VERBOSE:-0}

validate_core()
{
   core=$1

   for symbol in retro_set_environment retro_set_video_refresh \
      retro_set_audio_sample retro_set_audio_sample_batch \
      retro_set_input_poll retro_set_input_state retro_init retro_deinit \
      retro_api_version retro_get_system_info retro_get_system_av_info \
      retro_set_controller_port_device retro_run retro_unload_game \
      retro_load_game retro_get_region; do
      if ! "$host_nm" -D "$core" 2>/dev/null |
         awk '{ print $3 }' | grep -qx "$symbol"; then
         echo "error: $core does not export required symbol $symbol" >&2
         return 1
      fi
   done
}

stage_core()
{
   id=$1
   source=$2
   destination="$build_root/$id"

   rm -rf "$destination"
   mkdir -p "$destination"
   tar -C "$source" \
      --exclude=.git --exclude='*.o' --exclude='*.a' \
      --exclude='*.so' --exclude='*.d' -cf - . |
      tar -C "$destination" -xf -
}

build_core()
{
   id=$1
   source_dir=$2
   makefile=$3
   built_path=$4
   installed_name=$5
   shift 5
   make_dir=$(dirname "$makefile")
   make_name=$(basename "$makefile")

   output="$output_root/$installed_name"
   if test -s "$output"; then
      validate_core "$output"
      printf '  CORE    %-20s cached\n' "$id"
      return
   fi
   source="$source_root/$source_dir"
   if test ! -d "$source"; then
      echo "error: missing Linux core source: $source" >&2
      echo "run 'make deps' to restore core dependencies" >&2
      exit 1
   fi
   printf '  CORE    %-20s native\n' "$id"
   stage_core "$id" "$source"
   log="$build_root/$id/build.log"
   if test "$verbose" = 1; then
      MAKEFLAGS= make -C "$build_root/$id/$make_dir" -f "$make_name" \
         -j"$jobs" CC="$host_cc" CXX="$host_cxx" "$@"
   elif ! MAKEFLAGS= make -C "$build_root/$id/$make_dir" -f "$make_name" \
      -j"$jobs" CC="$host_cc" CXX="$host_cxx" "$@" >"$log" 2>&1; then
      cat "$log" >&2
      exit 1
   fi
   test -s "$build_root/$id/$built_path"
   cp "$build_root/$id/$built_path" "$output"
   validate_core "$output"
}

mkdir -p "$build_root" "$output_root"
for id in $core_ids; do
   case "$id" in
   gambatte)
      build_core "$id" gambatte-libretro Makefile.libretro \
         gambatte_libretro.so gambatte_libretro.so platform=unix
      ;;
   gpsp)
      build_core "$id" gpsp-libretro Makefile gpsp_libretro.so \
         gpsp_libretro.so platform=unix
      ;;
   snes9x2005)
      build_core "$id" snes9x2005 Makefile snes9x2005_libretro.so \
         snes9x2005_libretro.so platform=unix
      ;;
   snes9x2002)
      build_core "$id" snes9x2002 Makefile snes9x2002_libretro.so \
         snes9x2002_libretro.so platform=unix
      ;;
   quicknes)
      build_core "$id" QuickNES_Core Makefile quicknes_libretro.so \
         quicknes_libretro.so platform=unix
      ;;
   fceumm)
      build_core "$id" libretro-fceumm Makefile.libretro \
         fceumm_libretro.so fceumm_libretro.so platform=unix
      ;;
   gearboy)
      build_core "$id" Gearboy platforms/libretro/Makefile \
         platforms/libretro/gearboy_libretro.so gearboy_libretro.so \
         platform=unix
      ;;
   pce-fast)
      build_core "$id" beetle-pce-fast-libretro Makefile \
         mednafen_pce_fast_libretro.so pce-fast_libretro.so \
         platform=unix HAVE_CHD=0
      ;;
   *)
      echo "error: unsupported native Linux core id: $id" >&2
      echo "supported: gambatte gpsp snes9x2005 snes9x2002 quicknes fceumm gearboy pce-fast" >&2
      exit 2
      ;;
   esac
done
