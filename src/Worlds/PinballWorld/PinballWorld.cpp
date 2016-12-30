//
//  PinballWorld.cpp
//  PaperBounce3
//
//  Created by Chaim Gingold on 12/5/16.
//
//

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


PinballWorld::PinballWorld()
{
	setupSynthesis();

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
	mInputToFunction["flippers-left"] = []()
	{
		cout << "flippers-left" << endl;
	};

	mInputToFunction["flippers-right"] = []()
	{
		cout << "flippers-right" << endl;
	};
	
	mGamepadFunctions["flippers-left-down"]  = [this]() { mIsFlipperDown[0] = true; cout << "boo" << endl; };
	mGamepadFunctions["flippers-left-up"]    = [this]() { mIsFlipperDown[0] = false; };
	mGamepadFunctions["flippers-right-down"] = [this]() { mIsFlipperDown[1] = true; };
	mGamepadFunctions["flippers-right-up"]   = [this]() { mIsFlipperDown[1] = false; };
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
	getXml(xml, "Gravity", mGravity );
	getXml(xml, "BallReclaimAreaHeight", mBallReclaimAreaHeight );
	
	getXml(xml, "BumperMinRadius", mBumperMinRadius );
	getXml(xml, "BumperContourRadiusScale", mBumperContourRadiusScale );
	getXml(xml, "BumperKickAccel", mBumperKickAccel );

	getXml(xml, "BumperOuterColor",mBumperOuterColor);
	getXml(xml, "BumperInnerColor",mBumperInnerColor);
	
	getXml(xml, "FlipperMinLength",mFlipperMinLength);
	getXml(xml, "FlipperMaxLength",mFlipperMaxLength);
	getXml(xml, "FlipperRadiusToLengthScale",mFlipperRadiusToLengthScale);
	getXml(xml, "FlipperColor",mFlipperColor);
	
	getXml(xml, "CircleMinVerts", mCircleMinVerts );
	getXml(xml, "CircleMaxVerts", mCircleMaxVerts );
	getXml(xml, "CircleVertsPerPerimCm", mCircleVertsPerPerimCm );

	getXml(xml, "PartTrackLocMaxDist", mPartTrackLocMaxDist );
	getXml(xml, "PartTrackRadiusMaxDist", mPartTrackRadiusMaxDist );
	
	getXml(xml, "DebugDrawGeneratedContours", mDebugDrawGeneratedContours);
	getXml(xml, "DebugDrawAdjSpaceRays", mDebugDrawAdjSpaceRays );
	getXml(xml, "DebugDrawFlipperAccelHairs", mDebugDrawFlipperAccelHairs );
	
	getXml(xml, "3d/Enable", m3dEnable );
	getXml(xml, "3d/BackfaceCull", m3dBackfaceCull );
	getXml(xml, "3d/TableDepth", m3dTableDepth );
	getXml(xml, "3d/ZSkew", m3dZSkew );
	
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

void PinballWorld::update()
{
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
	// for now, from the top
	float fx = Rand::randFloat();
	
	vec2 loc = fromPlayfieldSpace(
		vec2( lerp(mPlayfieldBoundingBox.x1,mPlayfieldBoundingBox.x2,fx), mPlayfieldBoundingBox.y2 )
		);
	
	Ball& ball = newRandomBall(loc);
	
	ball.mCollideWithContours = true;
	ball.mColor = mBallDefaultColor; // no random colors that are set entirely in code, i think. :P
}

void PinballWorld::cullBalls()
{
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
	
	for( const auto &c : getBallContourCollisions() )
	{
//		mPureDataNode->sendBang("hit-object");

		if (0) cout << "ball contour collide (ball=" << c.mBallIndex << ", " << c.mContourIndex << ")" << endl;
		
		// tell part
		PartRef p = findPartForContour(c.mContourIndex);
		if (p) {
			
			Ball* ball=0;
			if (c.mBallIndex>=0 && c.mBallIndex<=getBalls().size())
			{
				ball = &getBalls()[c.mBallIndex];
			}
			
	//		cout << "collide part" << endl;
			if (ball) p->onBallCollide(*ball);
		}
		else {
	//		cout << "collide wall" << endl;
		}
	}
}

void PinballWorld::draw( DrawType drawType )
{
	// playfield markers
	gl::color(1,0,0);
	gl::drawLine(
		fromPlayfieldSpace(vec2(mPlayfieldBallReclaimX[0],mPlayfieldBallReclaimY)),
		fromPlayfieldSpace(vec2(mPlayfieldBallReclaimX[1],mPlayfieldBallReclaimY)) ) ;
	
	// world
	if (m3dEnable) draw3d(drawType);
	else draw2d(drawType);


	// --- debugging/testing ---
	
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
	// balls
	BallWorld::draw(drawType);
	
	// flippers, etc...
	drawParts();
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
			// draw me
			const bool punchOut = !c.mIsHole;
			
			if (punchOut) {
				// if punching out, go in; otherwise we stay at 0 (and fill)
				gl::pushModelView();
				gl::translate(vec3(0,0,m3dTableDepth));				
				if (kDebugVizDepth) gl::color(1,1,1);
			}
			else if (kDebugVizDepth) gl::color(.5,.5,.5);
			
			gl::drawSolid(c.mPolyLine);
			
			if (punchOut) gl::popModelView();
			
			// childers
			for( int childIndex : c.mChild ) {
				recurseTree(mVisionContours[childIndex]);
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

TriMesh PinballWorld::get3dMeshForPoly( const PolyLine2& poly, float znear, float zfar ) const
{
	float extrudeDepth = zfar - znear;
	
	std::function<Colorf(vec3)> posToColor = [&]( vec3 v ) -> Colorf
	{
		return lerp(
			Colorf(0,1,0),
			Colorf(1,0,0),
//			Colorf(1,1,1),
//			Colorf(.5,.5,.5),
			constrain( (v.z-znear)/(zfar-znear), 0.f, 1.f ) );
			// near -> far (from viewer)
	};
	
//	PolyLine2 poly=c.mPolyLine;
//			poly.reverse(); // turn it inside out, so normals face inward
	
	auto src = geom::Extrude( polyToShape(poly), extrudeDepth ).caps(false).subdivisions( 1 );
	auto dst = src >> geom::Translate(0,0,extrudeDepth/2+znear) >> geom::ColorFromAttrib( geom::POSITION, posToColor ) ;
	
	TriMesh mesh(dst);

	return mesh;
}

void PinballWorld::draw3d( DrawType drawType )
{
	beginDraw3d();
	
	#if 0
	auto lambert = gl::ShaderDef().lambert().color();
	auto shader = gl::getStockShader( lambert );
	gl::ScopedGlslProg glslScp(shader);
	#endif
	
	// 3d contours
	for( const auto &c : mVisionContours )
	{
		if ( !shouldContourBeAPart(c) )
		{
			if (0)
			{
//					gl::color(1,0,0);
//					gl::drawSolidCircle( c.mCenter, min(2.f,c.mRadius) );
				gl::color(0,1,0);
//					gl::drawCube( vec3(c.mCenter,0), vec3(1,1,1) * min(2.f,c.mRadius) * 2.f ) ;
				gl::drawSphere( vec3(c.mCenter,0), min(2.f,c.mRadius) * 2.f ) ;

				gl::color(0,1,1);
				gl::drawSphere( vec3(c.mCenter+getRightVec()*3.f,-1), min(2.f,c.mRadius) * 1.5f ) ;
				// +z is away from viewer
			}
			
			gl::draw( get3dMeshForPoly(c.mPolyLine,0.f,m3dTableDepth) );
		}
	}

	// 3d part sides
	for( const auto &p : mParts )
	{
		gl::draw( get3dMeshForPoly(p->getCollisionPoly(),0.f,m3dTableDepth) );
	}

	// done with 3d
	endDraw3d();

	// 2d stuff at table level (slap it on top)
	drawParts();
	BallWorld::draw(drawType);
}

void PinballWorld::drawAdjSpaceRays() const
{
	for( auto &p : mParts )
	{
		const AdjSpace space = getAdjacentSpace(p->mLoc,mVisionContours);
		
		gl::color(1,0,0);
		gl::drawLine(
			p->mLoc + getRightVec() *  space.mWidthRight,
			p->mLoc + getRightVec() * (space.mWidthRight + space.mRight) );

		gl::color(0,1,0);
		gl::drawLine(
			p->mLoc + getLeftVec()  *  space.mWidthLeft,
			p->mLoc + getLeftVec()  * (space.mWidthLeft + space.mLeft) );

		gl::color(0,0,1);
		gl::drawLine(
			p->mLoc + getLeftVec ()  * space.mWidthLeft,
			p->mLoc + getRightVec()  * space.mWidthRight );
	}
}

void PinballWorld::worldBoundsPolyDidChange()
{
}

void PinballWorld::keyDown( KeyEvent event )
{
	char c = event.getChar();
	
	auto i = mKeyToInput.find(c);
	
	if (i!=mKeyToInput.end())
	{
		auto j = mInputToFunction.find(i->second);
		if (j!=mInputToFunction.end())
		{
			j->second();
		}
	}
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
		if (c.mTreeDepth==0)
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
	
	// tell ball world
	BallWorld::setContours(physicsContours);
}

void PinballWorld::updateVision( const Vision::Output& visionOut, Pipeline& p )
{
	// capture contours, so we can later pass them into BallWorld (after merging in part shapes)
	mVisionContours = visionOut.mContours;
	
	// playfield layout
	updatePlayfieldLayout(visionOut.mContours);
	
	// generate parts
	PartVec newParts = getPartsFromContours(visionOut.mContours);
	mParts = mergeOldAndNewParts(mParts, newParts);
}

bool PinballWorld::shouldContourBeAPart( const Contour& c ) const
{
	return c.mTreeDepth>0 && c.mIsHole && c.mRadius < mPartMaxContourRadius;
}

AdjSpace
PinballWorld::getAdjacentSpace( vec2 loc, const ContourVector& cs ) const
{
	AdjSpace result;

	const Contour* leaf = cs.findLeafContourContainingPoint(loc);
	
	if (leaf)
	{
		float far = leaf->mRadius * 2.f;
		float t;
		
		if ( leaf->rayIntersection(loc + getLeftVec() * far, getRightVec(), &t) ) {
			result.mWidthLeft = far - t;
		}

		if ( leaf->rayIntersection(loc + getRightVec() * far, getLeftVec(), &t) ) {
			result.mWidthRight = far - t;
		}
	}
	
	auto filter = [&]( const Contour& c ) -> bool
	{
		// 1. not self
		//if ( c.mIsHole && c.contains(loc) ) return false;
		if ( &c == leaf ) return false; // supposed to be optimized version of c.mIsHole && c.contains(loc)
		// 2. not other parts
		else if ( shouldContourBeAPart(c) ) return false; // could be a part
		// OK
		else return true;
	};
	
	cs.rayIntersection( loc, getRightVec(), &result.mRight, filter );
	cs.rayIntersection( loc, getLeftVec (), &result.mLeft , filter );
	
	// bake in contour width into adjacent space calc
	result.mLeft  -= result.mWidthLeft;
	result.mRight -= result.mWidthRight;
	
	return result;
}

PartVec PinballWorld::getPartsFromContours( const ContourVector& contours )
{
	PartVec parts;
	
	for( const auto &c : contours )
	{
		if ( shouldContourBeAPart(c) )
		{
			// flipper orientation
			AdjSpace adjSpace = getAdjacentSpace(c.mCenter,contours);

			auto add = [&c,&parts,adjSpace]( Part* p )
			{
				p->mContourLoc = c.mCenter;
				p->mContourRadius = c.mRadius;
				parts.push_back( PartRef(p) );
			};
			
			if      (adjSpace.mRight < adjSpace.mLeft  && adjSpace.mRight < mFlipperDistToEdge)
			{
				add( new Flipper(*this, c.mCenter, c.mRadius, PartType::FlipperRight) );
			}
			else if (adjSpace.mLeft  < adjSpace.mRight && adjSpace.mLeft  < mFlipperDistToEdge)
			{
				
				add( new Flipper(*this, c.mCenter, c.mRadius, PartType::FlipperLeft) );
			}
//			else if (adjSpace.mLeft  < 1.f) // < epsilon
//			{
				// zero!
				// plunger?
				// something else?
//				p.mType = PartType::FlipperLeft;
//			}
			else
			{
				
				add( new Bumper( *this, c.mCenter, c.mRadius, adjSpace ) );
				
				// equal, and there really is space, so just pick something.
//				p.mType = PartType::FlipperLeft;
				
				// we could have a fuzzier sense of equal (left > right + kDelta),
				// which would allow us to have something different at the center of a space (where distance is equal-ish)
				// (like a bumper!)
				// or even a rule that said left is only when a left edge is within x space at left (eg), so very tight proximity rules.
			}
		}
	}
	
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
	
	for( PartRef &p : parts )
	{
		// does it match an old part?
		for( const PartRef& old : oldParts )
		{
			if ( old->mType == p->mType &&
				 distance( old->mContourLoc, p->mContourLoc ) < mPartTrackLocMaxDist &&
				 fabs( old->mContourRadius - p->mContourRadius ) < mPartTrackRadiusMaxDist
				)
			{
				// matched.
				bool replace=true;
				
				// but...
				// replace=false
				
				// replace with old.
				// (we are simply shifting pointers rather than copying contents, but i think this is fine)
				if (replace) p = old;
			}
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
		parent->mChild.push_back(contours.size()); // have parent point to where we will be
		contours.push_back(c);
	}
}

// Synthesis
void PinballWorld::setupSynthesis()
{
	mPureDataNode = cipd::PureDataNode::Global();
	
	// Load pong synthesis patch
	mPatch = mPureDataNode->loadPatch( DataSourcePath::create(getAssetPath("synths/pong.pd")) );
}

void PinballWorld::shutdownSynthesis() {
	// Close pong synthesis patch
	mPureDataNode->closePatch(mPatch);
}