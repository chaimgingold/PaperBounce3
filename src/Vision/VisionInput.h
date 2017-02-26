//
//  VisionInput.hpp
//  Tabla
//
//  Created by Chaim Gingold on 2/17/17.
//
//

#ifndef VisionInput_hpp
#define VisionInput_hpp

#include "cinder/Capture.h"

#include "LightLink.h"
#include "FileWatch.h"

class VisionInput
{
public:
	bool setup( const LightLink::CaptureProfile& );
	void stop();
	
	SurfaceRef getFrame();
	bool isFile() const { return mDebugFrame.get(); }
	
private:
	bool setupWithCamera( const LightLink::CaptureProfile& );
	bool setupWithFile  ( const LightLink::CaptureProfile& );

	// camera
	CaptureRef mCapture;

	// file (debug frame)
	SurfaceRef mDebugFrame;
	FileWatch  mDebugFrameFileWatch;
};

#endif /* VisionInput_hpp */