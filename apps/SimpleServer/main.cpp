/*********************************************************************/
/* Copyright (c) 2017, EPFL/Blue Brain Project                       */
/*                     Stefan.Eilemann@epfl.ch                       */
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

#include <deflect/Frame.h>
#include <deflect/SegmentDecoder.h>
#include <deflect/Server.h>

#include <QApplication>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QSet>
#include <QSurfaceFormat>

#include <iostream>

namespace
{
class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
protected:
    void initializeGL()
    {
        setWindowTitle("Waiting for stream");

        initializeOpenGLFunctions();
        glShadeModel(GL_FLAT);
        glClearColor(0.3f, 0.3f, 0.8f, 1.f);
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glColor3f(1.f, 1.f, 1.f);

        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
                &GLWidget::_cleanup);

        _server.connect(&_server, &deflect::Server::pixelStreamOpened, this,
                        &GLWidget::_newStream);
        _server.connect(&_server, &deflect::Server::pixelStreamClosed, this,
                        &GLWidget::_deleteStream);
        _server.connect(&_server, &deflect::Server::receivedFrame, this,
                        &GLWidget::_render);
    }

private:
    void _cleanup()
    {
        makeCurrent();
        doneCurrent();
    }

    void _newStream(const QString uri)
    {
        _uris.insert(uri);
        _active = uri;
        _server.requestFrame(uri);
        setWindowTitle(uri);
    }

    void _deleteStream(const QString uri)
    {
        _uris.remove(uri);

        if (_active == uri)
            _active.clear();

        if (_uris.empty())
        {
            resize(640, 480);
            setWindowTitle("Waiting for stream");
        }
        else
        {
            _active = *_uris.begin();
            _server.requestFrame(_active);
            setWindowTitle(_active);
        }
    }

    void _render(deflect::FramePtr frame)
    {
        const auto size = frame->computeDimensions();
        resize(size);

        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, size.width(), 0, size.height(), -1.f, 1.f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        std::cout << size.width() << "x" << size.height() << ": ";

        for (auto& segment : frame->segments)
        {
            if (segment.parameters.compressed)
                _decoder.decode(segment);

            QOpenGLTexture texture(QOpenGLTexture::Target2D);
            texture.setSize(segment.parameters.width,
                            segment.parameters.height);
            texture.setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                            segment.imageData.constData());
            texture.bind();

            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(segment.parameters.x, segment.parameters.y);
            glTexCoord2f(1, 0);
            glVertex2f(segment.parameters.x + segment.parameters.width,
                       segment.parameters.y);
            glTexCoord2f(1, 1);
            glVertex2f(segment.parameters.x + segment.parameters.width,
                       segment.parameters.y + segment.parameters.height);
            glTexCoord2f(0, 1);
            glVertex2f(segment.parameters.x,
                       segment.parameters.y + segment.parameters.height);
            glEnd();
            std::cout << segment.parameters.width << "x"
                      << segment.parameters.height << "+"
                      << segment.parameters.x << "+" << segment.parameters.y
                      << ", ";
        }
        std::cout << std::endl;
        glFlush();
        _server.requestFrame(_active);
    }

    deflect::Server _server;
    QSet<QString> _uris;
    QString _active;
    deflect::SegmentDecoder _decoder;
};
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    GLWidget glWidget;

    glWidget.show();
    return app.exec();
}
