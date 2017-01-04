//
//  PinballWorld.cpp
//  PaperBounce3
//
//  Created by Chaim Gingold on 12/5/16.
//
//

#include "PaperBounce3App.h"
#include "glm/glm.hpp"
#include "PinballWorld.h"
#include "PinballParts.h"
#include "geom.h"
#include "cinder/rand.h"
#include "cinder/audio/Context.h"
#include "cinder/audio/Source.h"
#include "Gamepad.h"
#include "xml.h"

using namespace ci;
using namespace ci::app;
using namespace std;
using namespace Pinball;

const bool kDoSound = false;
//const bool kLogButtons = true; // we will need them to configure new controllers :P

PinballWorld::PinballWorld()
{
	setupSynthesis();
	setupControls();
	setupGraphics();
}

void PinballWorld::setupControls()
{
	mIsFlipperDown[0] = false;
	mIsFlipperDown[1] = false;
	
	mFlipperState[0] = 0.f;
	mFlipperState[1] = 0.f;
	
	auto button = [this]( unsigned int id, string postfix )
	{
		auto i = mGamepadButtons.find(id);
		if (i!=mGamepadButtons.end())
		{
			string key = i->second + postfix;
			
			cout << "-> " << key << endl;
			
			auto j = mGamepadFunctions.find(key);
			if (j!=mGamepadFunctions.end())
			{
				j->second();
			}
		}
	};
	
	
	mGamepadManager.mOnButtonDown = [this,button]( const GamepadManager::Event& event )
	{
		cout << "down " << event.mId << endl;
		button(event.mId,"-down");
		
		if ( getBalls().empty() || mGamepadButtons.find(event.mId)==mGamepadButtons.end() )
		{
			// serve if no ball, or it isn't a mapped (eg flipper) button
			serveBall();
		}
	};

	mGamepadManager.mOnButtonUp = [this,button]( const GamepadManager::Event& event )
	{
		cout << "up "  << event.mId << endl;
		button(event.mId,"-up");
	};
	
	// inputs
	mInputToFunction["flippers-left-down"]  = [this]() { mIsFlipperDown[0] = true; };
	mInputToFunction["flippers-left-up"]    = [this]() { mIsFlipperDown[0] = false; };
	mInputToFunction["flippers-right-down"] = [this]() { mIsFlipperDown[1] = true; };
	mInputToFunction["flippers-right-up"]   = [this]() { mIsFlipperDown[1] = false; };
	
	mGamepadFunctions["flippers-left-down"]  = [this]() { mIsFlipperDown[0] = true; };
	mGamepadFunctions["flippers-left-up"]    = [this]() { mIsFlipperDown[0] = false; };
	mGamepadFunctions["flippers-right-down"] = [this]() { mIsFlipperDown[1] = true; };
	mGamepadFunctions["flippers-right-up"]   = [this]() { mIsFlipperDown[1] = false; };
}

void PinballWorld::setupGraphics()
{
	{
		ci::geom::Sphere sphereGeom;
		sphereGeom.colors(false);
		sphereGeom.radius(1.f);
		sphereGeom.center(vec3(0,0,0));
		mBallMesh = gl::VboMesh::create(sphereGeom);
	}

	auto load = [this]( string name, gl::GlslProgRef* to, std::function<void(void)> f )
	{
		mFileWatch.loadShader(
			PaperBounce3App::get()->hotloadableAssetPath( fs::path("shaders") / "pinball" / (name + ".vert") ),
			PaperBounce3App::get()->hotloadableAssetPath( fs::path("shaders") / "pinball" / (name + ".frag") ),
			[this,to,f](gl::GlslProgRef prog)
		{
			*to = prog; // allows null, so we can easily see if we broke it
			if (f) f();
		});
	};
	
	load( "wall", &mWallShader, 0 );
	load( "ball", &mBallShader, 0 );
	load( "floor", &mFloorShader, 0 );
	load( "ball-shadow", &mBallShadowShader, 0 );
	
	mFileWatch.load( PaperBounce3App::get()->hotloadableAssetPath( fs::path("images") / "env_map.jpg" ),
		[this]( fs::path path ){
			try {
				mCubeMap = gl::TextureCubeMap::create( loadImage(path),
					gl::TextureCubeMap::Format().mipmap() );
			} catch (...) {
				mCubeMap = 0;
			}
		}
	);
}

PinballWorld::~PinballWorld()
{
	shutdownSynthesis();
}

void PinballWorld::setParams( XmlTree xml )
{
	if ( xml.hasChild("BallWorld") ) {
		BallWorld::setParams( xml.getChild("BallWorld") );
	}
	
	getXml(xml, "UpVec", mUpVec );
	getXml(xml, "FlipperDistToEdge", mFlipperDistToEdge );
	
	getXml(xml, "PartMaxContourRadius", mPartMaxContourRadius );
	getXml(xml, "HolePartMaxContourRadius",mHolePartMaxContourRadius);
	getXml(xml, "Gravity", mGravity );
	getXml(xml, "BallReclaimAreaHeight", mBallReclaimAreaHeight );
	
	getXml(xml, "BumperMinRadius", mBumperMinRadius );
	getXml(xml, "BumperContourRadiusScale", mBumperContourRadiusScale );
	getXml(xml, "BumperKickAccel", mBumperKickAccel );

	getXml(xml, "BumperOuterColor",mBumperOuterColor);
	getXml(xml, "BumperInnerColor",mBumperInnerColor);
	getXml(xml, "BumperOnColor", mBumperOnColor);
	getXml(xml, "BumperStrobeColor",mBumperStrobeColor);
	
	getXml(xml, "FlipperMinLength",mFlipperMinLength);
	getXml(xml, "FlipperMaxLength",mFlipperMaxLength);
	getXml(xml, "FlipperRadiusToLengthScale",mFlipperRadiusToLengthScale);
	getXml(xml, "FlipperColor",mFlipperColor);

	getXml(xml, "RolloverTargetRadius",mRolloverTargetRadius);
	getXml(xml, "RolloverTargetMinWallDist",mRolloverTargetMinWallDist);
	getXml(xml, "RolloverTargetOnColor",mRolloverTargetOnColor);
	getXml(xml, "RolloverTargetOffColor",mRolloverTargetOffColor);
	getXml(xml, "RolloverTargetStrobeColor",mRolloverTargetStrobeColor);
	getXml(xml, "RolloverTargetDynamicRadius",mRolloverTargetDynamicRadius);
	
	getXml(xml, "CircleMinVerts", mCircleMinVerts );
	getXml(xml, "CircleMaxVerts", mCircleMaxVerts );
	getXml(xml, "CircleVertsPerPerimCm", mCircleVertsPerPerimCm );

	getXml(xml, "PartTrackLocMaxDist", mPartTrackLocMaxDist );
	getXml(xml, "PartTrackRadiusMaxDist", mPartTrackRadiusMaxDist );
	getXml(xml, "DejitterContourMaxDist", mDejitterContourMaxDist );
	
	getXml(xml, "DebugDrawGeneratedContours", mDebugDrawGeneratedContours);
	getXml(xml, "DebugDrawAdjSpaceRays", mDebugDrawAdjSpaceRays );
	getXml(xml, "DebugDrawFlipperAccelHairs", mDebugDrawFlipperAccelHairs );
	getXml(xml, "DebugDrawEnvMaps", mDebugDrawEnvMaps);
	
	getXml(xml, "3d/Enable", m3dEnable );
	getXml(xml, "3d/BackfaceCull", m3dBackfaceCull );
	getXml(xml, "3d/TableDepth", m3dTableDepth );
	getXml(xml, "3d/ZSkew", m3dZSkew );
	getXml(xml, "3d/CubeMapSize", mCubeMapSize );
	getXml(xml, "3d/DynamicEnvMap", m3dDynamicEnvMap );
	
	// gamepad
	if (xml.hasChild("Gamepad"))
	{
		XmlTree keys = xml.getChild("Gamepad");
		
		for( auto item = keys.begin("button"); item != keys.end(); ++item )
		{
			if (item->hasAttribute("id") && item->hasAttribute("do"))
			{
				unsigned int id = item->getAttributeValue<unsigned int>("id");
				string _do = item->getAttributeValue<string>("do");
				
				cout << id << " -> " << _do << endl;
				
				mGamepadButtons[id] = _do;
			}
		}
	}
	
	// keyboard
	if (xml.hasChild("KeyMap"))
	{
		XmlTree keys = xml.getChild("KeyMap");
		
		for( auto item = keys.begin("Key"); item != keys.end(); ++item )
		{
			if ( item->hasChild("char") && item->hasChild("input") )
			{
				string	charkey	= item->getChild("char").getValue<string>();
				string	input	= item->getChild("input").getValue();
				
				char ckey = charkey.front();
				
				cout << ckey << ", " << input << endl;

				mKeyToInput[ckey] = input;
			}
		}
	}
}

Rectf PinballWorld::toPlayfieldBoundingBox ( const PolyLine2 &poly ) const
{
	std::vector<vec2> pts = poly.getPoints();
	
	for( auto &p : pts )
	{
		p = toPlayfieldSpace(p);
	}
	
	return Rectf(pts);
}

void PinballWorld::gameWillLoad()
{
	// most important thing is to prevent BallWorld from doing its default thing.
}

void PinballWorld::sendGameEvent( GameEvent e )
{
	for( auto p : mParts ) {
		p->onGameEvent(e);
	}
}

void PinballWorld::update()
{
	mFileWatch.update();
	
	// input
	mGamepadManager.tick();
	tickFlipperState();

	// tick parts
	for( auto p : mParts ) p->tick();

	// update contours, merging in physics shapes from our parts
	updateBallWorldContours();

	// gravity
	vector<Ball>& balls = getBalls();
	for( Ball &b : balls ) {
		b.mAccel += getGravityVec() * mGravity;
	}
	
	// sim balls
	cullBalls();
	BallWorld::update();
		// TODO: To make pinball flippers super robust in terms of tunneling (especially at the tips),
		// we should make attachments for BallWorld contours (a parallel vector)
		// that specifies angular rotation (center, radians per second)--basically what
		// Flipper::getAccelForBall does, and have BallWorld do more fine grained rotations of
		// the contour itself. So we'd set the flipper to its rotation for the last frame,
		// and specify the rotation to be the rotation that will get us to its rotation for this frame,
		// and BallWorld will handle the granular integration itself.
	
	// respond to collisions
	processCollisions();
}

void PinballWorld::serveBall()
{
	const int oldCount = getBalls().size();

	// for now, from the top
	float fx = Rand::randFloat();
	
	vec2 loc = fromPlayfieldSpace(
		vec2( lerp(mPlayfieldBoundingBox.x1,mPlayfieldBoundingBox.x2,fx), mPlayfieldBoundingBox.y2 )
		);
	
	Ball& ball = newRandomBall(loc);
	
	ball.mCollideWithContours = true;
	ball.mColor = mBallDefaultColor; // no random colors that are set entirely in code, i think. :P
	
	sendGameEvent(GameEvent::ServeBall);
	if (oldCount>0) sendGameEvent(GameEvent::ServeMultiBall);
}

void PinballWorld::cullBalls()
{
	const int oldCount = getBalls().size();
	
	// remove?
	vector<Ball>& balls = getBalls();
	vector<Ball> newBalls;
	
	for( const auto &b : balls )
	{
		if ( toPlayfieldSpace(b.mLoc).y + b.mRadius < mPlayfieldBallReclaimY )
		{
			// kill
		}
		else
		{
			// keep
			newBalls.push_back(b);
		}
	}
	
	balls = newBalls;
	
	// events
	const int newCount = balls.size();
	
	if (newCount==0 && oldCount>0) sendGameEvent(GameEvent::LostLastMultiBall);
	if (newCount < oldCount) sendGameEvent(GameEvent::LostBall);
}

void PinballWorld::tickFlipperState()
{
	for( int i=0; i<2; ++i )
	{
		if (0)
		{
			// non-linear, creates lots of tunneling
			float frac[2] = { .35f, .5f };
			int fraci = mIsFlipperDown[i] ? 0 : 1;
			
			mFlipperState[i] = lerp( mFlipperState[i], mIsFlipperDown[i] ? 1.f : 0.f, frac[fraci] );
		}
		else
		{
			
			// linear, to help me debug physics
			float step = (1.f / 60.f) * ( 1.f / .1f );
			
			if ( mIsFlipperDown[i] ) mFlipperState[i] += step;
			else mFlipperState[i] -= step;
			
			mFlipperState[i] = constrain( mFlipperState[i], 0.f, 1.f );
		}
	}
}

float PinballWorld::getStrobe( float phase, float freq ) const
{
	float strobe;
	strobe = getStrobeTime();
	strobe = fmod( strobe, freq ) / freq;
	strobe = (cos( (strobe + phase)*M_PI*2.f) + 1.f) / 2.f;
	return strobe;
}

float PinballWorld::getFlipperAngularVel( int side ) const
{
	const float eps = .1f;
	const float radPerSec = (M_PI/2.f) / 6.f; // assume 6 steps per 90 deg of motion
	const float sign[2] = {1.f,-1.f};
	
	if ( mIsFlipperDown[side] && mFlipperState[side] < 1.f-eps ) return radPerSec * sign[side];
	else if ( !mIsFlipperDown[side] && mFlipperState[side] > eps ) return -radPerSec * sign[side];
	else return 0.f;
}

void PinballWorld::processCollisions()
{
	for( const auto &c : getBallBallCollisions() )
	{
	//	mPureDataNode->sendBang("new-game");

		if (0) cout << "ball ball collide (" << c.mBallIndex[0] << ", " << c.mBallIndex[1] << ")" << endl;
	}
	
	for( const auto &c : getBallWorldCollisions() )
	{
//		mPureDataNode->sendBang("hit-wall");

		if (0) cout << "ball world collide (" << c.mBallIndex << ")" << endl;
	}

	//pd::List hitObjects;
	//pd::List hitWalls;
	int hitObjects = 0;
	int hitWalls = 0;

	for( const auto &c : getBallContourCollisions() )
	{


		if (0) cout << "ball contour collide (ball=" << c.mBallIndex << ", " << c.mContourIndex << ")" << endl;
		
		// tell part
		PartRef p = findPartForContour(c.mContourIndex);
		if (p) {

			if (p.get()->getType() == PartType::Bumper) {
				// hitObjects.addFloat(0);
				hitObjects++;
			} else {
				// hitWalls.addFloat(0);
				hitWalls++;

			}

			Ball* ball=0;
			if (c.mBallIndex>=0 && c.mBallIndex<=getBalls().size())
			{
				ball = &getBalls()[c.mBallIndex];
			}
			
	//		cout << "collide part" << endl;
			if (ball) p->onBallCollide(*ball);
		}
		else {
			// hitWalls.addFloat(0);
			hitWalls++;
	//		cout << "collide wall" << endl;
		}
	}

	//mPureDataNode->sendList("hit-objects", hitObjects);
	//mPureDataNode->sendList("hit-walls", hitWalls);
	pd::List hitList;
	hitList.addFloat(MIN(5, hitObjects));
	hitList.addFloat(MIN(5, hitWalls));

	mPureDataNode->sendList("hit-list", hitList);
	//mPureDataNode->sendFloat("hit-walls", );
	//mPureDataNode->sendFloat("hit-objects", MIN(5, hitObjects));
}

void PinballWorld::drawBallCullLine() const
{
	// playfield markers
	gl::color(1,0,0);
	gl::drawLine(
		fromPlayfieldSpace(vec2(mPlayfieldBallReclaimX[0],mPlayfieldBallReclaimY)),
		fromPlayfieldSpace(vec2(mPlayfieldBallReclaimX[1],mPlayfieldBallReclaimY)) ) ;
}

void PinballWorld::prepareToDraw()
{
	BallWorld::prepareToDraw();
	
	if (m3dEnable) {
		prepare3dScene();
		if (m3dDynamicEnvMap) updateEnvMaps();
		else mEnvMaps.clear();
	}
}

void PinballWorld::draw( DrawType drawType )
{
	// world
	if (m3dEnable && (drawType==DrawType::UIMain || drawType==DrawType::Projector) ) draw3d(drawType);
	else draw2d(drawType);

	if ( mDebugDrawEnvMaps && drawType==DrawType::UIMain && !mEnvMaps.empty() )
	{
		const int kWidth = 100;
		const int kHeight = kWidth/2;
		
//		for( int i=0; i<mEnvMaps.size(); ++i )
		if (!mEnvMaps.empty())
		{
			int i=0;
			if (mEnvMaps[i]) {
				gl::drawHorizontalCross( mEnvMaps[i]->getTextureCubeMap(),
					//Rectf( 0, kHeight*i, kWidth, (kHeight+1)*i ) );
					Rectf( 0, 0, kWidth, kHeight ) );
			}
		}
	}

	// --- debugging/testing ---

	if (0) rolloverTest();
	
	// world orientation debug info
	if (0)
	{
		vec2 c = getWorldBoundsPoly().calcCentroid();
		float l = 10.f;

		gl::color(1,0,0);
		gl::drawLine( c, c + getRightVec() * l );

		gl::color(0,1,0);
		gl::drawLine( c, c + getLeftVec() * l );

		gl::color(0,0,1);
		gl::drawLine( c, c + getUpVec() * l );
	}
	
	// test ray line seg...
	if (mDebugDrawAdjSpaceRays) drawAdjSpaceRays();

	// test contour generation
	if (mDebugDrawGeneratedContours)
	{
		ContourVec cs = getContours();
		int i = cs.size(); // only start loop at new contours we append
		
		ContourToPartMap c2pm; // don't care
		
		getContoursFromParts(mParts,cs,c2pm);
		
		for( ; i<cs.size(); ++i )
		{
			const auto &c = cs[i];
			
			gl::color(0,.5,.5);
			gl::drawStrokedRect(c.mBoundingRect);

			gl::color(0,1,0);
			gl::draw(c.mPolyLine);
		}
	}
}

void PinballWorld::draw2d( DrawType drawType )
{
	drawBallCullLine();
	
	// flippers, etc...
	drawParts();

	// balls
	BallWorld::draw(drawType);
}

void PinballWorld::beginDraw3d() const
{
	// all of this may be for naught as it seems that it won't hold up to the
	// deformations we do for the projector transform.
	// so the right way to do this--without doing something really transformationally clever with
	// the ogl pipeline i can barely even get my brain to think of the possibility--would be to
	// render to a texture and then project onto the table. 
	
	// alternatives: we use a stencil to cut out the transparent part of the tabletop.
	// i think our 3d effect might still be a bit messed, but we might be able to adjust the skew
	// to compensate for that.
	// that actually could get us quite close.
	
	// i think +z is away from the camera (doh!), and i don't quite have that right yet...
	// also, projector view is quite messed compared to UI window :P, but UI is a start...
	

	
	// back face culling (a little weird, and not necessary)
	if (m3dBackfaceCull)
	{
		const bool isViewFlipped = (gl::getViewMatrix() * vec4(0,0,1,1)).w < 1.f; 
	//	cout << gl::getViewMatrix() * vec4(0,0,1,1) << endl;	
			// this insanity is to capture when things have turned inside out on us, and
			// we need to reverse culling... (e.g. for reversed projection mapping)
			// I have **NO** idea how robust this test is; it just seems to work given the one
			// test case I did. I'm sure there is a right way to do this, I just don't know what it is.
			

		gl::enableFaceCulling();
		gl::cullFace( isViewFlipped ? GL_FRONT : GL_BACK );
	}
	
	// depth
	gl::enableDepthRead();
	gl::enableDepthWrite();

	if (1)
	{
		const bool kDebugVizDepth = false;
		
		if (kDebugVizDepth) {
			gl::color(.5,.5,.5);
		} else {
			gl::colorMask(false, false, false, false);
		}
		
		glDepthFunc(GL_ALWAYS);
		
		// write 0 everywhere for tabletop
		gl::clearDepth(0.f);
		gl::clear(GL_DEPTH_BUFFER_BIT);

		// punch out holes where paper is at table floor, and tabletops for holes inside of that
		std::function<void(const Contour& c)> recurseTree;
		
		recurseTree = [&]( const Contour& c ) -> void
		{
			if ( !shouldContourBeAPart(c,mVisionContours) )
			{
				// draw me
				const bool punchOut = !c.mIsHole;
				
				if (punchOut) {
					// if punching out, go in; otherwise we stay at 0 (and fill)
					gl::pushModelView();
					gl::translate(vec3(0,0,m3dTableDepth*2.f)); // go beyond floor, because we will draw it in later
					if (kDebugVizDepth) gl::color(1,1,1);
				}
				else if (kDebugVizDepth) gl::color(.5,.5,.5);
				
				gl::drawSolid(c.mPolyLine);
				
				if (punchOut) gl::popModelView();
				
				// childers
				for( int childIndex : c.mChild ) {
					recurseTree(mVisionContours[childIndex]);
				}
			}
		};
		
		for( const auto &c : mVisionContours )
		{
			if (c.mTreeDepth==0) recurseTree(c);
		}
		gl::colorMask(true,true,true,true);
		glDepthFunc(GL_LESS);
	}

	// 3d transform
	{
		mat4 skew;
		skew[2][0] = getGravityVec().x * m3dZSkew;
		skew[2][1] = getGravityVec().y * m3dZSkew;
		gl::pushModelView();
		gl::multModelMatrix(skew);
	}	
}

void PinballWorld::endDraw3d() const
{
	gl::popModelView();

	gl::enableDepthRead(false);
	gl::enableDepthWrite(false);
	
	if (m3dBackfaceCull) {
		gl::enableFaceCulling(false);
	}
}

Shape2d PinballWorld::polyToShape( const PolyLine2& poly ) const
{
	Shape2d shape;
	shape.moveTo(poly.getPoints()[0]);
	for( int i=1; i<poly.size(); ++i ) shape.lineTo(poly.getPoints()[i]);
	if (poly.isClosed()) shape.close(); // so we can generalize poly -> shape
	return shape;
}

TriMeshRef PinballWorld::get3dMeshForPoly( const PolyLine2& poly, float znear, float zfar ) const
{
	float extrudeDepth = zfar - znear;
	
	std::function<Colorf(vec3)> posToColor = [&]( vec3 v ) -> Colorf
	{
		return Colorf(1,1,1);
		
		return lerp(
//			Colorf(0,1,0),
//			Colorf(1,0,0),
			Colorf(1,1,1),
//			Colorf(0,0,0),
			Colorf(.5,.5,.5),
			constrain( (v.z-znear)/(zfar-znear), 0.f, 1.f ) );
			// near -> far (from viewer)
	};
	
//	PolyLine2 poly=c.mPolyLine;
//			poly.reverse(); // turn it inside out, so normals face inward
	
	return TriMesh::create(
		   geom::Extrude( polyToShape(poly), extrudeDepth ).caps(false).subdivisions( 1 )
		>> geom::Translate(0,0,extrudeDepth/2+znear)
		>> geom::ColorFromAttrib( geom::POSITION, posToColor )
	);
}

void PinballWorld::prepare3dScene()
{
	mDrawScene = Scene();
	
	// 3d contours
	for( const auto &c : mVisionContours )
	{
		if ( !shouldContourBeAPart(c,mVisionContours) )
		{
			mDrawScene.mWalls.push_back( get3dMeshForPoly(c.mPolyLine,0.f,m3dTableDepth) );
		}
	}

	// 3d part sides
	for( auto &p : mParts )
	{
		p->addTo3dScene(mDrawScene);
	}
}

void PinballWorld::draw3dFloor() const
{
	// floor
	if (mFloorShader)
	{
		gl::ScopedGlslProg glslScp(mFloorShader);
		
		gl::pushModelView();
		gl::translate(0,0,m3dTableDepth);
		// we don't z fight with depth punch out because it is beyond the floor;
		// we are building a floor for things to clip against here.
		
		for( const auto &c : mVisionContours )
		{
			if ( !c.mIsHole && !shouldContourBeAPart(c,mVisionContours) )
			{
				gl::drawSolid(c.mPolyLine);
			}
		}
		
		gl::popModelView();
	}
}

void PinballWorld::draw3dScene() const
{
	auto drawSceneSegment = [this]( gl::GlslProgRef shader, const Scene::Meshes& meshes )
	{
		gl::ScopedGlslProg glslScp(shader);
		
		// mDrawScene is assembled in prepare3dScene
		for( auto w : meshes ) {
			if (w.mMesh) {
				gl::ScopedModelMatrix trans;
				gl::multModelMatrix(w.mTransform);
				gl::draw(*w.mMesh);
			}
		}
	};
	
	drawSceneSegment(mWallShader,mDrawScene.mWalls);
}

void PinballWorld::draw3dBalls() const
{
	if (mBallShader)
	{
		gl::ScopedGlslProg glslScp(mBallShader);
		
		if (mCubeMap && !m3dDynamicEnvMap) {
			mCubeMap->bind();
			mBallShader->uniform( "uCubeMapTex", 0 );
		}
		
		mBallShader->uniform( "uCubeMapTex", 0 );
		
		for( int i=0; i<getBalls().size(); ++i )
		{
			if (m3dDynamicEnvMap)
			{
				gl::TextureCubeMapRef env;
				
				if ( mEnvMaps.size() > i ) {
					env = mEnvMaps[i]->getTextureCubeMap();
				}
				
				if (!env) env = mCubeMap;
				if (env) env->bind();
			}
			
			const Ball& b = getBalls()[i];
			
			gl::ScopedModelMatrix model;
			gl::multModelMatrix( getBallTransform(b) );
			gl::translate(0,0,m3dTableDepth-b.mRadius/2);
			gl::draw(mBallMesh);
		}
	}
}

void PinballWorld::draw3dRibbons( DrawType drawType ) const
{
	gl::ScopedModelMatrix trans;
	gl::translate(0,0,m3dTableDepth - mBallDefaultRadius*.5f);
	
	gl::enableDepthWrite(false);
	BallWorld::drawRibbons(drawType);
	gl::enableDepthWrite(true);
}

void PinballWorld::draw3d( DrawType drawType )
{
	beginDraw3d();

	draw3dFloor();
	
	// collected 3d scene
	draw3dScene();

	// ball shadows
	if (mBallShadowShader)
	{
		gl::ScopedModelMatrix trans;
		gl::translate(0,0,m3dTableDepth - .01f);
		
		BallWorld::drawBalls(drawType,mBallShadowShader);
	}
	
	// parts (2d)
	for( const auto &p : mParts ) {
		p->draw();
	}

	// red line
	drawBallCullLine();

	// balls
	draw3dBalls();
	draw3dRibbons(drawType);

	// done with 3d
	endDraw3d();
}

void PinballWorld::updateEnvMaps()
{
	if (0)
	{
		mEnvMaps.resize(1);
		
		mEnvMaps[0] = getCubeMap( mEnvMaps[0], vec3(getWorldBoundsPoly().calcCentroid(),getTableDepth()/2.f) );
	}
	else
	{
		mEnvMaps.resize( getBalls().size() );
		
		for( int i=0; i<mEnvMaps.size(); ++i )
		{
			const auto& ball = getBalls()[i];
			
			mEnvMaps[i] = getCubeMap( mEnvMaps[i], vec3( ball.mLoc, getTableDepth() - ball.mRadius ) );
		}
	}
}

gl::FboCubeMapRef PinballWorld::getCubeMap( gl::FboCubeMapRef fbo, vec3 eye ) const
{
	if ( !fbo || fbo->getSize() != ivec2(mCubeMapSize, mCubeMapSize) ) {
		fbo = gl::FboCubeMap::create( mCubeMapSize, mCubeMapSize );
	}
	
	if (fbo)
	{
		gl::ScopedMatrices matrix;
		gl::ScopedViewport viewport( ivec2( 0, 0 ), fbo->getSize() );
//		gl::ScopedDepthTest depth(true);

		// we need to save the current FBO because we'll be calling bindFramebufferFace() below
		gl::context()->pushFramebuffer();
		
		for( uint8_t dir = 0; dir < 6; ++dir )
		{
			ci::CameraPersp camera( fbo->getWidth(), fbo->getHeight(), 90.0f, 1, 1000 );
			gl::setProjectionMatrix( camera.getProjectionMatrix() );
			gl::setViewMatrix( fbo->calcViewMatrix( GL_TEXTURE_CUBE_MAP_POSITIVE_X + dir, eye ) );
			
			fbo->bindFramebufferFace( GL_TEXTURE_CUBE_MAP_POSITIVE_X + dir );
			
			gl::clear();
			
			draw3dFloor();
			draw3dRibbons(DrawType::EnvMap);
			draw3dScene();
		}

		gl::context()->popFramebuffer();
	}
	
	return fbo;
}

void PinballWorld::drawAdjSpaceRays() const
{
	for( auto &p : mParts )
	{
		vec2 loc = p->getAdjSpaceOrigin();
		
		const AdjSpace space = getAdjacentSpace(loc,mVisionContours);

		auto draw = [&]( ColorA color, vec2 vec, float width, float m )
		{
			gl::color(color);
			gl::drawLine(
				loc + vec *  width,
				loc + vec * (width + m) );
		};
		
		draw( Color(1,0,0), getRightVec(), space.mWidthRight, space.mRight );
		draw( Color(0,1,0), getLeftVec(), space.mWidthLeft, space.mLeft );
		draw( Color(1,0,0), getUpVec(), space.mWidthUp, space.mUp );
		draw( Color(0,1,0), getDownVec(), space.mWidthDown, space.mDown );


		gl::color(0,0,1);
		gl::drawLine(
			loc + getLeftVec ()  * space.mWidthLeft,
			loc + getRightVec()  * space.mWidthRight );

		gl::color(0,0,1);
		gl::drawLine(
			loc + getUpVec  ()  * space.mWidthUp,
			loc + getDownVec()  * space.mWidthDown );
	}
}

void PinballWorld::worldBoundsPolyDidChange()
{
}

void PinballWorld::processKeyEvent( KeyEvent event, string suffix )
{
	char c = event.getChar();
	
	auto i = mKeyToInput.find(c);
	
	if (i!=mKeyToInput.end())
	{
		auto j = mInputToFunction.find(i->second + suffix);
		if (j!=mInputToFunction.end())
		{
			j->second();
		}
	}
}

void PinballWorld::keyDown( KeyEvent event )
{
	processKeyEvent(event, "-down");

	if ( getBalls().empty() || mKeyToInput.find(event.getChar())==mKeyToInput.end() )
	{
		// serve if no ball, or it isn't a mapped (eg flipper) key
		serveBall();
	}
}

void PinballWorld::keyUp( KeyEvent event )
{
	processKeyEvent(event, "-up");
}

void PinballWorld::mouseClick( vec2 loc )
{
	PartRef part( new RolloverTarget( *this, loc, mRolloverTargetRadius ) );
	
	part->setShouldAlwaysPersist(true);
	
	mParts.push_back(part);
}

void PinballWorld::drawParts() const
{
	for( const auto &p : mParts ) {
		p->draw();
	}
}

// Vision
Rectf PinballWorld::getPlayfieldBoundingBox( const ContourVec& cs ) const
{
	vector<vec2> pts;
	
	for( const auto& c : cs )
	{
		if (c.mTreeDepth==0 && !shouldContourBeAPart(c,cs))
		{
			pts.insert(pts.end(),c.mPolyLine.getPoints().begin(), c.mPolyLine.end());
		}
	}
	
	for( auto &p : pts )
	{
		p = toPlayfieldSpace(p);
	}
	
	return Rectf(pts);
}

void PinballWorld::updatePlayfieldLayout( const ContourVec& contours )
{
	mPlayfieldBoundingBox = getPlayfieldBoundingBox(contours);
//	cout << "min/max y: " << mPlayfieldBoundingBox.y1 << " " << mPlayfieldBoundingBox.y2 << endl;
	mPlayfieldBallReclaimY = mPlayfieldBoundingBox.y1 + mBallReclaimAreaHeight;
	
	mPlayfieldBallReclaimX[0] = mPlayfieldBoundingBox.x1;
	mPlayfieldBallReclaimX[1] = mPlayfieldBoundingBox.x2;
	
	float t;
	vec2 vleft  = vec2(mPlayfieldBallReclaimX[0],mPlayfieldBallReclaimY);
	vec2 vright = vec2(mPlayfieldBallReclaimX[1],mPlayfieldBallReclaimY);
	if ( contours.rayIntersection( fromPlayfieldSpace(vleft), getRightVec(), &t ) )
	{
		mPlayfieldBallReclaimX[0] = (vleft + toPlayfieldSpace(getRightVec() * t)).x;
	}
	
	if ( contours.rayIntersection( fromPlayfieldSpace(vright), getLeftVec(), &t ) )
	{
		mPlayfieldBallReclaimX[1] = (vright + toPlayfieldSpace(getLeftVec() * t)).x;
	}
}

void PinballWorld::updateBallWorldContours()
{
	// modify vision out for ball world
	// (we add contours to convey the new shapes to simulate)
	ContourVec physicsContours = mVisionContours;

	getContoursFromParts( mParts, physicsContours, mContoursToParts );
	
	// which contours to use?
	// (we could move this computation into updateVision(), but its cheap and convenient to do it here)
	ContourVec::Mask mask(physicsContours.size(),true); // all true...
	for( int i=0; i<mVisionContours.size(); ++i )
	{
		// ...except vision contours that became parts
		mask[i] = !shouldContourBeAPart(physicsContours[i],physicsContours);
	}
	
	// tell ball world
	BallWorld::setContours(physicsContours,ContourVec::getMaskFilter(mask));
}

ContourVec PinballWorld::dejitterVisionContours( ContourVec in, ContourVec old ) const
{
	ContourVec out = in;
	
	for( auto &c : out )
	{
		// find closest contour to compare ourselves against
		float bestscore = MAXFLOAT;
		const Contour* match = 0;
		
		for( auto &o : old )
		{
			if (o.mIsHole != c.mIsHole) continue;
			if (o.mTreeDepth != c.mTreeDepth) continue;
			
			float score = distance( c.mCenter, o.mCenter )
				+ fabs( c.mRadius - o.mRadius )
				;
			
			if (score < bestscore)
			{
				bestscore=score;
				match = &o;
			}
		}
		
		// match points
		if (match)
		{
			for( auto &p : c.mPolyLine.getPoints() )
			{
				float bestscore = mDejitterContourMaxDist;
				
				for ( auto op : match->mPolyLine.getPoints() )
				{
					float score = distance(p,op);
					if (score<bestscore)
					{
						bestscore = score;
						p = op;
					}
				}
			}
		}
	}
	
	return out;
}

void PinballWorld::updateVision( const Vision::Output& visionOut, Pipeline& p )
{
	// capture contours, so we can later pass them into BallWorld (after merging in part shapes)
	if ( mDejitterContourMaxDist > 0.f ) {
		mVisionContours = dejitterVisionContours( visionOut.mContours, mVisionContours );
	} else {
		mVisionContours = visionOut.mContours;
	}
	
	// playfield layout
	updatePlayfieldLayout(visionOut.mContours);
	
	// generate parts
	PartVec newParts = getPartsFromContours(visionOut.mContours);
	mParts = mergeOldAndNewParts(mParts, newParts);
}

bool PinballWorld::shouldContourBeAPart( const Contour& c, const ContourVec& cs ) const
{
	if ( c.mIsHole ) {
		return c.mTreeDepth>0 && c.mRadius < mPartMaxContourRadius;
	}
	else {
		bool depthOK;
		
		if (c.mTreeDepth==0) depthOK=true; // ok
		else if (c.mTreeDepth==2) // allow them to be nested 1 deep, but only if parent contour isn't a part!
		{
			auto p = cs.getParent(c);
			assert(p);
			depthOK = !shouldContourBeAPart(*p,cs);
		}
		else depthOK=false;
		
		return depthOK && c.mRadius < mHolePartMaxContourRadius;
	}
}

AdjSpace
PinballWorld::getAdjacentSpace( vec2 loc, const ContourVector& cs ) const
{
	const Contour* leaf = cs.findLeafContourContainingPoint(loc);
	
	return getAdjacentSpace(leaf, loc, cs);
}

AdjSpace
PinballWorld::getAdjacentSpace( const Contour* leaf, vec2 loc, const ContourVector& cs ) const
{
	AdjSpace result;

	if (leaf)
	{
		float far = leaf->mRadius * 2.f;
		
		auto calcSize = [&]( vec2 vec, float& result )
		{
			float t;
			if ( leaf->rayIntersection(loc + vec * far, -vec, &t) ) {
				result = far - t;
			}
		};
		
		calcSize( getLeftVec(), result.mWidthLeft );
		calcSize( getRightVec(), result.mWidthRight );
		calcSize( getUpVec(), result.mWidthUp );
		calcSize( getDownVec(), result.mWidthDown );
	}
	
	auto filter = [&]( const Contour& c ) -> bool
	{
		// 1. not self
		//if ( c.mIsHole && c.contains(loc) ) return false;
		if ( &c == leaf ) return false; // supposed to be optimized version of c.mIsHole && c.contains(loc)
		// 2. not other parts
		else if ( shouldContourBeAPart(c,cs) ) return false; // could be a part
		// OK
		else return true;
	};
	
	cs.rayIntersection( loc, getRightVec(), &result.mRight, filter );
	cs.rayIntersection( loc, getLeftVec (), &result.mLeft , filter );
	cs.rayIntersection( loc, getUpVec(),    &result.mUp , filter );
	cs.rayIntersection( loc, getDownVec(),  &result.mDown , filter );
	
	// bake in contour width into adjacent space calc
	result.mLeft  -= result.mWidthLeft;
	result.mRight -= result.mWidthRight;
	result.mUp    -= result.mWidthUp;
	result.mDown  -= result.mWidthDown;
	
	return result;
}

PartVec PinballWorld::getPartsFromContours( const ContourVector& contours )
{
	PartVec parts;
	
	for( const auto &c : contours )
	{
		if ( !shouldContourBeAPart(c,contours) ) continue;


		AdjSpace adjSpace = getAdjacentSpace(&c,c.mCenter,contours);

		auto add = [&c,&parts,adjSpace]( Part* p )
		{
			p->mContourLoc = c.mCenter;
			p->mContourRadius = c.mRadius;
			parts.push_back( PartRef(p) );
		};
		

		if ( c.mIsHole )
		{
			// flipper orientation
			const bool closeToRight  = adjSpace.mRight < mFlipperDistToEdge;
			const bool closeToLeft   = adjSpace.mLeft  < mFlipperDistToEdge;
//			const bool closeToBottom = adjSpace.mDown  < mFlipperDistToEdge;
			
			if ( closeToRight && !closeToLeft )
			{
				add( new Flipper(*this, c.mCenter, c.mRadius, PartType::FlipperRight) );
			}
			else if ( closeToLeft && !closeToRight )
			{
				add( new Flipper(*this, c.mCenter, c.mRadius, PartType::FlipperLeft) );
			}
//			else if ( closeToLeft && closeToRight && closeToBottom )
//			{
//				 add( new Plunger( *this, c.mCenter, c.mRadius ) );
//			}
			else
			{
				add( new Bumper( *this, c.mCenter, c.mRadius, adjSpace ) );
			}
		} // hole
		else
		{
			// non-hole:
			
			// make rollover target
			auto filter = [this,contours]( const Contour& c ) -> bool {
				return !shouldContourBeAPart(c,contours);
			};
			
			float dist;
			vec2 closestPt;
			
			contours.findClosestContour(c.mCenter,&closestPt,&dist,filter);
			
			vec2 rolloverLoc = c.mCenter;
			float r = mRolloverTargetRadius;
			
			const float kMaxDist = mRolloverTargetDynamicRadius*4.f;
			
			if ( closestPt != c.mCenter && dist < kMaxDist )
			{
				vec2 dir = normalize( closestPt - c.mCenter );

				float far=0.f;
				
				if (mRolloverTargetDynamicRadius)
				{
					r = min( mRolloverTargetRadius*4.f, max( r, distance(closestPt,c.mCenter) - c.mRadius ) );
					far = r;
				}
				
				rolloverLoc = closestPt + dir * (r+far);

				auto rt = new RolloverTarget(*this,rolloverLoc,r);
				rt->mContourPoly = c.mPolyLine;
				
				add( rt );
			}
		}
		
	} // for
	
	return parts;
}

PartRef PinballWorld::findPartForContour( const Contour& c ) const
{
	return findPartForContour( getContours().getIndex(c) );
}

PartRef PinballWorld::findPartForContour( int contouridx ) const
{
	auto i = mContoursToParts.find(contouridx);
	
	if (i==mContoursToParts.end()) return 0;
	else return i->second;
}

PartVec
PinballWorld::mergeOldAndNewParts( const PartVec& oldParts, const PartVec& newParts ) const
{
	PartVec parts = newParts;
	
	// match old parts to new ones
	for( PartRef &p : parts )
	{
		// does it match an old part?
		bool wasMatched = false;
		
		for( const PartRef& old : oldParts )
		{
			if ( !old->getShouldAlwaysPersist() && p->getShouldMergeWithOldPart(old) )
			{
				// matched.
				bool replace=true;
				
				// but...
				// replace=false
				
				// replace with old.
				// (we are simply shifting pointers rather than copying contents, but i think this is fine)
				if (replace) {
					p = old;
					wasMatched = true;
				}
			}
		}
		
		//
		if (!wasMatched) p->onGameEvent(GameEvent::NewPart);
	}
	
	// carry forward any old parts that should be
	for( const PartRef &p : oldParts )
	{
		if (p->getShouldAlwaysPersist())
		{
			bool doIt=true;
			
			// are we an expired trigger?
			if ( p->getType()==PartType::RolloverTarget )
			{
				auto rt = dynamic_cast<RolloverTarget*>(p.get());
				if ( rt && !isValidRolloverLoc( rt->mLoc, rt->mRadius, oldParts ) )
				{
//					doIt=false;
				}
			}
			
			if (doIt) parts.push_back(p);
		}
	}
	
	return parts;
}

void PinballWorld::getContoursFromParts( const PartVec& parts, ContourVec& contours, ContourToPartMap& c2p ) const
{
	c2p.clear();
	
	for( const PartRef p : parts )
	{
		PolyLine2 poly = p->getCollisionPoly();
		
		if (poly.size()>0)
		{
			addContourToVec( contourFromPoly(poly), contours );
			
			c2p[contours.size()-1] = p;
		}
	}
}

bool PinballWorld::isValidRolloverLoc( vec2 loc, float r, const PartVec& parts ) const
{
	// outside?
	if ( ! mVisionContours.findLeafContourContainingPoint(loc) ) {
		return false;
	}

	// too close to edge?
	float closestContourDist;
	if ( mVisionContours.findClosestContour(loc,0,&closestContourDist)
	  && closestContourDist < mRolloverTargetMinWallDist + r )
	{
		return false;
	}
	
	// parts
	for( const auto &p : parts ) {
		if ( !p->isValidLocForRolloverTarget(loc,r) ) return false;
	}
	
	// ok
	return true;
}

void PinballWorld::rolloverTest()
{
	const Rectf playfieldbb = toPlayfieldBoundingBox(getWorldBoundsPoly());
	
	Rand rgen(0);
	
	for ( int i=0; i<200; ++i )
	{
		vec2 p( rgen.nextFloat(), rgen.nextFloat() );
		
		p.x = lerp( playfieldbb.x1, playfieldbb.x2, p.x );
		p.y = lerp( playfieldbb.y1, playfieldbb.y2, p.y );
		
		p = fromPlayfieldSpace(p);
		float r = mRolloverTargetRadius;
		
		gl::color( isValidRolloverLoc(p, r, mParts) ? Color(0,1,0) : Color(1,0,0) );
		gl::drawSolidCircle(p, r);
	}
}

int PinballWorld::getNumCircleVerts( float r ) const
{
	return constrain( (int)(M_PI*r*r / mCircleVertsPerPerimCm), mCircleMinVerts, mCircleMaxVerts );
}

PolyLine2 PinballWorld::getCirclePoly( vec2 c, float r ) const
{
	const int n = getNumCircleVerts(r);

	PolyLine2 poly;
	
	for( int i=0; i<n; ++i )
	{
		float t = ((float)i/(float)(n-1)) * M_PI*2;
		poly.push_back( vec2(cos(t),sin(t)) );
	}
	
	poly.scale( vec2(1,1) * r );
	poly.offset(c);
	poly.setClosed();
	
	return poly;
}

PolyLine2 PinballWorld::getCapsulePoly( vec2 c[2], float r[2] ) const
{
	PolyLine2 poly;
	
	vec2 a2b = c[1] - c[0];
	vec2 a2bnorm = normalize(a2b);
	
	int numVerts[2] = { getNumCircleVerts(r[0]), getNumCircleVerts(r[1]) };

	// c0
	vec2 p1 = perp(a2bnorm) * r[0];
	mat4 r1 = glm::rotate( (float)(M_PI / ((float)numVerts[0])), vec3(0,0,1) );
	
	poly.push_back(p1+c[0]);
	for( int i=0; i<numVerts[0]; ++i )
	{
		p1 = vec2( r1 * vec4(p1,0,1) );
		poly.push_back(p1+c[0]);
	}

	// c1
	p1 = -perp(a2bnorm) * r[1];
	r1 = glm::rotate( (float)(M_PI / ((float)numVerts[1])), vec3(0,0,1) );
	
	poly.push_back(p1+c[1]);
	for( int i=0; i<numVerts[1]; ++i )
	{
		p1 = vec2( r1 * vec4(p1,0,1) );
		poly.push_back(p1+c[1]);
	}
	
	poly.setClosed();
	
	return poly;
}

Contour PinballWorld::contourFromPoly( PolyLine2 poly ) const
{
	Contour c;
	c.mPolyLine = poly;
	c.mCenter = poly.calcCentroid();
	c.mBoundingRect = Rectf( poly.getPoints() );
	c.mRadius = max( c.mBoundingRect.getWidth(), c.mBoundingRect.getHeight() ) * .5f ;
	c.mArea = M_PI * c.mRadius * c.mRadius; // use circle approximation for area

	c.mPolyLine.setClosed(); // ensure this is true in case any parts fail to do this right (ray casting cares) 
	
	// TODO: rotated bounding box correctly
	// just put in a coarse approximation for now in case it starts to matter
	c.mRotatedBoundingRect.mCenter = c.mBoundingRect.getCenter();
	c.mRotatedBoundingRect.mAngle  = 0.f;
	c.mRotatedBoundingRect.mSize = c.mBoundingRect.getSize(); // not sure if i got rotation/size semantics right
	
	return c;
}

void PinballWorld::addContourToVec( Contour c, ContourVec& contours ) const
{
	// ideally, BallWorld has an intermediate input representation of shapes to simulate, and we add to that.
	// but for now, we do this and use contour vec itself
	
	// stitch into polygon hierarchy
	c.mIsHole = true;
	Contour* parent = contours.findLeafContourContainingPoint(c.mCenter); // uh... a bit sloppy, but it should work enough
	
	// make sure we are putting a hole in a non-hole
	// (e.g. flipper is made by a hole, and so we don't want to be a non-hole in that hole, but a sibling to it)
	if (parent && parent->mIsHole) {
		parent = contours.getParent(*parent);
	}

	// link it in
	if (parent) {
		c.mParent = parent - &contours[0]; // point to parent
		c.mIsHole = true;
		c.mTreeDepth = parent->mTreeDepth + 1;
		c.mIsLeaf = true;
		c.mIndex = contours.size();
		parent->mChild.push_back(contours.size()); // have parent point to where we will be
		contours.push_back(c);
	}
}

// Synthesis
void PinballWorld::setupSynthesis()
{
	// Register file-watchers for all the major pd patch components
	auto app = PaperBounce3App::get();

	mPureDataNode = app->mPd;

	std::vector<fs::path> paths =
	{
		app->hotloadableAssetPath("synths/pinball-world.pd")
	};

	// Load pinball synthesis patch
	if (kDoSound) mFileWatch.load( paths, [this,app]()
	{
		// Reload the root patch
		auto rootPatch = app->hotloadableAssetPath("synths/pinball-world.pd");
		mPureDataNode->closePatch(mPatch);
		mPatch = mPureDataNode->loadPatch( DataSourcePath::create(rootPatch) ).get();
	});
}

void PinballWorld::shutdownSynthesis() {
	// Close pinball synthesis patch
	mPureDataNode->closePatch(mPatch);
}
