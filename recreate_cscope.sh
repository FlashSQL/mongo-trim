#!/bin/bash
#This script build cscope files and build ctags Library
MONGO_HOME=/home/vldb/mongo-pmem

cd $MONGO_HOME
find -name "*.h" -o -name "*.hpp" -o -name "*.i" -o -name "*.ic" -o -name "*.in" -o -name "*.c" -o -name "*.cc" -o -name "*.cpp" > cscope.files
cscope -b -q -k
ctags -L cscope.files
