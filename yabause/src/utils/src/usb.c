/*  Copyright 2006, 2024 Anthony Randazzo

	This file is part of Kronos.

	Kronos is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Kronos is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Kronos; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file usb.c
	\brief USB Devcart emulation functions.
*/


#include <ctype.h>
#include "cs0.h"
#include "error.h"
#include "usb.h"
#include "debug.h"
#include "scu.h"
#ifdef USESOCKET
#include "sock.h"
#include "threads.h"
#endif


USB* USBArea = NULL;

static volatile u8 usb_listener_thread_running;
static volatile u8 usb_connect_thread_running;
static volatile u8 usb_client_thread_running;

static int USBNetworkInit(const char* port);
static void USBNetworkDeInit(void);
static void USBNetworkStopClient();
static void USBNetworkStopConnect();
static void USBNetworkConnect(const char* ip, const char* port);
static int USBNetworkWaitForConnect();
static int USBNetworkSend(const void* buffer, int length);
static int USBNetworkReceive(void* buffer, int maxlength);


void* usb_client(void* data);
void* usb_listener(void* data);
void* usb_connect(void* data);



//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
static void USBNetworkStopClient()
{
	if (usb_client_thread_running)
	{
		if (USBArea->clientsocket != -1)
			YabSockCloseSocket(USBArea->clientsocket);
		USBArea->clientsocket = -1;

		usb_client_thread_running = 0;
		YabThreadWait(YAB_THREAD_NETLINKCLIENT);
	}

}

//////////////////////////////////////////////////////////////////////////////
static void USBNetworkStopListener()
{
	if (usb_listener_thread_running)
	{
		if (USBArea->listensocket != -1)
			YabSockCloseSocket(USBArea->listensocket);
		USBArea->listensocket = -1;
		usb_listener_thread_running = 0;
		YabThreadWait(YAB_THREAD_NETLINKLISTENER);
	}
}

//////////////////////////////////////////////////////////////////////////////

int USBNetworkRestartListener(int port)
{
	int ret;
	if ((ret = YabSockListenSocket(port, &USBArea->listensocket)) != 0)
		return ret;

	YabThreadStart(YAB_THREAD_NETLINKLISTENER, usb_listener, (void*)(pointer)USBArea->listensocket);
	//usb_listener((void*)(pointer)USBArea->listensocket);
	return ret;

}

//////////////////////////////////////////////////////////////////////////////

static int USBNetworkInit(const char* port)
{
	int ret;

	YabSockInit();

	USBArea->clientsocket = USBArea->listensocket = USBArea->connectsocket = -1;

	if (ret = USBNetworkRestartListener(atoi(port)) != 0)
		return ret;

	
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
static void USBNetworkStopConnect()
{
	if (usb_connect_thread_running)
	{
		if (USBArea->connectsocket != -1)
			YabSockCloseSocket(USBArea->connectsocket);
		USBArea->connectsocket = -1;

		usb_connect_thread_running = 0;
		YabThreadWait(YAB_THREAD_NETLINKCONNECT);
	}

}

//////////////////////////////////////////////////////////////////////////////

static void USBNetworkConnect(const char* ip, const char* port)
{
	usb_thread* connect;
	connect = malloc(sizeof(usb_thread));
	strcpy(connect->ip, ip);
	connect->port = atoi(port);

	USBNetworkStopListener();
	YabThreadStart(YAB_THREAD_NETLINKCONNECT, usb_connect, connect);
}

//////////////////////////////////////////////////////////////////////////////

static int USBNetworkWaitForConnect()
{
	if (usb_client_thread_running)
		return 0;
	else
		return -1;
}

//////////////////////////////////////////////////////////////////////////////

static void USBNetworkDeInit(void)
{
	USBNetworkStopListener();
	USBNetworkStopConnect();
	USBNetworkStopClient();

	YabSockDeInit();
}


//////////////////////////////////////////////////////////////////////////////

#ifndef USESOCKET
UNUSED
#endif




//////////////////////////////////////////////////////////////////////////////




//////////////////////////////////////////////////////////////////////////////

int USBInit(const char* ip, const char* port)
{


	if ((USBArea = (USB*)malloc(sizeof(USB))) == NULL)
	{
		
		YabSetError(YAB_ERR_CANNOTINIT, (void*)"USB Dev");
		return 0;
	}

	memset((void*)USBArea->inRingBuffer, 0, USB_BUFFER_SIZE);
	memset((void*)USBArea->outRingBuffer, 0, USB_BUFFER_SIZE);

	USBArea->inRingBufferHead = USBArea->inRingBufferTail = USBArea->inRingBufferSize = USBArea->inRingBufferLength = 0;
	USBArea->inRingBufferUpdate = 0;
	USBArea->outRingBufferHead = USBArea->outRingBufferTail = USBArea->outRingBufferSize = USBArea->outRingBufferLength = 0;
	USBArea->outRingBufferUpdate = 0;

	USBArea->cycles = 0;
	USBArea->escape_count = 0;


	if (ip == NULL || strcmp(ip, "") == 0)
		// Use Loopback ip
		sprintf(USBArea->ipstring, "127.0.0.1");
	else
		strcpy(USBArea->ipstring, ip);

	if (port == NULL || strcmp(port, "") == 0)
		// Default port
		sprintf(USBArea->portstring, "1337");
	else
	{
		size_t port_len = strlen(port);
		if (port_len >= 6)
		{
			YabSetError(YAB_ERR_OTHER, "Port is too long");
			return 0;
		}
		strcpy(USBArea->portstring, port);
	}

#ifdef USESOCKET
	return USBNetworkInit(USBArea->portstring);
#else
	return 0;
#endif
}

//////////////////////////////////////////////////////////////////////////////

void USBDeInit(void)
{
#ifdef USESOCKET
	USBNetworkDeInit();
#endif

	if (USBArea)
		free(USBArea);
}

//////////////////////////////////////////////////////////////////////////////

void USBExec(u32 timing)
{
	
}
#define USB_FIFO (0x100001)
#define USB_FLAGS (0x200001)

u8 FASTCALL USBFLAGS()
{
	u8 FLAGS = 0x80;
	//Reading from USB
	if (USBArea->outRingBufferSize <= 0)
	{
		FLAGS |= 0x01;
	}
	//Writing to USB
	if (USBArea->inRingBufferSize >= USB_BUFFER_SIZE)
	{
		FLAGS |= 0x02;
	}
	
	return FLAGS;
}

u8 FASTCALL USBReadByte(SH2_struct* context, UNUSED u8* memory, u32 addr)
{
	addr &= 0x1FFFFFF;
	if ((addr & USB_FIFO) == USB_FIFO) 
	{
		if (USBArea->outRingBufferSize == 0) return 0;
		USBArea->outRingBufferHead &= (USB_BUFFER_SIZE - 1);
		USBArea->outRingBufferSize--;
		return USBArea->outRingBuffer[USBArea->outRingBufferHead++];
	}
	if ((addr & USB_FLAGS) == USB_FLAGS)
	{
		return USBFLAGS();
	}
	return AR4MCs0ReadByte(context, memory, addr);
}
void FASTCALL USBWriteByte(SH2_struct* context, UNUSED u8* memory, u32 addr, u8 val)
{
	addr &= 0x1FFFFFF;
	if ((addr & USB_FIFO) == USB_FIFO)
	{
		if (USBArea->inRingBufferSize == USB_BUFFER_SIZE) return;
		USBArea->inRingBufferTail &= (USB_BUFFER_SIZE - 1);
		USBArea->inRingBufferSize++;
		USBArea->inRingBuffer[USBArea->inRingBufferTail++] = val;
		return;
	}
	if ((addr & USB_FLAGS) == USB_FLAGS)
	{
		//this is read only on cart
		FlashCs0WriteByte(context, memory, addr, val);
		return;
	}
	
	AR4MCs0WriteByte(context, memory, addr, val);
}

//////////////////////////////////////////////////////////////////////////////
#ifdef USESOCKET
void* usb_client(void* data)
{
	usb_thread* client = (usb_thread*)data;

	usb_client_thread_running = 1;

	while (usb_client_thread_running)
	{
		int bytes;
		if (YabSockSelect(client->sock, 1, 1) != 0)
		{
			continue;
		}

		if (USBArea->inRingBufferSize > 0 && YabSockIsWriteSet(client->sock))
		{
			//usb_LOG("Sending to external source...");
			int lengthOfWrite = USBArea->inRingBufferTail - USBArea->inRingBufferHead;
			int lengthOfSecondWrite = 0;
			if (lengthOfWrite < 0)
			{
				lengthOfWrite = USBArea->inRingBufferSize - USBArea->inRingBufferHead;
				lengthOfSecondWrite = USBArea->inRingBufferTail;

			}
			
			while (lengthOfWrite > 0) {
				// Send via USBNetwork connection
				if ((bytes = YabSockSend(client->sock, (void*)&USBArea->inRingBuffer[USBArea->inRingBufferHead], lengthOfWrite, 0)) >= 0)
				{
					//usb_LOG("Successfully sent %d byte(s)\n", bytes);
					USBArea->inRingBufferHead += bytes;
					USBArea->inRingBufferHead &= (USB_BUFFER_SIZE - 1);

					USBArea->inRingBufferSize -= bytes;

					USBArea->inRingBufferUpdate = 1;
					lengthOfWrite -= bytes;
				}
				else
				{
					free(data);
					return NULL;
					//usb_LOG("failed.\n");
				}
			}
			
			while (lengthOfSecondWrite > 0) {
				// Send via USBNetwork connection
				if ((bytes = YabSockSend(client->sock, (void*)&USBArea->inRingBuffer[USBArea->inRingBufferHead], lengthOfSecondWrite, 0)) >= 0)
				{
					//usb_LOG("Successfully sent %d byte(s)\n", bytes);
					USBArea->inRingBufferHead += bytes;
					USBArea->inRingBufferHead &= (USB_BUFFER_SIZE - 1);

					USBArea->inRingBufferSize -= bytes;
					lengthOfSecondWrite -= bytes;
					USBArea->inRingBufferUpdate = 1;
					
				}
				else
				{
					free(data);
					return NULL;
					//usb_LOG("failed.\n");
				}
			}
		}

		if (YabSockIsReadSet(client->sock))
		{
			int lengthOfRead = USBArea->outRingBufferTail - USBArea->outRingBufferHead;
			int lengthOfSecondRead = 0;
			if (lengthOfRead < 0)
			{
				lengthOfRead = USBArea->outRingBufferSize - USBArea->outRingBufferHead;
				lengthOfSecondRead = USBArea->outRingBufferTail;

			}
			while (lengthOfRead > 0) {
				//usb_LOG("Data is ready from external source...");
				if ((bytes = YabSockReceive(client->sock, (void*)&USBArea->outRingBuffer[USBArea->outRingBufferTail], lengthOfRead, 0)) > 0)
				{
					//usb_LOG("Successfully received %d byte(s)\n", bytes);
					USBArea->outRingBufferTail += bytes;
					USBArea->outRingBufferTail &= (USB_BUFFER_SIZE - 1);
					USBArea->outRingBufferSize += bytes;
					USBArea->outRingBufferUpdate = 1;
					lengthOfRead -= bytes;
				}
				else
				{
					u8 breakme = 1;
				}

			}
			while (lengthOfSecondRead > 0) {
				//usb_LOG("Data is ready from external source...");
				if ((bytes = YabSockReceive(client->sock, (void*)&USBArea->outRingBuffer[USBArea->outRingBufferTail], lengthOfSecondRead, 0)) > 0)
				{
					//usb_LOG("Successfully received %d byte(s)\n", bytes);
					USBArea->outRingBufferTail += bytes;
					USBArea->outRingBufferTail &= (USB_BUFFER_SIZE - 1);
					USBArea->outRingBufferSize += bytes;
					USBArea->outRingBufferUpdate = 1;
					lengthOfSecondRead -= bytes;
				}
				else
				{
					u8 breakme = 1;
				}

			}
		}
	}

	free(data);
	return NULL;
}

void* usb_listener(void* data)
{
	YabSock Listener = (YabSock)(pointer)data;
	usb_thread* client = NULL;

	usb_listener_thread_running = 1;

	//while(usb_listener_thread_running)
	//{
	USBArea->clientsocket = YabSockAccept(Listener);
	if (USBArea->clientsocket == -1)
	{
		perror("accept failed\n");
		//continue;

		return NULL;

	}

	if (client)      free(client);

	if ((client = malloc(sizeof(usb_thread))) == 0) {
		return NULL;
	}

	client->sock = USBArea->clientsocket;
	YabThreadStart(YAB_THREAD_NETLINKCLIENT, usb_client, (void*)client);


	//}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////////

void* usb_connect(void* data)
{
	usb_thread* connect = (usb_thread*)data;

	usb_connect_thread_running = 1;

	while (usb_connect_thread_running)
	{
		if (YabSockConnectSocket(connect->ip, connect->port, &connect->sock) == 0)
		{
			USBArea->connectsocket = connect->sock;

			YabThreadStart(YAB_THREAD_NETLINKCLIENT, usb_client, (void*)connect);
			return NULL;
		}
		else
			YabThreadYield();
	}

	USBNetworkRestartListener(connect->port);
	free(data);
	return NULL;
}

//////////////////////////////////////////////////////////////////////////////


#endif
