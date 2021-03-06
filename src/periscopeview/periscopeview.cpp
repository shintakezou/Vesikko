/*
* This source file is part of the osgOcean library
*
* Copyright (C) 2009 Kim Bale
* Copyright (C) 2009 The University of Hull, UK
*
* This program is free software; you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the Free Software
* Foundation; either version 3 of the License, or (at your option) any later
* version.

* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
* http://www.gnu.org/copyleft/lesser.txt.
*/

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgGA/FlightManipulator>
#include <osgGA/TrackballManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/GUIEventHandler>
#include <osg/Notify>
#include <osg/TextureCubeMap>
#include <osgDB/ReadFile>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osg/PositionAttitudeTransform>
#include <osg/Program>
#include <osgText/Text>
#include <osg/CullFace>
#include <osg/Fog>
#include <osgText/Font>
#include <osg/Switch>
#include <osg/Texture3D>
#include <string>
#include <vector>

#include <osgOcean/Version>
#include <osgOcean/OceanScene>
#include <osgOcean/FFTOceanSurface>
#include <osgOcean/SiltEffect>
#include <osgOcean/ShaderManager>

#include <QDebug>
#include "periscopeview.h"
#include "SkyDome.h"

#define USE_CUSTOM_SHADER
#define WORLD_RADIUS 50000

// ----------------------------------------------------
//               Camera Track Callback
// ----------------------------------------------------

class CameraTrackCallback: public osg::NodeCallback
{
public:
    virtual void operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        if( nv->getVisitorType() == osg::NodeVisitor::CULL_VISITOR )
        {
            osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
            osg::Vec3f centre,up,eye;
            // get MAIN camera eye,centre,up
            cv->getRenderStage()->getCamera()->getViewMatrixAsLookAt(eye,centre,up);
            // update position
            osg::MatrixTransform* mt = static_cast<osg::MatrixTransform*>(node);
            mt->setMatrix( osg::Matrix::translate( eye.x(), eye.y(), mt->getMatrix().getTrans().z() ) );
        }

        traverse(node, nv);
    }
};

/*
class BoatPositionCallback : public osg::NodeCallback
{
public:
    BoatPositionCallback(osgOcean::OceanScene* oceanScene)
        : _oceanScene(oceanScene) {}

    virtual void operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        if(nv->getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR ){
            osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(node);
            if (!mt || !_oceanScene.valid()) return;

            osg::Matrix mat = osg::computeLocalToWorld(nv->getNodePath());
            osg::Vec3d pos = mat.getTrans();

            osg::Vec3f normal;
            // Get the ocean surface height at the object's position, note
            // that this only considers one point on the object (the object's
            // geometric center) and not the whole object.
            float height = _oceanScene->getOceanSurfaceHeightAt(pos.x(), pos.y(), &normal);

            mat.makeTranslate(osg::Vec3f(pos.x(), pos.y(), height));

            osg::Matrix rot;
            rot.makeIdentity();
            rot.makeRotate( normal.x(), osg::Vec3f(1.0f, 0.0f, 0.0f),
                            normal.y(), osg::Vec3f(0.0f, 1.0f, 0.0f),
                            (1.0f-normal.z()), osg::Vec3f(0.0f, 0.0f, 1.0f));

            mat = rot*mat;
            mt->setMatrix(mat);
        }

        traverse(node, nv);
    }

    osg::observer_ptr<osgOcean::OceanScene> _oceanScene;
};
*/
// ----------------------------------------------------
//                  Scoped timer
// ----------------------------------------------------

class ScopedTimer
{
public:
    ScopedTimer(const std::string& description,
                std::ostream& output_stream = std::cout,
                bool endline_after_time = true)
        : _output_stream(output_stream)
        , _start()
        , _endline_after_time(endline_after_time)
    {
        _output_stream << description << std::flush;
        _start = osg::Timer::instance()->tick();
    }

    ~ScopedTimer()
    {
        osg::Timer_t end = osg::Timer::instance()->tick();
        _output_stream << osg::Timer::instance()->delta_s(_start, end) << "s";
        if (_endline_after_time) _output_stream << std::endl;
        else                     _output_stream << std::flush;
    }

private:
    std::ostream& _output_stream;
    osg::Timer_t _start;
    bool _endline_after_time;
};


// ----------------------------------------------------
//                  Scene Model
// ----------------------------------------------------

class SceneModel : public osg::Referenced
{
public:
    enum SCENE_TYPE{ CLEAR, DUSK, CLOUDY };

private:
    SCENE_TYPE _sceneType;

    osg::ref_ptr<osgText::Text> _modeText;
    osg::ref_ptr<osg::Group> _scene;

    osg::ref_ptr<osgOcean::OceanScene> _oceanScene;
    osg::ref_ptr<osgOcean::FFTOceanSurface> _oceanSurface;
    osg::ref_ptr<osg::TextureCubeMap> _cubemap;
    osg::ref_ptr<SkyDome> _skyDome;

    std::vector<std::string> _cubemapDirs;
    std::vector<osg::Vec4f>  _lightColors;
    std::vector<osg::Vec4f>  _fogColors;
    std::vector<osg::Vec3f>  _underwaterAttenuations;
    std::vector<osg::Vec4f>  _underwaterDiffuse;

    osg::ref_ptr<osg::Light> _light;

    std::vector<osg::Vec3f>  _sunPositions;
    std::vector<osg::Vec4f>  _sunDiffuse;
    std::vector<osg::Vec4f>  _waterFogColors;

public:
    SceneModel( const osg::Vec2f& windDirection = osg::Vec2f(1.0f,1.0f),
                float windSpeed = 12.f,
                float depth = 10000.f,
                float reflectionDamping = 0.35f,
                float scale = 1e-22,
                bool  isChoppy = true,
                float choppyFactor = -2.5f,
                float crestFoamHeight = 2.2f ):
        _sceneType(CLEAR)
    {
        _cubemapDirs.push_back( "sky_clear" );
        _cubemapDirs.push_back( "sky_dusk" );
        _cubemapDirs.push_back( "sky_fair_cloudy" );

        _fogColors.push_back( intColor( 199,226,255 ) );
        _fogColors.push_back( intColor( 244,228,179 ) );
        _fogColors.push_back( intColor( 172,224,251 ) );

        _waterFogColors.push_back( intColor(27,57,109) );
        _waterFogColors.push_back( intColor(44,69,106 ) );
        _waterFogColors.push_back( intColor(84,135,172 ) );

        _underwaterAttenuations.push_back( osg::Vec3f(0.015f, 0.0075f, 0.005f) );
        _underwaterAttenuations.push_back( osg::Vec3f(0.015f, 0.0075f, 0.005f) );
        _underwaterAttenuations.push_back( osg::Vec3f(0.008f, 0.003f, 0.002f) );

        _underwaterDiffuse.push_back( intColor(27,57,109) );
        _underwaterDiffuse.push_back( intColor(44,69,106) );
        _underwaterDiffuse.push_back( intColor(84,135,172) );

        _lightColors.push_back( intColor( 105,138,174 ) );
        _lightColors.push_back( intColor( 105,138,174 ) );
        _lightColors.push_back( intColor( 105,138,174 ) );

        _sunPositions.push_back( osg::Vec3f(326.573, 1212.99 ,1275.19) );
        _sunPositions.push_back( osg::Vec3f(520.f, 1900.f, 550.f ) );
        _sunPositions.push_back( osg::Vec3f(-1056.89f, -771.886f, 1221.18f ) );

        _sunDiffuse.push_back( intColor( 191, 191, 191 ) );
        _sunDiffuse.push_back( intColor( 251, 251, 161 ) );
        _sunDiffuse.push_back( intColor( 191, 191, 191 ) );

        build(windDirection, windSpeed, depth, reflectionDamping, scale, isChoppy, choppyFactor, crestFoamHeight);
    }

    void build( const osg::Vec2f& windDirection,
                float windSpeed,
                float depth,
                float reflectionDamping,
                float waveScale,
                bool  isChoppy,
                float choppyFactor,
                float crestFoamHeight )
    {
        {
            ScopedTimer buildSceneTimer("Building scene... \n", osg::notify(osg::NOTICE));

            _scene = new osg::Group;

            {
                ScopedTimer cubemapTimer("  . Loading cubemaps: ", osg::notify(osg::NOTICE));
                _cubemap = loadCubeMapTextures( _cubemapDirs[_sceneType] );
            }

            // Set up surface
            {
                ScopedTimer oceanSurfaceTimer("  . Generating ocean surface: ", osg::notify(osg::NOTICE));
                _oceanSurface = new osgOcean::FFTOceanSurface( 64, 256, 17,
                                                               windDirection, windSpeed, depth, reflectionDamping, waveScale, isChoppy, choppyFactor, 10.f, 256 );

                _oceanSurface->setEnvironmentMap( _cubemap.get() );
                _oceanSurface->setFoamBottomHeight( 2.2f );
                _oceanSurface->setFoamTopHeight( 3.0f );
                _oceanSurface->enableCrestFoam( true );
                _oceanSurface->setLightColor( _lightColors[_sceneType] );
                // Make the ocean surface track with the main camera position, giving the illusion
                // of an endless ocean surface.
                _oceanSurface->enableEndlessOcean(true);
            }

            // Set up ocean scene, add surface
            {
                ScopedTimer oceanSceneTimer("  . Creating ocean scene: ", osg::notify(osg::NOTICE));
                osg::Vec3f sunDir = -_sunPositions[_sceneType];
                sunDir.normalize();

                _oceanScene = new osgOcean::OceanScene( _oceanSurface.get() );
                _oceanScene->setLightID(0);
                _oceanScene->enableReflections(true);
                _oceanScene->enableRefractions(true);

                // Set the size of _oceanCylinder which follows the camera underwater.
                // This cylinder prevents the clear from being visible past the far plane
                // instead it will be the fog color.
                // The size of the cylinder should be changed according the size of the ocean surface.
                _oceanScene->setCylinderSize( WORLD_RADIUS-1000.f, 4000.f );

                _oceanScene->setAboveWaterFog(0.00008f, _fogColors[_sceneType] );
                _oceanScene->setUnderwaterFog(0.002f,  _waterFogColors[_sceneType] );
                _oceanScene->setUnderwaterDiffuse( _underwaterDiffuse[_sceneType] );
                _oceanScene->setUnderwaterAttenuation( _underwaterAttenuations[_sceneType] );

                _oceanScene->setSunDirection( sunDir );
                _oceanScene->enableGodRays(true);
                _oceanScene->enableSilt(true);
                _oceanScene->enableUnderwaterDOF(true);
                _oceanScene->enableDistortion(true);
                _oceanScene->enableGlare(true);
                _oceanScene->setGlareAttenuation(0.8f);

                // create sky dome and add to ocean scene
                // set masks so it appears in reflected scene and normal scene
                _skyDome = new SkyDome( WORLD_RADIUS-5000.f, 16, 16, _cubemap.get() );
                _skyDome->setNodeMask( _oceanScene->getReflectedSceneMask() | _oceanScene->getNormalSceneMask() );

                // add a pat to track the camera
                osg::MatrixTransform* transform = new osg::MatrixTransform;
                transform->setDataVariance( osg::Object::DYNAMIC );
                transform->setMatrix( osg::Matrixf::translate( osg::Vec3f(0.f, 0.f, 0.f) ));
                transform->setCullCallback( new CameraTrackCallback );

                transform->addChild( _skyDome.get() );

                _oceanScene->addChild( transform );

                {
                    // Create and add fake texture for use with nodes without any texture
                    // since the OceanScene default scene shader assumes that texture unit
                    // 0 is used as a base texture map.
                    osg::Image * image = new osg::Image;
                    image->allocateImage( 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE );
                    *(osg::Vec4ub*)image->data() = osg::Vec4ub( 0xFF, 0xFF, 0xFF, 0xFF );

                    osg::Texture2D* fakeTex = new osg::Texture2D( image );
                    fakeTex->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::REPEAT);
                    fakeTex->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::REPEAT);
                    fakeTex->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::NEAREST);
                    fakeTex->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::NEAREST);

                    osg::StateSet* stateset = _oceanScene->getOrCreateStateSet();
                    stateset->setTextureAttribute(0,fakeTex,osg::StateAttribute::ON);
                    stateset->setTextureMode(0,GL_TEXTURE_1D,osg::StateAttribute::OFF);
                    stateset->setTextureMode(0,GL_TEXTURE_2D,osg::StateAttribute::ON);
                    stateset->setTextureMode(0,GL_TEXTURE_3D,osg::StateAttribute::OFF);
                }

            }

            {
                ScopedTimer lightingTimer("  . Setting up lighting: ", osg::notify(osg::NOTICE));
                osg::LightSource* lightSource = new osg::LightSource;
                lightSource->setLocalStateSetModes();

                _light = lightSource->getLight();
                _light->setLightNum(0);
                _light->setAmbient( osg::Vec4d(0.3f, 0.3f, 0.3f, 1.0f ));
                _light->setDiffuse( _sunDiffuse[_sceneType] );
                _light->setSpecular(osg::Vec4d( 0.1f, 0.1f, 0.1f, 1.0f ) );
                _light->setPosition( osg::Vec4f(_sunPositions[_sceneType], 1.f) ); // point light

                _scene->addChild( lightSource );
                _scene->addChild( _oceanScene.get() );
                //_scene->addChild( sunDebug(_sunPositions[CLOUDY]) );
            }

            osg::notify(osg::NOTICE) << "complete.\nTime Taken: ";
        }
    }

    osgOcean::OceanTechnique* getOceanSurface( void )
    {
        return _oceanSurface.get();
    }

    osg::Group* getScene(void){
        return _scene.get();
    }

    osgOcean::OceanScene* getOceanScene()
    {
        return _oceanScene.get();
    }

    void changeScene( SCENE_TYPE type )
    {
        _sceneType = type;

        _cubemap = loadCubeMapTextures( _cubemapDirs[_sceneType] );
        _skyDome->setCubeMap( _cubemap.get() );
        _oceanSurface->setEnvironmentMap( _cubemap.get() );
        _oceanSurface->setLightColor( _lightColors[type] );

        _oceanScene->setAboveWaterFog(0.0012f, _fogColors[_sceneType] );
        _oceanScene->setUnderwaterFog(0.002f,  _waterFogColors[_sceneType] );
        _oceanScene->setUnderwaterDiffuse( _underwaterDiffuse[_sceneType] );
        _oceanScene->setUnderwaterAttenuation( _underwaterAttenuations[_sceneType] );

        osg::Vec3f sunDir = -_sunPositions[_sceneType];
        sunDir.normalize();

        _oceanScene->setSunDirection( sunDir );

        _light->setPosition( osg::Vec4f(_sunPositions[_sceneType],1.f) );
        _light->setDiffuse( _sunDiffuse[_sceneType] ) ;
    }
    /*
    // Load the islands model
    // Here we attach a custom shader to the model.
    // This shader overrides the default shader applied by OceanScene but uses uniforms applied by OceanScene.
    // The custom shader is needed to add multi-texturing and bump mapping to the terrain.
    osg::Node* loadIslands(void)
    {
        osgDB::Registry::instance()->getDataFilePathList().push_back("island");
        const std::string filename = "islands.ive";
        osg::ref_ptr<osg::Node> island = osgDB::readNodeFile(filename);

        if(!island.valid()){
            osg::notify(osg::WARN) << "Could not find: " << filename << std::endl;
            return NULL;
        }

#ifdef USE_CUSTOM_SHADER
        static const char terrain_vertex[]   = "terrain.vert";
        static const char terrain_fragment[] = "terrain.frag";

        osg::Program* program = osgOcean::ShaderManager::instance().createProgram("terrain", terrain_vertex, terrain_fragment, true);
        program->addBindAttribLocation("aTangent", 6);
#endif
        island->setNodeMask( _oceanScene->getNormalSceneMask() | _oceanScene->getReflectedSceneMask() | _oceanScene->getRefractedSceneMask() );
        island->getStateSet()->addUniform( new osg::Uniform( "uTextureMap", 0 ) );

#ifdef USE_CUSTOM_SHADER
        island->getOrCreateStateSet()->setAttributeAndModes(program,osg::StateAttribute::ON);
        island->getStateSet()->addUniform( new osg::Uniform( "uOverlayMap", 1 ) );
        island->getStateSet()->addUniform( new osg::Uniform( "uNormalMap",  2 ) );
#endif
        osg::PositionAttitudeTransform* islandpat = new osg::PositionAttitudeTransform;
        islandpat->setPosition(osg::Vec3f( -island->getBound().center()+osg::Vec3f(0.0, 0.0, -15.f) ) );
        islandpat->setScale( osg::Vec3f(4.f, 4.f, 3.f ) );
        islandpat->addChild(island.get());

        return islandpat;
    }
*/
    osg::ref_ptr<osg::TextureCubeMap> loadCubeMapTextures( const std::string& dir )
    {
        enum {POS_X, NEG_X, POS_Y, NEG_Y, POS_Z, NEG_Z};

        std::string filenames[6];

        filenames[POS_X] = "resources/textures/" + dir + "/east.png";
        filenames[NEG_X] = "resources/textures/" + dir + "/west.png";
        filenames[POS_Z] = "resources/textures/" + dir + "/north.png";
        filenames[NEG_Z] = "resources/textures/" + dir + "/south.png";
        filenames[POS_Y] = "resources/textures/" + dir + "/down.png";
        filenames[NEG_Y] = "resources/textures/" + dir + "/up.png";

        osg::ref_ptr<osg::TextureCubeMap> cubeMap = new osg::TextureCubeMap;
        cubeMap->setInternalFormat(GL_RGBA);

        cubeMap->setFilter( osg::Texture::MIN_FILTER,    osg::Texture::LINEAR_MIPMAP_LINEAR);
        cubeMap->setFilter( osg::Texture::MAG_FILTER,    osg::Texture::LINEAR);
        cubeMap->setWrap  ( osg::Texture::WRAP_S,        osg::Texture::CLAMP_TO_EDGE);
        cubeMap->setWrap  ( osg::Texture::WRAP_T,        osg::Texture::CLAMP_TO_EDGE);

        cubeMap->setImage(osg::TextureCubeMap::NEGATIVE_X, osgDB::readImageFile( filenames[NEG_X] ) );
        cubeMap->setImage(osg::TextureCubeMap::POSITIVE_X, osgDB::readImageFile( filenames[POS_X] ) );
        cubeMap->setImage(osg::TextureCubeMap::NEGATIVE_Y, osgDB::readImageFile( filenames[NEG_Y] ) );
        cubeMap->setImage(osg::TextureCubeMap::POSITIVE_Y, osgDB::readImageFile( filenames[POS_Y] ) );
        cubeMap->setImage(osg::TextureCubeMap::NEGATIVE_Z, osgDB::readImageFile( filenames[NEG_Z] ) );
        cubeMap->setImage(osg::TextureCubeMap::POSITIVE_Z, osgDB::readImageFile( filenames[POS_Z] ) );

        return cubeMap;
    }

    osg::Geode* sunDebug( const osg::Vec3f& position )
    {
        osg::ShapeDrawable* sphereDraw = new osg::ShapeDrawable( new osg::Sphere( position, 15.f ) );
        sphereDraw->setColor(osg::Vec4f(1.f,0.f,0.f,1.f));

        osg::Geode* sphereGeode = new osg::Geode;
        sphereGeode->addDrawable( sphereDraw );

        return sphereGeode;
    }

    osg::Vec4f intColor(unsigned int r, unsigned int g, unsigned int b, unsigned int a = 255 )
    {
        float div = 1.f/255.f;
        return osg::Vec4f( div*(float)r, div*(float)g, div*float(b), div*(float)a );
    }

    osgOcean::OceanScene::EventHandler* getOceanSceneEventHandler()
    {
        return _oceanScene->getEventHandler();
    }
};

// ----------------------------------------------------
//                   Event Handler
// ----------------------------------------------------

class SceneEventHandler : public osgGA::GUIEventHandler
{
public:
    SceneEventHandler( osgViewer::Viewer& viewer, osgOcean::OceanScene * scene, TextHUD *hud) : _viewer(viewer), _scene(scene), _hud(hud)
    {
        rotation = 0;
        toggleZoom = false;
    }

    virtual bool handle(const osgGA::GUIEventAdapter& ea,osgGA::GUIActionAdapter&)
    {
        switch(ea.getEventType())
        {
        case(osgGA::GUIEventAdapter::KEYDOWN):
        {
            if(ea.getKey() == 'z')
            {
                toggleZoom = true;
                return false;
            } else if(ea.getKey() == osgGA::GUIEventAdapter::KEY_Left) {
                rotation = -1;
                return false;
            } else if(ea.getKey() == osgGA::GUIEventAdapter::KEY_Right) {
                rotation = 1;
                return false;
            }
        }
        case(osgGA::GUIEventAdapter::KEYUP):
        {
            if(ea.getKey() == osgGA::GUIEventAdapter::KEY_Left) {
                rotation = 0;
                return false;
            } else if(ea.getKey() == osgGA::GUIEventAdapter::KEY_Right) {
                rotation = 0;
                return false;
            }
        }
        case(osgGA::GUIEventAdapter::RESIZE):
        {
            qDebug() << "resize " << ea.getWindowWidth() << ea.getWindowHeight();
            _scene->setScreenDims(osg::Vec2s(ea.getWindowWidth(), ea.getWindowHeight()));
            _hud->getHudCamera()->setViewport(0,0,ea.getWindowWidth(), ea.getWindowHeight());
        }
        }
        return false;
    }

    bool zoomToggled() {
        if(toggleZoom) {
            toggleZoom = false;
            return true;
        }
        return false;
    }
    int getRotation() {
        return rotation;
    }

private:
    osgViewer::Viewer& _viewer;
    osgOcean::OceanScene * _scene;
    TextHUD *_hud;
    int rotation;
    bool toggleZoom;
};

PeriscopeView::PeriscopeView(QObject *parent) : QObject(parent)
{
    periscopeDir = 0;
    osg::notify(osg::NOTICE) << "osgOcean " << osgOceanGetVersion() << std::endl << std::endl;
    float windx = 1.1f, windy = 1.1f;
    osg::Vec2f windDirection(windx, windy);
    subPitch = subRoll = subYaw = 0;

    float windSpeed = 12.f;
    float depth = 1000.f;
    float reflectionDamping = 0.35f;
    float scale = 1e-8;
    bool isChoppy = true;
    float choppyFactor = 2.5f;
    choppyFactor = -choppyFactor;
    float crestFoamHeight = 2.2f;
    double oceanSurfaceHeight = 0.0f;
    bool testCollision = false;
    bool disableShaders = false;
    //    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFiles(parser);
    int width = 400;
    int height = 300;
    viewer.setUpViewInWindow( 640,150,width,height, 0 );
    viewer.addEventHandler( new osgViewer::StatsHandler );
    hud = new TextHUD(this);
    osgOcean::ShaderManager::instance().enableShaders(!disableShaders);
    osg::ref_ptr<SceneModel> scene = new SceneModel(windDirection, windSpeed, depth, reflectionDamping, scale, isChoppy, choppyFactor, crestFoamHeight);

    scene->getOceanScene()->setOceanSurfaceHeight(oceanSurfaceHeight);
    _oceanScene = scene->getOceanScene();
    viewer.addEventHandler(scene->getOceanSceneEventHandler());
    viewer.addEventHandler(scene->getOceanSurface()->getEventHandler());

//    viewer.addEventHandler( new osgViewer::HelpHandler );
    viewer.getCamera()->setName("MainCamera");
    viewer.getCamera()->setProjectionMatrixAsPerspective(32, (float)width/(float)height, 2, WORLD_RADIUS);
    eventHandler = new SceneEventHandler(viewer, _oceanScene, hud);
    viewer.addEventHandler( eventHandler );
    osg::Group* root = new osg::Group;
    root->addChild( scene->getScene() );
    /*
    if (loadedModel.valid())
    {
        loadedModel->setNodeMask( scene->getOceanScene()->getNormalSceneMask() |
                                  scene->getOceanScene()->getReflectedSceneMask() |
                                  scene->getOceanScene()->getRefractedSceneMask() );
        scene->getOceanScene()->addChild(loadedModel.get());
    }
*/
    /*
    if (testCollision)
    {
        osgDB::Registry::instance()->getDataFilePathList().push_back("resources/boat");
        const std::string filename = "boat.3ds";
        osg::ref_ptr<osg::Node> boat = osgDB::readNodeFile(filename);
        if(boat.valid())
        {
            boat->setNodeMask( scene->getOceanScene()->getNormalSceneMask() |
                               scene->getOceanScene()->getReflectedSceneMask() |
                               scene->getOceanScene()->getRefractedSceneMask() );

            osg::ref_ptr<osg::MatrixTransform> boatTransform = new osg::MatrixTransform;
            boatTransform->addChild(boat.get());
            boatTransform->setMatrix(osg::Matrix::translate(osg::Vec3f(0.0f, 160.0f,0.0f)));
            boatTransform->setUpdateCallback( new BoatPositionCallback(scene->getOceanScene()) );

            scene->getOceanScene()->addChild(boatTransform.get());
        }
        else
        {
            osg::notify(osg::WARN) << "testCollision flag ignored - Could not find: " << filename << std::endl;
        }
    }
        */
    root->addChild( hud->getHudCamera() );
    ship = osgDB::readNodeFile("resources/models/ship.obj");
    if(!ship.valid()) {
        qDebug() << Q_FUNC_INFO << "can't load ship resources/models/ship.obj";
    } else {
        ship->setNodeMask( _oceanScene->getNormalSceneMask() |
                           _oceanScene->getReflectedSceneMask() |
                           _oceanScene->getRefractedSceneMask() );
    }
    torpedo = osgDB::readNodeFile("resources/models/torpedo.obj");
    if(!torpedo.valid()) {
        qDebug() << Q_FUNC_INFO << "can't load torpedo";
    } else {
        torpedo->setNodeMask( _oceanScene->getNormalSceneMask() |
                              _oceanScene->getReflectedSceneMask() |
                              _oceanScene->getRefractedSceneMask() );
    }

    _oceanScene->addChild(&explosion.getGroup());
    _oceanScene->addChild(&explosion.getPat());
    explosion.getPat().setPosition(osg::Vec3f(0, 50, 0));

    viewer.setSceneData( root );
    viewer.realize();
    connect(&killExplosionTimer, SIGNAL(timeout()), this, SLOT(killExplosion()));
}

void PeriscopeView::tick(double dt, int total) {
    double totalD = (double)total / 1000.0d;
    subRoll = sin(totalD)*0.4;
    subYaw = sin(totalD*1.1)*0.3;
    subPitch = sin(totalD*0.9)*0.3;
    pollKeyboard();
    periscopeDir += 50*eventHandler->getRotation()*dt;

    QMap<Vessel *, Vessel *> collidedVessels;
    foreach(Vessel *v, vesselsTransforms.keys()) {
        osg::MatrixTransform* t =vesselsTransforms[v];
        if(v->type==2) {
            foreach(Vessel *v2, vesselsTransforms.keys()) {
                if(v != v2 && v2->type != 2) {
                    osg::MatrixTransform* t2 =vesselsTransforms[v2];
                    if(t->getBound().intersects(t2->getBound())) {
                        collidedVessels.insert(v, v2);
                    }
                }
            }
        }
    }
    foreach(Vessel *t, collidedVessels.keys()) {
        emit collisionBetween(t, collidedVessels.value(t));
    }

    if( !viewer.done() )
        viewer.frame();
}

void PeriscopeView::setPeriscopeDirection(double dir) {
    periscopeDir = dir;
}

void PeriscopeView::vesselUpdated(Vessel *vessel) {
    if(vessel->id==0) {
        osg::Vec3f eye(vessel->x,vessel->y,20.f);
        osg::Vec3f centre = eye+osg::Vec3f(0.f,1.f,0.f);
        osg::Vec3f up(0.f, 0.f, 1.f);
        double periscopeDirection = vessel->heading + periscopeDir + subYaw;
        while(periscopeDirection >= 360) periscopeDirection -=360;
        while(periscopeDirection < 0) periscopeDirection +=360;
        osg::Matrixd myCameraMatrix;

        osg::Matrixd cameraRotation;
        osg::Matrixd cameraTrans;
        cameraTrans.makeTranslate( -vessel->x,vessel->y, -5 + vessel->depth);

        cameraRotation.makeRotate(
                    osg::DegreesToRadians(0.0), osg::Vec3(0,1,0), // roll
                    osg::DegreesToRadians(-90.0), osg::Vec3(1,0,0) , // pitch
                    osg::DegreesToRadians(0.0), osg::Vec3(0,0,1) ); // heading
        myCameraMatrix = cameraTrans*cameraRotation;
        cameraRotation.makeRotate(osg::DegreesToRadians(periscopeDirection), osg::Vec3(0,1,0), // hdg
                                  osg::DegreesToRadians(subPitch), osg::Vec3(1,0,0) , // pitch
                                  osg::DegreesToRadians(subRoll), osg::Vec3(0,0,1) ); //
        myCameraMatrix = myCameraMatrix*cameraRotation;
        viewer.getCamera()->setViewMatrix(myCameraMatrix);
        hud->setHeading(periscopeDirection);
    } else {
        double zeroDepth = 0;
        if(vessel->type==2) zeroDepth -= 0.5;
        osg::MatrixTransform *transform = vesselsTransforms[vessel];
        osg::Matrixd shipMatrix = osg::Matrix::rotate(osg::DegreesToRadians(0.0), osg::Vec3(0,1,0), // roll
                                                      osg::DegreesToRadians(0.0), osg::Vec3(1,0,0) , // pitch
                                                      osg::DegreesToRadians(- vessel->heading), osg::Vec3(0,0,1) );
        shipMatrix *= shipMatrix.translate(osg::Vec3f(vessel->x, -vessel->y, -vessel->depth + zeroDepth));
        transform->setMatrix(shipMatrix);
    }
}

void PeriscopeView::createVessel(Vessel *sub) {
    qDebug() << Q_FUNC_INFO << "type " << sub->type;

    osg::ref_ptr<osg::MatrixTransform> vesselTransform = new osg::MatrixTransform;
    if(sub->type==1) {
        vesselTransform->addChild(ship.get());
    } else if(sub->type==2) {
        vesselTransform->addChild(torpedo.get());
    }
    _oceanScene->addChild(vesselTransform.get());

    vesselsTransforms[sub]=vesselTransform.get();
}

void PeriscopeView::vesselDeleted(Vessel *sub) {
    qDebug() << Q_FUNC_INFO;
    _oceanScene->removeChild(vesselsTransforms[sub]);
    vesselsTransforms[sub]->unref();
    Q_ASSERT(vesselsTransforms.remove(sub));
}

void PeriscopeView::pollKeyboard() {
    static bool zoomHigh = false;
    if(eventHandler->zoomToggled()) {
        zoomHigh = !zoomHigh;
        float fov = 32;
        if(zoomHigh) fov = 8;
        viewer.getCamera()->setProjectionMatrixAsPerspective(fov, 16.f/9.f, 0.3, WORLD_RADIUS);
    }
}
void PeriscopeView::addExplosion(double x, double y, double intensity) {
    explosion.getPat().setPosition(osg::Vec3f(x, -y, 0));
    killExplosionTimer.setInterval(500);
    killExplosionTimer.start();
    explosion.setEnabled(true);
}

void PeriscopeView::killExplosion() {
    explosion.setEnabled(false);
}
