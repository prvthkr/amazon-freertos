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
 * @brief Error code returned by the large object transfer APIs.
 */
typedef enum AwsIotLargeObjectTransferError
{
    eAwsIotLargeObjectTransferSuccess = 0,
    eAwsIotLargeObjectTransferSessionNotFound,
    eAwsIotLargeObjectTransferSessionFound,
    eAwsIotLargeObjectTransferSessionAborted,
    eAwsIotLargeObjectTransferSessionTimedOut,
    eAwsIotLargeObjectTransferInvalidParam,
    eAwsIotLargeObjectTransferNoMemory,
    eAwsIotLargeObjectTransferNetworkError,
    eAwsIotLargeObjectTransferInternalError
} AwsIotLargeObjectTransferError_t ;

typedef enum AwsIotLargeObjectTransferStatus
{
    eAwsIotLargeObjectTransferStart = 0,
    eAwsIotLargeObjectTransferComplete
} AwsIotLargeObjectTransferStatus_t;

/**
 * @brief Keys for control message for large object transfer.
 */
#define largeObjTransferMESSAGE_TYPE_KEY        "m"
#define largeObjTransferSESSION_IDENTIFIER_KEY  "i"
#define largeObjTransferSIZE_KEY                "s"
#define largeObjTransferBLOCK_SIZE_KEY          "b"
#define largeObjTransferWINDOW_SIZE_KEY         "w"
#define largeObjTransferTIMEOUT_KEY             "t"
#define largeObjTransferNUM_RERTANS_KEY         "r"
#define largeObjTransferSESSION_EXPIRY_KEY      "x"
#define largeObjTransferERROR_CODE_KEY          "e"

/**
 * @brief Message types exchanged for large object transfer.
 */
#define largeObjTransferMESSAGE_TYPE_START   ( 0x1 )
#define largeObjTransferMESSAGE_TYPE_ABORT   ( 0x2 )
#define largeObjTransferMESSAGE_TYPE_RESUME  ( 0x3 )
#define largeObjTransferMESSAGE_TYPE_UPDATE  ( 0x4 )
#define largeObjTransferMESSAGE_TYPE_ACK     ( 0x5 )

/**
 * @brief Maximum window size supported for large object transfer.
 */
#define largeObjTransferMAX_WINDOW_SIZE        ( 32768 )


/**
 * @brief Number of parameters for START Message.
 */
#define largeObjTransferNUM_START_MESSAGE_PARAMS   ( 8 )

/**
 * @brief Parameters used for large object transfer.
 */
typedef struct AwsIotLargeObjectTransferParams
{
    uint16_t blockSize;                  //!< blockSize Size of each block for the transfer.
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
typedef BaseType_t ( * AwsIotLargeObjectTransferNetworkRecvCallback_t ) (
        void * pvContext,
        const void * pvReceivedData,
        size_t xDataLength );

/**
 * @brief Structure which wraps the underlying network stack for the large object transfer.
 * Structure contains the open network connection used to send/receive data, the function pointers to set
 * a receive callback and send data over the network connection.
 */
typedef struct AwsIotLargeObjectNetworkIface
{
    void *pvConnection;


    /** Function pointer to send data over a network connection **/
    size_t ( * send )( void * pvConnection,
            const void * const pvMessage ,
            size_t xLength );

    /** Function pointer to set the network receive callback **/
    int32_t ( * set_receive_callback )(
            void * pvConnection,
            void* pvRecvContext,
            AwsIotLargeObjectTransferNetworkRecvCallback_t xNetworkReceiveCb );

} AwsIotLargeObjectNetworkIface_t;

/**
 * @brief Type represents unique handle to a large object transfer session.
 */
typedef void * AwsIotLargeObjectTransferSession_t;

/**
 * @brief Callback used to receive events from large object transfer.
 */
typedef void ( *AwsIotLargeObjectTransferCallback_t )(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferStatus_t xStatus,
        AwsIotLargeObjectTransferError_t xError );

/**
 * @brief Callback invoked for each of the blocks of large object received.
 * Callback will be invoked multiple
 */
typedef void ( *AwsIotLargeObjectDataReceiveCallback_t ) (
        size_t xBlockOffset,
        const uint8_t *pBlockData,
        size_t xBlockLength,
        size_t xTotalLength );

/**
 * @brief Creates a new large object transfer session and saves the handle to the new session.
 *
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_CreateNewSession(
        AwsIotLargeObjectNetworkIface_t* pxNetworkInterface,
        AwsIotLargeObjectTransferSession_t* pxSession );

/**
 * @brief Initiates sending a large object to a peer.
 * Initiates transfer of a large object by sending START message to the peer.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send(
        AwsIotLargeObjectTransferSession_t xSession,
        const uint8_t* pucObject,
        size_t xSize,
        AwsIotLargeObjectTransferParams_t *pxSendParams,
        AwsIotLargeObjectTransferCallback_t xCallback );

/**
 * @brief API invoked by the application to indicate its ready to receive a large object.
 * Sends back an ACK message to the peer to start receiving the blocks. Each block received is
 * handled by the application using the receive callback.
 * This function should be called by the application only in response to a AWS_IOT_LARGE_OBJECT_TRANSFER_RECEIVE event notification.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetReceiveCallback(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferParams_t *pxReceiveParams,
        AwsIotLargeObjectTransferCallback_t xCallback,
        AwsIotLargeObjectDataReceiveCallback_t xDataCallback );
/**
 * @brief Resumes a large object transfer session.
 * Only Sender can resume a previously timedout session. Failed or Aborted sessions cannot be resumed.
 *
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_ResumeSession(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectNetworkIface_t* pxNetworkInterface );

/**
 * @brief Aborts a large object transfer session.
 * Aborts an ongoing large object transfer session. Both receiver and sender can abort a large object transfer sesssion.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_AbortSession( AwsIotLargeObjectTransferSession_t xSession );

/**
 * @brief Destroys the handle to a Large Object Transfer session.
 * Frees the resources associated with the session. Session should be aborted, completed or failed state.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_DestroySession( AwsIotLargeObjectTransferSession_t xSession );

/**
 * @brief Sets the parameters for an ongoing large object transfer session.
 *
 * The function is invoked to change the parameters for a large object transfer session.
 * Function sets the metadata locally and returns immediately.
 * The final negotiated metadata will be returned back in the AWS_IOT_LARGE_OBJECT_TRANSFER_METADATA_CHANGED event callback.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetSendParams(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferParams_t* pParams );

AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetReceiveParams(
        AwsIotLargeObjectTransferSession_t xSession,
        AwsIotLargeObjectTransferParams_t* pParams );

#endif /* AWS_IOT_LARGE_OBJECT_TRANSFER_H_ */
