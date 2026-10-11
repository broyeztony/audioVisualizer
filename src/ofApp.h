#pragma once

#include "ofMain.h"
#include <mutex>
#include <atomic>

// Audio is the subject; the camera is only a palette.
//
// The image is built from the signal itself - a scrolling spectrogram, an
// instantaneous waveform trace, a spectrum profile - drawn on black. The
// camera never appears: it is sampled per frequency bin purely to colour those
// forms, so the palette drifts with whatever is in front of the lens while the
// shapes stay strictly audio-driven.

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
	void dragEvent(ofDragInfo dragInfo);
	void audioIn(ofSoundBuffer& input);

	void setSelfTest(const string& outPath, const string& sourceImage);
	void setParam(const string& name, float value);

private:
	// --- analysis ---
	static const int kFftSize = 2048;   // power of two
	static const int kBins    = 256;    // display bins, log-mapped from kFftSize/2

	void  analyse();                    // ring buffer -> magnitudes + flux
	void  synthesizeAudio();            // self-test signal, same path as live
	float binFrequencyFraction(int bin) const;

	std::vector<float> magnitudes;      // kBins, 0..1, smoothed
	std::vector<float> prevMagnitudes;  // for spectral flux
	std::vector<float> waveform;        // downsampled trace for the overlay
	float flux = 0.0f;                  // transient measure
	float level = 0.0f;

	// --- audio io ---
	void   buildAudioDeviceList();
	void   startAudio(int listIndex);

	ofSoundStream soundStream;
	std::vector<ofSoundDevice> inputDevices;
	int    currentAudioDevice = -1;
	string audioDeviceName = "(none)";
	int    sampleRate = 48000;
	int    bufferSize = 512;

	std::mutex         audioMutex;
	std::vector<float> ring;
	size_t             ringWrite = 0;
	float              rmsShared = 0.0f;

	// --- camera: palette only, never drawn ---
	void buildVideoDeviceList();
	void startCamera(int listIndex);
	void serviceCamera();
	ofColor paletteColor(int bin);
	void    rebuildPalette();
	std::vector<ofColor> palette;   // kBins, smoothed scene-derived ramp

	ofVideoGrabber cam;
	std::vector<ofVideoDevice> videoDevices;
	int    currentVideoDevice = -1;
	string videoDeviceName = "(none)";
	bool   camReady = false;
	bool   camDelivered = false;
	int    camStarveFrames = 0;
	int    cameraAuthStatus();
	void   requestCameraAccess();
	std::atomic<bool> cameraJustGranted{false};
	bool   cameraRequestPending = false;

	ofImage stillImage;                 // drag-and-drop palette source
	bool    useStill = false;
	float   paletteScan = 0.0f;         // slow vertical drift through the source

	// --- strata (scrolling spectrogram) ---
	ofFbo     strata[2];
	int       strataCur = 0;
	ofTexture columnTex;
	ofPixels  columnPix;
	void      pushColumn();

	// --- glitch ---
	static const int kSlices = 64;
	std::vector<float> sliceOffset;     // px, decays toward zero
	void   updateGlitch();

	// --- tunables ---
	float scrollSpeed   = 3.0f;         // px of history per frame
	float bandHeight    = 0.46f;        // fraction of window height
	float glitchAmount  = 1.0f;         // scales tear displacement
	float fluxThreshold = 0.16f;        // transient sensitivity
	float gain          = 1.0f;
	float autoGain      = 1.0f;
	bool  autoGainOn    = true;
	float smoothing     = 0.55f;        // magnitude smoothing
	float saturation    = 1.0f;
	float waveAlpha     = 0.50f;
	float floorDb       = -62.0f;       // magnitude dynamic range

	// --- overlay ---
	ofTrueTypeFont clockFont;
	ofTrueTypeFont hudFont;
	bool showClock = true;
	bool showHud   = true;
	void drawStrata();
	void drawWaveform();
	void drawProfile();
	void drawFurniture();
	void drawHud();
	void drawClock();

	// --- self test ---
	bool   selfTest = false;
	string selfTestPath;
	string selfTestSource;
	int    selfTestFrames = 0;
	void   finishSelfTest();
};
