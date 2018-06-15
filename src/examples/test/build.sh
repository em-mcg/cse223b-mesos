#! /bin/bash
g++ bg_framework.cpp -std=c++11 -I.. -I../.. -I../../../include/ -I../../../build/include/ -I../../../build/src-o bg-framework -lmesos
g++ measure_framework.cpp -std=c++11 -I.. -I../.. -I../../../include/ -I../../../build/include/ -I../../../build/src -o measure-framework -lmesos
