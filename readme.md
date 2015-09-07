### reki

reki is a bittorrent tracker that uses [libuv][libuv] for IO and
[Redis][redis] for persistent storage.

#### Building

Building reki is a terribly involved process. It requires the following:

1. A POSIX OS environment.
1. A C99 capable compiler.
1. GNUMake compatible make.

To build reki, you must carefully observe the following arcane ritual:

1. Acquire a copy of the source code, perhaps with `git clone https://github.com/torque/reki.git`
1. Run `make` from within the source dir.
1. That's it! Really! Unless it didn't work.

[libuv]: https://github.com/libuv/libuv
[redis]: https://github.com/antirez/redis
