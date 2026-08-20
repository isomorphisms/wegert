cmake_minimum_required(VERSION 3.22.1)

project(wegert C)

add_library(
    native_app_glue
    STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
)

target_include_directories(
    native_app_glue
    PUBLIC
    "${ANDROID_NDK}/sources/android/native_app_glue"
)

add_library(
    wegert
    SHARED
    main.c
)

target_link_options(
    wegert
    PRIVATE
    "-Wl,-u,ANativeActivity_onCreate"
)

target_link_libraries(
    wegert
    PRIVATE
    native_app_glue
    android
    log
    EGL
    GLESv3
    m
)
