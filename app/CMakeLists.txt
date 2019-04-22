
cmake_minimum_required(VERSION 3.4.1)

add_library( # Sets the name of the library.
             native-lib
             # Sets the library as a shared library.
             SHARED
             # Provides a relative path to your source file(s).
             src/main/cpp/native-lib.cpp )

find_library( # Sets the name of the path variable.
              log-lib
              # Specifies the name of the NDK library that
              # you want CMake to locate.
              log )
find_library( # Sets the name of the path variable.
              android-lib
              # Specifies the name of the NDK library that
              # you want CMake to locate.
              android )
set(distribution_DIR ../../../../libs)
add_library( avcodec
             SHARED
             IMPORTED)
set_target_properties( avcodec
    PROPERTIES IMPORTED_LOCATION
     ${distribution_DIR}/${ANDROID_ABI}/libavcodec-56.so)
add_library( avdevice
      SHARED
      IMPORTED)
set_target_properties( avdevice
              PROPERTIES IMPORTED_LOCATION
             ${distribution_DIR}/${ANDROID_ABI}/libavdevice-56.so)
add_library( avfilter
         SHARED
         IMPORTED)
set_target_properties( avfilter
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libavfilter-5.so)

add_library( avformat
         SHARED
         IMPORTED)
set_target_properties( avformat
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libavformat-56.so)
add_library( avutil
         SHARED
         IMPORTED)
set_target_properties( avutil
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libavutil-54.so)
add_library( postproc
         SHARED
         IMPORTED)
set_target_properties( postproc
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libpostproc-53.so)
add_library( swresample
         SHARED
         IMPORTED)
set_target_properties( swresample
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libswresample-1.so)
add_library( swscale
         SHARED
         IMPORTED)
set_target_properties( swscale
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libswscale-3.so)
add_library( yuv
         SHARED
         IMPORTED)
set_target_properties( yuv
         PROPERTIES IMPORTED_LOCATION
         ${distribution_DIR}/${ANDROID_ABI}/libyuv.so)
include_directories(src/main/cpp/include/ffmpeg)
include_directories(src/main/cpp/include/libyuv)
target_link_libraries( # Specifies the target library.
                       native-lib
                       avcodec
                       avdevice
                       avfilter
                       avformat
                       avutil
                       postproc
                       swresample
                       swscale
                       yuv
                       ${log-lib}
                       ${android-lib})