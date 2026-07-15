cmake_minimum_required(VERSION 2.8.12)
project(resampler C)

add_library(resampler STATIC
    ../../speex_resampler/resample.c
)

target_include_directories(resampler PRIVATE
    ../../include
)

target_compile_options(resampler PRIVATE
    -fvisibility=hidden
)
