#include "msgr/msg_types.h"

namespace msgr {

const char *msgr_entity_type_name(int type)
{
	switch (type) {
	case MSGR_ENTITY_TYPE_MDS: return "mds";
	case MSGR_ENTITY_TYPE_OSD: return "osd";
	case MSGR_ENTITY_TYPE_MON: return "mon";
	case MSGR_ENTITY_TYPE_CLIENT: return "client";
	case MSGR_ENTITY_TYPE_AUTH: return "auth";
	default: return "unknown";
	}
}

}
