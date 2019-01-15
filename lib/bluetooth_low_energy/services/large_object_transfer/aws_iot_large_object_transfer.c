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

/**
 * FreeRTOS includes.
 */
#include "FreeRTOS.h"
#include "timers.h"

/**
 * Private includes.
 */
#include "private/aws_ble_service_internals.h"

/**
 * Header files.
 */
#include "aws_iot_large_object_transfer.h"
#include "aws_iot_serializer.h"

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


#define _VALID_SERIALIZER_RESULT( result, pxSerializerBuffer )   \
    (  ( result == AWS_IOT_SERIALIZER_SUCCESS ) || (  ( !pxSerializerBuffer ) && ( result == AWS_IOT_SERIALIZER_BUFFER_TOO_SMALL ) ) )

typedef struct _largeObjectSendSession
{
    uint16_t usUUID;
    const uint8_t *pucObject;
    AwsIotLargeObjectTransferParams_t pxParams;
    size_t xOffset;
    uint16_t usNextBlock;
    uint16_t usMaxBlocks;
    TimerHandle_t xAckReceivedTimer;
} _largeObjectSendSession_t;

typedef struct _largeObjectReceiveSession
{
    uint16_t usUUID;
    AwsIotLargeObjectReceiveCallback_t xRecieveCallback;
    AwsIotLargeObjectTransferParams_t xParams;
    size_t xOffset;
    uint16_t usMaxBlocks;
    uint8_t *pucBlockBitmap;
    TimerHandle_t xAckSendTimer;
} _largeObjectReceiveSession_t;

typedef struct _largeObjectTransferSession
{
    AwsIotLargeObjectNetworkIface_t xNetworkIface;
    AwsIotLargeObjectTransferEventCallback_t xEventCallback;
    _largeObjectSendSession_t* pxSendSession;
    _largeObjectReceiveSession_t* pxReceiveSession;
} _largeObjectTransferSession_t;

static uint16_t prusGenerateUUID( void )
{
    static uint16_t usUUID = 0;
    return usUUID++;
}

static BaseType_t prxSerializeStartMessage( _largeObjectSendSession_t* pxSendSession, uint8_t *pucBuffer, size_t* pxLength )
{
    BaseType_t xRet = pdTRUE;
    AwsIotSerializerEncoderObject_t xEncoder = AWS_IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_STREAM;
    AwsIotSerializerEncoderObject_t xContainer = AWS_IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    AwsIotSerializerScalarData_t xValue = { 0 };
    AwsIotSerializerError_t xResult = AWS_IOT_SERIALIZER_SUCCESS;

    xResult = bleMESSAGE_ENCODER.init( &xEncoder, pucBuffer, ( *pxLength ) );

    if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
    {
        xResult = bleMESSAGE_ENCODER.openContainer( &xEncoder, &xContainer, largeObjTransferNUM_START_MESSAGE_PARAMS );
        if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer) )
        {
            xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
            xValue.value.signedInt = largeObjTransferMESSAGE_TYPE_START;
            xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferMESSAGE_TYPE_KEY, xValue );

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->usUUID;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferSESSION_IDENTIFIER_KEY, xValue );
            }

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->pxParams.objectSize;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferSIZE_KEY, xValue );
            }

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->pxParams.blockSize;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferBLOCK_SIZE_KEY, xValue );
            }

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->pxParams.windowSize;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferWINDOW_SIZE_KEY, xValue );
            }

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->pxParams.timeoutMilliseconds;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferTIMEOUT_KEY, xValue );
            }

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->pxParams.numRetransmissions;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferNUM_RERTANS_KEY, xValue );
            }

            if( _VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                xValue.type = AWS_IOT_SERIALIZER_SCALAR_SIGNED_INT;
                xValue.value.signedInt = pxSendSession->pxParams.sessionExpiryMilliseconds;
                xResult = bleMESSAGE_ENCODER.appendKeyValue( &xContainer, largeObjTransferSESSION_EXPIRY_KEY, xValue );
            }

            if( !_VALID_SERIALIZER_RESULT( xResult, pucBuffer ) )
            {
                configPRINTF(( "Failed to encode the message, error = %d\n", xResult ));
                xRet = pdFALSE;
            }
            else
            {
                if( pucBuffer == NULL )
                {
                    *pxLength = bleMESSAGE_ENCODER.getExtraBufferSizeNeeded( &xEncoder );
                }
                else
                {
                    *pxLength = bleMESSAGE_ENCODER.getEncodedSize( &xEncoder, pucBuffer );
                }

            }

            bleMESSAGE_ENCODER.closeContainer( &xEncoder, &xContainer );

        }
        else
        {
            configPRINTF(("Failed to open the container, error = %d", xResult ));
            xRet = pdFALSE;
        }

        bleMESSAGE_ENCODER.destroy( &xEncoder );

    }
    else
    {
        configPRINTF(( "Failed to initialize the encoder, error = %d\n", xResult ));
        xRet = pdFALSE;
    }

    return xRet;
}

static AwsIotLargeObjectTransferError_t prxStartLargeObjectTransferSession(
        AwsIotLargeObjectNetworkIface_t* pxNetwork,
        _largeObjectSendSession_t* pxSendSession )
{
    BaseType_t xResult = pdFALSE;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
    uint8_t *pucMessage;
    size_t xLength, xSent;

    xResult = prxSerializeStartMessage( pxSendSession, NULL, &xLength );
    if( xResult == pdTRUE )
    {
        pucMessage = pvPortMalloc( xLength );
        if( pucMessage != NULL )
        {
            xResult = prxSerializeStartMessage( pxSendSession, pucMessage, &xLength );
            if( xResult == pdTRUE )
            {
                xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
            }
        }
        else
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
        }
    }

    if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
    {
        xSent = pxNetwork->send( pxNetwork->pvNetworkConnection, pucMessage, xLength );
        if( xSent < xLength )
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NETWORK_ERROR;
        }
    }

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_CreateSession(
        AwsIotLargeObjectNetworkIface_t* pxNetworkInterface,
        AwsIotLargeObjectTransferEventCallback_t xEventCallback,
        AwsIotLargeObjectTransferSession_t* pxSession )
{
    _largeObjectTransferSession_t *pxInternal = pvPortMalloc( sizeof( _largeObjectTransferSession_t ) );
    AwsIotLargeObjectTransferError_t xError;
    if( pxSession != NULL )
    {
        pxInternal->xNetworkIface = ( *pxNetworkInterface );
        pxInternal->xEventCallback = xEventCallback;
        *pxSession = ( AwsIotLargeObjectTransferSession_t  ) pxInternal;
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    }
    else
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
    }

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferSession_t xSession,
        const uint8_t* pucObject,
        AwsIotLargeObjectTransferParams_t *pxObjectParams )
{
    _largeObjectTransferSession_t *pxSession = ( _largeObjectTransferSession_t* ) xSession;
    _largeObjectSendSession_t *pxSendSession;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    BaseType_t xTimerStarted = pdFALSE;


    if( pxSession->pxSendSession == NULL )
    {
        pxSendSession = pvPortMalloc( sizeof( _largeObjectSendSession_t  ) );
        if( pxSendSession != NULL )
        {
            pxSendSession->pucObject = pucObject;
            pxSendSession->pxParams = ( *pxObjectParams );
            pxSendSession->usNextBlock = _START_BLOCK;
            pxSendSession->xOffset = 0;
            pxSendSession->usUUID = prusGenerateUUID();

            pxSendSession->xAckReceivedTimer = xTimerCreate(
                    "LoTTimer",
                    pdMS_TO_TICKS( pxSendSession->pxParams.timeoutMilliseconds ),
                    0,
                    ( void* ) pxSession,
                    NULL );

            if( pxSendSession->xAckReceivedTimer != NULL )
            {
                pxSession->pxSendSession = pxSendSession;
                xError = prxStartLargeObjectTransferSession( &pxSession->xNetworkIface, pxSendSession );
            }
            else
            {
                xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
            }

            if( xError == AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
                xTimerStarted = xTimerStart( pxSendSession->xAckReceivedTimer, 0L );
                if( xTimerStarted == pdFALSE )
                {
                    xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
                }
            }

            if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
                if( pxSendSession->xAckReceivedTimer != NULL )
                {
                    xTimerDelete( pxSendSession->xAckReceivedTimer, 0L );
                    pxSendSession->xAckReceivedTimer = NULL;
                }

                vPortFree( pxSendSession );
                pxSession->pxSendSession = NULL;
            }
        }
        else
        {
            xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
        }

    }
    else
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SESSION_IN_PROGRESS;
    }

    return xError;
}


/**
 * @brief Starts receiving large object from a peer.
 *
 * This function should be called in response to a AWS_IOT_LARGE_OBJECT_TRANSFER_RECEIVE event notification.
 * Prepares the receiver side with the buffer and sends an acknowledgment back to sender.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Receive(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectReceiveCallback_t xReceiveCallback,
        AwsIotLargeObjectTransferParams_t *pObjectParams )
{
    return AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
}

/**
 * @brief Sets the metadata for an ongoing large object transfer session.
 *
 * The function is invoked to change the metadata for a large object transfer session.
 * Function sets the metadata locally and returns immediately.
 * The final negotiated metadata will be returned back in the AWS_IOT_LARGE_OBJECT_TRANSFER_METADATA_CHANGED event callback.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetSessionParams(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferParams_t* pParams )
{
    return AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
}

/**
 * @brief Resumes a large object transfer session.
 * Session should not be failed or aborted.
 *
 * @param handle Handle to the large object transfer session.
 * @return
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_ResumeSession( AwsIotLargeObjectTransferSession_t xSession )
{
    return AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
}
/**
 * @brief Aborts a large object transfer session.
 * Aborts an ongoing large object transfer session. Aborted sessions cannot be resumed.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_AbortSession( AwsIotLargeObjectTransferSession_t xSession )
{
    return AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
}

/**
 * @brief Destroys the handle to a Large Object Transfer session.
 * Frees the resources associated with the session. Session should be aborted or completed.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_DestroySession( AwsIotLargeObjectTransferSession_t xSession )
{
    return AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
}
