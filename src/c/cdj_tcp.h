// ProLink TCP protocol — WORK IN PROGRESS
// Based on: references/adj/doc/LinkInfo-tcp.pcapng + live capture
// See: references/adj/doc/tcp-protocol-notes.md

#pragma once
#include <stdint.h>

#define CDJ_TCP_PORT             1051
#define CDJ_REMOTEDB_PORT        12523

#define CDJ_TCP_MAGIC            "\x11\x87\x23\x49\xae"
#define CDJ_TCP_MAGIC_LEN        5

// Both sides send this immediately after TCP connect
#define CDJ_TCP_HANDSHAKE        "\x11\x00\x00\x00\x01"
#define CDJ_TCP_HANDSHAKE_LEN   5

// Tag type bytes within messages
#define CDJ_TCP_TAG_INT          0x11  // + 4-byte big-endian uint32
#define CDJ_TCP_TAG_STRING       0x26  // + 4-byte char count + char_count*2 bytes UTF-16BE
#define CDJ_TCP_TAG_BYTE         0x01  // structure TBD /* UNCONFIRMED */

// Message types — upper 16 bits of 4-byte field at offset 10
// Lower 16 bits appear to be flags/version; exact meaning TBD /* UNCONFIRMED */
#define CDJ_TCP_MSG_REQ          0x1040  // request / session handshake
#define CDJ_TCP_MSG_TRACK        0x1041  // track metadata push (confirmed from pcap)
#define CDJ_TCP_MSG_END          0x1042  // end of burst
#define CDJ_TCP_MSG_ACK          0x1020  // client acknowledgement
#define CDJ_TCP_MSG_QUERY        0x1030  // query before track burst /* UNCONFIRMED */

// TCP strings are UTF-16-BE.
// CAUTION: rekordbox .pdb files use UTF-16-LE — these are NOT interchangeable.

int cdj_tcp_handshake(int fd);
