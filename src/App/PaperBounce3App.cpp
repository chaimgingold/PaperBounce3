#include "PaperBounce3App.h"

#include "BallWorld.h"
#include "PongWorld.h"
#include "MusicWorld.h"
#include "CalibrateWorld.h"
#include "PinballWorld.h"
#include "TokenWorld.h"
#include "RibbonWorld.h"
#include "AnimWorld.h"
#include "QuadTestWorld.h"

#include "geom.h"
#include "xml.h"
#include "View.h"
#include "ocv.h"

#include <map>
#include <string>
#include <memory>

#include <stdlib.h> // system()

const int  kRequestFrameRate = 60 ; // was 120, don't think it matters/we got there
const vec2 kDefaultWindowSize( 640, 480 );

const string kDocumentsDirectoryName = "PaperES";

void PaperBounce3App::FPS::start()
{
	mLastFrameTime = ci::app::getElapsedSeconds();
}

void PaperBounce3App::FPS::mark()
{
	double now = ci::app::getElapsedSeconds();
	
	mLastFrameLength = now - mLastFrameTime ;
	
	mLastFrameTime = now;
	
	mFPS = 1.f / mLastFrameLength;
}

PaperBounce3App::~PaperBounce3App()
{
	cout << "Shutting down..." << endl;

//	mGameWorld.reset();

	if (mCapture) mCapture->stop();
	
	cipd::PureDataNode::ShutdownGlobal();
}

PolyLine2 PaperBounce3App::getWorldBoundsPoly() const
{
	PolyLine2 p = getPointsAsPoly( mLightLink.getCaptureProfile().mCaptureWorldSpaceCoords, 4 );
	p.setClosed();
	return p;
}

fs::path PaperBounce3App::getDocsPath() const
{
	return getDocumentsDirectory() / kDocumentsDirectoryName ;
}

fs::path PaperBounce3App::getUserLightLinkFilePath() const
{
	return getDocsPath() / "LightLink.xml" ;
}

fs::path PaperBounce3App::getUserSettingsFilePath() const
{
	return getDocsPath() / "settings.xml" ;
}

void PaperBounce3App::setup()
{
	cout << getAppPath() << endl;
	
	// command line args
	string cmdLineArgGameName; // empty means 0th, by default
	
	auto args = getCommandLineArgs();
	
	for( size_t a=0; a<args.size(); ++a )
	{
		if ( args[a]=="-assets" && args.size()>a )
		{
			string overloadedAssetPath = args[a+1];
			if ( fs::exists(overloadedAssetPath) ) {
				mOverloadedAssetPath = overloadedAssetPath;
			}
		}

		if ( args[a]=="-gameworld" && args.size()>a )
		{
			cmdLineArgGameName = args[a+1];
		}
	}
	
	//
	mAppFPS.start();
	mCaptureFPS.start();

	// enumerate hardware
	{
		auto displays = Display::getDisplays() ;
		cout << displays.size() << " Displays" << endl ;
		for ( auto d : displays )
		{
			cout << "\t '" << d->getName() << "' " << d->getBounds() << endl ;
		}

		auto cameras = Capture::getDevices() ;
		cout << cameras.size() << " Cameras" << endl ;
		for ( auto c : cameras )
		{
			cout << "\t '" << c->getName() << "' '" << c->getUniqueId() << "'" << endl ;
		}
	}
	
	// make docs folder if needed
	if ( !fs::exists(getDocsPath()) ) fs::create_directory(getDocsPath());
	
	// configuration
	mFileWatch.loadXml( hotloadableAssetPath("config") / "app.xml", [this]( XmlTree xml )
	{
		// 1. get params
		if (xml.hasChild("PaperBounce3/LightLink") && !fs::exists(getUserLightLinkFilePath()) )
		{
			mLightLink.setParams(xml.getChild("PaperBounce3/LightLink"));
			ensureLightLinkHasLocalDeviceProfiles();
			lightLinkDidChange(); // allow saving, since user settings version doesn't exist yet
			// Note: To simplify logic, we could just have this save the data to getUserLightLinkFilePath()
			// and let the code below load it. (If somehow it was necessary to be so robust that we can't save
			// user data it might be useful, but that's a questionable scenario.)
		}
		
		if (xml.hasChild("PaperBounce3/App"))
		{
			XmlTree app = xml.getChild("PaperBounce3/App");
			
			getXml(app,"AutoFullScreenProjector",mAutoFullScreenProjector);
			getXml(app,"DrawCameraImage",mDrawCameraImage);
			getXml(app,"DrawContours",mDrawContours);
			getXml(app,"DrawContoursFilled",mDrawContoursFilled);
			getXml(app,"DrawMouseDebugInfo",mDrawMouseDebugInfo);
			getXml(app,"DrawPolyBoundingRect",mDrawPolyBoundingRect);
			getXml(app,"DrawContourTree",mDrawContourTree);
			getXml(app,"DrawPipeline",mDrawPipeline);
			getXml(app,"DrawContourMousePick",mDrawContourMousePick);
			
			getXml(app,"HasConfigWindow",mHasConfigWindow);
			getXml(app,"ConfigWindowPipelineWidth",mConfigWindowPipelineWidth);
			getXml(app,"ConfigWindowPipelineGutter",mConfigWindowPipelineGutter);
			getXml(app,"ConfigWindowMainImageMargin",mConfigWindowMainImageMargin);
			
			getXml(app,"KeyboardStringTimeout",mKeyboardStringTimeout);
			
			getXml(app,"DefaultPixelsPerWorldUnit",mDefaultPixelsPerWorldUnit);
		}
		
		if (xml.hasChild("PaperBounce3/RFID"))
		{
			XmlTree rfid = xml.getChild("PaperBounce3/RFID");
			
			for( auto item = rfid.begin("tag"); item != rfid.end(); ++item )
			{
				if ( item->hasChild("id") && item->hasChild("value") )
				{
					int		id		= item->getChild("id").getValue<int>();
					string	value	= item->getChild("value").getValue();
					
					mRFIDKeyToValue[id] = value;
				}
			}
		}
		
		// 2. respond
		updateDebugFrameCaptureDevicesWithPxPerWorldUnit(mDefaultPixelsPerWorldUnit);
		
		// TODO:
		// - get a new camera capture object so that resolution can change live (I think I do that now)
		// - respond to fullscreen projector flag
		// - aux display config
	});

	// settings: LightLink
	mFileWatch.loadXml( getUserLightLinkFilePath(), [this]( XmlTree xml )
	{
		// this overloads the one in config.xml
		if ( xml.hasChild("LightLink") )
		{
			mLightLink.setParams(xml.getChild("LightLink"));
			bool didChange = ensureLightLinkHasLocalDeviceProfiles();
			lightLinkDidChange(didChange);
			// don't save, since we are responding to a load, unless ensureLightLinkHasLocalDeviceProfiles() changed.
		}
	});

	// ui stuff (do before making windows)
	mTextureFont = gl::TextureFont::create( Font("Avenir",12) );
	
	// setup main window
	mMainWindow = getWindow();
	mMainWindow->setTitle("Projector");
	mMainWindow->setUserData( new WindowData(mMainWindow,false,*this) );

	// resize window
	setWindowSize( mLightLink.getCaptureProfile().mCaptureSize.x, mLightLink.getCaptureProfile().mCaptureSize.y ) ;

	// Fullscreen main window in secondary display
	auto displays = Display::getDisplays() ;

	if ( displays.size()>1 && mAutoFullScreenProjector )
	{
		// move to 2nd display
		mMainWindow->setPos( displays[1]->getBounds().getUL() );
		
		mMainWindow->setFullScreen(true) ;
	}

	mMainWindow->getSignalMove()  .connect( [&]{ this->saveUserSettings(); });
	mMainWindow->getSignalResize().connect( [&]{ this->saveUserSettings(); });
	

	// aux UI display
	if (mHasConfigWindow)
	{
		// note: would be nice to have this value hot-load, but not sure how to sequence
		// light link loading with that, and this is good enough for now.
		
		// for some reason this seems to sometimes create three windows once we fullscreen the main window :P
		// and then one of those is blank. That is the reason (besides kiosk mode) for the mHasConfigWindow switch,
		// as a workaround for that bug.
		mUIWindow = createWindow( Window::Format().size(
			mLightLink.getCaptureProfile().mCaptureSize.x,
			mLightLink.getCaptureProfile().mCaptureSize.y ) );
		
		mUIWindow->setTitle("Configuration");
		mUIWindow->setUserData( new WindowData(mUIWindow,true,*this) );
		
		// move it out of the way on one screen
		if ( mMainWindow->getDisplay() == mUIWindow->getDisplay() )
		{
			mUIWindow->setPos( mMainWindow->getPos() + ivec2( -mMainWindow->getWidth()-16,0) ) ;
		}
		
		// save changes (do this AFTER we set it up, so we don't save initial conditions!)
		mUIWindow->getSignalMove()  .connect( [&]{ this->saveUserSettings(); });
		mUIWindow->getSignalResize().connect( [&]{ this->saveUserSettings(); });
	}
	
	// load the games, rfid functions
	setupGameLibrary();
	setupRFIDValueToFunction();

	// settings (generic)
	mFileWatch.loadXml( getUserSettingsFilePath(), [this]( XmlTree xml )
	{
		// this overloads the one in config.xml
		if ( xml.hasChild("settings") )
		{
			loadUserSettingsFromXml(xml.getChild("settings"));
		}
	});
	
	// load default game (or command line arg)
	if ( !mGameWorld || !cmdLineArgGameName.empty() ) { // because loadUserSettingsFromXml might have done it
		loadDefaultGame(cmdLineArgGameName);
	}
}

void PaperBounce3App::setupGameLibrary()
{
	mGameLibrary.push_back( make_shared<BallWorldCartridge>() );
	mGameLibrary.push_back( make_shared<PinballWorldCartridge>() );
	mGameLibrary.push_back( make_shared<PongWorldCartridge>() );
	mGameLibrary.push_back( make_shared<TokenWorldCartridge>() );
	mGameLibrary.push_back( make_shared<CalibrateWorldCartridge>() );
	mGameLibrary.push_back( make_shared<QuadTestWorldCartridge>() );
	mGameLibrary.push_back( make_shared<MusicWorldCartridge>() );
	mGameLibrary.push_back( make_shared<RibbonWorldCartridge>() );
	mGameLibrary.push_back( make_shared<AnimWorldCartridge>() );
}

void PaperBounce3App::setupRFIDValueToFunction()
{
	mRFIDValueToFunction.clear();
	
	// game loader functions
	for( auto i : mGameLibrary )
	{
		string name = i->getSystemName();
		
		mRFIDValueToFunction[ string("cartridge/") + name ] = [this,name]()
		{
			int cartNum = findCartridgeByName(name);
			
			if (cartNum==-1) cout << "Failed to find cartridge '" << name << "'" << endl;
			else if (cartNum!=mGameWorldCartridgeIndex) this->loadGame(cartNum);
		};
	}
}

bool PaperBounce3App::ensureLightLinkHasLocalDeviceProfiles()
{
	bool dirty=false;
	
	// 1. Cameras

	// Make sure all cameras on this computer have capture device profiles
	if ( Capture::getDevices().empty() ) {
		cout << "No camera devices!" << endl;
	}
	else
	{
		auto devices = Capture::getDevices();
		
		for( const auto &d : devices )
		{
			if ( mLightLink.getCaptureProfilesForDevice(d->getName()).empty() )
			{
				LightLink::CaptureProfile profile(
					string("Default ") + d->getName(),
					d->getName(),
					vec2(640,480),
					mDefaultPixelsPerWorldUnit
					);
				
				mLightLink.mCaptureProfiles[profile.mName] = profile;
				dirty=true;
			} // make profile for device?
		} // for
	}
	
	// 2. Projectors
	if ( mLightLink.mProjectorProfiles.empty() )
	{
		vec2 size(640,480);
		const vec2 *coords=0;
		
		// base it off the capture profile
		if ( !mLightLink.mCaptureProfiles.empty() )
		{
			mLightLink.ensureActiveProfilesAreValid(); // just in case we made it
			const LightLink::CaptureProfile &ref = mLightLink.getCaptureProfile();
			
			size = ref.mCaptureSize;
			coords = ref.mCaptureWorldSpaceCoords;
		}
		
		// make
		LightLink::ProjectorProfile p( "Default", size, coords );
		
		// insert
		mLightLink.mProjectorProfiles[p.mName] = p;
		dirty=true;
	}
	
	// what if the profile list was empty, and we added some profiles that will be used?
	// so: if there is no active profile, then set it to one that we made.
	if (mLightLink.ensureActiveProfilesAreValid()) dirty=true;
	
	return false;
}

void PaperBounce3App::lightLinkDidChange( bool saveToFile )
{
	// start-up (and maybe choose) a valid capture device
	if ( !setupCaptureDevice() )
	{
		tryToSetupValidCaptureDevice();
	}

	// notify people
	// might need to privatize mLightLink and make this a proper setter
	// or rename it to be "notify" or "onChange" or "didChange" something
	mVision.setCaptureProfile(mLightLink.getCaptureProfile());
	if (mGameWorld) mGameWorld->setWorldBoundsPoly( getWorldBoundsPoly() );
	
	if (saveToFile)
	{
		XmlTree lightLinkXml = mLightLink.getParams();
		lightLinkXml.write( writeFile(getUserLightLinkFilePath()) );
	}
}

bool PaperBounce3App::tryToSetupValidCaptureDevice()
{
	// try other profiles...
	for ( auto i : mLightLink.mCaptureProfiles )
	{
		mLightLink.setCaptureProfile(i.second.mName);
		if ( setupCaptureDevice() ) return true;
	}
	
	// fail
	return false;
}

bool PaperBounce3App::setupCaptureDevice()
{
	mDebugFrame.reset();
	
	if ( mLightLink.mCaptureProfiles.empty() ) {
		return false;
	}

	const LightLink::CaptureProfile &profile = mLightLink.getCaptureProfile();
	
	if (!profile.mFilePath.empty())
	{
		cout << "Trying to load capture profile '" << profile.mName << "' for file '" << profile.mFilePath << "'" << endl;
		
		mDebugFrameFileWatch = FileWatch();
		
		mDebugFrameFileWatch.load( profile.mFilePath, [&]( fs::path )
		{
			try
			{
				mDebugFrame = make_shared<Surface>( loadImage(profile.mFilePath) );
			} catch (...){
				mDebugFrame.reset();
			}
		});
		
		return mDebugFrame != nullptr;
		// ideally we should erase it if it fails to load, but that complicates situations where
		// caller has an iterator into capture profiles (eg setupNextValidCaptureProfile). this isn't
		// that hard, but might not be that important.
		// we also might want to always return true so that user can escape missing files (removing from list)
	}
	else
	{
		cout << "Trying to load capture profile '" << profile.mName << "' for '" << profile.mDeviceName << "'" << endl;
		
		Capture::DeviceRef device = Capture::findDeviceByNameContains(profile.mDeviceName);

		if ( device )
		{
			if ( !mCapture || mCapture->getDevice() != device
				|| mCapture->getWidth () != (int32_t)profile.mCaptureSize.x
				|| mCapture->getHeight() != (int32_t)profile.mCaptureSize.y
				)
			{
				if (mCapture) {
					// kill old one
					mCapture->stop();
					mCapture = 0;
				}
				
				mCapture = Capture::create(profile.mCaptureSize.x, profile.mCaptureSize.y,device);
				mCapture->start();
				return true;
			}
			return true; // lazily true
		}
		else return false;
	}
}

void PaperBounce3App::setupNextValidCaptureProfile()
{
	int n=0;
	
	while ( n++ < mLightLink.mCaptureProfiles.size() )
	{
		auto i = mLightLink.mCaptureProfiles.find( mLightLink.getCaptureProfile().mName );
		
		i = ++i;
		
		if ( i==mLightLink.mCaptureProfiles.end() )
		{
			// wrap
			i = mLightLink.mCaptureProfiles.begin();
		}
		
		// set
		mLightLink.setCaptureProfile(i->second.mName);
		
		if ( setupCaptureDevice() ) {
			lightLinkDidChange();
			return; // success
		}
		// else: keep searching...
	}
	
	// mega-fail
}

void PaperBounce3App::loadDefaultGame( string byName )
{
	if ( !mGameLibrary.empty() )
	{
		int index=0;
		
		if (!byName.empty())
		{
			int i = findCartridgeByName(byName);
			if (i!=-1) index=i;
		}
		
		loadGame(index);
	}
}

void PaperBounce3App::loadGame( int libraryIndex )
{
	assert( libraryIndex >=0 && libraryIndex < mGameLibrary.size() );
	
	mGameWorldCartridgeIndex = libraryIndex ;
	
	mGameWorld = mGameLibrary[libraryIndex]->load();
	
	if ( mGameWorld )
	{
		cout << "loadGame: " << mGameWorld->getSystemName() << endl;
		
		// get config xml
		fs::path xmlConfigPath = getXmlConfigPathForGame(mGameWorld->getSystemName()) ;
		
		mFileWatch.loadXml( xmlConfigPath, [xmlConfigPath,this]( XmlTree xml )
		{
			// if we had already loaded this once, we stomp that old lambda and force a reload now.
			
			// why conditional?
			// make sure we aren't hotloading xml from game we aren't running anymore
			if ( xmlConfigPath == getXmlConfigPathForGame(mGameWorld->getSystemName()) )
			{
				// set params from xml
				setGameWorldXmlParams(xml);
			}
		});
		
		// set world bounds
		mGameWorld->setWorldBoundsPoly( getWorldBoundsPoly() );
		
		// notify
		mGameWorld->gameWillLoad();
	}
	
	// save settings
	saveUserSettings();
}

void PaperBounce3App::loadAdjacentGame( int libraryIndexDelta )
{
	if ( !mGameLibrary.empty() )
	{
		int i = mGameWorldCartridgeIndex + libraryIndexDelta;
		
		if ( i >= (int)mGameLibrary.size() ) i = i % (int)mGameLibrary.size();
		if ( i < 0 ) i = (int)mGameLibrary.size() - ((-i) % (int)mGameLibrary.size()) ;
		
		loadGame(i);
	}
}

int PaperBounce3App::findCartridgeByName( string name )
{
	for( size_t i=0; i<mGameLibrary.size(); ++i )
	{
		if ( mGameLibrary[i]->getSystemName() == name ) return i;
	}
	return -1;
}

fs::path
PaperBounce3App::getXmlConfigPathForGame( string name )
{
	return hotloadableAssetPath("config") / (name + ".xml") ;
}

void PaperBounce3App::setGameWorldXmlParams( XmlTree xml )
{
	if ( mGameWorld )
	{
		string name = mGameWorld->getSystemName();
		
		if ( xml.hasChild(name) )
		{
			XmlTree gameParams = xml.getChild(name);
			
			// load game specific params
			mGameWorld->setParams( gameParams );
			
			// load Vision params for it
			if ( gameParams.hasChild("Vision") )
			{
				Vision::Params p;
				p.set( gameParams.getChild("Vision") );
				mGameWorld->setVisionParams(p);
				mVision.setParams( mGameWorld->getVisionParams() );
			}
		}
	}
}

fs::path
PaperBounce3App::hotloadableAssetPath( fs::path p ) const
{
	if ( mOverloadedAssetPath.empty() ) return getAssetPath(p);
	else return fs::path(mOverloadedAssetPath) / p ;
}

void PaperBounce3App::mouseDown( MouseEvent event )
{
	if (getWindowData()) getWindowData()->mouseDown(event);
}

void PaperBounce3App::mouseUp( MouseEvent event )
{
	if (getWindowData()) getWindowData()->mouseUp(event);
}

void PaperBounce3App::mouseMove( MouseEvent event )
{
	if (getWindowData()) getWindowData()->mouseMove(event);
}

void PaperBounce3App::mouseDrag( MouseEvent event )
{
	if (getWindowData()) getWindowData()->mouseDrag(event);
}

void PaperBounce3App::fileDrop( FileDropEvent event )
{
	auto files = event.getFiles();
	if (files.size() > 0) {
		auto file = files.front();
		
		LightLink::CaptureProfile* profile = mLightLink.getCaptureProfileForFile(file.string());
		
		if ( profile )
		{
			mLightLink.setCaptureProfile(profile->mName);
			lightLinkDidChange();
		}
		else
		{
			auto surf = make_shared<Surface>( loadImage(file) );
			// let's verify it's cool, and get the size, but for consistency
			// we'll let lightLinkDidChange load the image.
			
			if (surf)
			{
				LightLink::CaptureProfile cp(file,surf->getSize(),mDefaultPixelsPerWorldUnit); 
				mLightLink.mCaptureProfiles[cp.mName] = cp;
				mLightLink.setCaptureProfile(cp.mName);
				lightLinkDidChange();
			}
		}
	}
}

void PaperBounce3App::updateDebugFrameCaptureDevicesWithPxPerWorldUnit( float x )
{
	bool dirty=false;
	
	for( auto &c : mLightLink.mCaptureProfiles )
	{
		if ( !c.second.mFilePath.empty() )
		{
			dirty=true;
			c.second.setWorldCoordsFromCaptureCoords(x);
		}
	}
	
	if (dirty) lightLinkDidChange();
}

void PaperBounce3App::addProjectorPipelineStages()
{
	// set that image
	mPipeline.then( "projector", mLightLink.getProjectorProfile().mProjectorSize ) ;
	
	mPipeline.setImageToWorldTransform(
		getOcvPerspectiveTransform(
			mLightLink.getProjectorProfile().mProjectorCoords,
			mLightLink.getProjectorProfile().mProjectorWorldSpaceCoords ));
}

void PaperBounce3App::update()
{
	mAppFPS.mark();
	
	mFileWatch.update();
	mDebugFrameFileWatch.update();
	
	updateVision();
	
	if (mGameWorld) mGameWorld->update();
}

void PaperBounce3App::updateVision()
{
	if ( (mCapture && mCapture->checkNewFrame()) || (mDebugFrame && (getElapsedFrames()%30==0)) )
	{
		mCaptureFPS.mark();
		
		// start pipeline
		if ( mPipeline.getQuery().empty() ) mPipeline.setQuery("undistorted");
		mPipeline.setCaptureAllStageImages(true);
			// reality is, we now need to capture all because even contour extraction
			// relies on images captured in the pipeline.
			// if we did need such an optimization, we could have everyone report the stages they want to capture,
			// and have a list of stages to capture.
//			    mDrawPipeline
//			|| (!mGameWorld || mGameWorld->getVisionParams().mCaptureAllPipelineStages)
//		);
		mPipeline.start();


		// get image
		Surface frame;
		if (mDebugFrame) {
			frame = *mDebugFrame.get();
		} else {
			frame = *mCapture->getSurface();
		}

//		cout << "frame size: " << frame.getWidth() << ", " << frame.getHeight() << endl;
		
		
		// vision it
		mVisionOutput = mVision.processFrame(frame,mPipeline);
		
		// finish off the pipeline with draw stage
		addProjectorPipelineStages();
		
		// pass contours to ballworld (probably don't need to store here)
		if (mGameWorld)
		{
			mGameWorld->updateVision( mVisionOutput, mPipeline );
		}
		
		// update pipeline visualization
		if (mUIWindow && mUIWindow->getUserData<WindowData>() ) mUIWindow->getUserData<WindowData>()->updatePipelineViews();
		
		// since the pipeline stage we are drawing might have changed... (or come into existence)
		// update the window mapping
		// this is a little weird to put here, but ah well
		updateMainImageTransform(mUIWindow);
		updateMainImageTransform(mMainWindow);
	}
}

void PaperBounce3App::cleanup() {
	if (mGameWorld) mGameWorld->cleanup();
}

void PaperBounce3App::updateMainImageTransform( WindowRef w )
{
	if (w)
	{
		WindowData *win = w->getUserData<WindowData>();
		
		// might not have been created yet (e.g., in setup)
		if (win) win->updateMainImageTransform();
	}
}

void PaperBounce3App::resize()
{
	updateMainImageTransform(getWindow());

	if (getWindow())
	{
		WindowData *win = getWindow()->getUserData<WindowData>();
		
		// might not have been created yet (e.g., in setup)
		if (win) win->resize();
	}
}

void PaperBounce3App::drawWorld( GameWorld::DrawType drawType )
{
	WindowData *win = getWindow()->getUserData<WindowData>();
	
	const vec2 mouseInWindow   = win->getMousePosInWindow();
	const vec2 mouseInWorld    = win->getMainImageView()->windowToWorld(mouseInWindow);
	const bool isMouseInWindow = win->getWindow()->getBounds().contains(win->getMousePosInWindow());
	
	const bool isUIWindow = win->getIsUIWindow();
	
	// draw frame
	if (0)
	{
		gl::color( 1, 1, 1 );
		gl::draw( getWorldBoundsPoly() ) ;
	}

	// draw contour bounding boxes, etc...
	if (mDrawPolyBoundingRect)
	{
		for( auto c : mVisionOutput.mContours )
		{
			if ( !c.mIsHole ) gl::color( 1.f, 1.f, 1.f, .2f ) ;
			else gl::color( 1.f, 0.f, .3f, .3f ) ;

			gl::drawStrokedRect(c.mBoundingRect);
		}
	}

	// draw contours
	if ( mDrawContours || isUIWindow )
	{
		// filled
		if ( mDrawContoursFilled )
		{
			// TODO make fill colors tunable
			ColorA holecolor(.0f,.0f,.0f,.8f);
			ColorA fillcolor(.2f,.2f,.4f,.5f);
			
			// recursive tree
			function<void(const Contour&)> drawOne = [&]( const Contour& c )
			{
				if ( c.mIsHole ) gl::color( holecolor );
				else gl::color( fillcolor );
				
				gl::drawSolid(c.mPolyLine);
				
				for( auto childIndex : c.mChild )
				{
					drawOne( mVisionOutput.mContours[childIndex] ) ;
				}
			};
			
			for( auto const &c : mVisionOutput.mContours )
			{
				if ( c.mTreeDepth==0 )
				{
					drawOne(c) ;
				}
			}
		}
		// outlines
		else
		{
			for( auto c : mVisionOutput.mContours )
			{
				ColorAf color ;
				
				if ( !c.mIsHole ) color = ColorAf(1,1,1);
				else color = ColorAf::hex( 0xF19878 ) ;
				
				gl::color(color) ;
				gl::draw(c.mPolyLine) ;
			}
		}

		if ( mDrawContourMousePick || isUIWindow )
		{
			// picked highlight
			const Contour* picked = mVisionOutput.mContours.findLeafContourContainingPoint( mouseInWorld ) ;
			
			if (picked)
			{
				if (picked->mIsHole) gl::color(6.f,.4f,.2f);
				else gl::color(.2f,.6f,.4f,.8f);
				
				gl::draw(picked->mPolyLine);
				
				if (0)
				{
					vec2 x = closestPointOnPoly(mouseInWorld,picked->mPolyLine);
					gl::color(.5f,.1f,.3f,.9f);
					gl::drawSolidCircle(x, 5.f);
				}
			}
		}
	}
	
	// draw balls
	if (mGameWorld) mGameWorld->draw(drawType);
	
	// mouse debug info
	if ( (mDrawMouseDebugInfo||isUIWindow) && isMouseInWindow && mGameWorld )
	{
		mGameWorld->drawMouseDebugInfo(mouseInWorld);
	}
}

void PaperBounce3App::draw()
{
	// this, unfortunately, still happens 2x more than needed,
	// since it will happen once per window.
	// but not worrying about that minor performance point right now.
	if (mGameWorld) mGameWorld->prepareToDraw();
	
	// draw window
	WindowData* win = getWindow()->getUserData<WindowData>() ;
	
	if (win) win->draw();
}

void PaperBounce3App::keyDown( KeyEvent event )
{
	// string aggregation -- still allow it to cascade to later handlers though
	if ( !event.isMetaDown() && !event.isControlDown() && !event.isAltDown() )
	{
		float now = getElapsedSeconds();
		
		if ( now - mLastKeyEventTime > mKeyboardStringTimeout )
		{
			mKeyboardString.clear();
		}
		
		mKeyboardString += event.getChar();
		mLastKeyEventTime=now;
		
		bool handled = parseKeyboardString(mKeyboardString);
		if (handled) mKeyboardString="";
	}
	
	bool handled = false;
	
	auto openFile = []( fs::path path ) {
		::system( (string("open \"") + path.string() + string("\"")).c_str() );		
	};
	
	// meta chars
	if ( event.isAltDown() )
	{
		bool caught=true;
		
		switch ( event.getCode() )
		{
			case KeyEvent::KEY_x:
				if (mGameWorld) {
					openFile( hotloadableAssetPath( fs::path("config") / "app.xml" ) );
				}
				break;
			
			case KeyEvent::KEY_l:
				openFile( getUserLightLinkFilePath() );
				break;

			case KeyEvent::KEY_TAB:
				setupNextValidCaptureProfile();
				break;
				
			case KeyEvent::KEY_c:
				mDrawContours = !mDrawContours;
				break;

			case KeyEvent::KEY_t:
				mDrawPipeline = !mDrawPipeline;
				break;
				
			default:
				caught=false;
				break;
		}
		
		if (caught) handled=true;
	}
	
	// handle char
	if ( !handled )
	{
		switch ( event.getCode() )
		{
			case KeyEvent::KEY_f:
				cout << "Frame rate: " << getFrameRate() << endl ;
				break ;

			case KeyEvent::KEY_x:
				openFile( getXmlConfigPathForGame( mGameWorld->getSystemName() ) );
				break ;
			
			case KeyEvent::KEY_UP:
				if (event.isMetaDown()) mPipeline.setQuery( mPipeline.getFirstStageName() );
				else mPipeline.setQuery( mPipeline.getPrevStageName(mPipeline.getQuery() ) );
				saveUserSettings();
				break;
				
			case KeyEvent::KEY_DOWN:
				if (event.isMetaDown()) mPipeline.setQuery( mPipeline.getLastStageName() );
				else mPipeline.setQuery( mPipeline.getNextStageName(mPipeline.getQuery() ) );
				saveUserSettings();
				break;
			
			case KeyEvent::KEY_LEFT:
				loadAdjacentGame(-1);
				break;
			
			case KeyEvent::KEY_RIGHT:
				loadAdjacentGame(1);
				break;
			case KeyEvent::KEY_ESCAPE:
				{
					mDebugFrame.reset();
					
					if ( !mLightLink.getCaptureProfile().mFilePath.empty() )
					{
						mLightLink.eraseCaptureProfile(mLightLink.getCaptureProfile().mName);
						setupNextValidCaptureProfile();
					}
				}
				break;
			
			default:
				if (mGameWorld) mGameWorld->keyDown(event);
				break;
		}
	}
}

bool PaperBounce3App::parseKeyboardString( string str )
{
	bool handled = false;
	
	// RFID reader
	// - digits + newline
	
//	cout << "parseKeyboardString( '" << str << "' )" << endl;
	
	auto getAsRFID = []( string str ) -> int
	{
		if ( str.back()!='\n' && str.back()!='\r' ) return 0;
		
		for( int i=0; i<str.length()-2; ++i ) if ( !isdigit(str[i]) ) return 0;
		
		return stoi( str.substr(0,str.length()-1) );
	};
	
	int rfid = getAsRFID(str);
	
	if (rfid)
	{
		// rfid -> value
		cout << "RFID: #" << rfid << endl;
		
		auto value = mRFIDKeyToValue.find(rfid);
		
		if (value == mRFIDKeyToValue.end()) cout << "\tUnmapped RFID key " << rfid << endl;
		else
		{
			cout << "\t" << rfid << " => " << value->second << endl;
			
			// value -> function
			auto func = mRFIDValueToFunction.find(value->second);
			if ( func != mRFIDValueToFunction.end() && func->second )
			{
				// do it
				func->second();
				handled=true;
			}
			else cout << "\tNo function defined" << endl;
		}
	}

	return handled;
}

void PaperBounce3App::loadUserSettingsFromXml( XmlTree xml )
{
	cout << "loadUserSettingsFromXml." << endl;
	
	if ( xml.hasChild("GameWorld") )
	{
		string name = xml.getChild("GameWorld").getValue();
		
		if ( !mGameWorld || mGameWorld->getSystemName() != name )
		{
			int i = findCartridgeByName(name);
			
			if (i!=-1)
			{
				cout << "loadUserSettingsFromXml: going to game world " << name << endl;
				loadGame(i);
			}
			else cout << "loadUserSettingsFromXml: unknown game world " << name << endl;
		}
	}
	
	if ( xml.hasChild("PipelineQuery") )
	{
		string name = xml.getChild("PipelineQuery").getValue();
		mPipeline.setQuery(name);
	}
	
	auto getWindowBounds = [&xml]( string xmlName, WindowRef win )
	{
		if ( xml.hasChild(xmlName) && win )
		{
			try {
				string val = xml.getChild(xmlName).getValue();
				
				Area a;
				if ( sscanf(val.c_str(), "(%d, %d)-(%d, %d)",&a.x1,&a.y1,&a.x2,&a.y2)==4 )
				{
					win->setSize(a.getSize());
					win->setPos (a.getUL());
				}
			} catch (...) {}
		}
	};
	
	getWindowBounds( "UIWindowBounds", mUIWindow );
	getWindowBounds( "MainWindowBounds", mMainWindow );
}

XmlTree PaperBounce3App::getUserSettingsXml() const
{
	XmlTree xml("settings","");
	
	if (mGameWorld)
	{
		xml.push_back( XmlTree("GameWorld",mGameWorld->getSystemName()) );
	}
	
	xml.push_back( XmlTree("PipelineQuery",mPipeline.getQuery()) );
	
	auto setWindowBounds = [&xml]( string xmlName, WindowRef win ) {
		if (win) xml.push_back( XmlTree(xmlName,  toString(win->getBounds()+win->getPos())) );
	};
	
	setWindowBounds("UIWindowBounds",mUIWindow);
	setWindowBounds("MainWindowBounds",mMainWindow);
	
	return xml;
}

void PaperBounce3App::saveUserSettings()
{
	XmlTree t = getUserSettingsXml();
	t.write( writeFile(getUserSettingsFilePath()) );
}

CINDER_APP( PaperBounce3App, RendererGl(RendererGl::Options().msaa(8)), [&]( App::Settings *settings ) {
	settings->setFrameRate(kRequestFrameRate);
	settings->setWindowSize(kDefaultWindowSize);
	settings->setPowerManagementEnabled(true); // contrary to intuition, 'true' means "disable sleep/idle"
	settings->setTitle("See Paper") ;
})