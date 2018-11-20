# mongo-trim
## Optimize MongoDB with TRIM command (published paper in [EDB 2016](https://dl.acm.org/citation.cfm?doid=3007818.3007844))

Author: Trong-Dat Nguyen (trauconnguyen@gmail.com)

### Build from source code:

First, make the "build" directory in your MONGO_HOME 

```
$ mkdir build
```

Next, to build only the server:

```
$ scons mongod -j40 
```

To build all core components

```
$ scons core -j40
```

See [docs/building.md](docs/building.md) for more detail
