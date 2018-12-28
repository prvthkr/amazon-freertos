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
 * @brief The file provides an interface to implement large object transfer protocol, which is used
 * to send bigger payloads, greater than MTU size for the link, directly to a peer.
 */


#ifndef AWS_IOT_LARGE_OBJECT_TRANSFER_H_
#define AWS_IOT_LARGE_OBJECT_TRANSFER_H_

#include <stdint.h>

/**
 * @brief Types of events handled within a large object transfer session.
 */
typedef enum AwsIotLargeObjectTransferEvent
{
    AWS_IOT_LARGE_OBJECT_TRANSFER_COMPLETE = 0,       //!< AWS_IOT_LARGE_OBJECT_TRANSFER_COMPLETE  Event indicates the completion of a large object send/receive session.
    AWS_IOT_LARGE_OBJECT_TRANSFER_METADATA_CHANGED,   //!< AWS_IOT_LARGE_OBJECT_TRANSFER_METADATA_CHANGED Event indicates the metadata for the session is changed.
    AWS_IOT_LARGE_OBJECT_TRANSFER_RECEIVE,            //!< AWS_IOT_LARGE_OBJECT_TRANSFER_RECEIVE  Event indicates that a new session has been initiated by a peer.
    AWS_IOT_LARGE_OBJECT_TRANSFER_RESUME,             //!< AWS_IOT_LARGE_OBJECT_TRANSFER_RESUME Event indicates a previous incomplete session is resumed.
    AWS_IOT_LARGE_OBJECT_TRANSFER_ABORTED,            //!< AWS_IOT_LARGE_OBJECT_TRANSFER_ABORTED Event indicates a  session has been aborted by the peer.
    //!<  Aborted session cannot be resumed.
    AWS_IOT_LARGE_OBJECT_TRANSFER_FAILED,             //!< AWS_IOT_LARGE_OBJECT_TRANSFER_FAILED Event indicates failure of a large object transfer session due to
    //!<  various other reasons,or example due to disconnect. Failed sessions can be resumed.
    AWS_IOT_LARGE_OBJECT_TRANSFER_TIMEDOUT            //!< AWS_IOT_LARGE_OBJECT_TRANSFER_TIMEDOUT Event denotes that large object transfer session has timedout.
    //!<  Timedout sessions can be resumed.
} AwsIotLargeObjectTransferEvent_t;

/**
 * @brief Various error types returned by the APIs.
 */
typedef enum AwsIotLargeObjectTransferError
{
    AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS = 0,          //!< AWS_IOT_LARGE_OBJECT_TRANSFER_SUCCESS API is successful.
    AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAM,        //!< AWS_IOT_LARGE_OBJECT_TRANSFER_INVALID_PARAM API failed due to an invalid param as input.
    AWS_IOT_LARGE_OBJECT_TRANSFER_SESSION_IN_PROGRESS,  //!< AWS_IOT_LARGE_OBJECT_TRANSFER_SESSION_IN_PROGRESS  API failed as there is already a session in progress.
    AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY,            //!< AWS_IOT_LARGE_OBJECT_TRANSFER_NO_MEMORY API failed as there is no enough memory for allocation.
    AWS_IOT_LARGE_OBJECT_TRANSFER_BUFFER_TOO_SMALL,     //!< AWS_IOT_LARGE_OBJECT_TRANSFER_BUFFER_TOO_SMALL API failed as the input buffer provided by the user was too small.
    AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR        //!< AWS_IOT_LARGE_OBJECT_TRANSFER_INTERNAL_ERROR API failed due to an internal error.
} AwsIotLargeObjectTransferError_t ;

/**
 *@brief Flags used to check for the presence of a metadata option.
 */
#define _METADATA_FLAG_SIZE                 ( 0x01 )
#define _METADATA_FLAG_BLOCK_SIZE           ( 0x02 )
#define _METADATA_FLAG_WINDOW_SIZE          ( 0x04 )
#define _METADATA_FLAG_TIMEOUT              ( 0x08 )
#define _METADATA_FLAG_RETRANS              ( 0x0A )

/**
 * @brief Metadata structure which contains various options which user can control.
 * User can set an option by first setting the bitmap using the option flag and then setting
 * the value for the option field.
 */
typedef struct
{
    uint32_t bitmap;                  //!< bitmap 32-bit Bitmap contains all the enabled options.
    uint16_t size;                    //!< size Total size of the large object.
    uint16_t blockSize;               //!< blockSize Size of each block for the transfer.
    uint16_t windowSize;              //!< windowSize Number of blocks which can be transferred at once without receiving an acknowledgement.
    uint16_t timeoutMilliseconds;     //!< timeoutMilliseconds Timeout in milliseconds for one window of transfer.
    uint16_t numRetransmissions;      //!< numRetries Number of window retransmissions before the sender fails the session.
} AwsIotLargeObjectTransferMetadata_t;

/**
 * @brief Type represents the handle to a large object transfer session.
 */
typedef void * AwsIotLargeObjectTransferHandle_t;

/**
 * @brief Callback invoked for various events within a large object transfer session.
 */
typedef void ( *AwsIotLargeObjectTransferEventCallback_t ) (
        AwsIotLargeObjectTransferHandle_t handle,
        AwsIotLargeObjectTransferEvent_t eventCode,
        const AwsIotLargeObjectTransferMetadata_t *pMetadata );

/**
 * @brief Creates a new large object transfer session and saves the handle to the new session.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Create(
        AwsIotLargeObjectTransferHandle_t* pHandle,
        AwsIotLargeObjectTransferEventCallback_t callback ) ;

/**
 * @brief Initiates sending a large object to a peer.
 *
 * Initiates a sending a large object by first sending the metadata of the session to the peer.
 * The function blocks until an acknowledgment is received from the peer or the specified timeout is reached.
 * The API is called to also resume a previously failed large object send session.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Send( AwsIotLargeObjectTransferHandle_t handle, const uint8_t* pBuffer, AwsIotLargeObjectTransferMetadata_t *pMetadata );

/**
 * @brief Starts receiving large object from a peer.
 *
 * This function should be called in response to a AWS_IOT_LARGE_OBJECT_TRANSFER_RECEIVE event notification.
 * Prepares the receiver side with the buffer and sends an acknowledgment back to sender.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Receive( AwsIotLargeObjectTransferHandle_t handle, const uint8_t* pBuffer, AwsIotLargeObjectTransferMetadata_t *pMetadata );

/**
 * @brief Sets the metadata for an ongoing large object transfer session.
 *
 * The function is invoked to change the metadata for a large object transfer session.
 * Function sets the metadata locally and returns immediately.
 * The final negotiated metadata will be returned back in the AWS_IOT_LARGE_OBJECT_TRANSFER_METADATA_CHANGED event callback.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_SetMetadata(AwsIotLargeObjectTransferHandle_t handle, AwsIotLargeObjectTransferMetadata_t* pMetadata );

/**
 * @brief Aborts a large object transfer session.
 * Aborts an ongoing large object transfer session. Aborted sessions cannot be resumed.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Abort(AwsIotLargeObjectTransferHandle_t handle);

/**
 * @brief Destroys the handle to a Large Object Transfer session.
 * Frees the resources associated with the session. Throws an error if there is an ongoing large object transfer session.
 */
AwsIotLargeObjectTransferError_t AwsIotLargeObjectTransfer_Destroy( AwsIotLargeObjectTransferHandle_t handle );


#endif /* AWS_IOT_LARGE_OBJECT_TRANSFER_H_ */
