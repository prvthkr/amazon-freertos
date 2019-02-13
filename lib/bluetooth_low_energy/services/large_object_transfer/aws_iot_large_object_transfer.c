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

#define _INCR_WINDOW( blockno, windowSize )                          ( ( blockno + windowSize ) % ( _NUM_BLOCKS_PER_WINDOW( windowSize ) ) )

#define _INCR_OFFSET( offset, windowSize, blockSize )                ( offset + ( _NUM_BLOCKS_PER_WINDOW( windowSize ) * blockSize ) )
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

#define _RESERVED_BITS_MASK                          (  ( uint8_t ) 0xE0 )

#define _LAST_BLOCK_MASK                             (  ( uint8_t ) 0x1 )

#define _RESUME_SESSION_MASK                         (  ( uint8_t ) 0x2 )

#define _BLOCK_DATA( pucBlock )                      ( *( ( uint8_t ) ( pucBlock +  4 ) ) )

#define _MAX_BLOCK_DATA_LEN( usMTU )                 ( ( usMTU - 5 ) )

#define _BLOCK_LEN( xDataLen )                       ( ( xDataLen + 5 ))

#define _BITMAP( pucACK )                            ( ( uint8_t * ) ( pucACK + 3 ) )

#define _ERROR_CODE( pucACK )                        ( * ( ( uint8_t * ) ( pucACK + 2 ) ) )

#define _BITMAP_LEN( xACKLen )                       ( xACKLen - 3 )

#define _ACK_LENGTH( xBitMapLen )                       ( xBitMapLen + 3 )

#define _SESSION_FREE( xState )                      ( ( xState == eAwsIotLargeObjectTransferInit ) || ( xState == eAwsIotLargeObjectTransferComplete ) )


AwsIotLargeObjectTransferError_t prxSendBlock(
        AwsIotLargeObjectTransferNetworkIface_t* pxNetwork,
        uint16_t usSessionId,
        uint16_t usBlockNum,
        BaseType_t xLastBlock,
        BaseType_t xResume,
        const uint8_t *pucBlockData,
        size_t xDataLength )

{
    size_t xBlockLength = _BLOCK_LEN( xDataLength );
    size_t xSent;
    uint8_t* pucBlock = NULL;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    uint8_t ucFlags = 0x00;

    pucBlock = AwsIotNetwork_Malloc( xBlockLength );
    if( pucBlock != NULL )
    {
        _SESSION_ID( pucBlock ) = usSessionId;
        _BLOCK_NUMBER( pucBlock ) = usBlockNum;

        ucFlags  |= _RESERVED_BITS_MASK;
        if( xLastBlock )
        {
            ucFlags |= _LAST_BLOCK_MASK;
        }

        if( xResume )
        {
            ucFlags |= _RESUME_SESSION_MASK;
        }

        _FLAGS( pucBlock ) = ucFlags;

        memcpy( _BLOCK_DATA( pucBlock ), pucBlockData, xDataLength );

        xSent = pxNetwork->send( pxNetwork->pvConnection,
                             pucBlock,
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

static AwsIotLargeObjectTransferError_t prvSendACK(
        AwsIotLargeObjectTransferNetworkIface_t* pxNetwork,
        uint16_t usSessionId,
        AwsIotLargeObjectTransferError_t xError,
        const uint8_t *pucBitMap,
        size_t xBitMapLength )
{
    uint8_t *pucAck = NULL;
    size_t xAckLength = _ACK_LENGTH( xBitMapLength );
    size_t xSent;

    pucAck = AwsIotNetwork_Malloc( xAckLength );
    if( pucAck != NULL )
    {
        _SESSION_ID( pucAck ) = usSessionId;
        _ERROR_CODE( pucAck ) = ( uint8_t ) xError;
        memcpy( _BITMAP( pucAck ), pucBitMap, xBitMapLength );
        xSent = pxNetwork->send( pxNetwork->pvConnection,
                                 pucAck,
                                 xAckLength );
        if( xSent < xAckLength )
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NETWORK_ERROR;
        }

        AwsIotNetwork_Free( pucAck );
    }

    return xError;
}


AwsIotLargeObjectTransferError_t prxSendWindow( AwsIotLargeObjectSendSession_t *pxSession )
{
    size_t xOffset, xLength;
    uint8_t *pucBlock;
    uint16_t ulIndex, ulBlockNum ;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    BaseType_t xLastBlock = pdFALSE;
    AwsIotLargeObjectTransferContext_t *pxContext = (  AwsIotLargeObjectTransferContext_t* ) ( pxSession->pvContext );

    for ( ulIndex = 0; ( ulIndex < pxSession->usWindowSize ) && ( !xLastBlock ); ulIndex++ )
    {
        ulBlockNum = ( pxSession->usBlockNum + ulIndex );
        xOffset = pxSession->xOffset
                + ( ulBlockNum * pxSession->usBlockSize );
        xLength = pxSession->usBlockSize;
        if( ( xOffset + xLength ) >= pxSession->xObjectLength )
        {
            xLength = ( pxSession->xObjectLength - xOffset );
            xLastBlock = pdTRUE;
        }
        pucBlock = ( pxSession->xObjectLength + xOffset );
        xError = prxSendBlock( &pxContext->xNetworkIface,
                               pxSession->usSessionID,
                               ulBlockNum,
                               xLastBlock,
                               pdFALSE,
                               pucBlock,
                               xLength );
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
        AwsIotLargeObjectSendSession_t *pxSession,
        const uint8_t *pucBitMap,
        size_t xBitMapLength )
{
    size_t xOffset, xLength;
    uint16_t ulIndex, ulBlockNum ;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    uint8_t *pucBlock;
    BaseType_t xLastBlock = pdFALSE;
    AwsIotLargeObjectTransferContext_t *pxContext = (  AwsIotLargeObjectTransferContext_t* ) ( pxSession->pvContext );

    if( xBitMapLength != _BITMAP_SIZE( pxSession->usWindowSize ) )
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PACKET;
    }
    else
    {
        for ( ulIndex = 0; ( ulIndex < pxSession->usWindowSize ) && ( !xLastBlock ); ulIndex++ )
        {
            ulBlockNum = ( pxSession->usBlockNum + ulIndex );
            if( prxIsValueSet( pucBitMap, ulBlockNum ) == pdTRUE )
            {
                xOffset = pxSession->xOffset + ( ulBlockNum * pxSession->usBlockSize );
                xLength = pxSession->usBlockSize;
                if( ( xOffset + xLength ) >= pxSession->xObjectLength )
                {
                    xLength = ( pxSession->xObjectLength - xOffset );
                    xLastBlock = pdTRUE;
                }
                pucBlock = ( pxSession->pucObject + xOffset );
                xError = prxSendBlock( &pxContext->xNetworkIface,
                                       pxSession->usSessionID,
                                       ulBlockNum,
                                       xLastBlock,
                                       pdFALSE,
                                       pucBlock,
                                       xLength );
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
    AwsIotLargeObjectSendSession_t* pxSession = ( AwsIotLargeObjectSession_t *) pvTimerGetTimerID( xTimer );
    configASSERT( pxSession != NULL );

    if( pxSession->usRetriesLeft > 0 )
    {
         if( prxSendWindow( pxSession ) == pdFALSE )
         {
             configPRINTF(( "Failed to retransmit window, session = %d\n", pxSession->usSessionID ));
             pxSession->xState = eAwsIotLargeObjectTransferFailed;
         }
         else
         {
             pxSession->usRetriesLeft--;
             if( xTimerStart( xTimer, 0 ) == pdFALSE )
             {
                 configPRINTF(( "Failed to create retransmit timer, session = %d\n", pxSession->usSessionID ));
                 pxSession->xState = eAwsIotLargeObjectTransferFailed;
             }
         }
    }
    else
    {
        pxSession->xState = eAwsIotLargeObjectTransferFailed;
    }
}

static void prvProcessBlock( AwsIotLargeObjectReceiveSession_t* pxSession, const uint8_t* pucBlock, size_t xLength )
{
    /** TODO: To be implemented **/

}

static void prvProcessACK( AwsIotLargeObjectSendSession_t* pxSession, const uint8_t* pucACK, size_t xLength )
{
    AwsIotLargeObjectTransferError_t xError = ( AwsIotLargeObjectTransferError_t ) ( _ERROR_CODE( pucACK ) );
    const uint8_t *pucBitMap;
    uint16_t usBitMapLen;

    if( xLength < 3 )
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PACKET;
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
         if( xTimerStop( pxSession->xRetransmitTimer, 0UL ) != pdPASS )
         {
             xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
         }
    }
    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        usBitMapLen = _BITMAP_LEN( xLength );
        if( usBitMapLen != 0 )
        {
            pucBitMap = _BITMAP( pucACK );
            xError = prxRetransmitMissingBlocks(
                    pxSession,
                    pucBitMap,
                    usBitMapLen );
        }
        else
        {
            /** Increase by window size */
            pxSession->usBlockNum = _INCR_WINDOW( pxSession->usBlockNum, pxSession->usWindowSize );
            if( pxSession->usBlockNum == 0 )
            {
                pxSession->xOffset = _INCR_OFFSET(
                        pxSession->xOffset,
                        pxSession->usWindowSize,
                        pxSession->usBlockSize );
            }

            if( pxSession->xOffset < pxSession->xObjectLength )
            {
                xError = prxSendWindow( pxSession );
                if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
                {
                    if( xTimerStart( pxSession->xRetransmitTimer, 0UL ) != pdPASS )
                    {
                        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
                    }
                }
            }
            else
            {
                pxSession->xState = eAwsIotLargeObjectTransferComplete;

            }
        }
    }

    if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        pxSession->xState = eAwsIotLargeObjectTransferFailed;
    }

}

AwsIotLargeObjectTransferError_t prxCreateReceiveSession(
        AwsIotLargeObjectReceiveSession_t *pxSession,
        AwsIotLargeObjectTransferContext_t *pxContext,
        uint16_t usSessionId )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    BaseType_t xRet = pdFALSE;

    pxSession->pvContext = pxContext;
    pxSession->usSessionID = usSessionId;
    pxSession->xOffset = 0;
    pxSession->usWindowSize = pxContext->xParameters.windowSize;
    pxSession->usBlockSize  = _MAX_BLOCK_DATA_LEN( pxContext->xParameters.usMTU );
    pxSession->usNumRetries = pxContext->xParameters.numRetransmissions;
    pxSession->usRetriesLeft = pxSession->usNumRetries;

    pxSession->xAckTimer = xTimerCreate(
            "ACKTimer",
            pd_MS_TO_TICKS( pxContext->xParameters.timeoutMilliseconds * 2 ),
            pdFALSE,
            ( &pxSession ),
            prvSendACK );

    if( pxSession->xAckTimer == NULL )
    {
        xError =  AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
    }

    return xError;
}


static void prvNetworkReceiveCallback(
        void * pvContext,
        const void * pvReceivedData,
        size_t xDataLength )
{
    AwsIotLargeObjectTransferContext_t* pxContext = ( AwsIotLargeObjectTransferContext_t * ) pvContext;
    uint8_t *pucData = ( uint8_t * ) pvReceivedData;
    uint16_t usSessionID, usIndex;
    AwsIotLargeObjectSession_t *pxSession;
    BaseType_t xSessionFound = pdFALSE, xFreeSession = pdFALSE;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;

    configASSERT( pxContext != NULL );
    usSessionID = _SESSION_ID( pucData );

    for( usIndex = 0; usIndex < pxContext->usNumSendSessions; usIndex++ )
    {
        pxSession = & ( pxContext->pxSendSessions[ usIndex ] );
        if( pxSession->xSend.usSessionID == usSessionID )
        {
            prvProcessACK( &pxSession->xSend, pucData, xDataLength );
            xSessionFound = pdTRUE;
            break;
        }
    }

    if( !xSessionFound )
    {
        for( usIndex = 0; usIndex < pxContext->usNumRecvSessions; usIndex++ )
        {
            pxSession = & ( pxContext->pxRecvSessions[ usIndex ] );
            if( pxSession->xRecv.usSessionID == usSessionID )
            {
                prvProcessBlock( &pxSession->xRecv, pucData, xDataLength );

                xSessionFound = pdTRUE;
                break;
            }
        }
    }

    if( !xSessionFound )
    {
        for( usIndex = 0; usIndex < pxContext->usNumRecvSessions; usIndex++ )
            {
                pxSession = & ( pxContext->pxRecvSessions[ usIndex ]);
                if( _SESSION_FREE( pxSession->xRecv.xState ) )
                {
                    xFreeSession = pdTRUE;
                    break;

                }
            }
        if( xFreeSession )
        {

            xError = prxCreateReceiveSession(
                                   &pxSession->xRecv,
                                   pxContext,
                                   usSessionID );

            if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
                prvHandleBlock( pxSession, pucData, xDataLength );
            }


            if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
                AwsIotLogError( "Cannot create a new session for session id %d, error = %d",
                                usSessionID,
                                xError );
            }
        }
        else
        {
            AwsIotLogError( "Cannot create a new session for session id %d, max sessions reached.", usSessionID );

        }
    }
}


AwsIotLargeObjectTransferError_t prxCreateSendSession(
        AwsIotLargeObjectSendSession_t *pxSession,
        AwsIotLargeObjectTransferContext_t *pxContext,
        uint16_t usSessionId,
        const uint8_t *pucObject,
        size_t xObjectLength )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    BaseType_t xRet = pdFALSE;

    pxSession->pvContext = pxContext;
    pxSession->usSessionID = usSessionId;
    pxSession->pucObject = pucObject;
    pxSession->xObjectLength = xObjectLength;
    pxSession->xOffset = 0;
    pxSession->usBlockNum = 0;
    pxSession->usWindowSize = pxContext->xParameters.windowSize;
    pxSession->usBlockSize  = _MAX_BLOCK_DATA_LEN( pxContext->xParameters.usMTU );
    pxSession->usNumRetries = pxContext->xParameters.numRetransmissions;

    pxSession->xRetransmitTimer =
            xTimerCreate(
                    "RetransmitTimer",
                    pd_MS_TO_TICKS( pxContext->xParameters.timeoutMilliseconds * 2 ),
                    pdFALSE,
                    ( pxSession ),
                    prvRetransmitWindow );

    if( pxSession->xRetransmitTimer == NULL )
    {
        xError =  AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        xError = prxSendWindow( pxSession );
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        if( xTimerStart( pxSession->xRetransmitTimer, 0 ) == pdFALSE )
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
        }
    }
    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Init(
        AwsIotLargeObjectTransferContext_t* pxContext,
        uint16_t usNumSendSessions,
        uint16_t usNumReceiveSessions )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    AwsIotLargeObjectTransferNetworkIface_t *pxNetworkIface;
    uint16_t usIdx, usBufferSize;
    AwsIotLargeObjectReceiveSession_t* pxRecvSession;

    if( pxContext == NULL )
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAM;
    }
    else
    {
        pxNetworkIface = &( pxContext->xNetworkIface );
        pxNetworkIface->setNetworkReceiveCallback(
                pxNetworkIface->pvConnection,
                pxContext,
                prvNetworkReceiveCallback );

        /** Allocate memory for sessions **/

        pxContext->pxSendSessions = AwsIotNetworkMalloc( sizeof( AwsIotLargeObjectSession_t ) * usNumSendSessions );
        if( pxContext->pxSendSessions != NULL )
        {
            memset(  pxContext->pxSendSessions,
                     0x00,
                     ( sizeof( AwsIotLargeObjectSession_t ) * usNumSendSessions ) );
            pxContext->usNumSendSessions = usNumSendSessions;
        }
        else
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
        }

        if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
        {
            pxContext->pxRecvSessions = AwsIotNetworkMalloc( sizeof( AwsIotLargeObjectSession_t ) * usNumReceiveSessions );
            if( pxContext->pxRecvSessions != NULL )
            {
                memset(  pxContext->pxRecvSessions,
                         0x00,
                         ( sizeof( AwsIotLargeObjectSession_t ) * usNumSendSessions ) );
                pxContext->usNumRecvSessions = usNumReceiveSessions;

                /** Allocate buffer size to hold one window per receive session **/
                usBufferSize = ( pxContext->xParameters.windowSize * pxContext->xParameters.usMTU );

                for( usIdx = 0; usIdx < usNumReceiveSessions; usIdx++ )
                {
                    pxRecvSession = &( pxContext->pxRecvSessions[ usIdx ].xRecv );
                    pxRecvSession->pucRecvBuffer = AwsIotNetworkMalloc( usBufferSize );
                    if( pxRecvSession->pucRecvBuffer != NULL )
                    {
                        pxRecvSession->xRecvBufLength = usBufferSize;
                    }
                    else
                    {
                        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
                        break;

                    }
                }
            }
            else
            {
                xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
            }
        }
    }

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferContext_t* pxContext,
        const uint8_t *pucObject,
        const size_t xObjectLength,
        uint16_t* pusSessionID )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_MAX_SESSIONS_REACHED;
    uint32_t usIndex;
    AwsIotLargeObjectSession_t* pxSession;

    for( usIndex = 0; usIndex < pxContext->usNumSendSessions; usIndex++ )
    {
        pxSession = & ( pxContext->pxSendSessions[ usIndex ]);
        if( _SESSION_FREE( pxSession->xSend.xState ) )
        {
            xError = prxCreateSendSession(
                    &pxSession->xSend,
                    pxContext,
                    usIndex,
                    pucObject,
                    xObjectLength );
            if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {

               *pusSessionID = usIndex;
            }

            break;
        }
    }

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Resume( AwsIotLargeObjectTransferContext_t* pxContext, uint16_t usSessionID )
{
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAM;
    AwsIotLargeObjectSendSession_t *pxSession;
    uint16_t usIndex;


    for( usIndex = 0; usIndex < pxContext->usNumSendSessions; usIndex++ )
    {
        pxSession = & ( pxContext->pxSendSessions[ usIndex ].xSend);
        if( ( pxSession->usSessionID == usSessionID )
                && ( pxSession->xState != eAwsIotLargeObjectTransferDataSend )
                && ( pxSession->xOffset < pxSession->xObjectLength ) )
        {
            xError = prxSendWindow( pxSession );
            if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
                if( xTimerStart( pxSession->xRetransmitTimer, 0UL ) != pdPASS )
                {
                    xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
                }
            }
        }
    }

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Abort( uint16_t usSessionID )
{
    return AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
}

void AwsIotLargeObjectTransfer_Destroy( AwsIotLargeObjectTransferContext_t* pxContext )
{
    uint16_t usIdx;
    AwsIotLargeObjectReceiveSession_t *pxRecvSession;

    if( pxContext->pxSendSessions != NULL )
    {
        AwsIotNetwork_Free( pxContext->pxSendSessions );
        pxContext->pxSendSessions = NULL;
    }

    for( usIdx = 0; usIdx < pxContext->usNumRecvSessions; usIdx++ )
    {
        pxRecvSession = &( pxContext->pxRecvSessions->xRecv );
        if( pxRecvSession->pucRecvBuffer != NULL )
        {
            AwsIotNetwork_Free( pxRecvSession->pucRecvBuffer );
            pxRecvSession->pucRecvBuffer = NULL;
        }
    }

    if( pxContext->pxRecvSessions != NULL )
    {
        AwsIotNetwork_Free( pxContext->pxRecvSessions );
        pxContext->pxRecvSessions = NULL;
    }

}
