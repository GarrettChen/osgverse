#include <osg/io_utils>
#include <osg/LightSource>
#include <osg/Texture2D>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgGA/StateSetManipulator>
#include <osgGA/TrackballManipulator>
#include <osgUtil/CullVisitor>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgEarth/Notify>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoTransform>
#include <osgEarth/MapNode>
#include <osgEarth/GLUtils>
#include <iostream>
#include <sstream>
#include <readerwriter/LoadSceneGLTF.h>
#include <pipeline/Pipeline.h>
#include <pipeline/Utilities.h>
#define SHADER_DIR "../shaders/"

class InteractiveHandler : public osgGA::GUIEventHandler
{
public:
    InteractiveHandler(osgEarth::EarthManipulator* em) : _manipulator(em), _viewpointSet(false) {}
    void addViewpoint(const osgEarth::Viewpoint& vp) { _viewPoints.push_back(vp); }
    
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        if (ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN ||
            ea.getEventType() == osgGA::GUIEventAdapter::PUSH)
        {
            if (_viewpointSet)
            { _manipulator->clearViewpoint(); _viewpointSet = false; }
        }
        else if (ea.getEventType() == osgGA::GUIEventAdapter::KEYUP)
        {
            if (ea.getKey() >= '1' && ea.getKey() <= '9')
            {
                int index = ea.getKey() - '1';
                if (index < _viewPoints.size())
                { _manipulator->setViewpoint(_viewPoints[index], 2.0); _viewpointSet = true; }
            }
        }
        return false;
    }

protected:
    osg::observer_ptr<osgEarth::EarthManipulator> _manipulator;
    std::vector<osgEarth::Viewpoint> _viewPoints;
    bool _viewpointSet;
};

class MyViewer : public osgViewer::Viewer
{
public:
    MyViewer(osgVerse::Pipeline* p) : osgViewer::Viewer(), _pipeline(p) {}
    osg::ref_ptr<osgVerse::Pipeline> _pipeline;

protected:
    virtual osg::GraphicsOperation* createRenderer(osg::Camera* camera)
    {
        if (_pipeline.valid()) return _pipeline->createRenderer(camera);
        else return osgViewer::Viewer::createRenderer(camera);
    }
};

int main(int argc, char** argv)
{
    osgEarth::initialize();
    osg::ArgumentParser arguments(&argc, argv);

    osg::ref_ptr<osg::Node> scene = osgVerse::loadGltf("../models/Sponza/Sponza.gltf", false);
    //osg::ref_ptr<osg::Node> scene = osgDB::readNodeFile("cessna.osg");
    if (!scene) { OSG_WARN << "Failed to load GLTF model"; return 1; }

    // Add tangent/bi-normal arrays for normal mapping
    osgVerse::TangentSpaceVisitor tsv;
    scene->accept(tsv);

    // The scene graph
    osg::ref_ptr<osg::MatrixTransform> sceneRoot = new osg::MatrixTransform;
    sceneRoot->addChild(scene.get());
    sceneRoot->setNodeMask(DEFERRED_SCENE_MASK | SHADOW_CASTER_MASK);
    sceneRoot->setMatrix(osg::Matrix::rotate(osg::PI_2, osg::X_AXIS));

    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform;
    root->addChild(sceneRoot.get());

#if false
    osgViewer::Viewer viewer;
#else
    osg::ref_ptr<osgVerse::Pipeline> pipeline = new osgVerse::Pipeline;
    MyViewer viewer(pipeline.get());
    setupStandardPipeline(pipeline.get(), &viewer, root.get(), SHADER_DIR, 1920, 1080);
#endif

    // Create a placer on earth for sceneRoot to copy
    osg::ref_ptr<osg::MatrixTransform> placer = new osg::MatrixTransform;

    // osgEarth configuration
    osg::ref_ptr<osg::Node> earthRoot = osgDB::readNodeFile("openstreetmap.earth");
    if (earthRoot.valid())
    {
        // Tell the database pager to not modify the unref settings
        viewer.getDatabasePager()->setUnrefImageDataAfterApplyPolicy(true, false);

        // install our default manipulator (do this before calling load)
        osg::ref_ptr<osgEarth::EarthManipulator> earthMani = new osgEarth::EarthManipulator(arguments);
        viewer.setCameraManipulator(earthMani.get());

        // read in the Earth file and open the map node
        osg::ref_ptr<osgEarth::MapNode> mapNode = osgEarth::MapNode::get(earthRoot.get());
        if (!mapNode->open()) { OSG_WARN << "Failed to open earth map"; return 1; }

#if false
        osgEarth::GLUtils::setGlobalDefaults(viewer.getCamera()->getOrCreateStateSet());
        viewer.getCamera()->setSmallFeatureCullingPixelSize(-1.0f);
#else
        // default uniform values and disable small feature culling
        osgEarth::GLUtils::setGlobalDefaults(pipeline->getForwardCamera()->getOrCreateStateSet());
        pipeline->getForwardCamera()->setSmallFeatureCullingPixelSize(-1.0f);
#endif

        // thread-safe initialization of the OSG wrapper manager. Calling this here
        // prevents the "unsupported wrapper" messages from OSG
        osgDB::Registry::instance()->getObjectWrapperManager()->findWrapper("osg::Image");

        // Add earth root to scene graph
        earthRoot->setNodeMask(~DEFERRED_SCENE_MASK);
        root->addChild(earthRoot.get());

        // Use a geo-transform node to place scene on earth
        osg::ref_ptr<osgEarth::GeoTransform> geo = new osgEarth::GeoTransform;
        geo->addChild(placer.get());
        mapNode->addChild(geo.get());

        // Set the geo-transform pose
        osgEarth::GeoPoint pos(osgEarth::SpatialReference::get("wgs84"),
                               116.4f, 39.9f, 150.0f, osgEarth::ALTMODE_RELATIVE);
        geo->setPosition(pos);

        // Set the viewpoint
        osgEarth::Viewpoint vp;
        vp.setNode(geo.get());
        vp.heading()->set(-45.0, osgEarth::Units::DEGREES);
        vp.pitch()->set(-20.0, osgEarth::Units::DEGREES);
        vp.range()->set(sceneRoot->getBound().radius() * 10.0, osgEarth::Units::METERS);

        osg::ref_ptr<InteractiveHandler> interacter = new InteractiveHandler(earthMani.get());
        interacter->addViewpoint(vp);
        viewer.addEventHandler(interacter.get());
    }

    viewer.addEventHandler(new osgViewer::StatsHandler);
    viewer.addEventHandler(new osgViewer::WindowSizeHandler);
    viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));
    viewer.setSceneData(root.get());
    while (!viewer.done())
    {
        sceneRoot->setMatrix(osg::Matrix::rotate(osg::PI_2, osg::X_AXIS)
                           * placer->getWorldMatrices()[0]);
        viewer.frame();
    }
    return 0;
}