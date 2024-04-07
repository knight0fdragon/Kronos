/*  Copyright 2006, 2013 Theo Berkau

	This file is part of Yabause.

	Yabause is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Yabause is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Yabause; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file netlink.c
	\brief Netlink emulation functions.
*/


#include <ctype.h>
#include "cs2.h"
#include "error.h"
#include "netlink.h"
#include "debug.h"
#include "yui.h"
#include "scu.h"
#ifdef USESOCKET
#include "sock.h"
#include "threads.h"
#endif

Netlink* NetlinkArea = NULL;

static volatile u8 netlink_listener_thread_running;
static volatile u8 netlink_connect_thread_running;
static volatile u8 netlink_client_thread_running;

static int NetworkInit(const char* port);
static void NetworkDeInit(void);
static void NetworkStopClient();
static void NetworkStopConnect();
static void NetworkConnect(const char* ip, const char* port);
static int NetworkWaitForConnect();
static int NetworkSend(const void* buffer, int length);
static int NetworkReceive(void* buffer, int maxlength);


void* netlink_client(void* data);
void* netlink_listener(void** data);
void* netlink_connect(void* data);



//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
static void NetworkStopClient()
{
	YuiMsg("Netlink stopping client.\n");
	if (netlink_client_thread_running)
	{
		if (NetlinkArea->clientsocket != -1)
			YabSockCloseSocket(NetlinkArea->clientsocket);
		NetlinkArea->clientsocket = -1;

		netlink_client_thread_running = 0;
		YabThreadWait(YAB_THREAD_NETLINKCLIENT);
	}
	YuiMsg("Netlink stopped client.\n");
}

//////////////////////////////////////////////////////////////////////////////
static void NetworkStopListener()
{
	YuiMsg("Netlink stopping listener.\n");
	if (netlink_listener_thread_running)
	{
		if (NetlinkArea->listensocket != -1) {
			YabSockCloseSocket(NetlinkArea->listensocket);
			YuiMsg("Socket closed.\n");
		}
		NetlinkArea->listensocket = -1;
		netlink_listener_thread_running = 0;
		YabThreadWait(YAB_THREAD_NETLINKLISTENER);
	}
	YuiMsg("Netlink stopped listener.\n");
}

//////////////////////////////////////////////////////////////////////////////

int NetworkRestartListener(int port)
{
	YuiMsg("Netlink restarting listener.\n");
	int ret;
	if ((ret = YabSockListenSocket(port, &NetlinkArea->listensocket)) != 0)
	{
		YuiMsg("Netlink failed to restart listener. Code %d\n", ret);
		return ret;
	}
	void* params[2] = { (void*)(pointer)NetlinkArea->listensocket, (void*)(pointer)port };

	YabThreadStart(YAB_THREAD_NETLINKLISTENER, netlink_listener, (void**)params);
	//netlink_listener((void*)(pointer)NetlinkArea->listensocket);
	YuiMsg("Netlink restarted listener.\n");
	return ret;

}

//////////////////////////////////////////////////////////////////////////////

static int NetworkInit(const char* port)
{
	YuiMsg("Netlink initializing\n");
	//int ret;

	YabSockInit();

	NetlinkArea->clientsocket = NetlinkArea->listensocket = NetlinkArea->connectsocket = -1;
	//TODO:Undo if necessary
	//if (ret = NetworkRestartListener(atoi(port)) != 0)
	//	return ret;

	NetlinkArea->connectstatus = NL_CONNECTSTATUS_IDLE;
	YuiMsg("Netlink initialized.\n");
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
static void NetworkStopConnect()
{
	YuiMsg("Netlink stopped connecting.\n");
	if (netlink_connect_thread_running)
	{
		if (NetlinkArea->connectsocket != -1)
			YabSockCloseSocket(NetlinkArea->connectsocket);
		NetlinkArea->connectsocket = -1;

		netlink_connect_thread_running = 0;
		YabThreadWait(YAB_THREAD_NETLINKCONNECT);
	}
	YuiMsg("Netlink stopped connecting.\n");
}

//////////////////////////////////////////////////////////////////////////////

static void NetworkConnect(const char* ip, const char* port)
{

	netlink_thread* connect;
	if (!(connect = malloc(sizeof(netlink_thread)))) return;

	strcpy(connect->ip, ip);
	connect->port = atoi(port);

	NetworkStopListener();

	YuiMsg("Netlink connecting to %s:%s\n", ip, port);
	YabThreadStart(YAB_THREAD_NETLINKCONNECT, netlink_connect, connect);
}

//////////////////////////////////////////////////////////////////////////////

static int NetworkWaitForConnect()
{
	//YuiMsg("Netlink waiting for connect.\n");
	if (netlink_client_thread_running)
		return 0;
	else
		return -1;
}

//////////////////////////////////////////////////////////////////////////////

static void NetworkDeInit(void)
{
	NetworkStopListener();
	NetworkStopConnect();
	NetworkStopClient();

	YabSockDeInit();
}

//////////////////////////////////////////////////////////////////////////////

UNUSED static void NetlinkLSRChange(u8 val)
{
	// If IER bit 2 is set and if any of the error or alarms bits are set(and
	// they weren't previously), trigger an interrupt
	if ((NetlinkArea->reg.IER & 0x4) && ((NetlinkArea->reg.LSR ^ val) & val & 0x1E))
	{
		NetlinkArea->reg.IIR = (NetlinkArea->reg.IIR & 0xF0) | 0x6;
		ScuSendExternalInterrupt12();
	}

	NetlinkArea->reg.LSR = val;
}

//////////////////////////////////////////////////////////////////////////////

#ifndef USESOCKET
UNUSED
#endif
static void NetlinkMSRChange(u8 set, u8 clear)
{
	u8 change;

	change = ((NetlinkArea->reg.MSR >> 4) ^ set) & set;
	change |= (((NetlinkArea->reg.MSR >> 4) ^ 0xFF) ^ clear) & clear;

	// If IER bit 3 is set and CTS/DSR/RI/RLSD changes, trigger interrupt
	if ((NetlinkArea->reg.IER & 0x8) && change)
	{
		NetlinkArea->reg.IIR = NetlinkArea->reg.IIR & 0xF0;
		ScuSendExternalInterrupt12();
	}

	NetlinkArea->reg.MSR &= ~(clear << 4);
	NetlinkArea->reg.MSR |= (set << 4) | change;
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL NetlinkReadByte(SH2_struct* context, u8* memory, u32 addr)
{
	u8 ret;

	switch (addr & 0xFFFFF)
	{
	case 0x95001: // Receiver Buffer/Divisor Latch Low Byte
	{
		if (NetlinkArea->reg.LCR & 0x80) // Divisor Latch Low Byte
			return NetlinkArea->reg.DLL;
		else // Receiver Buffer
		{
			if (NetlinkArea->outbuffersize == 0)
			{
#ifdef USESOCKET
				YabThreadWake(YAB_THREAD_NETLINKCLIENT);
#endif
				return 0x00;
			}

			ret = NetlinkArea->outbuffer[NetlinkArea->outbufferstart];
			NetlinkArea->outbufferstart++;
			NetlinkArea->outbuffersize--;

			// If the buffer is empty now, make sure the data available
			// bit in LSR is cleared
			if (NetlinkArea->outbuffersize == 0)
			{
				NetlinkArea->outbufferstart = NetlinkArea->outbufferend = 0;
				NetlinkArea->reg.LSR &= ~0x01;
			}

			// If interrupt has been triggered because of RBR having data, reset it
			if ((NetlinkArea->reg.IER & 0x1) && (NetlinkArea->reg.IIR & 0xF) == 0x4)
				NetlinkArea->reg.IIR = (NetlinkArea->reg.IIR & 0xF0) | 0x1;

			return ret;
		}

		return 0;
	}
	case 0x95005: // Interrupt Enable Register/Divisor Latch High Byte
	{
		if (NetlinkArea->reg.LCR & 0x80) // Divisor Latch High Byte
			return NetlinkArea->reg.DLM;
		else // Interrupt Enable Register
			return NetlinkArea->reg.IER;
	}
	case 0x95009: // Interrupt Identification Register
	{
		// If interrupt has been triggered because THB is empty, reset it
		if ((NetlinkArea->reg.IER & 0x2) && (NetlinkArea->reg.IIR & 0xF) == 0x2)
			NetlinkArea->reg.IIR = (NetlinkArea->reg.IIR & 0xF0) | 0x1;
		return NetlinkArea->reg.IIR;
	}
	case 0x9500D: // Line Control Register
	{
		return NetlinkArea->reg.LCR;
	}
	case 0x95011: // Modem Control Register
	{
		return NetlinkArea->reg.MCR;
	}
	case 0x95015: // Line Status Register
	{
		return NetlinkArea->reg.LSR;
	}
	case 0x95019: // Modem Status Register
	{
		// If interrupt has been triggered because of MSR change, reset it
		if ((NetlinkArea->reg.IER & 0x8) && (NetlinkArea->reg.IIR & 0xF) == 0)
			NetlinkArea->reg.IIR = (NetlinkArea->reg.IIR & 0xF0) | 0x1;
		ret = NetlinkArea->reg.MSR;
		NetlinkArea->reg.MSR &= 0xF0;
		return ret;
	}
	case 0x9501D: // Scratch
	{
		return NetlinkArea->reg.SCR;
	}
	default:
		break;
	}

	NETLINK_LOG("Unimplemented Netlink byte read: %08X\n", addr);
	return 0xFF;
}

//////////////////////////////////////////////////////////////////////////////

static void FASTCALL NetlinkDoATResponse(const char* string)
{
	strcpy((char*)&NetlinkArea->outbuffer[NetlinkArea->outbufferend], string);
	NetlinkArea->outbufferend += (u32)strlen(string);
	NetlinkArea->outbuffersize += (u32)strlen(string);
}


static void FASTCALL NetlinkDoATWriteData(const char* string)
{

	int i = 0;
	while (string[i] != 0)
	{
		NetlinkArea->inbuffer[NetlinkArea->inbufferend] = '\x10';
		
		NetlinkArea->inbufferend++;
		if (NetlinkArea->inbufferend == NETLINK_BUFFER_SIZE)
		{

			NetlinkArea->inbufferend = 0;
		}


		if (NetlinkArea->inbufferend == NetlinkArea->inbufferstart)
		{
			int error = 0;
		}

		NetlinkArea->inbuffer[NetlinkArea->inbufferend] = string[i];
		i++;
		NetlinkArea->inbufferend++;
		if (NetlinkArea->inbufferend == NETLINK_BUFFER_SIZE)
		{

			NetlinkArea->inbufferend = 0;
		}
		

		if (NetlinkArea->inbufferend == NetlinkArea->inbufferstart)
		{
			int error = 0;
		}
	}
	NetlinkArea->thb_write_time = 0;
}
//////////////////////////////////////////////////////////////////////////////

static int FASTCALL NetlinkFetchATParameter(volatile u8 buffer[1024], volatile  u32* offset)
{
	u8 val = buffer[*offset + 1];
	if (val >= '0' && val <= '9')
	{
		(*offset)++;
		return (val - 0x30);
	}
	else
		return 0;
}

//////////////////////////////////////////////////////////////////////////////

void NetlinkUpdateReceivedDataInt()
{
	if (NetlinkArea->outbuffersize > 0)
	{
		// Set Data available bit in LSR
		NetlinkArea->reg.LSR |= 0x01;

		// Trigger Interrrupt
		NetlinkArea->reg.IIR = 0x4;
		ScuSendExternalInterrupt12();
	}
}


int FASTCALL ExtractNumber(char* inbuffer, int* start)
{
	int spot = *start;
	int len = 0;
	while ((((char*)inbuffer)[spot]) >= '0' && (((char*)inbuffer)[spot]) <= '9') {
		len++;
		spot++;
		if (spot >= NETLINK_BUFFER_SIZE)
		{
			spot -= NETLINK_BUFFER_SIZE;
		}
	}
	char* number;

	if (number = malloc(len + 1))
	{
		number[len] = '\0';
		for (int i = len - 1; i >= 0; i--)
		{
			spot--;
			if (spot < 0)
			{
				spot += NETLINK_BUFFER_SIZE;
			}
			number[i] = ((char*)inbuffer)[spot];


		}
	}
	else
	{
		int error = 0;
		return -1;
	}
	*start += len;
	if (*start >= NETLINK_BUFFER_SIZE)
	{
		*start -= NETLINK_BUFFER_SIZE;
	}
	int val = atoi(number);
	free(number);
	return val;
}

char* FASTCALL ExtractString()
{
	int spot = NetlinkArea->inbufferstart;
	int len = 0;
	while ((((char*)NetlinkArea->inbuffer)[spot]) != 0xD) {
		len++;
		spot++;
		if (spot >= NETLINK_BUFFER_SIZE)
		{
			spot -= NETLINK_BUFFER_SIZE;
		}
	}
	char* dialString;

	if (dialString = malloc(len + 1))
	{
		dialString[len] = '\0';
		for (int i = len - 1; i >= 0; i--)
		{
			spot--;
			if (spot < 0)
			{
				spot += NETLINK_BUFFER_SIZE;
			}
			dialString[i] = ((char*)NetlinkArea->inbuffer)[spot];


		}
	}
	else
	{
		int error = 0;
	}
	return dialString;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL NetlinkWriteByte(SH2_struct* context, u8* memory, u32 addr, u8 val)
{

	switch (addr & 0xFFFFF)
	{
	case 0x2503D: // ???
	{
		return;
	}
	case 0x95001: // Transmitter Holding Buffer/Divisor Latch Low Byte
	{
		if (NetlinkArea->reg.LCR & 0x80) // Divisor Latch Low Byte
		{
			NetlinkArea->reg.DLL = val;
		}
		else // Transmitter Holding Buffer
		{
			if (NetlinkArea->thb_write_time == 0xFFFFFFFF)
				NetlinkArea->thb_write_time = 0;

			if (val == '+')
			{
				// Possible escape sequence?
				if (NetlinkArea->escape_count == 0 &&
					NetlinkArea->thb_write_time >= 1000000)
				{
					// Start of sequence
					NetlinkArea->escape_count++;
				}
				else if (NetlinkArea->escape_count >= 1)
				{
					// Middle/possible tail
					NetlinkArea->escape_count++;
				}
			}
			else
				NetlinkArea->escape_count = 0;

			NetlinkArea->inbuffer[NetlinkArea->inbufferend] = val;
			NetlinkArea->thb_write_time = 0;
			NetlinkArea->inbufferend++;
			if (NetlinkArea->inbufferend == NETLINK_BUFFER_SIZE)
			{

				NetlinkArea->inbufferend = 0;
			}
			

			// If interrupt has been triggered because THB is empty, reset it
			if ((NetlinkArea->reg.IER & 0x2) && (NetlinkArea->reg.IIR & 0xF) == 0x2)
				NetlinkArea->reg.IIR = (NetlinkArea->reg.IIR & 0xF0) | 0x1;

			if (NetlinkArea->modemstate == NL_MODEMSTATE_COMMAND)
			{
				if (val == 0x0D) {
					int breakme = 0;
				}
				if (val == 0x0D &&
					(strncmp((char*)&NetlinkArea->inbuffer[NetlinkArea->inbufferstart], "AT", 2) == 0 ||
						strncmp((char*)&NetlinkArea->inbuffer[NetlinkArea->inbufferstart], "at", 2) == 0)) // fix me
				{
					NetlinkArea->inbufferstart += 2;
					if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
					{
						NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
					}
					enum NL_RESULTCODE resultcode = NL_RESULTCODE_OK;
					int parameter;

					NETLINK_LOG("Program issued %s\n", NetlinkArea->inbuffer);

					// If echo is enabled, do it
					if (NetlinkArea->isechoenab)
						NetlinkDoATResponse((char*)&NetlinkArea->inbuffer[NetlinkArea->inbufferstart]);

					if (NetlinkArea->inbuffer[NetlinkArea->inbufferstart] == 0xD)
					{
						//fresh AT command, use this to start accepting
						NetworkRestartListener(atoi(NetlinkArea->portstring));
					}
					int end = NetlinkArea->inbufferend;
					// Handle AT command
					while (NetlinkArea->inbufferstart != end && NetlinkArea->inbuffer[NetlinkArea->inbufferstart] != 0xD)
					{
						switch (toupper(NetlinkArea->inbuffer[NetlinkArea->inbufferstart]))
						{
						case ' ':
							// Whitespace
							break;
						case '%':
							break;
						case '&':
							// Figure out second part of command
							NetlinkArea->inbufferstart++;
							if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
							{
								NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
							}

							switch (toupper(NetlinkArea->inbuffer[NetlinkArea->inbufferstart]))
							{
							case 'C':
								// Data Carrier Detect Options
								NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								break;
							case 'D':
								// Data Terminal Ready Options
								NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								break;
							case 'F':
								// Factory reset
								NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								break;
							case 'K':
								// Local Flow Control Options
								NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								break;
							case 'Q':
								// Communications Mode Options
								NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								break;
							case 'S':
								// Data Set Ready Options
								NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								break;
							default: break;
							}
							break;
						case 'S':
						{
							NetlinkArea->inbufferstart++;
							if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
							{
								NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
							}
							// Status Registers
							//int reg;

							//char* inbuffer = (char*)(NetlinkArea->inbuffer + NetlinkArea->inbufferstart);
							int reg = ExtractNumber(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);

							if (NetlinkArea->inbuffer[NetlinkArea->inbufferstart] == '=')
							{
								NetlinkArea->inbufferstart++;
								if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
								{
									NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
								}
								int seconds = ExtractNumber(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
								NetlinkArea->reg.SREG[reg & 0xFF] = seconds;

							}

							switch (reg)
							{
							case 7:
								// Wait Time for Carrier, Silence, or Dial Tone
								NetlinkArea->connect_timeout = NetlinkArea->reg.SREG[reg] * 1000000;
								break;
							default: break;
							}


							NetlinkArea->inbufferstart--;
							if (NetlinkArea->inbufferstart < 0)
							{
								NetlinkArea->inbufferstart += NETLINK_BUFFER_SIZE;
							}
							break;
						}
						case ')':
						case '*':
						case ':':
						case '?':
						case '@':
							break;
						case '\\':
							NetlinkArea->inbufferstart++;
							if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
							{
								NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
							}
							if (toupper(NetlinkArea->inbuffer[NetlinkArea->inbufferstart]) == 'N')
							{
								// linefeed
							}

							break;
						case 'A':
							// Answer Command(no other commands should follow)
							NETLINK_LOG("Starting answer\n");
							break;
						case 'D':
						{
							NetlinkArea->inbufferstart++;
							if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
							{
								NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
							}

							switch (NetlinkArea->inbuffer[NetlinkArea->inbufferstart])
							{
							case 't'://Tone Dialing
							case 'p': //Pulse Dialing
								NetlinkArea->inbufferstart++;
								if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
								{
									NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
								}
								break;
							}



							while (NetlinkArea->inbuffer[NetlinkArea->inbufferstart] == ' ')
							{
								NetlinkArea->inbufferstart++;
								if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
								{
									NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
								}

							}


							int nextByte = NetlinkArea->inbufferstart + 1;
							if (nextByte >= NETLINK_BUFFER_SIZE)
							{
								nextByte -= NETLINK_BUFFER_SIZE;
							}

							char digit = ((char*)NetlinkArea->inbuffer)[NetlinkArea->inbufferstart] - '0';
							char nextdigit = ((char*)NetlinkArea->inbuffer)[nextByte] - '0';

							//if we are dialing only zero, break out as we do not really want to dial
							if ((digit == 0) && !((nextdigit >= 0) && (nextdigit <= 0))) {
								NetlinkArea->inbufferstart++;
								if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
								{
									NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
								}
								break;
							}
							// Dial Command
							char* p;
							int j;






							NetlinkArea->internet_enable = 0;

							NetlinkArea->connectstatus = NL_CONNECTSTATUS_CONNECT;
							if (!NetlinkArea->internet_enable)
							{
#ifdef USESOCKET
								NetworkConnect(NetlinkArea->ipstring, NetlinkArea->portstring);
#endif
								NetlinkArea->connect_time = 0;
								NETLINK_LOG("Starting dial %s\n", NetlinkArea->ipstring);
								/*int spot = NetlinkArea->inbufferstart;
								int len = 0;
								while ((((char*)NetlinkArea->inbuffer)[spot]) != 0xD) {
									len++;
									spot++;
									if (spot >= NETLINK_BUFFER_SIZE)
									{
										spot -= NETLINK_BUFFER_SIZE;
									}
								}*/

								/*char* dialString;

								if (dialString = malloc(len + 1))
								{
									dialString[len] = '\0';
									for (int i = len - 1; i >= 0; i--)
									{
										spot--;
										if (spot < 0)
										{
											spot += NETLINK_BUFFER_SIZE;
										}
										dialString[i] = ((char*)NetlinkArea->inbuffer)[spot];


									}
								}
								else
								{
									int error = 0;
								}*/
								//strchr(dialedNumber, '\r')[0] = '\0';
								char* dialString = ExtractString();
								YuiMsg("Starting dial %s\n", dialString);
								

								NetlinkArea->inbufferstart += strlen(dialString);
								
								
								NetlinkDoATWriteData(dialString);
								free(dialString);
							}


							//NetlinkArea->inbufferstart = strchr(((char*)NetlinkArea->inbuffer)[NetlinkArea->inbufferstart], '\r') - inbuffer - 1;
							//if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
							//{
								//NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
							//}
							//if ((p = strchr(inbuffer + i + 2, '*')) != NULL)
							//{
							//	// Fetch IP
							//	char ipstring[45];

							//	sscanf(p + 1, "%[^\r]\r", ipstring);

							//	// replace ',' with '.'
							//	for (j = 0; ipstring[j] != '\0'; j++)
							//		if (ipstring[j] == ',')
							//			ipstring[j] = '.';

							//	// Get port string if necessary
							//	if ((p = strchr(ipstring, '*')) != NULL)
							//	{
							//		p[0] = '\0';
							//		strcpy(NetlinkArea->portstring, p + 1);
							//	}
							//	strcpy(NetlinkArea->ipstring, ipstring);
							//}
							//else
							//{
							//	// If we're using Sega's old network, just assume we're using internet mode
							//	char number[45];

							//	sscanf(inbuffer + i + 2, "%[^\r]\r", number);
							//	//remove_all_chars(number, '-');
							//	char* pr = number, * pw = number;
							//	while (*pr) {
							//		*pw = *pr++;
							//		pw += (*pw != '-');
							//	}
							//	*pw = '\0';

							//	if (strcmp(number, "18007798852") == 0 ||
							//		strcmp(number, "8007798852") == 0)
							//		NetlinkArea->internet_enable = 1;
							//}

							/*NetlinkArea->inbufferstart--;
							if (NetlinkArea->inbufferstart < 0)
							{
								NetlinkArea->inbufferstart += NETLINK_BUFFER_SIZE;
							}
							*/
							break;
						}
						case 'E':
							// Command State Character Echo Selection
							parameter = NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);

							// Parameter can only be 0 or 1
							if (parameter < 2)
								NetlinkArea->isechoenab = parameter;
							else
								resultcode = NL_RESULTCODE_ERROR;

							break;
						case 'I':

							// Internal Memory Tests
							switch (NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart))
							{
							case 0:
								NetlinkDoATResponse("\r\n28800\r\n");
								break;
							default: break;
							}
							break;
						case 'L':

							// Speaker Volume Level Selection
							NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
							break;
						case 'M':

							// Speaker On/Off Selection
							NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
							break;
						case 'V':

							// Result Code Format Options
							NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
							break;
						case 'W':

							// Negotiation Progress Message Selection
							NetlinkFetchATParameter(NetlinkArea->inbuffer, &NetlinkArea->inbufferstart);
							break;
						default:
							NETLINK_LOG("Unsupported AT command %c", NetlinkArea->inbuffer[NetlinkArea->inbufferstart]);
							break;
						}

						NetlinkArea->inbufferstart++;
						if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
						{
							NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
						}
					}

					//we no longer need the \r
					if (NetlinkArea->inbuffer[NetlinkArea->inbufferstart] == 0xD) {
						NetlinkArea->inbufferstart++;
						if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
						{
							NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
						}
					}


					switch (resultcode)
					{
					case NL_RESULTCODE_OK: // OK
						NetlinkDoATResponse("\r\nOK\r\n");
						break;
					case NL_RESULTCODE_CONNECT: // CONNECT
						NetlinkDoATResponse("\r\nCONNECT\r\n");
						break;
					case NL_RESULTCODE_RING: // RING
						NetlinkDoATResponse("\r\nRING\r\n");
						break;
					case NL_RESULTCODE_NOCARRIER: // NO CARRIER
						NetlinkDoATResponse("\r\nNO CARRIER\r\n");
						break;
					case NL_RESULTCODE_ERROR: // ERROR
						NetlinkDoATResponse("\r\nERROR\r\n");
						break;
					case NL_RESULTCODE_CONNECT1200: // CONNECT 1200
						NetlinkDoATResponse("\r\nCONNECT 1200\r\n");
						break;
					case NL_RESULTCODE_NODIALTONE: // NO DIALTONE
						NetlinkDoATResponse("\r\nNO DIALTONE\r\n");
						break;
					case NL_RESULTCODE_BUSY: // BUSY
						NetlinkDoATResponse("\r\nBUSY\r\n");
						break;
					case NL_RESULTCODE_NOANSWER: // NO ANSWER
						NetlinkDoATResponse("\r\nNO ANSWER\r\n");
						break;
					default: break;
					}

					//memset((void*)NetlinkArea->inbuffer, 0, NetlinkArea->inbuffersize);
					//NetlinkArea->inbufferstart = NetlinkArea->inbufferend = NetlinkArea->inbuffersize = 0;

					if (NetlinkArea->outbuffersize > 0)
					{
						// Set Data available bit in LSR
						NetlinkArea->reg.LSR |= 0x01;

						// Trigger Interrrupt
						NetlinkArea->reg.IIR = 0x4;
						ScuSendExternalInterrupt12();
					}
				}
			}
			else if (NetlinkArea->connectstatus == NL_CONNECTSTATUS_LOGIN1 &&
				NetlinkArea->modemstate == NL_MODEMSTATE_DATA &&
				val == 0x0D)
			{
				// Internet login name
				NetlinkArea->connectstatus = NL_CONNECTSTATUS_LOGIN2;
				NETLINK_LOG("login response: %s", NetlinkArea->inbuffer + NetlinkArea->inbufferstart);
				NetlinkDoATResponse("\r\npassword:");
				NetlinkUpdateReceivedDataInt();
			}
			else if (NetlinkArea->connectstatus == NL_CONNECTSTATUS_LOGIN2 &&
				NetlinkArea->modemstate == NL_MODEMSTATE_DATA &&
				val == 0x0D)
			{
				// Internet password
				NetlinkArea->connectstatus = NL_CONNECTSTATUS_LOGIN3;
				NETLINK_LOG("password response: %s", NetlinkArea->inbuffer + NetlinkArea->inbufferstart);
				NetlinkDoATResponse("\r\n$");
				NetlinkUpdateReceivedDataInt();
			}
			else if (NetlinkArea->connectstatus == NL_CONNECTSTATUS_LOGIN3 &&
				NetlinkArea->modemstate == NL_MODEMSTATE_DATA &&
				val == 0x0D)
			{
				// Internet password
				NETLINK_LOG("shell response: %s", NetlinkArea->inbuffer + NetlinkArea->inbufferstart);
				NetlinkArea->connectstatus = NL_CONNECTSTATUS_CONNECTED;
			}
		}

		return;
	}
	case 0x95005: // Interrupt Enable Register/Divisor Latch High Byte
	{
		if (NetlinkArea->reg.LCR & 0x80) // Divisor Latch High Byte
		{
			NetlinkArea->reg.DLM = val;
		}
		else // Interrupt Enable Register
		{
			NetlinkArea->reg.IER = val;
		}

		return;
	}
	case 0x95009: // FIFO Control Register
	{
		NetlinkArea->reg.FCR = val;

		if (val & 0x1)
			// set FIFO enabled bits
			NetlinkArea->reg.IIR |= 0xC0;
		else
			// clear FIFO enabled bits
			NetlinkArea->reg.IIR &= ~0xC0;

		return;
	}
	case 0x9500D: // Line Control Register
	{
		NetlinkArea->reg.LCR = val;
		return;
	}
	case 0x95011: // Modem Control Register
	{
		NetlinkArea->reg.MCR = val;
		return;
	}
	case 0x95019: // Modem Status Register(read-only)
		return;
	case 0x9501D: // Scratch
	{
		NetlinkArea->reg.SCR = val;
		return;
	}
	default:
		break;
	}

	NETLINK_LOG("Unimplemented Netlink byte write: %08X\n", addr);
}

//////////////////////////////////////////////////////////////////////////////

int NetlinkInit(const char* ip, const char* port)
{


	if ((NetlinkArea = (Netlink*)malloc(sizeof(Netlink))) == NULL)
	{
		Cs2Area->carttype = CART_NONE;
		YabSetError(YAB_ERR_CANNOTINIT, (void*)"Netlink");
		return 0;
	}

	memset((void*)NetlinkArea->inbuffer, 0, NETLINK_BUFFER_SIZE);
	memset((void*)NetlinkArea->outbuffer, 0, NETLINK_BUFFER_SIZE);

	NetlinkArea->inbufferstart = NetlinkArea->inbufferend = 0;
	NetlinkArea->inbufferupdate = 0;
	NetlinkArea->outbufferstart = NetlinkArea->outbufferend = NetlinkArea->outbuffersize = 0;
	NetlinkArea->outbufferupdate = 0;

	NetlinkArea->isechoenab = 1;
	NetlinkArea->cycles = 0;
	NetlinkArea->thb_write_time = 0xFFFFFFFF;
	NetlinkArea->modemstate = NL_MODEMSTATE_COMMAND;
	NetlinkArea->escape_count = 0;

	NetlinkArea->reg.RBR = 0x00;
	NetlinkArea->reg.IER = 0x00;
	NetlinkArea->reg.DLL = 0x00;
	NetlinkArea->reg.DLM = 0x00;
	NetlinkArea->reg.IIR = 0x01;
	//      NetlinkArea->reg.FCR = 0x??; // have no idea
	NetlinkArea->reg.LCR = 0x00;
	NetlinkArea->reg.MCR = 0x00;
	NetlinkArea->reg.LSR = 0x60;
	NetlinkArea->reg.MSR = 0x30;
	NetlinkArea->reg.SCR = 0x01;

	NetlinkArea->reg.SREG[7] = 50;
	NetlinkArea->connect_timeout = NetlinkArea->reg.SREG[7] * 1000000;

	if (ip == NULL || strcmp(ip, "") == 0)
		// Use Loopback ip
		sprintf(NetlinkArea->ipstring, "127.0.0.1");
	else
		strcpy(NetlinkArea->ipstring, ip);

	if (port == NULL || strcmp(port, "") == 0)
		// Default port
		sprintf(NetlinkArea->portstring, "1337");
	else
	{
		size_t port_len = strlen(port);
		if (port_len >= 6)
		{
			YabSetError(YAB_ERR_OTHER, "Netlink port is too long");
			return 0;
		}
		strcpy(NetlinkArea->portstring, port);
	}

#ifdef USESOCKET
	return NetworkInit(NetlinkArea->portstring);
#else
	return 0;
#endif
}

//////////////////////////////////////////////////////////////////////////////

void NetlinkDeInit(void)
{
#ifdef USESOCKET
	NetworkDeInit();
#endif

	if (NetlinkArea)
		free(NetlinkArea);
}

//////////////////////////////////////////////////////////////////////////////

void NetlinkExec(u32 timing)
{
	NetlinkArea->cycles += timing;
	NetlinkArea->connect_time += timing;
	if (NetlinkArea->thb_write_time != 0xFFFFFFFF)
		NetlinkArea->thb_write_time += timing;

	if (NetlinkArea->cycles >= 20000)
	{
		NetlinkArea->cycles -= 20000;

		if (NetlinkArea->escape_count == 3 && NetlinkArea->thb_write_time >= 1000000)
		{
			// Switch back to command mode
			NETLINK_LOG("Detected escape sequence, switching back to command mode\n");
			NetlinkArea->modemstate = NL_MODEMSTATE_COMMAND;
		}

		switch (NetlinkArea->connectstatus)
		{
		case NL_CONNECTSTATUS_IDLE:
		{
#ifdef USESOCKET
			if (NetworkWaitForConnect() == 0)
			{
				NetlinkArea->connectstatus = NL_CONNECTSTATUS_CONNECTED;
				NetlinkArea->modemstate = NL_MODEMSTATE_DATA;

				// This is probably wrong, but let's give it a try anyways
				NetlinkDoATResponse("\r\nRING\r\n\r\nCONNECT\r\n");
				NetlinkMSRChange(0x08, 0x00);
				NetlinkUpdateReceivedDataInt();

				NETLINK_LOG("Connected via listener\n");
			}
#endif
			break;
		}
		case NL_CONNECTSTATUS_CONNECT:
		{
#ifdef USESOCKET
			if (NetlinkArea->internet_enable || NetworkWaitForConnect() == 0)
			{
				NetlinkArea->connectstatus = NL_CONNECTSTATUS_CONNECTED;
				NetlinkArea->modemstate = NL_MODEMSTATE_DATA;

				NetlinkDoATResponse("\r\nCONNECT 28800\r\n");
				NetlinkMSRChange(0x08, 0x00);
				NetlinkUpdateReceivedDataInt();
				NETLINK_LOG("Connected via remote ip connect\n");

				if (NetlinkArea->internet_enable)
				{
					NetlinkArea->connectstatus = NL_CONNECTSTATUS_LOGIN1;
					NetlinkDoATResponse("\r\nlogin:");
				}
			}
			else if (NetlinkArea->connect_time >= NetlinkArea->connect_timeout)
			{
				// Kill connect attempt
				NetworkStopConnect();
				NetlinkDoATResponse("\r\nNO ANSWER\r\n");
				NetlinkUpdateReceivedDataInt();
				NetlinkArea->connectstatus = NL_CONNECTSTATUS_IDLE;
			}
#endif
			break;
		}
		case NL_CONNECTSTATUS_CONNECTED:
		{
#ifdef USESOCKET

			if (NetlinkArea->outbufferupdate)
			{
				NetlinkMSRChange(0x08, 0x00);
				NetlinkUpdateReceivedDataInt();

				//NETLINK_LOG("Received %d byte(s) from external source\n", NetlinkArea->bytes_read);
				NetlinkArea->outbufferupdate = 0;
			}
#endif

			break;
		}
		default: break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
#ifdef USESOCKET
void* netlink_client(void* data)
{
	netlink_thread* client = (netlink_thread*)data;

	netlink_client_thread_running = 1;

	while (netlink_client_thread_running)
	{
		int bytes;
		if (YabSockSelect(client->sock, 1, 1) != 0)
		{
			continue;
		}

		int bufferSize = NetlinkArea->inbufferend - NetlinkArea->inbufferstart;
		if (bufferSize < 0 ) bufferSize += NETLINK_BUFFER_SIZE;

		if ( (NetlinkArea->modemstate == NL_MODEMSTATE_DATA) 
		  && bufferSize > 0 
		  && YabSockIsWriteSet(client->sock) 
		  && NetlinkArea->thb_write_time > 1000)
		{
			//NETLINK_LOG("Sending to external source...");

			// Send via network connection
			char* data;
			if (data = malloc(bufferSize))
			{
				int start = NetlinkArea->inbufferstart;
				if (start >= NETLINK_BUFFER_SIZE)
				{
					start -= NETLINK_BUFFER_SIZE;
				}
				int end = NetlinkArea->inbufferend;
				for (int i = 0; i < bufferSize; i++)
				{
					data[i] = NetlinkArea->inbuffer[start];
					start++;
					if (start >= NETLINK_BUFFER_SIZE)
					{
						start -= NETLINK_BUFFER_SIZE;
					}
				}

				if ((bytes = YabSockSend(client->sock, (void*)data, bufferSize, 0)) >= 0)
				{
					//NETLINK_LOG("Successfully sent %d byte(s)\n", bytes);

					NetlinkArea->inbufferstart += bytes;
					bufferSize -= bytes;
					if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
					{
						NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
					}

					if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
					{
						int error = 0;
					}

					NetlinkArea->inbufferupdate = 1;
				}
				else
				{
					free(data);
					return NULL;
					//NETLINK_LOG("failed.\n");
				}
				free(data);
			}
			else
			{
				int error = 0;
				return NULL;
			}
		}


		if (( NetlinkArea->modemstate == NL_MODEMSTATE_COMMAND)
			&& bufferSize > 0
			&& YabSockIsWriteSet(client->sock)
			&& NetlinkArea->thb_write_time > 1000)
		{
			//NETLINK_LOG("Sending to external source...");

			// Send via network connection
			char* data;
			if (data = malloc(bufferSize))
			{
				int start = NetlinkArea->inbufferstart;
				if (start >= NETLINK_BUFFER_SIZE)
				{
					start -= NETLINK_BUFFER_SIZE;
				}
				int end = NetlinkArea->inbufferend;
				for (int i = 0; i < bufferSize; i++)
				{
					data[i] = NetlinkArea->inbuffer[start];
					start++;
					if (start >= NETLINK_BUFFER_SIZE)
					{
						start -= NETLINK_BUFFER_SIZE;
					}
				}

				for (int d = 0; d < bufferSize; d++)
				{
					if (data[d] != '\x10') continue;

					if ((bytes = YabSockSend(client->sock, (void*)&data[d], 2, 0)) >= 0)
					{
						//NETLINK_LOG("Successfully sent %d byte(s)\n", bytes);

						NetlinkArea->inbufferstart += bytes;
						
						if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
						{
							NetlinkArea->inbufferstart -= NETLINK_BUFFER_SIZE;
						}

						if (NetlinkArea->inbufferstart >= NETLINK_BUFFER_SIZE)
						{
							int error = 0;
						}

						NetlinkArea->inbufferupdate = 1;
					}
					else
					{
						free(data);
						return NULL;
						//NETLINK_LOG("failed.\n");
					}
					d += bytes - 1;
				}

				free(data);
			}
			else
			{
				int error = 0;
				return NULL;
			}
		}


		//if (NetlinkArea->modemstate == NL_MODEMSTATE_COMMAND && NetlinkArea->inbuffersize > 0 && YabSockIsWriteSet(client->sock) && NetlinkArea->thb_write_time > 1000)
		//{
		//	//NETLINK_LOG("Sending to external source...");

		//	// Send via network connection
		//	if ((bytes = YabSockSend(client->sock, (void*)&NetlinkArea->inbuffer[NetlinkArea->inbufferstart], NetlinkArea->inbufferend - NetlinkArea->inbufferstart, 0)) >= 0)
		//	{
		//		//NETLINK_LOG("Successfully sent %d byte(s)\n", bytes);
		//		if (NetlinkArea->inbufferend > bytes)
		//		{
		//			NetlinkArea->inbufferstart += bytes;
		//			NetlinkArea->inbuffersize -= bytes;
		//		}
		//		else
		//			NetlinkArea->inbufferstart = NetlinkArea->inbufferend = NetlinkArea->inbuffersize = 0;
		//		NetlinkArea->inbufferupdate = 1;
		//	}
		//	else
		//	{
		//		free(data);
		//		return NULL;
		//		//NETLINK_LOG("failed.\n");
		//	}
		//}

		if (YabSockIsReadSet(client->sock))
		{
			//NETLINK_LOG("Data is ready from external source...");
			if ((bytes = YabSockReceive(client->sock, (void*)&NetlinkArea->outbuffer[NetlinkArea->outbufferend], sizeof(NetlinkArea->outbuffer) - 1 - NetlinkArea->outbufferend, 0)) > 0)
			{
				//NETLINK_LOG("Successfully received %d byte(s)\n", bytes);
				NetlinkArea->outbufferend += bytes;
				NetlinkArea->outbuffersize += bytes;
				NetlinkArea->outbufferupdate = 1;
			}
		}
	}

	free(data);
	return NULL;
}

void* netlink_listener(void** data)
{
	YabSock Listener = (YabSock)(pointer)data[0];
	int port = (int)(pointer)data[1];
	netlink_thread* client = NULL;

	netlink_listener_thread_running = 1;

	//while(netlink_listener_thread_running)
	//{
	YuiMsg("Waiting to accept on %d.\n", port);
	NetlinkArea->clientsocket = YabSockAccept(Listener);

	if (NetlinkArea->clientsocket == -1)
	{
		YuiMsg("Accept failed\n");
		perror("accept failed\n");
		//continue;

		return NULL;

	}
	YuiMsg("Accepting\n");
	if (client)      free(client);

	if ((client = malloc(sizeof(netlink_thread))) == 0) {
		YuiMsg("Accept failed\n");
		return NULL;
	}
	YuiMsg("Accepted\n");
	client->sock = NetlinkArea->clientsocket;
	YabThreadStart(YAB_THREAD_NETLINKCLIENT, netlink_client, (void*)client);


	//}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////////

void* netlink_connect(void* data)
{
	netlink_thread* connect = (netlink_thread*)data;

	netlink_connect_thread_running = 1;
	YuiMsg("Attempting connection.\n");
	while (netlink_connect_thread_running)
	{
		if (YabSockConnectSocket(connect->ip, connect->port, &connect->sock) == 0)
		{
			NetlinkArea->connectsocket = connect->sock;
			YuiMsg("Client started.\n");
			YabThreadStart(YAB_THREAD_NETLINKCLIENT, netlink_client, (void*)connect);
			YuiMsg("Client threaded.\n");
			return NULL;
		}
		else
			YabThreadYield();
	}
	YuiMsg("Connection ended.\n");
	//NetworkRestartListener(connect->port);
	free(data);
	return NULL;
}

//////////////////////////////////////////////////////////////////////////////


#endif
