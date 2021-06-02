#
# Copyright 2021 Xilinx Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.5)

project(defect-detect)

SET(CMAKE_INSTALL_PREFIX $ENV{OECORE_TARGET_SYSROOT})

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-narrowing -fpermissive -rdynamic -Wl,--no-undefined" )
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -std=c++14")
SET(CMAKE_CXX_FLAGS_DEBUG "-O0")
SET(CMAKE_C_FLAGS_DEBUG "-O0")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(GStreamer REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS opencv_core opencv_video opencv_videoio opencv_imgproc opencv_imgcodecs opencv_highgui)

SET(INSTALL_PATH "opt/xilinx")

add_library(ivas_cca SHARED src/ivas_cca.c)
target_include_directories(ivas_cca PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(ivas_cca
  jansson ivasutil gstivasinfermeta-1.0)
install(TARGETS ivas_cca DESTINATION ${INSTALL_PATH}/lib)

add_library(ivas_otsu SHARED src/ivas_otsu.c)
target_include_directories(ivas_otsu PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(ivas_otsu
  jansson ivasutil gstivasinfermeta-1.0)
install(TARGETS ivas_otsu DESTINATION ${INSTALL_PATH}/lib)

add_library(ivas_text2overlay SHARED src/ivas_text2overlay.cpp src/ivas_text2overlay.hpp)
target_include_directories(ivas_text2overlay PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(ivas_text2overlay
  gstreamer-1.0 gstbase-1.0 gobject-2.0 glib-2.0 gstvideo-1.0 gstallocators-1.0
  gstivasinfermeta-1.0 jansson ivasutil ${OpenCV_LIBS} glog)
install(TARGETS ivas_text2overlay DESTINATION ${INSTALL_PATH}/lib)

add_library(ivas_preprocess SHARED src/ivas_preprocess.c)
target_include_directories(ivas_preprocess PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(ivas_preprocess
  jansson ivasutil gstivasinfermeta-1.0)
install(TARGETS ivas_preprocess DESTINATION ${INSTALL_PATH}/lib)

add_executable(defect-detect src/main.cpp)
target_include_directories(defect-detect PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(defect-detect
  gstapp-1.0 gstreamer-1.0 gstbase-1.0 gobject-2.0 glib-2.0 gstvideo-1.0 gstallocators-1.0 glib-2.0 gobject-2.0 )
install(TARGETS defect-detect DESTINATION ${INSTALL_PATH}/bin)

install(FILES
    README
    DESTINATION ${INSTALL_PATH}/
    RENAME      README_DEFECT_DETECT
    )

install(PROGRAMS
    script/01.file-defect-detect-display.sh
    DESTINATION ${INSTALL_PATH}/bin)

install(FILES
    config/otsu-accelarator.json
    config/text2overlay.json
    config/cca-accelarator.json
    config/preprocess-accelarator.json
    DESTINATION ${INSTALL_PATH}/share/ivas/defect-detect)

install(DIRECTORY
    notebook/
    DESTINATION ${INSTALL_PATH}/share/notebooks/defect-detect)

set(VERSION "1.0.1")
set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "DefectDetect")
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
set(CPACK_RPM_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
include(CPack)
