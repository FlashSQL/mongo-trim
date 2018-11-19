# mongo-trim
Optimize MongoDB with TRIM command (published paper in EDB 2016)

Build from source code:

First, make the "build" directory in your MONGO_HOME 

$ mkdir build

Next, to build only the server:

$ scons mongod -j40 

To build all core components

$ scons core -j40

See docs/building.md for more detail
