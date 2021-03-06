/*********************************************************************/
/* Copyright (c) 2017, EPFL/Blue Brain Project                       */
/*                     Daniel.Nachbaur@epfl.ch                       */
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

#include "DeflectServer.h"

#include <boost/test/unit_test.hpp>

DeflectServer::DeflectServer()
{
    _server = new deflect::Server(0 /* OS-chosen port */);
    _server->moveToThread(&_thread);
    _thread.connect(&_thread, &QThread::finished, _server,
                    &deflect::Server::deleteLater);
    _thread.start();

    _server->connect(_server, &deflect::Server::pixelStreamOpened,
                     [&](const QString) {
                         ++_openedStreams;
                         _mutex.lock();
                         _receivedState = true;
                         _received.wakeAll();
                         _mutex.unlock();
                     });

    _server->connect(_server, &deflect::Server::pixelStreamClosed,
                     [&](const QString) {
                         --_openedStreams;
                         _mutex.lock();
                         _receivedState = true;
                         _received.wakeAll();
                         _mutex.unlock();
                     });

    _server->connect(_server, &deflect::Server::receivedSizeHints,
                     [&](const QString id, const deflect::SizeHints hints) {
                         if (_sizeHintsCallback)
                             _sizeHintsCallback(id, hints);
                         _mutex.lock();
                         _receivedState = true;
                         _received.wakeAll();
                         _mutex.unlock();
                     });

    _server->connect(_server, &deflect::Server::receivedData,
                     [&](const QString id, QByteArray data) {
                         if (_dataReceivedCallback)
                             _dataReceivedCallback(id, data);
                         _mutex.lock();
                         _receivedState = true;
                         _received.wakeAll();
                         _mutex.unlock();
                     });

    _server->connect(_server, &deflect::Server::receivedFrame,
                     [&](deflect::FramePtr frame) {
                         if (_frameReceivedCallback)
                             _frameReceivedCallback(frame);
                         ++_receivedFrames;
                         _mutex.lock();
                         _receivedState = true;
                         _received.wakeAll();
                         _mutex.unlock();
                     });

    _server->connect(_server, &deflect::Server::registerToEvents,
                     [&](const QString id, const bool exclusive,
                         deflect::EventReceiver* evtReceiver,
                         deflect::BoolPromisePtr success) {

                         if (_registerToEventsCallback)
                             _registerToEventsCallback(id, exclusive,
                                                       evtReceiver);

                         _eventReceiver = evtReceiver;
                         success->set_value(true);
                     });
}

DeflectServer::~DeflectServer()
{
    _thread.quit();
    _thread.wait();
}

void DeflectServer::waitForMessage()
{
    for (size_t j = 0; j < 20; ++j)
    {
        _mutex.lock();
        _received.wait(&_mutex, 100 /*ms*/);
        if (_receivedState)
        {
            _mutex.unlock();
            break;
        }
        _mutex.unlock();
    }
    BOOST_CHECK(_receivedState);
    _receivedState = false;
}

void DeflectServer::processEvent(const deflect::Event& event)
{
    BOOST_REQUIRE(_eventReceiver);
    _eventReceiver->processEvent(event);
}
