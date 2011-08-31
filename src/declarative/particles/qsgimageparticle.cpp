/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the Declarative module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <private/qsgcontext_p.h>
#include <private/qsgadaptationlayer_p.h>
#include <qsgnode.h>
#include <qsgtexturematerial.h>
#include <qsgtexture.h>
#include <QFile>
#include "qsgimageparticle_p.h"
#include "qsgparticleemitter_p.h"
#include "qsgsprite_p.h"
#include "qsgspriteengine_p.h"
#include <QOpenGLFunctions>
#include <qsgengine.h>

QT_BEGIN_NAMESPACE

//###Switch to define later, for now user-friendly (no compilation) debugging is worth it
DEFINE_BOOL_CONFIG_OPTION(qmlParticlesDebug, QML_PARTICLES_DEBUG)

#ifdef Q_WS_MAC
#define SHADER_DEFINES "#version 120\n"
#else
#define SHADER_DEFINES ""
#endif

//TODO: Make it larger on desktop? Requires fixing up shader code with the same define
#define UNIFORM_ARRAY_SIZE 64

const float CONV = 0.017453292519943295;
class ImageMaterialData
{
    public:
    ImageMaterialData()
        : texture(0), colorTable(0)
    {}

    ~ImageMaterialData(){
        delete texture;
        delete colorTable;
    }

    QSGTexture *texture;
    QSGTexture *colorTable;
    float sizeTable[UNIFORM_ARRAY_SIZE];
    float opacityTable[UNIFORM_ARRAY_SIZE];

    qreal timestamp;
    qreal entry;
    qreal framecount;
    qreal animcount;
};

//TODO: Move shaders inline once they've stablilized
class TabledMaterialData : public ImageMaterialData {};
class TabledMaterial : public QSGSimpleMaterialShader<TabledMaterialData>
{
    QSG_DECLARE_SIMPLE_SHADER(TabledMaterial, TabledMaterialData)

public:
    TabledMaterial()
    {
        QFile vf(":defaultshaders/imagevertex.shader");
        vf.open(QFile::ReadOnly);
        m_vertex_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define TABLE\n#define DEFORM\n#define COLOR\n")
            + vf.readAll();

        QFile ff(":defaultshaders/imagefragment.shader");
        ff.open(QFile::ReadOnly);
        m_fragment_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define TABLE\n#define DEFORM\n#define COLOR\n")
            + ff.readAll();

        Q_ASSERT(!m_vertex_code.isNull());
        Q_ASSERT(!m_fragment_code.isNull());
    }

    const char *vertexShader() const { return m_vertex_code.constData(); }
    const char *fragmentShader() const { return m_fragment_code.constData(); }

    QList<QByteArray> attributes() const {
        return QList<QByteArray>() << "vPos" << "vTex" << "vData" << "vVec"
            << "vColor" << "vDeformVec" << "vRotation";
    };

    void initialize() {
        QSGSimpleMaterialShader<TabledMaterialData>::initialize();
        program()->bind();
        program()->setUniformValue("texture", 0);
        program()->setUniformValue("colortable", 1);
        glFuncs = QOpenGLContext::currentContext()->functions();
        m_timestamp_id = program()->uniformLocation("timestamp");
        m_entry_id = program()->uniformLocation("entry");
        m_sizetable_id = program()->uniformLocation("sizetable");
        m_opacitytable_id = program()->uniformLocation("opacitytable");
    }

    void updateState(const TabledMaterialData* d, const TabledMaterialData*) {
        glFuncs->glActiveTexture(GL_TEXTURE1);
        d->colorTable->bind();

        glFuncs->glActiveTexture(GL_TEXTURE0);
        d->texture->bind();

        program()->setUniformValue(m_timestamp_id, (float) d->timestamp);
        program()->setUniformValue("framecount", (float) 1);
        program()->setUniformValue("animcount", (float) 1);
        program()->setUniformValue(m_entry_id, (float) d->entry);
        program()->setUniformValueArray(m_sizetable_id, (float*) d->sizeTable, UNIFORM_ARRAY_SIZE, 1);
        program()->setUniformValueArray(m_opacitytable_id, (float*) d->opacityTable, UNIFORM_ARRAY_SIZE, 1);
    }

    int m_entry_id;
    int m_timestamp_id;
    int m_sizetable_id;
    int m_opacitytable_id;
    QByteArray m_vertex_code;
    QByteArray m_fragment_code;
    QOpenGLFunctions* glFuncs;
};

class DeformableMaterialData : public ImageMaterialData {};
class DeformableMaterial : public QSGSimpleMaterialShader<DeformableMaterialData>
{
    QSG_DECLARE_SIMPLE_SHADER(DeformableMaterial, DeformableMaterialData)

public:
    DeformableMaterial()
    {
        QFile vf(":defaultshaders/imagevertex.shader");
        vf.open(QFile::ReadOnly);
        m_vertex_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define DEFORM\n#define COLOR\n")
            + vf.readAll();

        QFile ff(":defaultshaders/imagefragment.shader");
        ff.open(QFile::ReadOnly);
        m_fragment_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define DEFORM\n#define COLOR\n")
            + ff.readAll();

        Q_ASSERT(!m_vertex_code.isNull());
        Q_ASSERT(!m_fragment_code.isNull());
    }

    const char *vertexShader() const { return m_vertex_code.constData(); }
    const char *fragmentShader() const { return m_fragment_code.constData(); }

    QList<QByteArray> attributes() const {
        return QList<QByteArray>() << "vPos" << "vTex" << "vData" << "vVec"
            << "vColor" << "vDeformVec" << "vRotation";
    };

    void initialize() {
        QSGSimpleMaterialShader<DeformableMaterialData>::initialize();
        program()->bind();
        program()->setUniformValue("texture", 0);
        glFuncs = QOpenGLContext::currentContext()->functions();
        m_timestamp_id = program()->uniformLocation("timestamp");
        m_entry_id = program()->uniformLocation("entry");
    }

    void updateState(const DeformableMaterialData* d, const DeformableMaterialData*) {
        glFuncs->glActiveTexture(GL_TEXTURE0);
        d->texture->bind();

        program()->setUniformValue(m_timestamp_id, (float) d->timestamp);
        program()->setUniformValue(m_entry_id, (float) d->entry);
    }

    int m_entry_id;
    int m_timestamp_id;
    QByteArray m_vertex_code;
    QByteArray m_fragment_code;
    QOpenGLFunctions* glFuncs;
};

class SpriteMaterialData : public ImageMaterialData {};
class SpriteMaterial : public QSGSimpleMaterialShader<SpriteMaterialData>
{
    QSG_DECLARE_SIMPLE_SHADER(SpriteMaterial, SpriteMaterialData)

public:
    SpriteMaterial()
    {
        QFile vf(":defaultshaders/imagevertex.shader");
        vf.open(QFile::ReadOnly);
        m_vertex_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define SPRITE\n#define TABLE\n#define DEFORM\n#define COLOR\n")
            + vf.readAll();

        QFile ff(":defaultshaders/imagefragment.shader");
        ff.open(QFile::ReadOnly);
        m_fragment_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define SPRITE\n#define TABLE\n#define DEFORM\n#define COLOR\n")
            + ff.readAll();

        Q_ASSERT(!m_vertex_code.isNull());
        Q_ASSERT(!m_fragment_code.isNull());
    }

    const char *vertexShader() const { return m_vertex_code.constData(); }
    const char *fragmentShader() const { return m_fragment_code.constData(); }

    QList<QByteArray> attributes() const {
        return QList<QByteArray>() << "vPos" << "vTex" << "vData" << "vVec"
            << "vColor" << "vDeformVec" << "vRotation" << "vAnimData";
    };

    void initialize() {
        QSGSimpleMaterialShader<SpriteMaterialData>::initialize();
        program()->bind();
        program()->setUniformValue("texture", 0);
        program()->setUniformValue("colortable", 1);
        glFuncs = QOpenGLContext::currentContext()->functions();
        m_timestamp_id = program()->uniformLocation("timestamp");
        m_framecount_id = program()->uniformLocation("framecount");
        m_animcount_id = program()->uniformLocation("animcount");
        m_entry_id = program()->uniformLocation("entry");
        m_sizetable_id = program()->uniformLocation("sizetable");
        m_opacitytable_id = program()->uniformLocation("opacitytable");
    }

    void updateState(const SpriteMaterialData* d, const SpriteMaterialData*) {
        glFuncs->glActiveTexture(GL_TEXTURE1);
        d->colorTable->bind();

        // make sure we end by setting GL_TEXTURE0 as active texture
        glFuncs->glActiveTexture(GL_TEXTURE0);
        d->texture->bind();

        program()->setUniformValue(m_timestamp_id, (float) d->timestamp);
        program()->setUniformValue(m_framecount_id, (float) d->framecount);
        program()->setUniformValue(m_animcount_id, (float) d->animcount);
        program()->setUniformValue(m_entry_id, (float) d->entry);
        program()->setUniformValueArray(m_sizetable_id, (float*) d->sizeTable, 64, 1);
        program()->setUniformValueArray(m_opacitytable_id, (float*) d->opacityTable, UNIFORM_ARRAY_SIZE, 1);
    }

    int m_timestamp_id;
    int m_framecount_id;
    int m_animcount_id;
    int m_entry_id;
    int m_sizetable_id;
    int m_opacitytable_id;
    QByteArray m_vertex_code;
    QByteArray m_fragment_code;
    QOpenGLFunctions* glFuncs;
};

class ColoredMaterialData : public ImageMaterialData {};
class ColoredMaterial : public QSGSimpleMaterialShader<ColoredMaterialData>
{
    QSG_DECLARE_SIMPLE_SHADER(ColoredMaterial, ColoredMaterialData)

public:
    ColoredMaterial()
    {
        QFile vf(":defaultshaders/imagevertex.shader");
        vf.open(QFile::ReadOnly);
        m_vertex_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define COLOR\n")
            + vf.readAll();

        QFile ff(":defaultshaders/imagefragment.shader");
        ff.open(QFile::ReadOnly);
        m_fragment_code = QByteArray(SHADER_DEFINES)
            + QByteArray("#define COLOR\n")
            + ff.readAll();

        Q_ASSERT(!m_vertex_code.isNull());
        Q_ASSERT(!m_fragment_code.isNull());
    }

    const char *vertexShader() const { return m_vertex_code.constData(); }
    const char *fragmentShader() const { return m_fragment_code.constData(); }

    void activate() {
        QSGSimpleMaterialShader<ColoredMaterialData>::activate();
#ifndef QT_OPENGL_ES_2
        glEnable(GL_POINT_SPRITE);
        glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
#endif
    }

    void deactivate() {
        QSGSimpleMaterialShader<ColoredMaterialData>::deactivate();
#ifndef QT_OPENGL_ES_2
        glDisable(GL_POINT_SPRITE);
        glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
#endif
    }

    QList<QByteArray> attributes() const {
        return QList<QByteArray>() << "vPos" << "vData" << "vVec" << "vColor";
    }

    void initialize() {
        QSGSimpleMaterialShader<ColoredMaterialData>::initialize();
        program()->bind();
        program()->setUniformValue("texture", 0);
        glFuncs = QOpenGLContext::currentContext()->functions();
        m_timestamp_id = program()->uniformLocation("timestamp");
        m_entry_id = program()->uniformLocation("entry");
    }

    void updateState(const ColoredMaterialData* d, const ColoredMaterialData*) {
        glFuncs->glActiveTexture(GL_TEXTURE0);
        d->texture->bind();

        program()->setUniformValue(m_timestamp_id, (float) d->timestamp);
        program()->setUniformValue(m_entry_id, (float) d->entry);
    }

    int m_timestamp_id;
    int m_entry_id;
    QByteArray m_vertex_code;
    QByteArray m_fragment_code;
    QOpenGLFunctions* glFuncs;
};

class SimpleMaterialData : public ImageMaterialData {};
class SimpleMaterial : public QSGSimpleMaterialShader<SimpleMaterialData>
{
    QSG_DECLARE_SIMPLE_SHADER(SimpleMaterial, SimpleMaterialData)

public:
    SimpleMaterial()
    {
        QFile vf(":defaultshaders/imagevertex.shader");
        vf.open(QFile::ReadOnly);
        m_vertex_code = QByteArray(SHADER_DEFINES)
            + vf.readAll();

        QFile ff(":defaultshaders/imagefragment.shader");
        ff.open(QFile::ReadOnly);
        m_fragment_code = QByteArray(SHADER_DEFINES)
            + ff.readAll();

        Q_ASSERT(!m_vertex_code.isNull());
        Q_ASSERT(!m_fragment_code.isNull());
    }

    const char *vertexShader() const { return m_vertex_code.constData(); }
    const char *fragmentShader() const { return m_fragment_code.constData(); }

    void activate() {
        QSGSimpleMaterialShader<SimpleMaterialData>::activate();
#ifndef QT_OPENGL_ES_2
        glEnable(GL_POINT_SPRITE);
        glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
#endif
    }

    void deactivate() {
        QSGSimpleMaterialShader<SimpleMaterialData>::deactivate();
#ifndef QT_OPENGL_ES_2
        glDisable(GL_POINT_SPRITE);
        glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
#endif
    }

    QList<QByteArray> attributes() const {
        return QList<QByteArray>() << "vPos" << "vData" << "vVec";
    }

    void initialize() {
        QSGSimpleMaterialShader<SimpleMaterialData>::initialize();
        program()->bind();
        program()->setUniformValue("texture", 0);
        glFuncs = QOpenGLContext::currentContext()->functions();
        m_timestamp_id = program()->uniformLocation("timestamp");
        m_entry_id = program()->uniformLocation("entry");
    }

    void updateState(const SimpleMaterialData* d, const SimpleMaterialData*) {
        glFuncs->glActiveTexture(GL_TEXTURE0);
        d->texture->bind();

        program()->setUniformValue(m_timestamp_id, (float) d->timestamp);
        program()->setUniformValue(m_entry_id, (float) d->entry);
    }

    int m_timestamp_id;
    int m_entry_id;
    QByteArray m_vertex_code;
    QByteArray m_fragment_code;
    QOpenGLFunctions* glFuncs;
};

void fillUniformArrayFromImage(float* array, const QImage& img, int size)
{
    if (img.isNull()){
        for (int i=0; i<size; i++)
            array[i] = 1.0;
        return;
    }
    QImage scaled = img.scaled(size,1);
    for (int i=0; i<size; i++)
        array[i] = qAlpha(scaled.pixel(i,0))/255.0;
}

/*!
    \qmlclass ImageParticle QSGImageParticle
    \inqmlmodule QtQuick.Particles 2
    \inherits ParticlePainter
    \brief The ImageParticle element visualizes logical particles using an image

    This element renders a logical particle as an image. The image can be
        - colorized
        - roatated
        - deformed
        - a sprite-based animation
*/
/*!
    \qmlproperty url QtQuick.Particles2::ImageParticle::source
*/
/*!
    \qmlproperty url QtQuick.Particles2::ImageParticle::colorTable
*/
/*!
    \qmlproperty url QtQuick.Particles2::ImageParticle::sizeTable

    Note that currently sizeTable is ignored for sprite particles.
*/
/*!
    \qmlproperty url QtQuick.Particles2::ImageParticle::opacityTable
*/
/*!
    \qmlproperty color QtQuick.Particles2::ImageParticle::color
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::colorVariation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::redVariation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::greenVariation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::blueVariation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::alpha
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::alphaVariation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::rotation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::rotationVariation
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::rotationSpeed
*/
/*!
    \qmlproperty real QtQuick.Particles2::ImageParticle::rotationSpeedVariation
*/
/*!
    \qmlproperty bool QtQuick.Particles2::ImageParticle::autoRotation
*/
/*!
    \qmlproperty StochasticDirection QtQuick.Particles2::ImageParticle::xVector
*/
/*!
    \qmlproperty StochasticDirection QtQuick.Particles2::ImageParticle::yVector
*/
/*!
    \qmlproperty list<Sprite> QtQuick.Particles2::ImageParticle::sprites
*/
/*!
    \qmlproperty EntryEffect QtQuick.Particles2::ImageParticle::entryEffect

    This property provides basic and cheap entrance and exit effects for the particles.
    For fine-grained control, see sizeTable and opacityTable.

    Acceptable values are
    \list
    \o None: Particles just appear and disappear.
    \o Fade: Particles fade in from 0. opacity at the start of their life, and fade out to 0. at the end.
    \o Scale: Particles scale in from 0 size at the start of their life, and scale back to 0 at the end.
    \endlist

    Default value is Fade.
*/


QSGImageParticle::QSGImageParticle(QSGItem* parent)
    : QSGParticlePainter(parent)
    , m_do_reset(false)
    , m_color_variation(0.0)
    , m_rootNode(0)
    , m_material(0)
    , m_alphaVariation(0.0)
    , m_alpha(1.0)
    , m_redVariation(0.0)
    , m_greenVariation(0.0)
    , m_blueVariation(0.0)
    , m_rotation(0)
    , m_autoRotation(false)
    , m_xVector(0)
    , m_yVector(0)
    , m_rotationVariation(0)
    , m_rotationSpeed(0)
    , m_rotationSpeedVariation(0)
    , m_spriteEngine(0)
    , m_bloat(false)
    , perfLevel(Unknown)
    , m_lastLevel(Unknown)
    , m_debugMode(false)
    , m_entryEffect(Fade)
{
    setFlag(ItemHasContents);
    m_debugMode = qmlParticlesDebug();
}

QSGImageParticle::~QSGImageParticle()
{
    delete m_material;
}

QDeclarativeListProperty<QSGSprite> QSGImageParticle::sprites()
{
    return QDeclarativeListProperty<QSGSprite>(this, &m_sprites, spriteAppend, spriteCount, spriteAt, spriteClear);
}

void QSGImageParticle::setImage(const QUrl &image)
{
    if (image == m_image_name)
        return;
    m_image_name = image;
    emit imageChanged();
    reset();
}


void QSGImageParticle::setColortable(const QUrl &table)
{
    if (table == m_colortable_name)
        return;
    m_colortable_name = table;
    emit colortableChanged();
    reset();
}

void QSGImageParticle::setSizetable(const QUrl &table)
{
    if (table == m_sizetable_name)
        return;
    m_sizetable_name = table;
    emit sizetableChanged();
    reset();
}

void QSGImageParticle::setOpacitytable(const QUrl &table)
{
    if (table == m_opacitytable_name)
        return;
    m_opacitytable_name = table;
    emit opacitytableChanged();
    reset();
}

void QSGImageParticle::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    emit colorChanged();
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setColorVariation(qreal var)
{
    if (var == m_color_variation)
        return;
    m_color_variation = var;
    emit colorVariationChanged();
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setAlphaVariation(qreal arg)
{
    if (m_alphaVariation != arg) {
        m_alphaVariation = arg;
        emit alphaVariationChanged(arg);
    }
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setAlpha(qreal arg)
{
    if (m_alpha != arg) {
        m_alpha = arg;
        emit alphaChanged(arg);
    }
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setRedVariation(qreal arg)
{
    if (m_redVariation != arg) {
        m_redVariation = arg;
        emit redVariationChanged(arg);
    }
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setGreenVariation(qreal arg)
{
    if (m_greenVariation != arg) {
        m_greenVariation = arg;
        emit greenVariationChanged(arg);
    }
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setBlueVariation(qreal arg)
{
    if (m_blueVariation != arg) {
        m_blueVariation = arg;
        emit blueVariationChanged(arg);
    }
    if (perfLevel < Colored)
        reset();
}

void QSGImageParticle::setRotation(qreal arg)
{
    if (m_rotation != arg) {
        m_rotation = arg;
        emit rotationChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setRotationVariation(qreal arg)
{
    if (m_rotationVariation != arg) {
        m_rotationVariation = arg;
        emit rotationVariationChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setRotationSpeed(qreal arg)
{
    if (m_rotationSpeed != arg) {
        m_rotationSpeed = arg;
        emit rotationSpeedChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setRotationSpeedVariation(qreal arg)
{
    if (m_rotationSpeedVariation != arg) {
        m_rotationSpeedVariation = arg;
        emit rotationSpeedVariationChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setAutoRotation(bool arg)
{
    if (m_autoRotation != arg) {
        m_autoRotation = arg;
        emit autoRotationChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setXVector(QSGStochasticDirection* arg)
{
    if (m_xVector != arg) {
        m_xVector = arg;
        emit xVectorChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setYVector(QSGStochasticDirection* arg)
{
    if (m_yVector != arg) {
        m_yVector = arg;
        emit yVectorChanged(arg);
    }
    if (perfLevel < Deformable)
        reset();
}

void QSGImageParticle::setBloat(bool arg)
{
    if (m_bloat != arg) {
        m_bloat = arg;
        emit bloatChanged(arg);
    }
    if (perfLevel < 9999)
        reset();
}

void QSGImageParticle::setEntryEffect(EntryEffect arg)
{
    if (m_entryEffect != arg) {
        m_entryEffect = arg;
        if (m_material)
            getState<ImageMaterialData>(m_material)->entry = (qreal) m_entryEffect;
        emit entryEffectChanged(arg);
    }
}

void QSGImageParticle::reset()
{
    QSGParticlePainter::reset();
    m_pleaseReset = true;
    update();
}

void QSGImageParticle::createEngine()
{
    if (m_spriteEngine)
        delete m_spriteEngine;
    if (m_sprites.count())
        m_spriteEngine = new QSGSpriteEngine(m_sprites, this);
    else
        m_spriteEngine = 0;
    reset();
}

static QSGGeometry::Attribute SimpleParticle_Attributes[] = {
    { 0, 2, GL_FLOAT },             // Position
    { 1, 4, GL_FLOAT },             // Data
    { 2, 4, GL_FLOAT }             // Vectors
};

static QSGGeometry::AttributeSet SimpleParticle_AttributeSet =
{
    3, // Attribute Count
    ( 2 + 4 + 4 ) * sizeof(float),
    SimpleParticle_Attributes
};

static QSGGeometry::Attribute ColoredParticle_Attributes[] = {
    { 0, 2, GL_FLOAT },             // Position
    { 1, 4, GL_FLOAT },             // Data
    { 2, 4, GL_FLOAT },             // Vectors
    { 3, 4, GL_UNSIGNED_BYTE },     // Colors
};

static QSGGeometry::AttributeSet ColoredParticle_AttributeSet =
{
    4, // Attribute Count
    ( 2 + 4 + 4 ) * sizeof(float) + 4 * sizeof(uchar),
    ColoredParticle_Attributes
};

static QSGGeometry::Attribute DeformableParticle_Attributes[] = {
    { 0, 2, GL_FLOAT },             // Position
    { 1, 2, GL_FLOAT },             // TexCoord
    { 2, 4, GL_FLOAT },             // Data
    { 3, 4, GL_FLOAT },             // Vectors
    { 4, 4, GL_UNSIGNED_BYTE },     // Colors
    { 5, 4, GL_FLOAT },             // DeformationVectors
    { 6, 3, GL_FLOAT },             // Rotation
};

static QSGGeometry::AttributeSet DeformableParticle_AttributeSet =
{
    7, // Attribute Count
    (2 + 2 + 4 + 4 + 4 + 3) * sizeof(float) + 4 * sizeof(uchar),
    DeformableParticle_Attributes
};

static QSGGeometry::Attribute SpriteParticle_Attributes[] = {
    { 0, 2, GL_FLOAT },             // Position
    { 1, 2, GL_FLOAT },             // TexCoord
    { 2, 4, GL_FLOAT },             // Data
    { 3, 4, GL_FLOAT },             // Vectors
    { 4, 4, GL_UNSIGNED_BYTE },     // Colors
    { 5, 4, GL_FLOAT },             // DeformationVectors
    { 6, 3, GL_FLOAT },             // Rotation
    { 7, 4, GL_FLOAT }              // Anim Data
};

static QSGGeometry::AttributeSet SpriteParticle_AttributeSet =
{
    8, // Attribute Count
    (2 + 2 + 4 + 4 + 4 + 4 + 3) * sizeof(float) + 4 * sizeof(uchar),
    SpriteParticle_Attributes
};

QSGGeometryNode* QSGImageParticle::buildParticleNodes()
{
#ifdef QT_OPENGL_ES_2
    if (m_count * 4 > 0xffff) {
        printf("ImageParticle: Too many particles - maximum 16,000 per ImageParticle.\n");//ES 2 vertex count limit is ushort
        return 0;
    }
#endif

    if (count() <= 0)
        return 0;

    if (m_sprites.count() || m_bloat) {
        perfLevel = Sprites;
    } else if (!m_colortable_name.isEmpty() || !m_sizetable_name.isEmpty()
               || !m_opacitytable_name.isEmpty()) {
        perfLevel = Tabled;
    } else if (m_autoRotation || m_rotation || m_rotationVariation
               || m_rotationSpeed || m_rotationSpeedVariation) {
        perfLevel = Deformable;
    } else if (m_alphaVariation || m_alpha != 1.0 || m_color.isValid()
               || m_redVariation || m_blueVariation || m_greenVariation) {
        perfLevel = Colored;
    } else {
        perfLevel = Simple;
    }

    if (perfLevel >= Colored  && !m_color.isValid())
        m_color = QColor(Qt::white);//Hidden default, but different from unset

    QImage image;
    if (perfLevel >= Sprites){
        if (!m_spriteEngine) {
            qWarning() << "ImageParticle: No sprite engine...";
            return 0;
        }
        image = m_spriteEngine->assembledImage();
        if (image.isNull())//Warning is printed in engine
            return 0;
    } else {
        image = QImage(m_image_name.toLocalFile());
        if (image.isNull()) {
            printf("ImageParticle: loading image failed '%s'\n", qPrintable(m_image_name.toLocalFile()));
            return 0;
        }
    }


    if (m_material) {
        delete m_material;
        m_material = 0;
    }

    //Setup material
    QImage colortable;
    QImage sizetable;
    QImage opacitytable;
    switch (perfLevel) {//Fallthrough intended
    case Sprites:
        m_material = SpriteMaterial::createMaterial();
        getState<ImageMaterialData>(m_material)->framecount = m_spriteEngine->maxFrames();
        m_spriteEngine->setCount(m_count);
    case Tabled:
        if (!m_material)
            m_material = TabledMaterial::createMaterial();
        colortable = QImage(m_colortable_name.toLocalFile());
        sizetable = QImage(m_sizetable_name.toLocalFile());
        opacitytable = QImage(m_opacitytable_name.toLocalFile());
        if (colortable.isNull())
            colortable = QImage(":defaultshaders/identitytable.png");
        Q_ASSERT(!colortable.isNull());
        getState<ImageMaterialData>(m_material)->colorTable = sceneGraphEngine()->createTextureFromImage(colortable);
        fillUniformArrayFromImage(getState<ImageMaterialData>(m_material)->sizeTable, sizetable, UNIFORM_ARRAY_SIZE);
        fillUniformArrayFromImage(getState<ImageMaterialData>(m_material)->opacityTable, opacitytable, UNIFORM_ARRAY_SIZE);
    case Deformable:
        if (!m_material)
            m_material = DeformableMaterial::createMaterial();
    case Colored:
        if (!m_material)
            m_material = ColoredMaterial::createMaterial();
    default://Also Simple
        if (!m_material)
            m_material = SimpleMaterial::createMaterial();
        getState<ImageMaterialData>(m_material)->texture = sceneGraphEngine()->createTextureFromImage(image);
        getState<ImageMaterialData>(m_material)->texture->setFiltering(QSGTexture::Linear);
        getState<ImageMaterialData>(m_material)->entry = (qreal) m_entryEffect;
        m_material->setFlag(QSGMaterial::Blending);
    }

    foreach (const QString &str, m_particles){
        int gIdx = m_system->m_groupIds[str];
        int count = m_system->m_groupData[gIdx]->size();
        QSGGeometryNode* node = new QSGGeometryNode();
        node->setMaterial(m_material);

        m_nodes.insert(gIdx, node);
        m_idxStarts.insert(gIdx, m_lastIdxStart);
        m_lastIdxStart += count;

        //Create Particle Geometry
        int vCount = count * 4;
        int iCount = count * 6;

        QSGGeometry *g;
        if (perfLevel == Sprites)
            g = new QSGGeometry(SpriteParticle_AttributeSet, vCount, iCount);
        else if (perfLevel == Tabled)
            g = new QSGGeometry(DeformableParticle_AttributeSet, vCount, iCount);
        else if (perfLevel == Deformable)
            g = new QSGGeometry(DeformableParticle_AttributeSet, vCount, iCount);
        else if (perfLevel == Colored)
            g = new QSGGeometry(ColoredParticle_AttributeSet, count, 0);
        else //Simple
            g = new QSGGeometry(SimpleParticle_AttributeSet, count, 0);

        node->setGeometry(g);
        if (perfLevel <= Colored){
            g->setDrawingMode(GL_POINTS);
            if (m_debugMode){
                GLfloat pointSizeRange[2];
                glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, pointSizeRange);
                qDebug() << "Using point sprites, GL_ALIASED_POINT_SIZE_RANGE " <<pointSizeRange[0] << ":" << pointSizeRange[1];
            }
        }else
            g->setDrawingMode(GL_TRIANGLES);

        for (int p=0; p < count; ++p)
            commit(gIdx, p);//commit sets geometry for the node, has its own perfLevel switch

        if (perfLevel == Sprites)
            initTexCoords<SpriteVertex>((SpriteVertex*)g->vertexData(), vCount);
        else if (perfLevel == Tabled)
            initTexCoords<DeformableVertex>((DeformableVertex*)g->vertexData(), vCount);
        else if (perfLevel == Deformable)
            initTexCoords<DeformableVertex>((DeformableVertex*)g->vertexData(), vCount);

        if (perfLevel > Colored){
            quint16 *indices = g->indexDataAsUShort();
            for (int i=0; i < count; ++i) {
                int o = i * 4;
                indices[0] = o;
                indices[1] = o + 1;
                indices[2] = o + 2;
                indices[3] = o + 1;
                indices[4] = o + 3;
                indices[5] = o + 2;
                indices += 6;
            }
        }

    }

    foreach (QSGGeometryNode* node, m_nodes){
        if (node == *(m_nodes.begin()))
            continue;
        (*(m_nodes.begin()))->appendChildNode(node);
    }

    return *(m_nodes.begin());
}

QSGNode *QSGImageParticle::updatePaintNode(QSGNode *, UpdatePaintNodeData *)
{
    if (m_pleaseReset){
        m_lastLevel = perfLevel;

        delete m_rootNode;//Automatically deletes children
        m_rootNode = 0;
        m_nodes.clear();

        m_idxStarts.clear();
        m_lastIdxStart = 0;

        if (m_material)
            delete m_material;
        m_material = 0;

        m_pleaseReset = false;
    }

    if (m_system && m_system->isRunning())
        prepareNextFrame();
    if (m_rootNode){
        update();
        //### Should I be using dirty geometry too/instead?
        foreach (QSGGeometryNode* node, m_nodes)
            node->markDirty(QSGNode::DirtyMaterial);
    }

    return m_rootNode;
}

void QSGImageParticle::prepareNextFrame()
{
    if (m_rootNode == 0){//TODO: Staggered loading (as emitted)
        m_rootNode = buildParticleNodes();
        if (m_rootNode == 0)
            return;
        if(m_debugMode){
            qDebug() << "QSGImageParticle Feature level: " << perfLevel;
            qDebug() << "QSGImageParticle Nodes: ";
            int count = 0;
            foreach(int i, m_nodes.keys()){
                qDebug() << "Group " << i << " (" << m_system->m_groupData[i]->size() << " particles)";
                count += m_system->m_groupData[i]->size();
            }
            qDebug() << "Total count: " << count;
        }
    }
    qint64 timeStamp = m_system->systemSync(this);

    qreal time = timeStamp / 1000.;

    switch (perfLevel){//Fall-through intended
    case Sprites:
        //Advance State
        getState<ImageMaterialData>(m_material)->animcount = m_spriteEngine->spriteCount();
        m_spriteEngine->updateSprites(timeStamp);
        foreach (const QString &str, m_particles){
            int gIdx = m_system->m_groupIds[str];
            int count = m_system->m_groupData[gIdx]->size();

            Vertices<SpriteVertex>* particles = (Vertices<SpriteVertex> *) m_nodes[gIdx]->geometry()->vertexData();
            for (int i=0; i < count; i++){
                int spriteIdx = m_idxStarts[gIdx] + i;
                Vertices<SpriteVertex> &p = particles[i];
                int curIdx = m_spriteEngine->spriteState(spriteIdx);
                if (curIdx != p.v1.animIdx){
                    p.v1.animIdx = p.v2.animIdx = p.v3.animIdx = p.v4.animIdx = curIdx;
                    p.v1.animT = p.v2.animT = p.v3.animT = p.v4.animT = m_spriteEngine->spriteStart(spriteIdx)/1000.0;
                    p.v1.frameCount = p.v2.frameCount = p.v3.frameCount = p.v4.frameCount = m_spriteEngine->spriteFrames(spriteIdx);
                    p.v1.frameDuration = p.v2.frameDuration = p.v3.frameDuration = p.v4.frameDuration = m_spriteEngine->spriteDuration(spriteIdx);
                }
            }
        }
    case Tabled:
    case Deformable:
    case Colored:
    case Simple:
    default: //Also Simple
        getState<ImageMaterialData>(m_material)->timestamp = time;
        break;
    }

}

void QSGImageParticle::reloadColor(const Color4ub &c, QSGParticleData* d)
{
    d->color = c;
    //TODO: get index for reload - or make function take an index
}

void QSGImageParticle::initialize(int gIdx, int pIdx)
{
    Color4ub color;
    QSGParticleData* datum = m_system->m_groupData[gIdx]->data[pIdx];
    qreal redVariation = m_color_variation + m_redVariation;
    qreal greenVariation = m_color_variation + m_greenVariation;
    qreal blueVariation = m_color_variation + m_blueVariation;
    int spriteIdx = m_idxStarts[gIdx] + datum->index;
    switch (perfLevel){//Fall-through is intended on all of them
        case Sprites:
            // Initial Sprite State
            datum->animT = datum->t;
            datum->animIdx = 0;
            if (m_spriteEngine){
                m_spriteEngine->startSprite(spriteIdx);
                datum->frameCount = m_spriteEngine->spriteFrames(spriteIdx);
                datum->frameDuration = m_spriteEngine->spriteDuration(spriteIdx);
            }else{
                datum->frameCount = 1;
                datum->frameDuration = 9999;
            }
        case Tabled:
        case Deformable:
            //Initial Rotation
            if (m_xVector){
                const QPointF &ret = m_xVector->sample(QPointF(datum->x, datum->y));
                datum->xx = ret.x();
                datum->xy = ret.y();
            }
            if (m_yVector){
                const QPointF &ret = m_yVector->sample(QPointF(datum->x, datum->y));
                datum->yx = ret.x();
                datum->yy = ret.y();
            }
            datum->rotation =
                    (m_rotation + (m_rotationVariation - 2*((qreal)rand()/RAND_MAX)*m_rotationVariation) ) * CONV;
            datum->rotationSpeed =
                    (m_rotationSpeed + (m_rotationSpeedVariation - 2*((qreal)rand()/RAND_MAX)*m_rotationSpeedVariation) ) * CONV;
            datum->autoRotate = m_autoRotation?1.0:0.0;
        case Colored:
            //Color initialization
            // Particle color
            color.r = m_color.red() * (1 - redVariation) + rand() % 256 * redVariation;
            color.g = m_color.green() * (1 - greenVariation) + rand() % 256 * greenVariation;
            color.b = m_color.blue() * (1 - blueVariation) + rand() % 256 * blueVariation;
            color.a = m_alpha * m_color.alpha() * (1 - m_alphaVariation) + rand() % 256 * m_alphaVariation;
            datum->color = color;
        default:
            break;
    }
}

void QSGImageParticle::commit(int gIdx, int pIdx)
{
    if (m_pleaseReset)
        return;
    QSGGeometryNode *node = m_nodes[gIdx];
    if (!node)
        return;
    QSGParticleData* datum = m_system->m_groupData[gIdx]->data[pIdx];
    node->setFlag(QSGNode::OwnsGeometry, false);
    SpriteVertex *spriteVertices = (SpriteVertex *) node->geometry()->vertexData();
    DeformableVertex *deformableVertices = (DeformableVertex *) node->geometry()->vertexData();
    ColoredVertex *coloredVertices = (ColoredVertex *) node->geometry()->vertexData();
    SimpleVertex *simpleVertices = (SimpleVertex *) node->geometry()->vertexData();
    switch (perfLevel){//No automatic fall through intended on this one
    case Sprites:
        spriteVertices += pIdx*4;
        for (int i=0; i<4; i++){
            spriteVertices[i].x = datum->x  - m_systemOffset.x();
            spriteVertices[i].y = datum->y  - m_systemOffset.y();
            spriteVertices[i].t = datum->t;
            spriteVertices[i].lifeSpan = datum->lifeSpan;
            spriteVertices[i].size = datum->size;
            spriteVertices[i].endSize = datum->endSize;
            spriteVertices[i].vx = datum->vx;
            spriteVertices[i].vy = datum->vy;
            spriteVertices[i].ax = datum->ax;
            spriteVertices[i].ay = datum->ay;
            spriteVertices[i].xx = datum->xx;
            spriteVertices[i].xy = datum->xy;
            spriteVertices[i].yx = datum->yx;
            spriteVertices[i].yy = datum->yy;
            spriteVertices[i].rotation = datum->rotation;
            spriteVertices[i].rotationSpeed = datum->rotationSpeed;
            spriteVertices[i].autoRotate = datum->autoRotate;
            spriteVertices[i].animIdx = datum->animIdx;
            spriteVertices[i].frameDuration = datum->frameDuration;
            spriteVertices[i].frameCount = datum->frameCount;
            spriteVertices[i].animT = datum->animT;
            spriteVertices[i].color.r = datum->color.r;
            spriteVertices[i].color.g = datum->color.g;
            spriteVertices[i].color.b = datum->color.b;
            spriteVertices[i].color.a = datum->color.a;
        }
        break;
    case Tabled: //Fall through until it has its own vertex class
    case Deformable:
        deformableVertices += pIdx*4;
        for (int i=0; i<4; i++){
            deformableVertices[i].x = datum->x  - m_systemOffset.x();
            deformableVertices[i].y = datum->y  - m_systemOffset.y();
            deformableVertices[i].t = datum->t;
            deformableVertices[i].lifeSpan = datum->lifeSpan;
            deformableVertices[i].size = datum->size;
            deformableVertices[i].endSize = datum->endSize;
            deformableVertices[i].vx = datum->vx;
            deformableVertices[i].vy = datum->vy;
            deformableVertices[i].ax = datum->ax;
            deformableVertices[i].ay = datum->ay;
            deformableVertices[i].xx = datum->xx;
            deformableVertices[i].xy = datum->xy;
            deformableVertices[i].yx = datum->yx;
            deformableVertices[i].yy = datum->yy;
            deformableVertices[i].rotation = datum->rotation;
            deformableVertices[i].rotationSpeed = datum->rotationSpeed;
            deformableVertices[i].autoRotate = datum->autoRotate;
            deformableVertices[i].color.r = datum->color.r;
            deformableVertices[i].color.g = datum->color.g;
            deformableVertices[i].color.b = datum->color.b;
            deformableVertices[i].color.a = datum->color.a;
        }
        break;
    case Colored:
        coloredVertices += pIdx*1;
        for (int i=0; i<1; i++){
            coloredVertices[i].x = datum->x  - m_systemOffset.x();
            coloredVertices[i].y = datum->y  - m_systemOffset.y();
            coloredVertices[i].t = datum->t;
            coloredVertices[i].lifeSpan = datum->lifeSpan;
            coloredVertices[i].size = datum->size;
            coloredVertices[i].endSize = datum->endSize;
            coloredVertices[i].vx = datum->vx;
            coloredVertices[i].vy = datum->vy;
            coloredVertices[i].ax = datum->ax;
            coloredVertices[i].ay = datum->ay;
            coloredVertices[i].color.r = datum->color.r;
            coloredVertices[i].color.g = datum->color.g;
            coloredVertices[i].color.b = datum->color.b;
            coloredVertices[i].color.a = datum->color.a;
        }
        break;
    case Simple:
        simpleVertices += pIdx*1;
        for (int i=0; i<1; i++){
            simpleVertices[i].x = datum->x - m_systemOffset.x();
            simpleVertices[i].y = datum->y - m_systemOffset.y();
            simpleVertices[i].t = datum->t;
            simpleVertices[i].lifeSpan = datum->lifeSpan;
            simpleVertices[i].size = datum->size;
            simpleVertices[i].endSize = datum->endSize;
            simpleVertices[i].vx = datum->vx;
            simpleVertices[i].vy = datum->vy;
            simpleVertices[i].ax = datum->ax;
            simpleVertices[i].ay = datum->ay;
        }
        break;
    default:
        break;
    }

    node->setFlag(QSGNode::OwnsGeometry, true);
}



QT_END_NAMESPACE
