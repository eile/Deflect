/*********************************************************************/
/* Copyright (c) 2013-2017, EPFL/Blue Brain Project                  */
/*                          Daniel.Nachbaur@epfl.ch                  */
/*                          Raphael Dumusc <raphael.dumusc@epfl.ch>  */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/*   1. Redistributions of source code must retain the above         */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer.                                                  */
/*                                                                   */
/*   2. Redistributions in binary form must reproduce the above      */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer in the documentation and/or other materials       */
/*      provided with the distribution.                              */
/*                                                                   */
/*    THIS  SOFTWARE IS PROVIDED  BY THE  UNIVERSITY OF  TEXAS AT    */
/*    AUSTIN  ``AS IS''  AND ANY  EXPRESS OR  IMPLIED WARRANTIES,    */
/*    INCLUDING, BUT  NOT LIMITED  TO, THE IMPLIED  WARRANTIES OF    */
/*    MERCHANTABILITY  AND FITNESS FOR  A PARTICULAR  PURPOSE ARE    */
/*    DISCLAIMED.  IN  NO EVENT SHALL THE UNIVERSITY  OF TEXAS AT    */
/*    AUSTIN OR CONTRIBUTORS BE  LIABLE FOR ANY DIRECT, INDIRECT,    */
/*    INCIDENTAL,  SPECIAL, EXEMPLARY,  OR  CONSEQUENTIAL DAMAGES    */
/*    (INCLUDING, BUT  NOT LIMITED TO,  PROCUREMENT OF SUBSTITUTE    */
/*    GOODS  OR  SERVICES; LOSS  OF  USE,  DATA,  OR PROFITS;  OR    */
/*    BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON ANY THEORY OF    */
/*    LIABILITY, WHETHER  IN CONTRACT, STRICT  LIABILITY, OR TORT    */
/*    (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN ANY WAY OUT    */
/*    OF  THE  USE OF  THIS  SOFTWARE,  EVEN  IF ADVISED  OF  THE    */
/*    POSSIBILITY OF SUCH DAMAGE.                                    */
/*                                                                   */
/* The views and conclusions contained in the software and           */
/* documentation are those of the authors and should not be          */
/* interpreted as representing official policies, either expressed   */
/* or implied, of The University of Texas at Austin.                 */
/*********************************************************************/

#include "StreamSendWorker.h"

#include "NetworkProtocol.h"
#include "Segment.h"
#include "SizeHints.h"

#include <iostream>
#include <sstream>

namespace
{
const unsigned int SEGMENT_SIZE = 512;
const unsigned int SMALL_IMAGE_SIZE = 64;
}

namespace deflect
{
StreamSendWorker::StreamSendWorker(Socket& socket, const std::string& id)
    : _socket(socket)
    , _id(id)
    , _dequeuedRequests(std::thread::hardware_concurrency() / 2)
{
    _imageSegmenter.setNominalSegmentDimensions(SEGMENT_SIZE, SEGMENT_SIZE);
}

StreamSendWorker::~StreamSendWorker()
{
    stop();
}

void StreamSendWorker::run()
{
    _running = true;
    while (true)
    {
        if (!_running)
            break;

        size_t count = 0;
        if (!_pendingFinish)
            count = _requests.wait_dequeue_bulk(_dequeuedRequests.begin(),
                                                _dequeuedRequests.size());
        else
        {
            // in case we encountered a finish request, get all remaining send
            // requests w/o waiting
            count = _requests.try_dequeue_bulk(_dequeuedRequests.begin(),
                                               _dequeuedRequests.size());

            // no more pending sends, now process the finish request and reset
            // for next finish
            if (count == 0)
            {
                count = 1;
                _finishRequest.isFinish = false; // reset this to process this
                                                 // request now
                _dequeuedRequests[0] = _finishRequest;
                _pendingFinish = false;
            }
        }

        for (size_t i = 0; i < count; ++i)
        {
            bool success = true;
            auto& request = _dequeuedRequests[i];

            // postpone a finish request to maintain order (as the lockfree
            // does not guarantee order)
            if (request.isFinish)
            {
                if (_pendingFinish)
                {
                    if (request.promise)
                        request.promise->set_exception(std::make_exception_ptr(
                            std::runtime_error("Already have pending finish")));
                    continue;
                }

                _finishRequest = request;
                _pendingFinish = true;
                continue;
            }

            try
            {
                for (auto& task : request.tasks)
                {
                    if (!task())
                    {
                        success = false;
                        break;
                    }
                }

                if (request.promise)
                    request.promise->set_value(success);
            }
            catch (...)
            {
                if (request.promise)
                    request.promise->set_exception(std::current_exception());
            }
        }
    }
}

void StreamSendWorker::stop()
{
    {
        _running = false;
        _enqueueRequest(std::vector<Task>());
    }

    quit();
    wait();

    Request request;
    while (_requests.try_dequeue(request))
    {
        if (request.promise)
            request.promise->set_value(false);
    }
}

Stream::Future StreamSendWorker::enqueueImage(const ImageWrapper& image,
                                              const bool finish)
{
    if (_pendingFinish)
    {
        return make_exception_future<bool>(
            std::runtime_error("Pending finish, no send allowed"));
    }

    if (image.compressionPolicy != COMPRESSION_ON && image.pixelFormat != RGBA)
    {
        return make_exception_future<bool>(std::invalid_argument(
            "Currently, RAW images can only be sent in RGBA format. Other "
            "formats support remain to be implemented."));
    }

    if (image.compressionPolicy == COMPRESSION_ON)
    {
        if (image.compressionQuality < 1 || image.compressionQuality > 100)
        {
            std::stringstream msg;
            msg << "JPEG compression quality must be between 1 and 100, got "
                << image.compressionQuality << std::endl;
            return make_exception_future<bool>(
                std::invalid_argument(msg.str()));
        }
    }

    std::vector<Task> tasks;

    if (image.width <= SMALL_IMAGE_SIZE && image.height <= SMALL_IMAGE_SIZE)
    {
        try
        {
            auto segment = _imageSegmenter.createSingleSegment(image);

            tasks.emplace_back(
                [this, segment] { return _sendSegment(segment); });

            // as we expect to encounter a lot of these small sends, be
            // optimistic and fulfill the promise already to reduce load in the
            // send thread (c.f. lock ops performance on KNL)
            _requests.enqueue({nullptr, tasks, false});
            if (finish)
                return enqueueFinish();
            return make_ready_future(true);
        }
        catch (...)
        {
            return make_exception_future<bool>(std::current_exception());
        }
    }
    else
        tasks.emplace_back([this, image] { return _sendImage(image); });

    if (finish)
        tasks.emplace_back([this] { return _sendFinish(); });

    return _enqueueRequest(std::move(tasks));
}

Stream::Future StreamSendWorker::enqueueFinish()
{
    return _enqueueRequest({[this] { return _sendFinish(); }}, true);
}

Stream::Future StreamSendWorker::enqueueOpen()
{
    return _enqueueRequest({[this] {
        return _send(MESSAGE_TYPE_PIXELSTREAM_OPEN,
                     QByteArray::number(NETWORK_PROTOCOL_VERSION));
    }});
}

Stream::Future StreamSendWorker::enqueueClose()
{
    return _enqueueRequest({[this] { return _send(MESSAGE_TYPE_QUIT, {}); }});
}

Stream::Future StreamSendWorker::enqueueObserverOpen()
{
    return _enqueueRequest({[this] {
        return _send(MESSAGE_TYPE_OBSERVER_OPEN,
                     QByteArray::number(NETWORK_PROTOCOL_VERSION));
    }});
}

Stream::Future StreamSendWorker::enqueueBindRequest(const bool exclusive)
{
    return _enqueueRequest({[this, exclusive] {
        return _send(exclusive ? MESSAGE_TYPE_BIND_EVENTS_EX
                               : MESSAGE_TYPE_BIND_EVENTS,
                     {});
    }});
}

Stream::Future StreamSendWorker::enqueueSizeHints(const SizeHints& hints)
{
    return _enqueueRequest({[this, hints] {
        return _send(MESSAGE_TYPE_SIZE_HINTS,
                     QByteArray{(const char*)(&hints), sizeof(SizeHints)});
    }});
}

Stream::Future StreamSendWorker::enqueueData(const QByteArray data)
{
    return _enqueueRequest(
        {[this, data] { return _send(MESSAGE_TYPE_DATA, data); }});
}

Stream::Future StreamSendWorker::_enqueueRequest(std::vector<Task>&& tasks,
                                                 const bool isFinish)
{
    PromisePtr promise(new Promise);
    _requests.enqueue({promise, tasks, isFinish});
    return promise->get_future();
}

bool StreamSendWorker::_sendImage(const ImageWrapper& image)
{
    const auto sendFunc =
        std::bind(&StreamSendWorker::_sendSegment, this, std::placeholders::_1);
    return _imageSegmenter.generate(image, sendFunc);
}

bool StreamSendWorker::_sendImageView(const View view)
{
    return _send(MESSAGE_TYPE_IMAGE_VIEW,
                 QByteArray{(const char*)(&view), sizeof(View)});
}

bool StreamSendWorker::_sendSegment(const Segment& segment)
{
    if (segment.exception)
        std::rethrow_exception(segment.exception);

    if (segment.view != _currentView)
    {
        if (!_sendImageView(segment.view))
            return false;
        _currentView = segment.view;
    }

    auto message = QByteArray{(const char*)(&segment.parameters),
                              sizeof(SegmentParameters)};
    message.append(segment.imageData);
    return _send(MESSAGE_TYPE_PIXELSTREAM, message, false);
}

bool StreamSendWorker::_sendFinish()
{
    return _send(MESSAGE_TYPE_PIXELSTREAM_FINISH_FRAME, {});
}

bool StreamSendWorker::_send(const MessageType type, const QByteArray& message,
                             const bool waitForBytesWritten)
{
    return _socket.send(MessageHeader(type, message.size(), _id), message,
                        waitForBytesWritten);
}
}
