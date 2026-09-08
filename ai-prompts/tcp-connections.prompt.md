According to this docuemnt https://djl-analysis.deepsymmetry.org/djl-analysis/missing.html
and this packate analysis  `/home/teknopaul/bzr_workspace/adj/doc/LinkInfo-tcp.pcapng`

Modern XDJs open TCP connections, and the protocol has not been reverse engineered yet.

I beleive from looking at the packets its a simple binary stream of length+value bytes where each value is a message.
I suspect message format will be similar to data sent over UDP.

When connecting with `adj` to pysical XDJs, the XDJs report on screen "connected to a deck with older firmware" and I suspect this maybe because 
the TCP part is not implemented.  Clearly TCP is more reliable and messages aare not limeted to UDP max packet size, so it stands to reason
that a more modern protocol might use TCP to send similar information, but support longer track names and more details.

To debug this we can look at the pcap file to get a head start, but further analysis will require starting ADJ,
connecting to the network, and performing player ID negitiation then starting the expected TCP sockets and dumping the binary messages for analysis.

Hopefully we can reverse engineer the messages within the protocol by reading messages without the other decks expecting us to write more than is covered by the pcap dump.

Since the TCP protocol is implemented in the same binary and OS as the UDP protocol we can probably make some guesses about how data is represented,
since its likley that the same structs are used for both protocols, and its likely that String representations use similar encoders to how the database format
represents strings. It is also likely that the TCP protocol sends the same type of content but probably is used for String data that might be longer than fits in
a UDP packet.  Track name data is this class of information, users control the track names not the rekordbox software.
Since it is a streaming protocol we can also imagine that it is not used for much realtime timing information since the TCP stream must be drained before a
timing packet arrives.

Another possible reason for using TCP is to detect liveness, is aprogram crashes  the kernel will close TCP stream soother decks know faster about userlan software failures.
Potentially gif beat grid visual representations are sent, these will be longer than UDP packets and maybe some decks have features to render beat grid from multiple decks.

Current UDP packets send informatin about future beat grid points, this is limited to 4 beats ahead, TCP would not have that limitation so that also coule be a use case for TCP 
connections.

The database file transfer across the network uses an old NFS protocol, and sends the entire DB in one file.  As dbs get bigger since modern pendrives can hold
much more data maybe Pioneer hitthe size limits for sending the whole DB, so another potential use case is a better way of transfering partial track information 
from the database wihtout sending the whole db file.

We know that very new XDJs have added features to the UDP protocol so I dontsuspect that the TCP is a rewrite of the UDP protocol I suspect it will
only be used for new features that require arbirary length data streams.

