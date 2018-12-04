#!/bin/bash

MONGO_HOME=/home/vldb/mongo-trim

IS_DEBUG=0
#IS_DEBUG=1

BUILD_NAME=
#BUILD_FLAGS="-Imongo/db/pmem/ -DUNIV_PMEMOBJ_BUF"
#BUILD_FLAGS="-Ithird_party/wiredtiger/src/pmem/"
#BUILD_FLAGS="-Ithird_party/wiredtiger/src/pmem/ -DUNIV_PMEMOBJ_BUF -DUNIV_PMEMOBJ_BUF_FLUSHER -DUNIV_PMEMOBJ_BUF_PARTITION"

cd $MONGO_HOME

#echo ${BUILD_FLAGS}
#python2 buildscripts/scons.py mongod --dbg=on --opt=off -j 40 LIBS='pmem pmemobj' CXXFLAGS='-g' --prefix=${MONGO_HOME}

if [ $IS_DEBUG -eq 0 ]; then
#python2 buildscripts/scons.py mongod -j 40  CXXFLAGS='-g' --prefix=${MONGO_HOME}
scons mongod -j 40  CXXFLAGS='-Wno-maybe-uninitialized -Wno-unused-variable' --prefix=${MONGO_HOME}

cp build/opt/mongo/mongod .
cp build/opt/mongo/mongo .
cp build/opt/mongo/mongos .
else
echo "Build the Debug mode"
#python2 buildscripts/scons.py mongod --dbg=on --opt=off -j 40  CXXFLAGS='-g' --prefix=${MONGO_HOME}
scons mongod --dbg=on --opt=off -j 40  --prefix=${MONGO_HOME}

cp build/debug/mongo/mongod .
cp build/debug/mongo/mongo .
cp build/debug/mongo/mongos .
fi
