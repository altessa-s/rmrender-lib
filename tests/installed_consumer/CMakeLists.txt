# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
cmake_minimum_required(VERSION 3.21)
project(rmrender_installed_consumer LANGUAGES C)
find_package(RMRender CONFIG REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE RMRender::cairo_document RMRender::cairo_marks)
