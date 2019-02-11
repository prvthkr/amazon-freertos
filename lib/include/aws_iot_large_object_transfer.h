/*
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
 */

/**
 * @file aws_iot_large_object_transfer.h
 * @brief The file provides an interface for large object transfer protocol. Large Object transfer is used
 * to send larger payloads, ( greater than link MTU size) directly to a peer.
 */


#ifndef AWS_IOT_LARGE_OBJECT_TRANSFER_H_
#define AWS_IOT_LARGE_OBJECT_TRANSFER_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Status codes passed in from large object transfer callback invocations.
 */
typedef enum AwsIotLargeObjectTransferStatus
{
    eAwsIotLargeObjectTransferInit = 0,
    eAwsIotLargeObjectTransferInProgress,//!< eAwsIotLargeObjectTransferInProgress
    eAwsIotLargeObjectTransferFailed,    //!< eAwsIotLargeObjectTransferFailed
    eAwsIotLargeObjectTransferComplete   //!< eAwsIotLargeObjectTransferComplete
} AwsIotLargeObjectTransferStatus_t;

/**
 * @brief Error codes returned by large object transfer APIs.
 */
typedef enum AwsIotLargeObjectTransferError
{
    AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS = 0,
    AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY,
    AWS_IOT_LARGE_OBJECT_TRANSFER_MAX_SESSIONS_REACHED,
    AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAMS,
    AWS_IOT_LARGE_OBJECT_TRANSFER_NETWORK_ERROR,
    AWS_IOT_LARGE_OBJECT_TRANSFER_TIMED_OUT,
    AWS_IOT_LARGE_OBJECT_TRANSFER_EXPIRED,
    AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR
} AwsIotLargeObjectTransferError_t;

typedef enum AwsIotLargeObjectTransferType
{
    AWS_IOT_LARGE_OBJECT_SESSION_SEND = 0,
    AWS_IOT_LARGE_OBJECT_SESSION_RECIEVE
} AwsIotLargeObjectTransferType_t;

/**
 * @brief Network parameters negotiated for large object transfer.
 */
typedef struct AwsIotLargeObjectTransferParams
{
    uint16_t usMTU;                      //!< usMTU : Maximum size of the packet which can be transmitted over the connection.
    uint16_t windowSize;                 //!< windowSize Number of blocks which can be transferred at once without receiving an acknowledgement.
    uint16_t timeoutMilliseconds;        //!< timeoutMilliseconds Timeout in milliseconds for one window of transfer.
    uint16_t numRetransmissions;         //!< numRetransmissions Number of retransmissions.
    uint32_t sessionExpiryMilliseconds;  //!< sessionTimeout Session timeout in milliseconds.

} AwsIotLargeObjectTransferParams_t;



/**
 * @brief Callback used to receive bytes of maximum MTU size from a physical network.
 * Returns pdFALSE if message is incomplete. Caller needs to invoke the function again with the complete message.
 *         pdTRUE if the message parsing is complete.
 *
 */
typedef void( * AwsIotLargeObjectTransferReceiveCallback_t ) (
        void * pvContext,
        const void * pvReceivedData,
        size_t xDataLength );

typedef struct AwsIotLargeObjectTransferNetwork
{
    /** Pointer to a connection object **/
    void *pvConnection;

    /** Function pointer to send data over a connection **/
    typedef size_t ( * send )(
            void * pvConnection,
            const void * const pvMessage ,
            size_t xLength );

    /** Function pointer to set the receive callback for a connection **/
    typedef int32_t ( * setReceiveCallback )(
            void * pvConnection,
            void* pvContext,
            AwsIotLargeObjectTransferReceiveCallback_t );

} AwsIotLargeObjectTransferNetwork_t;


typedef struct _windowBitMap
{
    uint16_t usSize;
    union {
        uint8_t ucValue[4];
        uint8_t* pucPtr;
    } data;
} _windowBitMap_t;

typedef struct _largeObjectSendSession
{
    size_t xOffset;
    uint16_t usWindowSize;
    uint16_t usBlockSize;
    uint16_t usBlockNum;
    uint16_t usRetriesLeft;
    uint16_t usNumRetries;
    TimerHandle_t xRetransmitTimer;
    uint8_t *pucData;
    size_t xDataLength;

} _largeObjectSendSession_t;


typedef struct _largeObjectRecvSession
{
    size_t xOffset;
    uint16_t usWindowSize;
    uint16_t usBlockSize;
    _windowBitMap_t xBlockSReceived;
    uint16_t usRetriesLeft;
    uint16_t usNumRetries;
    TimerHandle_t xAckTimer;
    AwsIotLargeObjectDataReceiveCallback_t xDataCallback;
} _largeObjectRecvSession_t;

typedef struct AwsIotLargeObjectTransferSession
{
    uint16_t usUUID;
    AwsIotLargeObjectTransferStatus_t xState;
    AwsIotLargeObjectTransferNetwork_t *pxNetwork;
    AwsIotLargeObjectTransferType_t xType;

    union {
        _largeObjectSendSession_t xSend;
        _largeObjectRecvSession_t xReceive;
    } session;

} AwsIotLargeObjectTransferSession_t;



/**
 * @brief Callback used to receive events from large object transfer.
 */
typedef void ( *AwsIotLargeObjectTransferCallback_t )(
        AwsIotLargeObjectTransferSession_t* pxSession,
        AwsIotLargeObjectTransferStatus_t xStatus );



/**
 * @brief Structure which wraps the underlying context for the large object transfer sessions.
 * Context should be created before creating all large object sessions and destroyed only when all the sessions are completed.
 */
typedef struct AwsIotLargeObjectTransferContext
{

    AwsIotLargeObjectTransferNetwork_t xNetwork;

    /** Parameters used for large object transfer **/
    AwsIotLargeObjectTransferParams_t xParameters;

    /** Callback used to receive events on large object transfer sessions. **/
    AwsIotLargeObjectTransferCallback_t xCallback;

    /** Total number of sessions **/
    uint16_t usNumSessions;

    /** Array that holds the large object transfer sessions **/
    AwsIotLargeObjectTransferSession_t* pxSessions;

} AwsIotLargeObjectTransferContext_t;


/**
 * @brief Callback invoked for each of the blocks of large object received.
 * Callback will be invoked multiple times with the offset within the large object,
 * data, and the length of the data.
 */
typedef void ( *AwsIotLargeObjectDataReceiveCallback_t ) (
        size_t xOffset,
        const uint8_t *pucData,
        size_t xDataLength );

/**
 * @brief Destroys the resources for a context.
 * Frees the resources associated with the context. All Sessions should be aborted, completed or failed state.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Init( AwsIotLargeObjectTransferContext_t* pxContext );

/**
 * @brief Initiates sending a large object to a peer.
 * Initiates transfer of a large object by sending START message to the peer.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferContext_t* pxContext,
        const uint8_t* pucObject,
        size_t xSize,
        AwsIotLargeObjectTransferSession_t** pxSession );

/**
 * @brief API invoked by the application to indicate its ready to receive a large object.
 * Each block received is
 * handled by the application using the receive data callback.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetReceiveCallback(
        AwsIotLargeObjectTransferContext_t* pxContext,
        AwsIotLargeObjectTransferSession_t* pxSession,
        AwsIotLargeObjectDataReceiveCallback_t xDataCallback );
/**
 * @brief Resumes a large object transfer session.
 * Only Sender can resume a previously timedout session. Failed or Aborted sessions cannot be resumed.
 *
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Reconnect( AwsIotLargeObjectTransferSession_t* pxSession );

/**
 * @brief Aborts a large object transfer session.
 * Aborts an ongoing large object transfer session. Both receiver and sender can abort a large object transfer sesssion.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Abort( AwsIotLargeObjectTransferSession_t* pxSession );

/**
 * @brief Destroys the resources for a context.
 * Frees the resources associated with the context. All Sessions should be aborted, completed or failed state.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Destroy( AwsIotLargeObjectTransferContext_t* pxContext );

#endif /* AWS_IOT_LARGE_OBJECT_TRANSFER_H_ */
