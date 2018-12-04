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

### Hardware requirements
+ Samsung 840 Pro - With the modified firmware supports Multi-streamed technique. OR
+ Samsung [PM953 2.5" NVMe PCIe](https://www.samsung.com/us/dell/pdfs/samsung_flyer_PM953_v2.pdf)

### Using Macros:

`TDN_TRIM4`: the TRIM command optimization. Save replaced ranges in the inner data structure. When the number of discard ranges reaches a threshold, invoke the __trim_ranges() thread.

`TDN_TRIM4_2`: similar with `TDN_TRIM4`, differences is saving discard ranges instead of replaced ranges
