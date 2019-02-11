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

/*
 * Maximum blocks sper windown should be twice the window size to avoid receiver
 * treating retransmitted blocks as next window blocks.
 */
#define _NUM_BLOCKS_PER_WINDOW( windowSize )                         ( 2 * windowSize )

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

#define _SESSION_FREE( xState )                     ( ( xState == eAwsIotLargeObjectTransferInit ) || ( xState == eAwsIotLargeObjectTransferComplete ) )


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

        xSent = pxNetwork->send( pxNetwork->pvConnection,
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
        ulBlockNum = ( pxSendSession->usBlockNum + ulIndex );
        xOffset = pxSendSession->xOffset
                + ( ulBlockNum * pxSendSession->usBlockSize );
        xLength = pxSendSession->usBlockSize;
        if( ( xOffset + xLength ) >= pxSendSession->xDataLength )
        {
            xLength = ( pxSendSession->xDataLength - xOffset );
            xLastBlock = pdTRUE;
        }
        pucBlock = ( pxSendSession->pucData + xOffset );
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
        AwsIotLargeObjectTransferContext_t* pxContext,
        _largeObjectSendSession_t *pxSendSession,
        const uint8_t *pucBitMap,
        size_t xBitMapLength )
{
    size_t xOffset, xLength;
    uint16_t ulIndex, ulBlockNum ;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    uint8_t *pucBlock;
    BaseType_t xLastBlock = pdFALSE;

    if( xBitMapLength != _BITMAP_SIZE( pxSendSession->usWindowSize ) )
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAMS;
    }
    else
    {
        for ( ulIndex = 0; ( ulIndex < pxSendSession->usWindowSize ) && ( !xLastBlock ); ulIndex++ )
        {
            ulBlockNum = ( pxSendSession->usBlockNum + ulIndex );
            if( prxIsValueSet( pucBitMap, ulBlockNum ) == pdTRUE )
            {
                xOffset = pxSendSession->xOffset + ( ulBlockNum * pxSendSession->usBlockSize );
                xLength = pxSendSession->usBlockSize;
                if( ( xOffset + xLength ) >= pxSendSession->xDataLength )
                {
                    xLength = ( pxSendSession->xDataLength - xOffset );
                    xLastBlock = pdTRUE;
                }
                pucBlock = ( pxSendSession->pucData + xOffset );
                xError = prxSendBlock( pxContext, pucBlock, xLength, xLastBlock );
                if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
                {
                    break;
                }
            }
        }
    }

    return xError;
}

void prvRetransmitWindow( TimerHandle_t xTimer )
{
    AwsIotLargeObjectTransferSession_t *pxSession;
    _largeObjectSendSession_t* pxSendSession;

    pxSession = ( AwsIotLargeObjectTransferSession_t *) pvTimerGetTimerID( xTimer );
    configASSERT( pxSession != NULL );

    pxSendSession = & ( pxSession->session.xSend );

    if( pxSendSession->usRetriesLeft > 0 )
    {
         if( prxSendWindow( &pxSession->pxNetwork, pxSendSession ) == pdFALSE )
         {
             configPRINTF(( "Failed to retransmit window, session = %d\n", pxSession->usUUID ));
             pxSession->xState = eAwsIotLargeObjectTransferFailed;
         }
         else
         {
             pxSendSession->usRetriesLeft--;
             if( xTimerStart( xTimer, 0 ) == pdFALSE )
             {
                 configPRINTF(( "Failed to create retransmit timer, session = %d\n", pxSession->usUUID ));
                 pxSession->xState = eAwsIotLargeObjectTransferFailed;
             }
         }
    }
    else
    {
        pxSession->xState = eAwsIotLargeObjectTransferFailed;
    }
}

AwsIotLargeObjectTransferError_t prxInitiateLargeObjectTransfer(
        AwsIotLargeObjectTransferSession_t *pxSession,
        AwsIotLargeObjectTransferParams_t *pxParams )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    BaseType_t xRet = pdFALSE;
    _largeObjectSendSession_t *pxSendSession = & ( pxSession->session.xSend );

    pxSendSession->xOffset = 0;
    pxSendSession->usBlockNum = 0;
    pxSendSession->usWindowSize = pxParams->windowSize;
    pxSendSession->usBlockSize  = _MAX_BLOCK_DATA_LEN( pxParams->usMTU );
    pxSendSession->usNumRetries = pxParams->numRetransmissions;

    pxSendSession->xRetransmitTimer = xTimerCreate(
            "RetransmitTimer",
            pd_MS_TO_TICKS( pxParams->timeoutMilliseconds * 2 ),
            pdFALSE,
            ( pxSession ),
            prvRetransmitWindow );

    if( pxSendSession->xRetransmitTimer == NULL )
    {
        xError =  AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        xError = prxSendWindow( pxSession->pxNetwork, pxSendSession );
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

static void prvReceiveCallback(
        void * pvContext,
        const void * pvReceivedData,
        size_t xDataLength )
{
   uint8_t *pucBlock = ( const uint8_t * ) pvReceivedData;
   configASSERT( pvReceivedData != NULL );
   uint16_t usUUID = _SESSION_ID( pucBlock );
   uint16_t usBlockNumber = _BLOCK_NUMBER( pucBlock );
}


AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Init( AwsIotLargeObjectTransferContext_t* pxContext )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    AwsIotLargeObjectTransferNetwork_t *pxNetwork = & ( pxContext->xNetwork );

    pxNetwork->setReceiveCallback(
            pxNetwork->pvConnection,
            pxContext,
            prvReceiveCallback );

    memset( pxContext->pxSessions, 0, sizeof( AwsIotLargeObjectTransferSession_t ) * pxContext->usNumSessions );

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferContext_t* pxContext,
        AwsIotLargeObjectTransferSession_t** ppxSession )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_MAX_SESSIONS_REACHED;
    uint32_t usIndex;
    AwsIotLargeObjectTransferSession_t* pxSessionPtr;

    for( usIndex = 0; usIndex < pxContext->usNumSessions; usIndex++ )
    {
        pxSessionPtr = & ( pxContext->pxSessions[ usIndex ]);
        if( _SESSION_FREE( pxSessionPtr->xState ) )
        {
            pxSessionPtr->xState = eAwsIotLargeObjectTransferInProgress;
            pxSessionPtr->pxNetwork = &pxContext->xNetwork;
            pxSessionPtr->usUUID = usIndex;
            pxSessionPtr->xType = AWS_IOT_LARGE_OBJECT_SESSION_SEND;
            xError = prxInitiateLargeObjectTransfer( pxSessionPtr, &pxContext->xParameters );
            if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
               *ppxSession = pxSessionPtr;
            }

            break;
        }
    }

    return xError;
}
