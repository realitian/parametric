/* Copyright (C) 2016 Robert Osfield
 *
 * This application is open source is published under GNU GPL license.
*/

#include <osg/ShapeDrawable>
#include <osg/CullFace>

#include <osgGA/StateSetManipulator>

#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

template<typename T>
struct parameter_ptr
{
    typedef T element_type;
    element_type* _ptr;

    parameter_ptr():_ptr(0) {}

    parameter_ptr(element_type* ptr):_ptr(ptr) {}

    template<typename P>
    parameter_ptr(osg::ref_ptr<P>& rptr):_ptr(rptr.get()) {}

    T& operator*() const { return *_ptr; }
    T* operator->() const { return _ptr; }
    T* get() const { return _ptr; }

    bool operator!() const   { return _ptr==0; } // not required
    bool valid() const       { return _ptr!=0; }

};


osg::ref_ptr<osg::Geometry> createMesh(const osg::Vec3& origin, const osg::Vec3& uAxis, const osg::Vec3& vAxis, unsigned int uCells, unsigned vCells)
{
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    geometry->setUseVertexBufferObjects(true);

    unsigned int numVertices = (uCells+1)*(vCells+1);


    // set up vertices
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->reserve(numVertices);

    osg::Vec3 ua = uAxis; ua /= static_cast<float>(uCells);
    osg::Vec3 va = vAxis; va /= static_cast<float>(vCells);

    for(unsigned int r=0; r<=vCells; ++r)
    {
        for(unsigned int c=0; c<=uCells; ++c)
        {
            vertices->push_back(origin + ua*static_cast<float>(c) + va*static_cast<float>(r));
        }
    }
    geometry->setVertexArray(vertices);


    // set up normal
    osg::Vec3 normal(uAxis ^ vAxis);
    normal.normalize();

    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array();
    normals->push_back(normal);
    geometry->setNormalArray(normals, osg::Array::BIND_OVERALL);


    // set up colour
    osg::Vec4 color(1.0,1.0,1.0,1.0);
    osg::ref_ptr<osg::Vec4Array> colours = new osg::Vec4Array;
    colours->push_back(color);
    geometry->setColorArray(colours, osg::Array::BIND_OVERALL);


    // set up mesh

    OSG_NOTICE<<"numVertices = "<<numVertices<<std::endl;
    OSG_NOTICE<<"numVertices>>16 = "<<(numVertices>>16)<<std::endl;


    osg::ref_ptr<osg::DrawElements> primitives;
    if ((numVertices>>16)==0) primitives = new osg::DrawElementsUShort(GL_TRIANGLES);
    else primitives = new osg::DrawElementsUInt(GL_TRIANGLES);

    geometry->addPrimitiveSet(primitives);

    for(unsigned int r=0; r<vCells; ++r)
    {
        for(unsigned int c=0; c<uCells; ++c)
        {
            unsigned int p0 = c+r*(uCells+1);
            unsigned int p1 = p0+(uCells+1);
            unsigned int p2 = p0+1;
            unsigned int p3 = p1+1;
            primitives->addElement(p0);
            primitives->addElement(p2);
            primitives->addElement(p1);
            primitives->addElement(p2);
            primitives->addElement(p3);
            primitives->addElement(p1);
        }
    }

    osg::Vec3 wAxis = normal*((uAxis.length()+vAxis.length())*0.5);

    osg::BoundingBox bb;
    bb.expandBy(origin);
    bb.expandBy(origin+uAxis);
    bb.expandBy(origin+vAxis);
    bb.expandBy(origin+uAxis+vAxis);

    bb.expandBy(origin+wAxis);
    bb.expandBy(origin+uAxis+wAxis);
    bb.expandBy(origin+vAxis+wAxis);
    bb.expandBy(origin+uAxis+vAxis+wAxis);

    geometry->setInitialBound(bb);

    return geometry;
}


osg::ref_ptr<osg::Texture2D> createDepthTexture(unsigned int width, unsigned int height)
{
    osg::ref_ptr<osg::Texture2D> depthTexture = new osg::Texture2D;
    depthTexture->setTextureSize(width, height);
    depthTexture->setInternalFormat(GL_DEPTH_COMPONENT);
    depthTexture->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::LINEAR);
    depthTexture->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::LINEAR);
    depthTexture->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::CLAMP_TO_BORDER);
    depthTexture->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::CLAMP_TO_BORDER);
    depthTexture->setBorderColor(osg::Vec4(1.0f,1.0f,1.0f,1.0f));
    return depthTexture;
}

osg::ref_ptr<osg::Camera> createDepthCamera(parameter_ptr<osg::Texture> depthTexture)
{
    osg::ref_ptr<osg::Camera> camera = new osg::Camera;
    camera->attach(osg::Camera::DEPTH_BUFFER, depthTexture.get());
    camera->setViewport(0, 0, depthTexture->getTextureWidth(), depthTexture->getTextureHeight());

    // clear the depth and colour bufferson each clear.
    camera->setClearMask(GL_DEPTH_BUFFER_BIT);

    // set the camera to render before the main camera.
    camera->setRenderOrder(osg::Camera::PRE_RENDER);

    // tell the camera to use OpenGL frame buffer object where supported.
    camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);

    camera->setReferenceFrame(osg::Transform::RELATIVE_RF);
    camera->setProjectionMatrix(osg::Matrixd::identity());
    camera->setViewMatrix(osg::Matrixd::identity());

    return camera;
}

void addShaders(osg::ArgumentParser& arguments, osg::StateSet* stateset)
{
    std::string z_function;
    while(arguments.read("--Z_FUNCTION",z_function)) { stateset->setDefine("Z_FUNCTION", z_function); }


    std::string visible_function;
    while(arguments.read("--VISIBLE_FUNCTION",visible_function)) { stateset->setDefine("VISIBLE_FUNCTION", visible_function); }


    float cutOff = 0.001;
    while(arguments.read("--cutOff", cutOff)) {}
    stateset->addUniform(new osg::Uniform("cutOff",cutOff));

    osg::ref_ptr<osg::Program> program = new osg::Program;

    std::string filename;
    typedef std::map<osg::Shader::Type, osg::ref_ptr<osg::Shader> > ShaderMap;
    ShaderMap shaders;

    while(arguments.read("--shader",filename))
    {
        osg::ref_ptr<osg::Shader> shader = osgDB::readRefShaderFile(filename);
        if (shader.valid())
        {
            shaders[shader->getType()] = shader;
        }
    }

    for(ShaderMap::iterator itr = shaders.begin();
        itr != shaders.end();
        ++itr)
    {
        program->addShader(itr->second);
    }

    stateset->setAttribute(program);
}

int main(int argc, char** argv)
{
    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);

    osgViewer::Viewer viewer(arguments);

    viewer.addEventHandler(new osgViewer::StatsHandler());
    viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));

    unsigned int width=1280;
    unsigned int height=1024;

    osg::ref_ptr<osg::Group> group = new osg::Group;

    bool visibleBoundaries = false;
    while(arguments.read("-b")) visibleBoundaries = true;

    bool depthBoundaries = false;
    while(arguments.read("-d")) depthBoundaries = true;

    typedef std::vector< osg::ref_ptr<osg::Node> > Boundaries;
    Boundaries boundaries;

    osg::Vec3 center;
    osg::Vec3 dimensions;
    while(arguments.read("--sphere", center.x(), center.y(), center.z(), dimensions.x()))
    {
        boundaries.push_back(new osg::ShapeDrawable(new osg::Sphere(center, dimensions.x())));
    }

    while(arguments.read("--box", center.x(), center.y(), center.z(), dimensions.x(), dimensions.y(), dimensions.z()))
    {
        boundaries.push_back(new osg::ShapeDrawable(new osg::Box(center, dimensions.x(), dimensions.y(), dimensions.z())));
    }

    while(arguments.read("--cone", center.x(), center.y(), center.z(), dimensions.x(), dimensions.y()))
    {
        boundaries.push_back(new osg::ShapeDrawable(new osg::Cone(center, dimensions.x(), dimensions.y())));
    }

    while(arguments.read("--capsule", center.x(), center.y(), center.z(), dimensions.x(), dimensions.y()))
    {
        boundaries.push_back(new osg::ShapeDrawable(new osg::Capsule(center, dimensions.x(), dimensions.y())));
    }

    while(arguments.read("--cylinder", center.x(), center.y(), center.z(), dimensions.x(), dimensions.y()))
    {
        boundaries.push_back(new osg::ShapeDrawable(new osg::Cylinder(center, dimensions.x(), dimensions.y())));
    }

    std::string modelFilename;
    while(arguments.read("--model", modelFilename))
    {
        osg::ref_ptr<osg::Node> model = osgDB::readRefNodeFile(modelFilename);
        if (model) boundaries.push_back(model);
    }

    if (visibleBoundaries)
    {
        for(Boundaries::iterator itr = boundaries.begin();
            itr != boundaries.end();
            ++itr)
        {
            group->addChild(*itr);
        }
    }

    typedef std::vector< osg::ref_ptr<osg::Texture2D> > Textures;
    Textures frontDepthTextures;
    Textures backDepthTextures;

    if (depthBoundaries)
    {
        for(Boundaries::iterator itr = boundaries.begin();
            itr != boundaries.end();
            ++itr)
        {
            osg::ref_ptr<osg::Node> boundarySubgraph = *itr;

            // set up the depth texture for front face of the boundary
            osg::ref_ptr<osg::Texture2D> frontDepthTexture = createDepthTexture(width, height);
            osg::ref_ptr<osg::Camera> frontDepthCamera = createDepthCamera(frontDepthTexture);
            frontDepthCamera->getOrCreateStateSet()->setAttributeAndModes(new osg::CullFace(osg::CullFace::BACK), osg::StateAttribute::ON);
            frontDepthCamera->addChild(boundarySubgraph);
            group->addChild(frontDepthCamera);
            frontDepthTextures.push_back(frontDepthTexture);

            // set up the depth texture for back face of the boundary
            osg::ref_ptr<osg::Texture2D> backDepthTexture = createDepthTexture(width, height);
            osg::ref_ptr<osg::Camera> backDepthCamera = createDepthCamera(backDepthTexture);
            backDepthCamera->getOrCreateStateSet()->setAttributeAndModes(new osg::CullFace(osg::CullFace::FRONT), osg::StateAttribute::ON);
            backDepthCamera->addChild(boundarySubgraph);
            group->addChild(backDepthCamera);
            backDepthTextures.push_back(backDepthTexture);
        }
    }

    for(Textures::iterator itr = frontDepthTextures.begin();
        itr != frontDepthTextures.end();
        ++itr)
    {
        OSG_NOTICE<<"Front depth texture"<<itr->get()<<std::endl;
    }

    for(Textures::iterator itr = backDepthTextures.begin();
        itr != backDepthTextures.end();
        ++itr)
    {
        OSG_NOTICE<<"Back depth texture"<<itr->get()<<std::endl;
    }

    osg::Vec3 origin(0.0, 0.0, 0.0);
    osg::Vec3 uAxis(1.0,0.0,0.0);
    osg::Vec3 vAxis(0.0,1.0,0.0);

    unsigned int uCells = 10;
    unsigned int vCells = 10;


    while (arguments.read("--columns", uCells)) {}
    while (arguments.read("--rows", vCells)) {}


    osg::ref_ptr<osg::Geometry> geometry = createMesh(origin, uAxis, vAxis, uCells, vCells);


    addShaders(arguments, geometry->getOrCreateStateSet());


    group->addChild(geometry);

    viewer.setSceneData(group);

    std::string filename;
    if (arguments.read("-o",filename))
    {
        osgDB::writeNodeFile(*(viewer.getSceneData()), filename);
        return 1;
    }

    return viewer.run();
}
