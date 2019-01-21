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
    eAwsIotLargeObjectTransferStart = 0, //!< eAwsIotLargeObjectTransferStart
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
    AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_NETWORK_PARAMS,
    AWS_IOT_LARGE_OBJECT_TRANSFER_NETWORK_ERROR,
    AWS_IOT_LARGE_OBJECT_TRANSFER_TIMED_OUT,
    AWS_IOT_LARGE_OBJECT_TRANSFER_EXPIRED
} AwsIotLargeObjectTransferError_t;

/**
 * @brief Maximum window size supported for large object transfer.
 */
#define largeObjectTransferMAX_WINDOW_SIZE        ( 32768 )

#define largeObjectTransferMAX_SESSIONS           ( 65535 )
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

/**
 * @brief Structure which wraps the underlying network stack for the large object transfer.
 * Structure contains the open network connection used to send/receive data, the function pointers to set
 * a receive callback and send data over the network connection.
 */
typedef struct AwsIotLargeObjectTransferNetwork
{
    void *pvNetworkConnection;

    /** Function pointer to send data over a network connection **/
    typedef size_t ( * AwsIotLargeObjectTransferSendCallback_t )(
            void * pvConnection,
            const void * const pvMessage ,
            size_t xLength );

    /** Function pointer to set the network receive callback **/
    typedef int32_t ( * AwsIotLargeObjectTransferSetReceiveCallback_t )(
            void * pvNetworkConnection,
            void* pvRecvContext,
            AwsIotLargeObjectTransferReceiveCallback_t xNetworkReceiveCb );

    /** Function pointers to get the network params used **/
    typedef void ( *AwsIotLargeObjectTransferGetParams_t )(
            AwsIotLargeObjectTransferParams_t* pxParams );

} AwsIotLargeObjectTransferNetwork_t;

/**
 * @brief Type represents unique handle to a large object transfer session.
 */
typedef void *AwsIotLargeObjectTransferSession_t;

/**
 * @brief Callback used to receive events from large object transfer.
 */
typedef void ( *AwsIotLargeObjectTransferCallback_t )(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferStatus_t xStatus );

/**
 * @brief Callback invoked for each of the blocks of large object received.
 * Callback will be invoked multiple
 */
typedef void ( *AwsIotLargeObjectDataReceiveCallback_t ) (
        AwsIotLargeObjectTransferSession_t xSession,
        const uint8_t *xData,
        size_t xDataLength );

/**
 * @brief Initiates sending a large object to a peer.
 * Initiates transfer of a large object by sending START message to the peer.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferNetwork_t* pxNetwork,
        AwsIotLargeObjectTransferSession_t* pxSession,
        const uint8_t* pucObject,
        size_t xSize,
        AwsIotLargeObjectTransferCallback_t xSendCallback );

/**
 * @brief API invoked by the application to indicate its ready to receive a large object.
 * Each block received is
 * handled by the application using the receive data callback.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetReceiveCallback(
        AwsIotLargeObjectTransferNetwork_t* pxNetwork,
        AwsIotLargeObjectTransferCallback_t xRecvCallback,
        AwsIotLargeObjectDataReceiveCallback_t xDataCallback );
/**
 * @brief Resumes a large object transfer session.
 * Only Sender can resume a previously timedout session. Failed or Aborted sessions cannot be resumed.
 *
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Reconnect(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferNetwork_t* pxNetwork );

/**
 * @brief Aborts a large object transfer session.
 * Aborts an ongoing large object transfer session. Both receiver and sender can abort a large object transfer sesssion.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Abort( AwsIotLargeObjectTransferSession_t xSession );

/**
 * @brief Destroys the handle to a Large Object Transfer session.
 * Frees the resources associated with the session. Session should be aborted, completed or failed state.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Destroy( AwsIotLargeObjectTransferSession_t xSession );

#endif /* AWS_IOT_LARGE_OBJECT_TRANSFER_H_ */
