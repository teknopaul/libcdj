# IdUseReply


The message you call "Second-stage" (type 0x02) I believe what Pioneer call an IdUseRequest message. 

It is broadcast on 50000 to 192.168.1.255.

If a CDJ already has this ID number assigned there is an IdUseReply message (type 0x03) _unicast_ on port 50000 back to the player that tried to take a pre-assigned ID.
In order to handle this messages psoftware players should bind and listen  to their local IP port 50000.


This is a dump of a reply from a player 04, (len 0x27) 

(edit: first 01 is N the packet index identifier from the offending request)



                              type
51 73 70 74 31 57 6d 4a 4f 4c 03 00 58 44 4a 2d
31 30 30 30 00 00 00 00 00 00 00 00 00 00 00 00
01 02 00 27 04 01 01
         ln id N

The 01 02 00 seems standard across all messages in this part of the protocol.


In the boot sequence, if an XDJ receives this message, it tries a different player ID immediately in its next broadcast IdUse request message.

Seems this is how automatic player ID assignment works. Any CDJ can claim an ID with a IdUseReply and the booting CDJ will respect that.
