/* net_ps2.c: Quakespasm's network drivers on the PS2 (net_bsd.c of the
   SDL build): the loopback only (single player), no LAN drivers yet */

#include "q_stdinc.h"
#include "arch_def.h"
#include "quakedef.h"
#include "net_defs.h"

#include "net_loop.h"

net_driver_t net_drivers[] = {
	{"Loopback", false, Loop_Init, Loop_Listen, Loop_SearchForHosts, Loop_Connect, Loop_CheckNewConnections,
	 Loop_GetMessage, Loop_SendMessage, Loop_SendUnreliableMessage, Loop_CanSendMessage,
	 Loop_CanSendUnreliableMessage, Loop_Close, Loop_Shutdown},
};
const int net_numdrivers = Q_COUNTOF(net_drivers);

net_landriver_t net_landrivers[1];
const int net_numlandrivers = 0;
