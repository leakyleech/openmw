// The Ogre renderable used to hold and display the terrain meshes.
class TerrainMesh : public Ogre::Renderable, public Ogre::MovableObject
{
public:

  TerrainMesh(int segNum, Ogre::SceneNode *parent)
    : Ogre::Renderable(),
      Ogre::MovableObject()
  {
    using namespace Ogre;

    // Get the mesh properties from the archive. The pointer is only
    // valid for the duration of this function.
    const MeshInfo &info = *g_archive.getMeshInfo(segNum);

    // Split all this off into sub-functions again later when you're
    // finished.

    // Use MW coordinates all the way
    mBounds.setExtents(0,0,info.minHeight,
                       // was (mWidth-1) * vertexSeparation
                       info.worldWidth, info.worldWidth,
                       info.maxHeight);

    mCenter = mBounds.getCenter();
    mBoundingRadius = mBounds.getHalfSize().length();

    // TODO: VertexData has a clone() function. This probably means we
    // can set this up once and then clone it, to get a completely
    // unnoticable increase in performance :)
    mVertices = new VertexData();
    mVertices->vertexStart = 0;
    mVertices->vertexCount = info.vertRows*info.vertCols;

    VertexDeclaration* vertexDecl = mVertices->vertexDeclaration;
    size_t currOffset = 0;

    vertexDecl->addElement(0, currOffset, VET_FLOAT3, VES_POSITION);
    currOffset += VertexElement::getTypeSize(VET_FLOAT3);

    vertexDecl->addElement(0, currOffset, VET_FLOAT3, VES_NORMAL);
    currOffset += VertexElement::getTypeSize(VET_FLOAT3);

    vertexDecl->addElement(0, currOffset, VET_FLOAT2,
                           VES_TEXTURE_COORDINATES, 0);
    currOffset += VertexElement::getTypeSize(VET_FLOAT2);

    assert(vertexDecl->getVertexSize(0) == currOffset);

    HardwareVertexBufferSharedPtr mMainBuffer;
    mMainBuffer = HardwareBufferManager::getSingleton().createVertexBuffer
      (
       vertexDecl->getVertexSize(0), // size of one whole vertex
       mVertices->vertexCount,       // number of vertices
       HardwareBuffer::HBU_STATIC_WRITE_ONLY, // usage
       false); // no shadow buffer

    // Bind the data
    mVertices->vertexBufferBinding->setBinding(0, mMainBuffer);

    // Fill the buffer
    float* verts = static_cast<float*>
      (mMainBuffer->lock(HardwareBuffer::HBL_DISCARD));
    info.fillVertexBuffer(verts);
    mMainBuffer->unlock();

    // Create the index data holder
    mIndices = new IndexData();
    mIndices->indexCount = info.indexCount;
    mIndices->indexBuffer =
      HardwareBufferManager::getSingleton().createIndexBuffer
      ( HardwareIndexBuffer::IT_16BIT,
        info.indexCount,
        HardwareBuffer::HBU_STATIC_WRITE_ONLY,
        false);

    // Fill the buffer with warm fuzzy archive data
    unsigned short* indices = static_cast<unsigned short*>
      (mIndices->indexBuffer->lock
       (0, mIndices->indexBuffer->getSizeInBytes(),
        HardwareBuffer::HBL_DISCARD));
    info.fillIndexBuffer(indices);
    mIndices->indexBuffer->unlock();

    // Finally, create the material
    const std::string texName = info.getTexName();

    // TODO: A better thing to do here is to keep the material loaded
    // and retrieve it if it exists.
    assert(!MaterialManager::getSingleton().resourceExists(texName));
    mMaterial = MaterialManager::getSingleton().create
      (texName, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    Pass* pass = mMaterial->getTechnique(0)->getPass(0);
    pass->setLightingEnabled(false);

    int lev = info.getLevel();
    if(lev != 1)
      {
        // This material just has a normal texture
        pass->createTextureUnitState(texName)
          //->setTextureAddressingMode(TextureUnitState::TAM_CLAMP)
          ;
      }
    else
      {
        // We have to use alpha splatting
        float scale = info.getTexScale();

        // Get the background texture
        const char *bgTex = info.getBackgroundTex();
        pass->createTextureUnitState(bgTex)
          ->setTextureScale(scale,scale);

        int alphaSize = info.getAlphaSize();

        // Loop through all the textures in this mesh
        for(int tnum=0; tnum<info.alphaNum; tnum++)
          {
            const AlphaInfo &alpha = *info.getAlphaInfo(tnum);

            // Name of the alpha map texture to create
            std::string alphaName = alpha.getAlphaName();

            // Name of the texture
            std::string tname = alpha.getTexName();

            // TODO: Need to store the result and either delete it in
            // the destructor or fetch it again the next time we run.
            Ogre::TexturePtr texPtr = Ogre::TextureManager::
              getSingleton().createManual
              (alphaName,
               Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
               Ogre::TEX_TYPE_2D,
               alphaSize,alphaSize,
               1,0, // depth, mipmaps
               Ogre::PF_A8, // One-channel alpha
               Ogre::TU_STATIC_WRITE_ONLY);

            // Get the pointer
            Ogre::HardwarePixelBufferSharedPtr pixelBuffer = texPtr->getBuffer();
            pixelBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD);
            const Ogre::PixelBox& pixelBox = pixelBuffer->getCurrentLock();
            Ogre::uint8* pDest = static_cast<Ogre::uint8*>(pixelBox.data);

            // Copy alpha data from file
            alpha.fillAlphaBuffer(pDest);

            // Finish everything up with a lot of Ogre-code
            pixelBuffer->unlock();
            pass = mMaterial->getTechnique(0)->createPass();
            pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
            pass->setLightingEnabled(false);
            pass->setDepthFunction(Ogre::CMPF_EQUAL);

            Ogre::TextureUnitState* tus = pass->createTextureUnitState(alphaName);
            //tus->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);

            tus->setAlphaOperation(Ogre::LBX_BLEND_TEXTURE_ALPHA,
                                   Ogre::LBS_TEXTURE,
                                   Ogre::LBS_TEXTURE);
            tus->setColourOperationEx(Ogre::LBX_BLEND_DIFFUSE_ALPHA,
                                      Ogre::LBS_TEXTURE,
                                      Ogre::LBS_TEXTURE);
            tus->setIsAlpha(true);

            // Add the actual texture on top of the alpha map.
            tus = pass->createTextureUnitState(tname);
            tus->setColourOperationEx(Ogre::LBX_BLEND_DIFFUSE_ALPHA,
                                      Ogre::LBS_TEXTURE,
                                      Ogre::LBS_CURRENT);

            tus->setTextureScale(scale, scale);
          }
      }

    // Finally, set up the scene node.
    mNode = parent->createChildSceneNode(Vector3(info.x, info.y, 0.0));
    mNode->attachObject(this);
  }

  ~TerrainMesh()
  {
    delete mIndices;

    // TODO: This used to crash. See what happens now.
    delete mVertices;

    assert(mNode);

    // We haven't tried moving this further up - there's an off chance
    // it might have something to do with the crash.
    mNode->detachAllObjects();
    mNode->getCreator()->destroySceneNode(mNode);
  }

  //-----------------------------------------------------------------------
  // These are all Ogre functions that we have to override
  //-----------------------------------------------------------------------

  // Internal Ogre function. We should call visitor->visit on all
  // Renderables that are part of this object. In our case, this is
  // only ourselves.
  void visitRenderables(Renderable::Visitor* visitor,
                        bool debugRenderables = false) {
    visitor->visit(this, 0, false);
  }

  void getRenderOperation( Ogre::RenderOperation& op ) {
    op.useIndexes = true;
    op.operationType = Ogre::RenderOperation::OT_TRIANGLE_LIST;
    op.vertexData = mVertices;
    op.indexData = mIndices;
  }

  void getWorldTransforms( Ogre::Matrix4* xform ) const {
    *xform = mNode->_getFullTransform();
  }

  const Ogre::Quaternion& getWorldOrientation(void) const {
    return mNode->_getDerivedOrientation();
  }

  const Ogre::Vector3& getWorldPosition(void) const {
    return mNode->_getDerivedPosition();
  }

  Ogre::Real getSquaredViewDepth(const Ogre::Camera *cam) const {
    Ogre::Vector3 diff = mCenter - cam->getDerivedPosition();
    // Use squared length to avoid square root
    return diff.squaredLength();
  }

  const Ogre::LightList& getLights(void) const {
    if (mLightListDirty) {
      getParentSceneNode()->getCreator()->_populateLightList
        (mCenter, mBoundingRadius, mLightList);
      mLightListDirty = false;
    }
    return mLightList;
  }
  virtual const Ogre::String& getMovableType( void ) const {
    static Ogre::String t = "MW_TERRAIN";
    return t;
  }
  void _updateRenderQueue( Ogre::RenderQueue* queue ) {
    mLightListDirty = true;
    queue->addRenderable(this, mRenderQueueID);
  }
  const Ogre::AxisAlignedBox& getBoundingBox( void ) const {
    return mBounds;
  };
  Ogre::Real getBoundingRadius(void) const {
    return mBoundingRadius;
  }
  virtual const MaterialPtr& getMaterial(void) const
  { return mMaterial; }

  //-----------------------------------------------------------------------
  //-----------------------------------------------------------------------

private:

  Ogre::SceneNode* mNode;

  Ogre::MaterialPtr mMaterial;

  Ogre::VertexData* mVertices;
  Ogre::IndexData* mIndices;

  mutable bool mLightListDirty;

  Ogre::Real mBoundingRadius;
  Ogre::AxisAlignedBox mBounds;
  Ogre::Vector3 mCenter;
};
