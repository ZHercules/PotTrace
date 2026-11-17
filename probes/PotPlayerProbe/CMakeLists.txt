cmake_minimum_required(VERSION 3.20)
project(PotTraceProbe LANGUAGES CXX)
add_library(PotTraceProbe SHARED ProbeBridge.cpp)
target_compile_features(PotTraceProbe PRIVATE cxx_std_20)
