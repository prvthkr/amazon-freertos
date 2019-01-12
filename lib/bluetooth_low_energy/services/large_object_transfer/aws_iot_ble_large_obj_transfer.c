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
 * @file aws_iot_ble_large_obj_transfer.c
 * @brief File implements the large object transfer protocol as a BLE GATT service.
 */
#include "FreeRTOS.h"
#include "aws_iot_large_object_transfer.h"
#include "aws_iot_serializer.h"
#include "private/aws_ble_service_internals.h"
#include "FreeRTOS_POSIX/time.h"

static uint16_t usUUID = 0;

#define _START_BLOCK                                            ( 0 )

/*
 * Maximum block number  should be twice the window size to avoid receiver
 * treating retransmitted blocks as next window blocks.
 */
#define _BLOCK_NUMBER_MAX( windowSize )                         ( 2 * windowSize )

/*
 * Block number wraps to zero after reaching _BLOCK_NUMBER_MAX - 1.
 */
#define _NEXT_BLOCK_NUMBER( blockNumber, maxBlockNumber )          ( ( blockNumber + 1 ) % maxBlockNumber )

/*
 * Does a round up conversion of number of bits to number of bytes.
 */
#define _NUM_BITS_TO_BYTES( numBits )                          (  ( ( size_t ) numBits + 7 ) >> 3 )
/*
 *
 * Bitmap used to represent the missing block numbers in a window.
 */
#define _BITMAP_SIZE_NUM_BYTES( windowSize )                   ( _NUM_BITS_TO_BYTES( _BLOCK_NUMBER_MAX( windowSize ) )

typedef struct _largeObjectSendSession
{
    uint16_t usUUID;
    const uint8_t *pucObject;
    AwsIotLargeObjectTransferParams_t pxParams;
    size_t xOffsetSent;
    uint16_t usNextBlock;
    uint16_t usMaxBlocks;
    timer_t xAckReceiveTimer;
} _largeObjectSendSession_t;

typedef struct _largeObjectReceiveSession
{
    uint16_t usUUID;
    AwsIotLargeObjectBlockRecvCallback_t xBlockRecieveCallback;
    AwsIotLargeObjectTransferParams_t xParams;
    size_t xOffsetReceived;
    uint16_t usMaxBlocks;
    uint8_t *pucBitmap;
    timer_t xAckSendTimer;
} _largeObjectReceiveSession_t;

typedef struct _largeObjectSession
{
    AwsIotLargeObjectNetworkIface_t xNetworkIface;
    AwsIotLargeObjectTransferCallback_t xCallback;
    _largeObjectSendSession_t* pxSendSession;
    _largeObjectReceiveSession_t* pxReceiveSession;
} _largeObjectSession_t;


static BaseType_t prxSerializeSTARTMessage( _largeObjectSendSession_t* pxSendSession, uint8_t *pucBuffer, size_t* pxLength )
{
    BaseType_t xRet = pdFALSE;
    AwsIotSerializerEncoderObject_t xEncoder = AWS_IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_STREAM;
    AwsIotSerializerEncoderObject_t xContainer = AWS_IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    AwsIotSerializerScalarData_t xData = { 0 };
    AwsIotSerializerError_t xError = AWS_IOT_SERIALIZER_SUCCESS;

    xError = bleMESSAGE_ENCODER.init( &xEncoder, pucBuffer, pxLength );




}

static BaseType_t prxStartLargeObjectTransfer( AwsIotLargeObjectNetworkIface_t* pxNetwork, _largeObjectSendSession_t* pxSendSession )
{
    BaseType_t xRet = pdFALSE;



    return xRet;

}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Create(
        AwsIotLargeObjectNetworkIface_t* pxNetworkInterface,
        AwsIotLargeObjectTransferCallback_t xCallback,
        AwsIotLargeObjectTransferHandle_t* pxHandle )
{
    _largeObjectSession_t *pxSession = pvPortMalloc( sizeof( _largeObjectSession_t ) );
    AwsIotLargeObjectTransferError_t xError;
    if( pxSession != NULL )
    {
        memeset( pxSession, 0, sizeof(_largeObjectSession_t ) );
        pxSession->xNetworkIface = ( *pxNetworkInterface );
        pxSession->xCallback = xCallback;
        *pxHandle = ( AwsIotLargeObjectTransferHandle_t  ) pxSession;
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    }
    else
    {
        xError = AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY;
    }

    return xError;
}

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferHandle_t xHandle,
        const uint8_t* pucObject,
        AwsIotLargeObjectTransferParams_t *pxObjectParams )
{
    _largeObjectSession_t *pxSession = ( _largeObjectSession_t* ) xHandle;
    _largeObjectSendSession_t *pxSendSession = NULL;
    AwsIotLargeObjectTransferError_t xError = AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS;
    struct sigevent timerEvent =
    {
        .sigev_notify = SIGEV_THREAD,
        .sigev_signo = 0,
        .sigev_value = ( void * ) pxSession,
        .sigev_notify_function = NULL,
        .sigev_notify_attributes = NULL
    };

    if( pxSession->pxSendSession == NULL )
    {
        pxSendSession = pvPortMalloc( sizeof( _largeObjectSendSession_t  ) );
        if( pxSendSession != NULL )
        {
            pxSendSession->pucObject = pucObject;
            pxSendSession->pxParams = ( *pxObjectParams );
            pxSendSession->usNextBlock = _START_BLOCK;
            pxSendSession->xOffsetSent = 0;
            pxSendSession->usUUID = ++usUUID;
            if( timer_create( CLOCK_REALTIME, &timerEvent, &pxSendSession->xAckReceiveTimer ) == 0 )
            {
                pxSession->pxSendSession = pxSendSession;
                if( prx)

            }
            else
            {
                xError = AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR;
            }

            if( xError != AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS )
            {
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
        AwsIotLargeObjectTransferHandle_t handle,
        AwsIotLargeObjectBlockRecvCallback_t receiveCallback,
        AwsIotLargeObjectTransferParams_t *pObjectParams )
{
    _largeObjectSession_t* pSession = ( _largeObjectSession_t* ) handle;
    pSession->params = (*pObjectParams);
    pSession->recieveCallback = receiveCallback;

}

/**
 * @brief Sets the metadata for an ongoing large object transfer session.
 *
 * The function is invoked to change the metadata for a large object transfer session.
 * Function sets the metadata locally and returns immediately.
 * The final negotiated metadata will be returned back in the AWS_IOT_LARGE_OBJECT_TRANSFER_METADATA_CHANGED event callback.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetParams(
        AwsIotLargeObjectTransferHandle_t handle,
        AwsIotLargeObjectTransferParams_t* pParams );

/**
 * @brief Resumes a large object transfer session.
 * Session should not be failed or aborted.
 *
 * @param handle Handle to the large object transfer session.
 * @return
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Resume( AwsIotLargeObjectTransferHandle_t handle );
/**
 * @brief Aborts a large object transfer session.
 * Aborts an ongoing large object transfer session. Aborted sessions cannot be resumed.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Abort( AwsIotLargeObjectTransferHandle_t handle );

/**
 * @brief Destroys the handle to a Large Object Transfer session.
 * Frees the resources associated with the session. Session should be aborted or completed.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Destroy( AwsIotLargeObjectTransferHandle_t handle );
