/*********************************************************************/
/* Copyright (c) 2013-2017, EPFL/Blue Brain Project                  */
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

#include "FrameDispatcher.h"

#include "Frame.h"
#include "ReceiveBuffer.h"

#include <cassert>
#include <iostream>

namespace deflect
{
class FrameDispatcher::Impl
{
public:
    Impl() {}
    FramePtr consumeLatestFrame(const QString& uri)
    {
        FramePtr frame(new Frame);
        frame->uri = uri;

        ReceiveBuffer& buffer = streamBuffers[uri];

        while (buffer.hasCompleteFrame())
            frame->segments = buffer.popFrame();

        assert(!frame->segments.empty());

        // receiver will request a new frame once this frame was consumed
        buffer.setAllowedToSend(false);

        return frame;
    }

    typedef std::map<QString, ReceiveBuffer> StreamBuffers;
    StreamBuffers streamBuffers;
    std::map<QString, size_t> observers;
};

FrameDispatcher::FrameDispatcher(QObject* parent_)
    : QObject(parent_)
    , _impl(new Impl)
{
}

FrameDispatcher::~FrameDispatcher()
{
}

void FrameDispatcher::addSource(const QString uri, const size_t sourceIndex)
{
    _impl->streamBuffers[uri].addSource(sourceIndex);

    if (_impl->streamBuffers[uri].getSourceCount() == 1 &&
        _impl->observers[uri] == 0)
    {
        emit pixelStreamOpened(uri);
    }
}

void FrameDispatcher::removeSource(const QString uri, const size_t sourceIndex)
{
    if (!_impl->streamBuffers.count(uri))
        return;

    _impl->streamBuffers[uri].removeSource(sourceIndex);

    deleteStream(uri);
}

void FrameDispatcher::addObserver(const QString uri)
{
    ++_impl->observers[uri];

    if (_impl->observers[uri] == 1 &&
        (!_impl->streamBuffers.count(uri) ||
         _impl->streamBuffers[uri].getSourceCount() == 0))
    {
        emit pixelStreamOpened(uri);
    }
}

void FrameDispatcher::removeObserver(QString uri)
{
    if (_impl->observers[uri] > 0)
        --_impl->observers[uri];

    deleteStream(uri);
}

void FrameDispatcher::processSegment(const QString uri,
                                     const size_t sourceIndex,
                                     deflect::Segment segment)
{
    if (_impl->streamBuffers.count(uri))
        _impl->streamBuffers[uri].insert(segment, sourceIndex);
}

void FrameDispatcher::processFrameFinished(const QString uri,
                                           const size_t sourceIndex)
{
    if (!_impl->streamBuffers.count(uri))
        return;

    ReceiveBuffer& buffer = _impl->streamBuffers[uri];
    try
    {
        buffer.finishFrameForSource(sourceIndex);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "processFrameFinished got exception, closing stream: "
                  << e.what() << std::endl;
        emit bufferSizeExceeded(uri);
        return;
    }

    if (buffer.isAllowedToSend() && buffer.hasCompleteFrame())
        emit sendFrame(_impl->consumeLatestFrame(uri));
}

void FrameDispatcher::requestFrame(const QString uri)
{
    if (!_impl->streamBuffers.count(uri))
        return;

    ReceiveBuffer& buffer = _impl->streamBuffers[uri];
    buffer.setAllowedToSend(true);
    if (buffer.hasCompleteFrame())
        emit sendFrame(_impl->consumeLatestFrame(uri));
}

void FrameDispatcher::deleteStream(const QString uri)
{
    if (_impl->streamBuffers[uri].getSourceCount() == 0 &&
        _impl->streamBuffers.count(uri))
    {
        _impl->streamBuffers.erase(uri);

        if (_impl->observers[uri] == 0)
            emit pixelStreamClosed(uri);
    }
}
}
