#include "GLVideoWidget.h"
#include <QPainter>
#include <QOpenGLPaintDevice>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFunctions>
#include <QOpenGLPixelTransferOptions>
#include <QOpenGLExtraFunctions>

#ifdef Q_OS_WIN
typedef void *(APIENTRY *MAP_BUF_T)(GLenum, GLenum);
static MAP_BUF_T glMapBuffer = nullptr;
#endif

#define GLFUNCS (QOpenGLContext::currentContext()->extraFunctions())

GLVideoWidget::GLVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent), ps(this)
{
    connect(&ps, SIGNAL(perSec(double)), this, SIGNAL(fps(double)));
}

GLVideoWidget::~GLVideoWidget()
{
    makeCurrent();
    delete pd; pd = nullptr;
    delete tex; tex = nullptr;
    if (pbos[0] && GLFUNCS) GLFUNCS->glDeleteBuffers(NPBOS, pbos);
}

void GLVideoWidget::updateFrame(const Frame & inframe)
{
    frame = inframe;
    if (tex && prog && !frame.isNull()) {
        constexpr int TEX_STORAGE = GL_RGB, PIX_FORMAT = GL_BGRA, PIX_PACK = GL_UNSIGNED_INT_8_8_8_8_REV; //TODO: auto-detect these...
        //const auto t0 = Util::getTime(); Q_UNUSED(t0);
        makeCurrent();
        if (!tex->isCreated()) tex->create();
        glBindTexture(GL_TEXTURE_RECTANGLE, tex->textureId());
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        index = (index + 1) % NPBOS;
        const int nextIndex = (index + 1) % NPBOS;
        if (pbos[index] && pbos[nextIndex] && GLFUNCS && bool(glMapBuffer)) {
            // use PBOs to read pixels asynchronously in the background...
            if (const Frame & fi = pboFrames[index]; !fi.isNull()) {
                // set the "current texture" to be the PBO we just wrote to in the last iteration -- we have 1 frame delay but it's ok. :)
                // note the frame image data is kept persistent for our 2 PBO buffers permanently..

                GLFUNCS->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[index]);
                glTexImage2D(GL_TEXTURE_RECTANGLE, 0, TEX_STORAGE, fi.img.width(), fi.img.height(), 0, PIX_FORMAT, PIX_PACK, nullptr);
                tex->setSize(fi.img.width(), fi.img.height());
                glBindTexture(GL_TEXTURE_RECTANGLE, 0);
            }
            const Frame & fn = (pboFrames[nextIndex] = frame);
            // bind PBO to update texture source
            GLFUNCS->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[nextIndex]);
            GLFUNCS->glBufferData(GL_PIXEL_UNPACK_BUFFER, fn.img.sizeInBytes(), nullptr, GL_STREAM_DRAW);
            // map the buffer object into client's memory

            if(GLubyte* ptr = reinterpret_cast<GLubyte*>(glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY)); ptr)
            {
                // update data directly on the mapped buffer
                memcpy(ptr, fn.img.bits(), size_t(fn.img.sizeInBytes()));
                GLFUNCS->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release the mapped buffer
            }
            GLFUNCS->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        } else {
            glTexImage2D(GL_TEXTURE_RECTANGLE, 0, TEX_STORAGE, frame.img.width(), frame.img.height(), 0, PIX_FORMAT, PIX_PACK, frame.img.bits());
            tex->setSize(frame.img.width(), frame.img.height());
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        }
        //qDebug("setData took %lld msec",Util::getTime()-t0);
    }
    update();
}

void GLVideoWidget::initializeGL()
{
    QOpenGLVersionProfile profile(format());
    auto [maj, min] = profile.version();
    Debug("Using OpenGL Version %d.%d",maj,min);
#ifdef Q_OS_WIN
    if (!glMapBuffer) {
        auto addy = wglGetProcAddress("glMapBuffer");
        if (!addy) addy = wglGetProcAddress("glMapBufferARB");
        if (addy) {
            glMapBuffer = reinterpret_cast<MAP_BUF_T>(addy);
            Debug() << "Windows fixup: found glMapBuffer!";
        } else {
            Debug() << "Windows fixup: did NOT find glMapBuffer!";
        }
    }
#endif

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_LIGHT0 );
    glDisable( GL_STENCIL_TEST );
    glDisable( GL_DITHER );
    glEnable(GL_TEXTURE_RECTANGLE);
    glShadeModel( GL_FLAT );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    if (GLFUNCS) GLFUNCS->glGenBuffers(NPBOS, pbos);
    if (!pbos[0])
        Warning() << "glGenBuffers failed -- PBOs unavailable.";
    tex = new QOpenGLTexture(QOpenGLTexture::TargetRectangle);
}

void GLVideoWidget::resizeGL(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const qreal retinaScale = devicePixelRatio();
    pixWidth = GLsizei(w * retinaScale); pixHeight = GLsizei(h * retinaScale);
    glViewport(0, 0, pixWidth, pixHeight);
    glOrtho( 0., GLdouble(pixWidth), 0, GLdouble(pixHeight), -1., 1.);
    if (pd) delete pd;
    pd = new QOpenGLPaintDevice(pixWidth, pixHeight);
    if (prog) delete prog;
    prog = new QOpenGLShaderProgram(this);
    try {
        if ( ! prog->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                             "#version 120\n"
                                             "varying vec2 texCoord;\n"
                                             "\n"
                                             "void main(void)\n"
                                             "{\n"
                                             "    gl_Position = ftransform();\n"
                                             "    texCoord = gl_MultiTexCoord0.st;\n"
                                             "}\n"
                                             ) )
            throw QString("Vertex shader failed to compile: ") + prog->log();
        if ( ! prog->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                             "#version 120\n"
                                             "#extension GL_ARB_texture_rectangle : enable\n"
                                             "uniform sampler2DRect tex;\n"
                                             "varying vec2 texCoord;\n"
                                             "\n"
                                             "void main()\n"
                                             "{\n"
                                             "    vec4 color = texture2DRect(tex,texCoord);\n"
                                             "    gl_FragColor = color;\n"
                                             "}\n"
                                             ) )
            throw QString("Fragment shader failed to compile: ") + prog->log();
        if ( ! prog->link() )
            throw QString("Error on link: ") + prog->log();
    } catch(const QString & e) {
        // failed.. will use QPainter method in paintGL()
        delete prog; prog = nullptr;
        Error() << "OpenGL Shader program failure: " << e;
    }
}

void GLVideoWidget::paintGL()
{
    if (frame.isNull()) {
        glClearColor(0.0,0.0,0.0,1.0);
        glClear(GL_COLOR_BUFFER_BIT);
    } else if (tex && prog) {
        //const auto t0 = Util::getTime(); Q_UNUSED(t0);
        prog->bind();
        constexpr int texUnit = 0;
        prog->setUniformValue("tex", texUnit);
        QOpenGLContext::currentContext()->functions()->glActiveTexture(GL_TEXTURE0+texUnit);
        glBindTexture(GL_TEXTURE_RECTANGLE, tex->textureId());

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glColor4f(1.f,1.f,1.f,1.f);
        const GLint w = pixWidth, h = pixHeight;
        const GLint tw = tex->width(), th = tex->height();
        const GLint
        v[] = {
            0,0, w,0, w,h, 0,h
        },/*
        t[] = {
            // don't flip image vertically
            0,0, tw,0, tw,th, 0,th
        };*/
        t[] = {
            // flip image vertically
            0,th, tw,th, tw,0, 0,0
        };

        glVertexPointer(2, GL_INT, 0, v);
        glTexCoordPointer(2, GL_INT, 0, t);
        glDrawArrays(GL_QUADS, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);

        QOpenGLContext::currentContext()->functions()->glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        prog->release();

        //qDebug("render using tex took: %lld msec",Util::getTime()-t0);
        emit displayedFrame(frame.num);
    } else if (pd) {
        //const auto t0 = Util::getTime(); Q_UNUSED(t0);
        // NB: uncomment code that creates the pd in resizeGL() if you uncomment this...
        const QRect r(QPoint(), pd->size());
        QPainter p(pd);
        p.setRenderHint(QPainter::SmoothPixmapTransform, /*set to false for now.. true*/false);
        p.drawImage(r, frame.img);

        //qDebug("render using QPainter took: %lld msec",Util::getTime()-t0);
        emit displayedFrame(frame.num);
    }
    ps.mark();
}
