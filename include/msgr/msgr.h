#ifndef MSGR_ab90e4d80ced459f963d48803e2dcb99_H
#define MSGR_ab90e4d80ced459f963d48803e2dcb99_H

#include <sys/socket.h> // for struct sockaddr_storage

/*
 * Data types for message passing layer
 */

#define MSGR_MON_PORT    5678  /* default monitor port */

/*
 * client-side processes will try to bind to ports in this
 * range, simply for the benefit of tools like nmap or wireshark
 * that would like to identify the protocol.
 */
#define MSGR_PORT_FIRST  5678

/*
 * tcp connection banner.  include a protocol version. and adjust
 * whenever the wire protocol changes.  try to keep this string length
 * constant.
 */
#define MSGR_BANNER "msgr v001"
#define MSGR_BANNER_MAX_LEN 30

namespace msgr {

/*
 * Rollover-safe type and comparator for 32-bit sequence numbers.
 * Comparator returns -1, 0, or 1.
 */
typedef uint32_t msgr_seq_t;

static inline int32_t msgr_seq_cmp(uint32_t a, uint32_t b)
{
       return (int32_t)a - (int32_t)b;
}


/*
 * entity_name -- logical name for a process participating in the
 * network, e.g. 'mds0' or 'osd3'.
 */
struct msgr_entity_name {
	uint8_t type;      /* MSGR_ENTITY_TYPE_* */
	__le64 num;
} __attribute__ ((packed));

#define MSGR_ENTITY_TYPE_MON    0x01
#define MSGR_ENTITY_TYPE_MDS    0x02
#define MSGR_ENTITY_TYPE_OSD    0x04
#define MSGR_ENTITY_TYPE_CLIENT 0x08
#define MSGR_ENTITY_TYPE_AUTH   0x20

#define MSGR_ENTITY_TYPE_ANY    0xFF

extern const char *msgr_entity_type_name(int type);

/*
 * entity_addr -- network address
 */
struct msgr_entity_addr {
	__le32 type;
	__le32 nonce;  /* unique id for process (e.g. pid) */
	struct sockaddr_storage in_addr;
} __attribute__ ((packed));

struct msgr_entity_inst {
	struct msgr_entity_name name;
	struct msgr_entity_addr addr;
} __attribute__ ((packed));


/* used by message exchange protocol */
#define MSGR_MSGR_TAG_READY         1  /* server->client: ready for messages */
#define MSGR_MSGR_TAG_RESETSESSION  2  /* server->client: reset, try again */
#define MSGR_MSGR_TAG_WAIT          3  /* server->client: wait for racing
					  incoming connection */
#define MSGR_MSGR_TAG_RETRY_SESSION 4  /* server->client + cseq: try again
					  with higher cseq */
#define MSGR_MSGR_TAG_RETRY_GLOBAL  5  /* server->client + gseq: try again
					  with higher gseq */
#define MSGR_MSGR_TAG_CLOSE         6  /* closing pipe */
#define MSGR_MSGR_TAG_MSG           7  /* message */
#define MSGR_MSGR_TAG_ACK           8  /* message ack */
#define MSGR_MSGR_TAG_UNUSED        9  /* Unused */
#define MSGR_MSGR_TAG_BADPROTOVER  10  /* bad protocol version */
#define MSGR_MSGR_TAG_BADAUTHORIZER 11 /* bad authorizer */
#define MSGR_MSGR_TAG_FEATURES      12 /* insufficient features */
#define MSGR_MSGR_TAG_SEQ           13 /* 64-bit int follows with seen seq number */
#define MSGR_MSGR_TAG_KEEPALIVE2     14
#define MSGR_MSGR_TAG_KEEPALIVE2_ACK 15  /* keepalive reply */


/*
 * connection negotiation
 */
struct msgr_msg_connect {
	__le64 features;     /* supported feature bits */
	__le32 host_type;    /* MSGR_ENTITY_TYPE_* */
	__le32 global_seq;   /* count connections initiated by this host */
	__le32 connect_seq;  /* count connections initiated in this session */
	__le32 protocol_version;
	__le32 authorizer_protocol;
	__le32 authorizer_len;
	uint8_t  flags;         /* MSGR_MSG_CONNECT_* */
} __attribute__ ((packed));

struct msgr_msg_connect_reply {
	uint8_t tag;
	__le64 features;     /* feature bits for this session */
	__le32 global_seq;
	__le32 connect_seq;
	__le32 protocol_version;
	__le32 authorizer_len;
	uint8_t flags;
} __attribute__ ((packed));

#define MSGR_MSG_CONNECT_LOSSY  1  /* messages i send may be safely dropped */


struct msgr_msg_header {
	__le64 seq;       /* message seq# for this session */
	__le64 tid;       /* transaction id */
	__le16 type;      /* message type */
	__le16 priority;  /* priority.  higher value == higher priority */
	__le16 version;   /* version of message encoding */

	__le32 front_len; /* bytes in main payload */
	__le32 middle_len;/* bytes in middle payload */
	__le32 data_len;  /* bytes of data payload */
	__le16 data_off;  /* sender: include full offset;
			     receiver: mask against ~PAGE_MASK */

	struct msgr_entity_name src;

	/* oldest code we think can decode this.  unknown if zero. */
	__le16 compat_version;
	__le16 reserved;
	__le32 crc;       /* header crc32c */
} __attribute__ ((packed));

#define MSGR_MSG_PRIO_LOW     64
#define MSGR_MSG_PRIO_DEFAULT 127
#define MSGR_MSG_PRIO_HIGH    196
#define MSGR_MSG_PRIO_HIGHEST 255

struct msgr_msg_footer {
	__le32 front_crc, middle_crc, data_crc;
	// sig holds the 64 bits of the digital signature for the message PLR
	__le64  sig;
	uint8_t flags;
} __attribute__ ((packed));

#define MSGR_MSG_FOOTER_COMPLETE  (1<<0)   /* msg wasn't aborted */
#define MSGR_MSG_FOOTER_NOCRC     (1<<1)   /* no data crc */
#define MSGR_MSG_FOOTER_SIGNED	   (1<<2)   /* msg was signed */

} // namespace msgr

#endif // MSGR_ab90e4d80ced459f963d48803e2dcb99_H
