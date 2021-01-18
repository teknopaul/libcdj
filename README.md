# libcdj

![xdj-1000 player](doc/xdj-trans.png)  <->  ![linux](doc/tux.png)  <->  ![xdj-1000 player](doc/xdj-trans.png) 

Binaries and native libraries for communicating with Pioneer CDJs and XDJs.

This is based on protocol analysis by [Deep-Symmetry](https://github.com/Deep-Symmetry/) who has discovered and collated most  of what is known by the opensource community about Pioneers proprietary ProDJLink protocol.


N.B for tools written in python head over to [python-prodj-link](https://github.com/flesniak/python-prodj-link)


## Components

- `libcdj` - common lib for handling messages and extracting data that is known from within the protocol  
  e.g. taking a beat packet and extracting the bpm.
- `libvdj` - common lib to create virtual CDJs that partake in a ProLink network.  
  This handles internally network connections, discovery, keep-alives and tracking the backline, i.e. all the known CDJs, VDJs and rekordbox instances on the network.
- `vdj` - cli app that uses `libvdj`
- `vdj-debug` - tool to dump ProLink messages
- `cdj-mon` - monitor UDP broadcasts on a ProLink network

## Build on Ubuntu

output is created in a `target/` directory

    make
    make test
    make deb

Plus hopefully familiar `sudo make install` `sudo make uninstall`

Dependeds on standard C libs in `build-essential`

Asl requires your suystem to have

    #include <pthread.h>
    #include <stdatomic.h>

## Usage

CLI tools have a `-h` flag with instructions.


## Documentation

Currently the `.c` files are commented more than the `.h`, the code in `cdj-mon` and `vdj-debug` or hit me up on github if you need help.

