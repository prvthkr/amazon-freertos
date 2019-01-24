/*
 * Amazon FreeRTOS
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file aws_iot_large_object_transfer.c
 * @brief File implements the large object transfer protocol.
 */

/**
 * Standard header files.
 */
#include <stddef.h>
#include <stdlib.h>

/*
 * FreeRTOS includes.
 */
#include "FreeRTOS.h"
#include "timers.h"
#include "aws_iot_large_object_transfer.h"

#define _START_BLOCK                                            ( 0 )

/*
 * Maximum block number  should be twice the window size to avoid receiver
 * treating retransmitted blocks as next window blocks.
 */
#define _BLOCK_NUMBER_MAX( windowSize )                         ( 2 * windowSize )

/*
 * Block number wraps to zero after reaching _BLOCK_NUMBER_MAX - 1.
 */
#define _NEXT_BLOCK_NUMBER( blockNumber, maxBlockNumber )       ( ( blockNumber + 1 ) % maxBlockNumber )

/*
 * Does a round up conversion of number of bits to number of bytes.
 */
#define _NUM_BITS_TO_BYTES( numBits )                          (  ( ( size_t ) numBits + 7 ) >> 3 )
/*
 *
 * Bitmap used to represent the missing block numbers in a window.
 */
#define _BITMAP_SIZE_NUM_BYTES( windowSize )                   ( _NUM_BITS_TO_BYTES( _BLOCK_NUMBER_MAX( windowSize ) )


typedef enum
{
   eAwsIotLargeObjectSessionReceive = 0,
   eAwsIotLargeObjectSessionSend
} _largeObjectSessionType_t;

typedef struct _largeObjectSendSession
{
    AwsIotLargeObjectTransferCallback_t xCallback;
    const uint8_t *pucObject;
    size_t xOffset;
    size_t xSize;
    uint16_t usNextBlock;
    uint16_t usMaxBlocks;
    TimerHandle_t xAckReceivedTimer;
} _largeObjectSendSession_t;

typedef struct _largeObjectReceiveSession
{
    AwsIotLargeObjectTransferCallback_t xCallback;
    AwsIotLargeObjectDataReceiveCallback_t xDataCallback;
    size_t xOffset;
    uint8_t *pucData;
    size_t xLength;
    uint16_t usMaxBlocks;
    uint8_t *pucBitmap;
    TimerHandle_t xAckSendTimer;
} _largeObjectReceiveSession_t;

typedef struct _largeObjectSessionInternal
{
    _largeObjectSessionType_t xType;
    AwsIotLargeObjectTransferNetwork_t xNetwork;
    uint16_t usIdentifier;

    union {
        _largeObjectSendSession_t xSend;
        _largeObjectReceiveSession_t xReceive;
    } session;

} _largeObjectSessionInternal_t;

