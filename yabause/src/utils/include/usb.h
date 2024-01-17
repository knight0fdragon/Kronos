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

#ifndef USB_H
#define USB_H
#include "sock.h"
#include "sh2core.h"
#define USB_BUFFER_SIZE     1024
#ifdef __cplusplus
extern "C" {
#endif

    typedef struct
    {
        u8 FIFO;
        u8 FLAGS;
    } usbregs_struct;
    typedef struct {
        volatile u8 inRingBuffer[USB_BUFFER_SIZE];
        volatile u8 outRingBuffer[USB_BUFFER_SIZE];
        volatile u32 inRingBufferHead, inRingBufferTail, inRingBufferSize,inRingBufferLength;
        volatile int inRingBufferUpdate;
        volatile u32 outRingBufferHead, outRingBufferTail, outRingBufferSize,outRingBufferLength;
        volatile int outRingBufferUpdate;
        usbregs_struct reg;
        YabSock listensocket;
        YabSock connectsocket;
        YabSock clientsocket;
        u32 cycles;
        char ipstring[16];
        char portstring[6];
        int escape_count;
    } USB;
    typedef struct
    {
        char ip[16];
        int port;
        YabSock sock;
    } usb_thread;

    extern USB* USBArea;


    u8 FASTCALL USBReadByte(SH2_struct* context, u8* memory, u32 addr);
    void FASTCALL USBWriteByte(SH2_struct* context, u8* memory, u32 addr, u8 val);
    int USBInit(const char* ip, const char* port);
    void USBDeInit(void);
    void USBExec(u32 timing);
#ifdef __cplusplus
}
#endif
#endif //USB_H