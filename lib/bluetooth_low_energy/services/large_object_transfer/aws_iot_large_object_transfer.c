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

/* Build using a config header, if provided. */
#ifdef AWS_IOT_CONFIG_FILE
    #include AWS_IOT_CONFIG_FILE
#endif

/*
 * FreeRTOS includes.
 */
#include "FreeRTOS.h"
#include "timers.h"
#include "aws_iot_large_object_transfer.h"


#define _MAX_LARGE_OBJECT_SEND_SESSIONS                         ( 1 )

#define _MAX_LARGE_OBJECT_RECV_SESSIONS                         ( 1 )

#define _BLOCK_NUMBER_MIN                                       ( 0 )

/*
 * Maximum block number  should be twice the window size to avoid receiver
 * treating retransmitted blocks as next window blocks.
 */
#define _BLOCK_NUMBER_MAX( windowSize )                         ( 2 * windowSize )

/*
 * Does a round up conversion of number of bits to number of bytes.
 */
#define _BITS_TO_BYTES( numBits )                          (  ( ( size_t ) numBits + 7 ) >> 3 )
/*
 *
 * Size of Bitmap used to represent the missing block numbers in a window.
 */
#define _BITMAP_SIZE( windowSize )                   ( _BITS_TO_BYTES( _BLOCK_NUMBER_MAX( windowSize ) ) )

#define _SESSION_ID( pucBlock )                      ( *( ( uint16_t ) pucBlock ) )

#define _BLOCK_NUMBER( pucBlock )                    ( *( ( uint16_t ) ( pucBlock + 2 ) ) )

#define _FLAGS( pucBlock )                           ( *( ( uint8_t ) ( pucBlock +  4 ) ) )

#define _BLOCK_DATA( pucBlock )                      ( *( ( uint8_t ) ( pucBlock +  4 ) ) )

#define _MAX_BLOCK_DATA_LEN( usMTU )                 ( ( usMTU - 5 ) )

#define _BLOCK_LEN( xDataLen )                       ( ( xDataLen + 5 ))

typedef struct _largeObjectTransferBitMap
{
    uint16_t usSize;
    union {
        uint8_t ucValue[4];
        uint8_t* pucPtr;
    } data;
} _largeObjectTransferBitMap_t;


typedef struct _largeObjectSendSession
{
    const uint8_t *pucObject;
    size_t xOffset;
    size_t xSize;
    uint16_t usBlockNumber;
    uint16_t usBlockSize;
    uint16_t usWindowSize;
    uint16_t usMaxBlocks;
    uint16_t usNumRetries;
    TimerHandle_t xRetransmitTimer;
} _largeObjectSendSession_t;

typedef struct _largeObjectReceiveSession
{
    AwsIotLargeObjectDataReceiveCallback_t xDataCallback;
    size_t xOffset;
    uint8_t *pucData;
    size_t xLength;
    uint16_t usMaxBlocks;
    uint16_t usNumRetries;
    _largeObjectTransferBitMap_t xBitMap;
    TimerHandle_t xAckTimer;
} _largeObjectReceiveSession_t;

typedef struct _largeObjectSessionInternal
{
    BaseType_t xUsed;
    uint16_t usUUID;
    AwsIotLargeObjectTransferNetwork_t xNetwork;
    AwsIotLargeObjectTransferCallback_t xCallback;
    AwsIotLargeObjectTransferStatus_t xStatus;
    union {
        _largeObjectSendSession_t xSend;
        _largeObjectReceiveSession_t xReceive;
    } session;
} _largeObjectSessionInternal_t;

_largeObjectSessionInternal_t xSendSessions[ _MAX_LARGE_OBJECT_SEND_SESSIONS ] = { 0 };
_largeObjectSessionInternal_t xRecieveSessions[ _MAX_LARGE_OBJECT_RECV_SESSIONS ] = { 0 };

AwsIotLargeObjectTransferError_t prxSendBlock(
        AwsIotLargeObjectTransferNetwork_t* pxNetwork,
        const uint8_t *pucBlockData,
        size_t xDataLength,
        BaseType_t xLastBlock )

{
    size_t xBlockLength = _BLOCK_LEN( xDataLength );
    size_t xSent;
    uint8_t* pucBlock = NULL;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;

    pucBlock = AwsIotNetwork_Malloc( xBlockLength );
    if( pucBlock != NULL )
    {
        xSent = pxNetwork->send( pxNetwork->pvNetworkConnection,
                             ( const void * ) pucBlockData,
                             xBlockLength );
        if( xSent < xBlockLength )
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NETWORK_ERROR;
        }
        AwsIotNetwork_Free( pucBlock );
    }
    else
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
    }

    return xError;

}


AwsIotLargeObjectTransferError_t prxSendWindow( AwsIotLargeObjectTransferNetwork_t* pxNetwork, _largeObjectSendSession_t *pxSendSession )
{
    size_t xOffset, xLength;
    uint16_t ulIndex, ulBlockNum ;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    uint8_t *pucBlock;
    BaseType_t xLastBlock = pdFALSE;

    for ( ulIndex = 0; ( ulIndex < pxSendSession->usWindowSize ) && ( !xLastBlock ); ulIndex++ )
    {
        ulBlockNum = ( pxSendSession->usBlockNumber + ulIndex );
        xOffset = pxSendSession->xOffset + ( ulBlockNum * pxSendSession->usBlockSize );
        xLength = pxSendSession->usBlockSize;
        if( ( xOffset + xLength ) >= pxSendSession->xSize )
        {
            xLength = ( pxSendSession->xSize - xOffset );
            xLastBlock = pdTRUE;
        }
        pucBlock = ( pxSendSession->pucObject + xOffset );
        xError = prxSendBlock( pxNetwork, pucBlock, xLength, xLastBlock );
        if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
        {
            break;
        }
    }

    return xError;
}

BaseType_t prxIsValueSet( const uint8_t *pucBitMap, uint16_t ulValue )
{
    BaseType_t xRet = pdFALSE;
    uint16_t ulIndex = ( ulValue >> 3 ); /* Divide by 8 */
    uint16_t ulPos   = ( ulValue & 0x7 );

    if( pucBitMap[ ulIndex ] & ( 0x1 << ulPos ) )
    {
        xRet = pdTRUE;
    }

    return xRet;
}

AwsIotLargeObjectTransferError_t prxRetransmitMissingBlocks(
        AwsIotLargeObjectTransferNetwork_t* pxNetwork,
        _largeObjectSendSession_t *pxSendSession,
        const uint8_t *pucBitMap,
        size_t xBitMapLength )
{
    size_t xOffset, xLength;
    uint16_t ulIndex, ulBlockNum ;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    uint8_t *pucBlock;
    BaseType_t xLastBlock = pdFALSE;

    if( xBitMapLength == _BITMAP_SIZE( pxSendSession->usWindowSize ) )
    {
        for ( ulIndex = 0; ( ulIndex < pxSendSession->usWindowSize ) && ( !xLastBlock ); ulIndex++ )
        {
            ulBlockNum = ( pxSendSession->usBlockNumber + ulIndex );
            if( prxIsValueSet( pucBitMap, ulBlockNum ) == pdTRUE )
            {
                xOffset = pxSendSession->xOffset + ( ulBlockNum * pxSendSession->usBlockSize );
                xLength = pxSendSession->usBlockSize;
                if( ( xOffset + xLength ) >= pxSendSession->xSize )
                {
                    xLength = ( pxSendSession->xSize - xOffset );
                    xLastBlock = pdTRUE;
                }
                pucBlock = ( pxSendSession->pucObject + xOffset );
                xError = prxSendBlock( pxNetwork, pucBlock, xLength, xLastBlock );
                if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
                {
                    break;
                }
            }
        }
    }
    else
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAMS;
    }

    return xError;
}

void prvRetransmitWindow( TimerHandle_t xTimer )
{
    _largeObjectSessionInternal_t *pxSession;
    _largeObjectSendSession_t* pxSendSession;
    BaseType_t xResult;

    pxSession = ( _largeObjectSessionInternal_t * ) pvTimerGetTimerID( xTimer );
    configASSERT( pxSession != NULL );
    pxSendSession = &( pxSession->session.xSend );

    if( pxSendSession->usNumRetries > 0 )
    {
         if( prxSendWindow( &pxSession->xNetwork, pxSendSession ) == pdFALSE )
         {
             configPRINTF(( "Failed to retransmit window, session = %d\n", pxSession->usUUID ));
             pxSession->xStatus = eAwsIotLargeObjectTransferFailed;
         }
         else
         {
             pxSendSession->usNumRetries--;
             if( xTimerStart( xTimer, 0 ) == pdFALSE )
             {
                 configPRINTF(( "Failed to create retransmit timer, session = %d\n", pxSession->usUUID ));
                 pxSession->xStatus = eAwsIotLargeObjectTransferFailed;
             }
         }
    }
    else
    {
        pxSession->xStatus = eAwsIotLargeObjectTransferFailed;
    }
}

AwsIotLargeObjectTransferError_t prxInitiateLargeObjectTransfer( _largeObjectSessionInternal_t *pxSession )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    AwsIotLargeObjectTransferParams_t xParams = { 0 };
    BaseType_t xRet = pdFALSE;
    _largeObjectSendSession_t *pxSendSession;

    xRet = pxSession->xNetwork.getParams( &xParams );
    if( xRet == pdTRUE )
    {
        pxSendSession = &pxSession->session.xSend;
        pxSendSession->xOffset = 0;
        pxSendSession->usBlockNumber = _BLOCK_NUMBER_MIN;
        pxSendSession->usMaxBlocks = _BLOCK_NUMBER_MAX( xParams.windowSize );
        pxSendSession->usWindowSize = xParams.windowSize;
        pxSendSession->usBlockSize = _MAX_BLOCK_DATA_LEN( xParams.usMTU );
        pxSendSession->usNumRetries = xParams.numRetransmissions;

        pxSendSession->xRetransmitTimer = xTimerCreate(
                "LoTRetransmitTimer",
                pd_MS_TO_TICKS( xParams.timeoutMilliseconds * 2 ),
                pdFALSE,
                (pxSession),
                prvRetransmitWindow );

        if( pxSendSession->xRetransmitTimer == NULL )
        {
            xError =  AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
        }

    }
    else
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAMS;
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        xError = prxSendWindow( &pxSession->xNetwork, pxSendSession );
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        if( xTimerStart( pxSendSession->xRetransmitTimer, 0 ) == pdFALSE )
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
        }
    }

    return xError;
}

void vLargeObjectNetworkReceiveCallback(
        void * pvContext,
        const void * pvReceivedData,
        size_t xDataLength )
{
   uint8_t *pucBlock = ( const uint8_t * ) pvReceivedData;
   configASSERT( pvReceivedData != NULL );
   uint16_t usUUID = _SESSION_ID( pucBlock );
   uint16_t usBlockNumber = _BLOCK_NUMBER( pucBlock );

}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferNetwork_t* pxNetwork,
        AwsIotLargeObjectTransferSession_t* pxSession,
        const uint8_t* pucObject,
        size_t xSize,
        AwsIotLargeObjectTransferCallback_t xSendCallback )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    _largeObjectSessionInternal_t *pxInternalSession;
    uint32_t ulIndex;

    for( ulIndex = 0; ulIndex < _MAX_LARGE_OBJECT_SEND_SESSIONS; ulIndex++ )
    {
        if( xSendSessions[ ulIndex ].xUsed == pdFALSE )
        {
            pxInternalSession = &xSendSessions[ ulIndex ];
            pxInternalSession->xUsed = pdTRUE;
            pxInternalSession->xNetwork = *pxNetwork;
            pxInternalSession->session.xSend.pucObject = pucObject;
            pxInternalSession->session.xSend.xSize = xSize;
            pxInternalSession->xCallback = xSendCallback;
            xError = prxInitiateLargeObjectTransfer( pxInternalSession );
            if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
                ( *pxSession ) = ( AwsIotLargeObjectTransferSession_t ) pxInternalSession;
            }

            break;
        }
    }

    if( ulIndex == _MAX_LARGE_OBJECT_SEND_SESSIONS )
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_MAX_SESSIONS_REACHED;

    }

    return xError;
}
