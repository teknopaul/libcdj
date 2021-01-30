# libcdj

![xdj-1000 player](doc/xdj-trans.png)  <->  ![linux](doc/tux.png)  <->  ![xdj-1000 player](doc/xdj-trans.png) 

Binaries and native libraries for communicating with Pioneer CDJs and XDJs.

This is based on protocol analysis by [Deep-Symmetry](https://github.com/Deep-Symmetry/) who has discovered and collated most  of what is known by the opensource community about Pioneers proprietary ProDJLink protocol.


N.B for tools written in python head over to [python-prodj-link](https://github.com/flesniak/python-prodj-link)

This project is written in C and seems to compile ok on a [raspbery pi](https://www.raspberrypi.org/), hopefully that makes it interesting for embedded projects.

I intend to get libcdj's sister project [adj](https://github.com/teknopaul/adj) running headless on an rpi.


## Components

- `libcdj` - common lib for handling messages and extracting data that is known from within the protocol  
  e.g. taking a beat packet and extracting the bpm.
- `libvdj` - common lib to create virtual CDJs that partake in a ProLink network.  
  This handle network connections, discovery, keep-alives and tracking link members in the backline, i.e. all the known CDJs, VDJs and rekordbox instances on the network.

- `vdj` - cli app that uses `libvdj`
- `vdj-debug` - tool to dump ProLink messages
- `cdj-mon` - monitor UDP broadcasts on a ProLink network
- `vdj-mon` - monitor that acts as a Vitual DJ player


## Build on Ubuntu

output is created in a `target/` directory.

    make
    make test
    sudo make deb

Plus familiar `sudo make install` `sudo make uninstall`, these install the cli apps, the `.so` shared library, the `.a` for static binding, and development headers in `/usr/include/cdj`

Depends on standard C libs in `build-essential`

Also requires your system to have

    #include <pthread.h>
    #include <stdatomic.h>


## Usage

CLI tools have a `-h` flag with instructions.

Too many cool tricks are possible with this lib to list them all here, but to whet your appetite, if you have the (cheaper) XDJ players that only report BPM to one decimal place, these tools allow you to get full resolution, e.g. 120.00.  Beat sync is millisecond accurate.

Naturally you can also sync music software running on your PC to decks, and decks to software on your PC.

Alsa DJ is a tool that allows syncing midi equipment to CDJs, and allows the DJ to manually control the sync as well.

![alsa dj ui](doc/screen-ani.gif)


## Developer Docs

This project provides some useful cli apps, but its most useful as a development library for integrating hooks to the CDJ ProLink network to existing music applications.  e.g. I'm looking to integrate `libvdj` into LMMS and/or Hydrogen to create Ableton Live type apps.

Currently the `.c` files are commented more than the `.h`, the code in `cdj-mon` and `vdj-mon` serve as exmaples, or hit me up on github if you need help.

You are advised to read at least the introduciton to [deepsymmetry packet analysis](https://djl-analysis.deepsymmetry.org/djl-analysis/packets.html) to understand the protocol prior to starting any serious hacking.  The rest of this page makes little sense if you don't understand the basics of the ProLink protocol, it is not complicated presuming you are a C developer.

### libcdj

`libcdj` contains functions for interpreting CDJ messages and writing them.

#### Reading messages

You need to understand which port a message came on to be able to interpret it, methods that take an int port function expect CDJ_DISCOVERY_PORT, CDJ_BROADCAST_PORT or CDJ_UPDATE_PORT.

A UDP packet arrives with just the binary data and length

    unsigned char* packet - binary data
    uint16_t length - length of the UDP packet

To process the message a struct should be created with some of the common data already parsed.

    cdj_discovery_packet_t* d_pkt = cdj_new_discovery_packet(packet, length);
    if (d_pkt) {
        // either read the d_pkt struct, e.g. player_id
        // or call cdj_discovery_*(d_pkt) methods to extract data you want
        // d_pkt->type defines which type of message it was,e.g. CDJ_KEEP_ALIVE 0x06
        // check cdh.h structs to see what else is automatically parsed from the message
        free(d_pkt);
    }

To understand what each message does, check DeepSymetry's packet analysis that describes each message in all the detail we know.

#### Writing messages

Create a packet with one of the `cdj_create_*_packet` functions.
These all take a pointer to a `uint16_t` as the first arg, into this the length of the generated packet will be set, and a list of args that are needed for that type of message.

e.g.

    uint16_t len;
    unsigned char* packet = cdj_create_initial_discovery_packet(&len, 'C');

Messages are sent via UDP to the network.

e.g.

    sendto(sock, packet, len, 0, (struct sockaddr*) &dest, sizeof(struct sockaddr_in));

The function `cdj_print_packet()` prints a packet to stdout for inspection in a format compatible with DeepSymetry's docs.

N.B. `libvdj` provides sockets and a higher level api. If you want to do your own socket handling and threading, you can just use functions from `libcdj`.

N.B. ProLink packets represent ip address and macs in a different manner to typical INET code, `libcdj` has some conversion helper methods.

`libcdj` also has method to convert pitch and bpm as it is represented in the protocol to simpler formats.




### libvdj

`libvdj` uses `libcdj` to create a virtual cdj player in software.  There are different ways to utilize this library. 
1. You can use the data structures and socket handling alone and process all messages yourself by implementing message handlers and calling the `vdj_sendto_*` methods.
2. Use `libvdj`'s `managed_*` functions that handle joining the network informing other CDJ's of your presence and monitoring for status changes and beats from the other CDJs.  The `_managed_` functions also allow you to process the messages as well  or handle higher level events such a beat, change of sync master,or new CDJ being plugged in.
3. A higher level API that abstracts all of the CDJ messaging and networking.


#### Overview

Api functions are prefixed `vdj_`  
The principal program handle is a pointer to `vdj_t`, this represents a virtual CDJ, its network sockets and state.  
`libvdc` and `libcdj` Names used are slightly different from DeepSymetry, some names are based on looking at rekordbox's internal strings.  
The term `backline` refers to all network connected DJ devices.  
`managed` threads are threads that handle the ProLink protocol.

To initialize a VDJ, call the init method supplying the name of the network interface you wish to use e.g. `eth0`.

    vdj_t* vdj_init_iface(char* interface, unsigned int flags);

Supplying NULL for the interface causes vdj to scan the NICs looking to see if there is only one physical non-ethernet card, if there is it uses that.

This method returns a pointer to a struct that is used throughout the `libvdj` api (or NULL).
The flags bitmask should contain the player id you wish to use ored with other options. For example to mimic a CDJ with auto-assigned player_id on the default NIC initialize as follows...
  
    v = vdj_init(VDJ_FLAG_AUTO_ID | VDJ_FLAG_DEV_CDJ);

N.B. you need to be player numbered 1 to 4 usually, unless you are just snooping (only listen to  broadcast ports) or wish to mimic a DJM.

To send a message use.

    int vdj_discovery(vdj_t* v, unsigned char* packet, uint16_t length);
    int vdj_broadcast(vdj_t* v, unsigned char* packet, uint16_t length);
    int vdj_update(vdj_t* v, struct sockaddr_in* dest, unsigned char* packet, uint16_t length);


To have `libvdj` manage sockets for you call either `vdj_open_sockets(v)` which creates sockets for writing and reading the two broadcast ports and reading the two unicast ports.

    int vdj_open_sockets(vdj_t* v);

N.B. you can only have one full VDJ per interface. If you only want to open just the broadcast sockets use `vdj_open_broadcast_sockets` which provides monitoring and supports multiple VDJs on the same computer.

    int vdj_open_broadcast_sockets(vdj_t* v);

Typically a VDJ will then inform other CDJs of its presence and start a keep-alive thread to join the network.  `libvdj` provides methods to do that.

    vdj_exec_discovery(v);      // requires unicast sockets, take a few seconds to return
    vdj_init_keepalive_thread(v);


If you wish to behave like a CDJ, you will need to find all the other CDJs on the network, store their IP addresses and send them unicast status updates every 200ms.

`libvdj` provides methods to do this that will manage the `v->backline` struct's data for you by interpreting messages on the network.

    vdj_init_managed_discovery_thread(v, NULL); // recv broadcast on :50000
    vdj_init_managed_update_thread(v, NULL);    // recv unicast on :50002
    vdj_init_status_thread(v);                  // sendto unicast on :50002

You can do the discovery yourself to fine tune it. Or you can do a combination of managed operations and your own handlers, the second argument to `vdj_init__managed_*` method is a function pointer to a callback executed whenever a message is received, its called after `libvdj` has updated the `v->backline` data.

[cdj_mon.c](https://github.com/teknopaul/libcdj/blob/master/src/c/cdj_mon.c) provides a full example of snooping.

[vdj_mon.c](https://github.com/teknopaul/libcdj/blob/master/src/c/vdj_mon.c) provides a full example of acting as a virtual CDJ.


Be warned in 2021 this API is not stable since this is quite experimental.

Patches welcome.



















