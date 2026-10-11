#include "ofApp.h"

#import <AVFoundation/AVFoundation.h>

namespace {
constexpr size_t kRingSamples = 1 << 15;

const char* kFontCandidates[] = {
	"/System/Library/Fonts/Supplemental/Arial.ttf",
	"/System/Library/Fonts/Supplemental/Helvetica.ttf",
	"/System/Library/Fonts/SFNS.ttf",
};

// In-place iterative Cooley-Tukey. Written out rather than pulled from an
// addon so the project stays dependency-free: ofxFft is not in core.
void fftRadix2(std::vector<float>& re, std::vector<float>& im) {
	const size_t n = re.size();
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const float ang = -2.0f * PI / (float)len;
		const float wr = cosf(ang), wi = sinf(ang);
		for (size_t i = 0; i < n; i += len) {
			float cr = 1.0f, ci = 0.0f;
			for (size_t k = 0; k < len / 2; k++) {
				const float ur = re[i + k],           ui = im[i + k];
				const float xr = re[i + k + len / 2], xi = im[i + k + len / 2];
				const float vr = xr * cr - xi * ci;
				const float vi = xr * ci + xi * cr;
				re[i + k] = ur + vr;  im[i + k] = ui + vi;
				re[i + k + len / 2] = ur - vr;  im[i + k + len / 2] = ui - vi;
				const float ncr = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = ncr;
			}
		}
	}
}
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetVerticalSync(true);
	ofSetFrameRate(60);
	ofBackground(0);
	ofSetEscapeQuitsApp(false);
	ofDisableArbTex();
	ofEnableAlphaBlending();

	ring.assign(kRingSamples, 0.0f);
	magnitudes.assign(kBins, 0.0f);
	prevMagnitudes.assign(kBins, 0.0f);
	waveform.assign(1024, 0.0f);
	sliceOffset.assign(kSlices, 0.0f);

	// One column of the spectrogram, written each frame and blitted into the
	// history buffer. Low frequencies at the bottom.
	columnPix.allocate(1, kBins, OF_PIXELS_RGB);
	columnPix.setColor(ofColor(0));
	columnTex.allocate(columnPix);
	columnTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);

	ofFbo::Settings fs;
	fs.width = 1280; fs.height = kBins;
	fs.internalformat = GL_RGBA;
	fs.useDepth = false;
	fs.minFilter = GL_LINEAR; fs.maxFilter = GL_LINEAR;
	for (int i = 0; i < 2; i++) {
		strata[i].allocate(fs);
		strata[i].begin(); ofClear(0, 0, 0, 255); strata[i].end();
	}

	for (const char* path : kFontCandidates) {
		if (ofFile::doesFileExist(path)) {
			clockFont.load(path, 40, true, true);
			hudFont.load(path, 13, true, true);
			if (clockFont.isLoaded()) break;
		}
	}

	if (selfTest) {
		if (!selfTestSource.empty() && selfTestSource != "camera"
		    && stillImage.load(selfTestSource)) {
			useStill = true;
		} else if (selfTestSource == "camera") {
			buildVideoDeviceList();
			if (cameraAuthStatus() == 0) startCamera(0);
		}
		showHud = false;
		return;
	}

	buildVideoDeviceList();
	switch (cameraAuthStatus()) {
		case 0: startCamera(0); break;
		case 2: requestCameraAccess(); videoDeviceName = "(awaiting permission)"; break;
		default:
			videoDeviceName = "(denied - palette falls back to a built-in ramp)";
			break;
	}

	buildAudioDeviceList();
	startAudio(0);
}

void ofApp::setSelfTest(const string& outPath, const string& sourceImage) {
	selfTest = true;  selfTestPath = outPath;  selfTestSource = sourceImage;
}

void ofApp::setParam(const string& name, float v) {
	if      (name == "scroll")     scrollSpeed   = v;
	else if (name == "band")       bandHeight    = v;
	else if (name == "glitch")     glitchAmount  = v;
	else if (name == "flux")       fluxThreshold = v;
	else if (name == "gain")       gain          = v;
	else if (name == "smoothing")  smoothing     = v;
	else if (name == "saturation") saturation    = v;
	else if (name == "wavealpha")  waveAlpha     = v;
	else if (name == "floordb")    floorDb       = v;
	else ofLogWarning("ofApp") << "unknown parameter: " << name;
}

// --- audio -----------------------------------------------------------------

void ofApp::buildAudioDeviceList() {
	inputDevices.clear();
	for (auto& d : soundStream.getDeviceList()) {
		if (d.inputChannels > 0) inputDevices.push_back(d);
	}
	for (size_t i = 0; i < inputDevices.size(); i++) {
		ofLogNotice("ofApp") << "  [" << i << "] " << inputDevices[i].name
		                     << "  in=" << inputDevices[i].inputChannels;
	}
}

void ofApp::startAudio(int listIndex) {
	if (inputDevices.empty()) { audioDeviceName = "(no input devices)"; return; }
	listIndex = ofClamp(listIndex, 0, (int)inputDevices.size() - 1);
	soundStream.close();

	ofSoundStreamSettings settings;
	auto& dev = inputDevices[listIndex];
	settings.setInDevice(dev);
	settings.setInListener(this);
	settings.sampleRate        = sampleRate;
	settings.numInputChannels  = std::min(2, (int)dev.inputChannels);
	settings.numOutputChannels = 0;
	settings.bufferSize        = bufferSize;

	if (soundStream.setup(settings)) {
		currentAudioDevice = listIndex;
		audioDeviceName    = dev.name;
		ofLogNotice("ofApp") << "listening on [" << listIndex << "] " << dev.name;
	} else {
		audioDeviceName = "(failed: " + dev.name + ")";
	}
}

void ofApp::audioIn(ofSoundBuffer& input) {
	const size_t frames   = input.getNumFrames();
	const size_t channels = std::max<size_t>(1, input.getNumChannels());

	std::lock_guard<std::mutex> lock(audioMutex);
	double sumSquares = 0.0;
	for (size_t f = 0; f < frames; f++) {
		float mono = 0.0f;
		for (size_t c = 0; c < channels; c++) mono += input[f * channels + c];
		mono /= (float)channels;
		ring[ringWrite] = mono;
		ringWrite = (ringWrite + 1) % ring.size();
		sumSquares += (double)mono * mono;
	}
	rmsShared = frames ? (float)std::sqrt(sumSquares / (double)frames) : 0.0f;
}

// Self-test signal: a moving harmonic stack over noise, written into the same
// ring buffer the device fills, so the analysis and drawing paths under test
// are exactly the live ones.
void ofApp::synthesizeAudio() {
	static float phase = 0.0f;
	phase += 0.013f;
	std::lock_guard<std::mutex> lock(audioMutex);
	double sumSquares = 0.0;
	for (int i = 0; i < 800; i++) {
		float t = (float)ringWrite / (float)sampleRate;
		float base = 110.0f * (1.0f + 0.5f * sinf(phase * 0.7f));
		float v = 0.0f;
		for (int h = 1; h <= 7; h++) {
			v += (0.55f / h) * sinf(TWO_PI * base * h * t + h * 0.4f);
		}
		v += 0.10f * ofSignedNoise(t * 900.0f);
		v *= 0.55f + 0.45f * sinf(phase * 2.3f);
		ring[ringWrite] = ofClamp(v, -1.0f, 1.0f);
		ringWrite = (ringWrite + 1) % ring.size();
		sumSquares += (double)v * v;
	}
	rmsShared = (float)std::sqrt(sumSquares / 800.0);
}

// Logarithmic bin mapping: linear FFT bins put almost everything musical in
// the leftmost sliver, which reads as a flat wall rather than structure.
float ofApp::binFrequencyFraction(int bin) const {
	const float t = (float)bin / (float)(kBins - 1);
	const float minF = 30.0f, maxF = 16000.0f;
	const float f = minF * powf(maxF / minF, t);
	return f / ((float)sampleRate * 0.5f);
}

void ofApp::analyse() {
	std::vector<float> re(kFftSize), im(kFftSize, 0.0f);
	float rms;
	{
		std::lock_guard<std::mutex> lock(audioMutex);
		size_t start = (ringWrite + ring.size() - kFftSize) % ring.size();
		for (int i = 0; i < kFftSize; i++) {
			// Hann window: without it every block boundary rings and the
			// spectrum smears into a solid band.
			const float w = 0.5f * (1.0f - cosf(TWO_PI * i / (kFftSize - 1)));
			re[i] = ring[(start + i) % ring.size()] * w;
		}
		rms = rmsShared;
	}

	level = ofLerp(level, rms, 0.25f);
	if (autoGainOn) {
		float target = (level > 0.0008f) ? ofClamp(0.20f / level, 0.5f, 18.0f) : 1.0f;
		autoGain = ofLerp(autoGain, target, 0.05f);
	} else {
		autoGain = 1.0f;
	}

	for (int i = 0; i < kFftSize; i++) re[i] *= gain * autoGain;
	fftRadix2(re, im);

	const int half = kFftSize / 2;
	float newFlux = 0.0f;
	for (int b = 0; b < kBins; b++) {
		// Average the FFT bins falling inside this display bin, so the high
		// end - where one display bin spans hundreds of FFT bins - does not
		// alias into sparkle.
		const float f0 = binFrequencyFraction(b);
		const float f1 = binFrequencyFraction(std::min(b + 1, kBins - 1));
		int i0 = ofClamp((int)(f0 * half), 0, half - 1);
		int i1 = ofClamp((int)(f1 * half), i0 + 1, half);

		float sum = 0.0f;
		for (int i = i0; i < i1; i++) {
			sum += sqrtf(re[i] * re[i] + im[i] * im[i]);
		}
		// Normalise. An N-point FFT of a full-scale windowed sine peaks near
		// A*N/4 (Hann coherent gain 0.5), so raw magnitudes run into the
		// hundreds, every bin clamps to 1, and the spectrogram is a solid wall.
		float mag = (sum / (float)(i1 - i0)) / ((float)kFftSize * 0.25f);

		// dB with a floor: linear magnitude is all peak and no body.
		float db = 20.0f * log10f(std::max(mag, 1e-7f));
		float v  = ofClamp(ofMap(db, floorDb, 0.0f, 0.0f, 1.0f), 0.0f, 1.0f);

		newFlux += std::max(0.0f, v - prevMagnitudes[b]);
		prevMagnitudes[b] = v;
		magnitudes[b] = ofLerp(magnitudes[b], v, smoothing);
	}
	flux = ofLerp(flux, newFlux / kBins, 0.4f);

	// Trace for the waveform overlay.
	{
		std::lock_guard<std::mutex> lock(audioMutex);
		const size_t span = 4096;
		size_t start = (ringWrite + ring.size() - span) % ring.size();
		for (size_t i = 0; i < waveform.size(); i++) {
			size_t idx = (start + (i * span) / waveform.size()) % ring.size();
			waveform[i] = ring[idx] * gain * autoGain;
		}
	}
}

// --- camera: palette source only -------------------------------------------

void ofApp::buildVideoDeviceList() { videoDevices = cam.listDevices(); }

int ofApp::cameraAuthStatus() {
	AVAuthorizationStatus st = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
	if (st == AVAuthorizationStatusAuthorized) return 0;
	if (st == AVAuthorizationStatusDenied || st == AVAuthorizationStatusRestricted) return 1;
	return 2;
}

void ofApp::requestCameraAccess() {
	if (cameraRequestPending) return;
	cameraRequestPending = true;
	std::atomic<bool>* flag = &cameraJustGranted;
	[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
	                         completionHandler:^(BOOL granted) {
		if (granted) flag->store(true);
	}];
}

void ofApp::startCamera(int listIndex) {
	camReady = false;
	if (videoDevices.empty()) { videoDeviceName = "(no camera)"; return; }
	listIndex = ofClamp(listIndex, 0, (int)videoDevices.size() - 1);
	cam.close();
	cam.setDeviceID(videoDevices[listIndex].id);
	cam.setDesiredFrameRate(30);
	// Third argument is bTexture. The pixels are what the palette reads, but
	// the texture doubles as the "is it delivering?" signal.
	camReady = cam.setup(640, 480, true);
	if (camReady) {
		currentVideoDevice = listIndex;
		videoDeviceName    = videoDevices[listIndex].deviceName;
	} else {
		videoDeviceName = "(failed: " + videoDevices[listIndex].deviceName + ")";
	}
}

void ofApp::serviceCamera() {
	if (!camReady) return;
	cam.update();
	if (cam.getTexture().isAllocated()) {
		if (!camDelivered) {
			camDelivered = true;
			ofLogNotice("ofApp") << "palette source: camera "
			                     << (int)cam.getWidth() << "x" << (int)cam.getHeight();
		}
		camStarveFrames = 0;
		return;
	}
	if (++camStarveFrames > 600) {
		ofLogWarning("ofApp") << "camera delivered no frames after 10s - "
		                      << "palette falls back to a built-in ramp";
		camReady = false;
		camDelivered = false;
		videoDeviceName += " (no frames)";
	}
}

// The camera's only job: supply colours, never structure.
//
// Mapping bin -> camera x and letting the sample row drift made consecutive
// columns adjacent camera rows, so scrolling reassembled the actual picture -
// the camera reappeared as an image, which is precisely what it must not do.
// Instead the scene is reduced to a luminance-sorted ramp from scattered
// samples: the palette tracks the scene's colours while carrying none of its
// geometry.
void ofApp::rebuildPalette() {
	const ofPixels* src = nullptr;
	if (useStill && stillImage.isAllocated())  src = &stillImage.getPixels();
	else if (camReady && camDelivered)         src = &cam.getPixels();

	std::vector<ofColor> samples;
	if (src && src->isAllocated() && src->getWidth() > 1) {
		const int w = src->getWidth(), h = src->getHeight();
		const int n = 96;
		samples.reserve(n);
		for (int i = 0; i < n; i++) {
			// Scattered and slowly drifting, so a static scene still breathes.
			const float u = ofNoise(i * 0.37f,        paletteScan * 1.7f);
			const float v = ofNoise(i * 0.71f + 13.0f, paletteScan * 1.7f);
			samples.push_back(src->getColor(ofClamp((int)(u * (w - 1)), 0, w - 1),
			                                ofClamp((int)(v * (h - 1)), 0, h - 1)));
		}
		std::sort(samples.begin(), samples.end(), [](const ofColor& a, const ofColor& b) {
			return (0.299f*a.r + 0.587f*a.g + 0.114f*a.b)
			     < (0.299f*b.r + 0.587f*b.g + 0.114f*b.b);
		});
	}

	if (palette.size() != (size_t)kBins) palette.assign(kBins, ofColor(0));

	for (int b = 0; b < kBins; b++) {
		const float t = (float)b / (float)(kBins - 1);
		ofColor target;
		if (samples.empty()) {
			target = ofColor(30 + 200 * t, 90 + 90 * sinf(t * PI), 220 - 150 * t);
		} else {
			const float fi = t * (samples.size() - 1);
			const int i0 = (int)fi;
			const int i1 = std::min(i0 + 1, (int)samples.size() - 1);
			target = samples[i0].getLerped(samples[i1], fi - i0);
		}
		// Smoothed, or the per-frame resort flickers the whole palette.
		palette[b] = palette[b].getLerped(target, 0.06f);
	}
}

ofColor ofApp::paletteColor(int bin) {
	if (palette.size() != (size_t)kBins) return ofColor(200);
	ofColor c = palette[ofClamp(bin, 0, kBins - 1)];
	if (saturation != 1.0f) {
		const float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
		c.r = (unsigned char)ofClamp(luma + (c.r - luma) * saturation, 0.0f, 255.0f);
		c.g = (unsigned char)ofClamp(luma + (c.g - luma) * saturation, 0.0f, 255.0f);
		c.b = (unsigned char)ofClamp(luma + (c.b - luma) * saturation, 0.0f, 255.0f);
	}
	return c;
}

// --- strata -----------------------------------------------------------------

void ofApp::pushColumn() {
	for (int b = 0; b < kBins; b++) {
		const float m = magnitudes[b];
		ofColor c = paletteColor(b);
		// Brightness carries the magnitude; the camera carries only hue. A
		// quiet bin must go to black or the background stops being black.
		const float k = powf(m, 1.10f);
		c.r = (unsigned char)ofClamp(c.r * k, 0, 255);
		c.g = (unsigned char)ofClamp(c.g * k, 0, 255);
		c.b = (unsigned char)ofClamp(c.b * k, 0, 255);
		columnPix.setColor(0, kBins - 1 - b, c);   // low frequencies at the bottom
	}
	columnTex.loadData(columnPix);

	const int w = strata[0].getWidth(), h = strata[0].getHeight();
	const int step = std::max(1, (int)scrollSpeed);

	ofFbo& dst = strata[1 - strataCur];
	dst.begin();
	ofClear(0, 0, 0, 255);
	ofSetColor(255);
	strata[strataCur].draw(-step, 0);           // shift history left
	columnTex.draw(w - step, 0, step, h);       // newest at the right edge
	dst.end();
	strataCur = 1 - strataCur;
}

// Transients tear the history horizontally. Offsets decay, so a hit leaves a
// trail that settles rather than a permanent break.
void ofApp::updateGlitch() {
	for (auto& o : sliceOffset) o *= 0.86f;

	if (flux > fluxThreshold) {
		const int hits = 1 + (int)ofRandom(0.0f, 3.99f);
		for (int i = 0; i < hits; i++) {
			int s = (int)ofRandom(0.0f, (float)kSlices - 0.01f);
			int run = 1 + (int)ofRandom(0.0f, 5.0f);
			float amp = ofRandom(-1.0f, 1.0f) * 90.0f * glitchAmount
			          * ofClamp(flux / std::max(fluxThreshold, 1e-4f), 0.0f, 3.0f);
			for (int k = 0; k < run && s + k < kSlices; k++) sliceOffset[s + k] = amp;
		}
	}
}

// --- loop -------------------------------------------------------------------

void ofApp::update() {
	if (cameraJustGranted.exchange(false)) {
		startCamera(currentVideoDevice < 0 ? 0 : currentVideoDevice);
	}
	serviceCamera();
	if (selfTest) synthesizeAudio();

	paletteScan = fmodf(paletteScan + 0.0016f, 1.0f);
	rebuildPalette();

	analyse();
	updateGlitch();
	pushColumn();
}

void ofApp::draw() {
	ofBackground(0);
	drawStrata();
	drawProfile();
	drawWaveform();
	drawFurniture();
	if (showClock) drawClock();
	if (showHud)   drawHud();
	finishSelfTest();
}

void ofApp::drawStrata() {
	const float W = ofGetWidth(), H = ofGetHeight();
	const float bandH = H * bandHeight;
	const float top   = (H - bandH) * 0.5f;

	ofFbo& fbo = strata[strataCur];
	const float srcH = fbo.getHeight();
	const float sliceSrcH = srcH / kSlices;
	const float sliceDstH = bandH / kSlices;

	ofSetColor(255);
	for (int s = 0; s < kSlices; s++) {
		fbo.getTexture().drawSubsection(
			sliceOffset[s], top + s * sliceDstH, W, sliceDstH + 0.5f,
			0, s * sliceSrcH, fbo.getWidth(), sliceSrcH);
	}
}

// Instantaneous spectrum as fine ticks under the band - the "now" slice that
// the strata above is a history of.
void ofApp::drawProfile() {
	const float W = ofGetWidth(), H = ofGetHeight();
	const float base = (H + H * bandHeight) * 0.5f + 46.0f;
	const float maxH = 86.0f;

	ofSetLineWidth(1.0f);
	for (int b = 0; b < kBins; b += 2) {
		const float x = (b + 0.5f) / kBins * W;
		const float m = magnitudes[b];
		if (m < 0.02f) continue;
		ofColor c = paletteColor(b);
		c.a = (unsigned char)(50 + 150 * m);
		ofSetColor(c);
		ofDrawLine(x, base, x, base - m * maxH);
	}
}

void ofApp::drawWaveform() {
	const float W = ofGetWidth(), H = ofGetHeight();
	const float mid = H * 0.5f;
	const float amp = H * bandHeight * 0.46f;

	ofSetColor(255, 255, 255, (unsigned char)(255 * waveAlpha));
	ofSetLineWidth(1.0f);
	ofPolyline line;
	for (size_t i = 0; i < waveform.size(); i++) {
		const float x = (float)i / (waveform.size() - 1) * W;
		line.addVertex(x, mid - ofClamp(waveform[i], -1.0f, 1.0f) * amp);
	}
	line.draw();
}

// Thin rules and ticks. The reference work reads as instrumentation, and the
// framing is most of what does that.
void ofApp::drawFurniture() {
	const float W = ofGetWidth(), H = ofGetHeight();
	const float bandH = H * bandHeight;
	const float top = (H - bandH) * 0.5f, bot = top + bandH;

	ofSetLineWidth(1.0f);
	ofSetColor(255, 255, 255, 38);
	ofDrawLine(0, top, W, top);
	ofDrawLine(0, bot, W, bot);

	// Decade ticks along the frequency axis.
	ofSetColor(255, 255, 255, 26);
	for (int b = 0; b < kBins; b += kBins / 16) {
		const float y = bot - (float)b / kBins * bandH;
		ofDrawLine(0, y, 14, y);
		ofDrawLine(W - 14, y, W, y);
	}
	ofSetColor(255, 255, 255, 20);
	for (int i = 0; i <= 8; i++) {
		const float x = (float)i / 8 * W;
		ofDrawLine(x, bot, x, bot + 7);
	}
}

void ofApp::drawClock() {
	const string stamp = ofGetTimestampString("%H:%M:%S");
	const float margin = 26.0f;
	if (clockFont.isLoaded()) {
		ofRectangle r = clockFont.getStringBoundingBox(stamp, 0, 0);
		ofSetColor(255, 235);
		clockFont.drawString(stamp, ofGetWidth() - r.width - margin,
		                     ofGetHeight() - margin);
	} else {
		ofSetColor(255);
		ofDrawBitmapString(stamp, ofGetWidth() - 90, ofGetHeight() - margin);
	}
}

void ofApp::drawHud() {
	std::vector<string> lines = {
		"audio    [" + ofToString(currentAudioDevice) + "] " + audioDeviceName,
		"palette  " + string(useStill ? "still image"
		                              : (camDelivered ? videoDeviceName
		                                              : videoDeviceName + " - built-in ramp")),
		"scroll " + ofToString(scrollSpeed, 1) + "   band " + ofToString(bandHeight, 2) +
		    "   glitch " + ofToString(glitchAmount, 2) +
		    "   flux " + ofToString(flux, 3) + "/" + ofToString(fluxThreshold, 2) +
		    "   gain " + ofToString(gain * autoGain, 1),
		"",
		"d/D audio device   c camera   drag image = palette source",
		"UP/DOWN glitch   LEFT/RIGHT flux threshold   [/] smoothing",
		"-/= gain   a auto-gain   ,/. saturation   w/q band height   e/Q scroll",
		"t clock   h hud   f fullscreen   s save frame   cmd-Q quits",
	};
	const float pad = 10.0f, lh = 17.0f;
	ofSetColor(0, 170);
	ofDrawRectangle(0, 0, 580, lh * lines.size() + pad * 2 + 24);
	ofSetColor(235);
	float y = pad + lh;
	for (auto& s : lines) {
		if (hudFont.isLoaded()) hudFont.drawString(s, pad, y);
		else                    ofDrawBitmapString(s, pad, y);
		y += lh;
	}
	const float meterW = 560.0f * ofClamp(level * 6.0f, 0.0f, 1.0f);
	ofSetColor(60);
	ofDrawRectangle(pad, y - 4, 560, 5);
	ofSetColor(level > 0.0008f ? ofColor(120, 230, 150) : ofColor(200, 90, 90));
	ofDrawRectangle(pad, y - 4, meterW, 5);
}

void ofApp::finishSelfTest() {
	if (!selfTest) return;
	// Long enough for the history to fill the width before the grab.
	if (++selfTestFrames >= 420) {
		ofSaveScreen(selfTestPath);
		ofLogNotice("ofApp") << "self-test frame written to " << selfTestPath;
		ofExit();
	}
}

// --- input ------------------------------------------------------------------

void ofApp::keyPressed(int key) {
	switch (key) {
		case 'd': if (!inputDevices.empty()) startAudio((currentAudioDevice + 1) % inputDevices.size()); break;
		case 'D': if (!inputDevices.empty()) startAudio((currentAudioDevice - 1 + (int)inputDevices.size()) % inputDevices.size()); break;
		case 'c': if (!videoDevices.empty()) { useStill = false; startCamera((currentVideoDevice + 1) % videoDevices.size()); } break;
		case 'a': autoGainOn = !autoGainOn; break;
		case 't': showClock = !showClock; break;
		case 'h': showHud = !showHud; break;
		case 'f': ofToggleFullscreen(); break;
		case 's': ofSaveScreen("renders/frame-" + ofGetTimestampString("%Y%m%d-%H%M%S") + ".png"); break;
		case '[': smoothing = ofClamp(smoothing - 0.05f, 0.05f, 1.0f); break;
		case ']': smoothing = ofClamp(smoothing + 0.05f, 0.05f, 1.0f); break;
		case '-': gain = ofClamp(gain - 0.1f, 0.1f, 8.0f); break;
		case '=': gain = ofClamp(gain + 0.1f, 0.1f, 8.0f); break;
		case ',': saturation = ofClamp(saturation - 0.08f, 0.0f, 2.0f); break;
		case '.': saturation = ofClamp(saturation + 0.08f, 0.0f, 2.0f); break;
		case 'w': bandHeight = ofClamp(bandHeight + 0.03f, 0.1f, 0.95f); break;
		case 'q': bandHeight = ofClamp(bandHeight - 0.03f, 0.1f, 0.95f); break;
		case 'e': scrollSpeed = ofClamp(scrollSpeed + 1.0f, 1.0f, 12.0f); break;
		case 'Q': scrollSpeed = ofClamp(scrollSpeed - 1.0f, 1.0f, 12.0f); break;
		case OF_KEY_UP:    glitchAmount  = ofClamp(glitchAmount + 0.1f, 0.0f, 4.0f); break;
		case OF_KEY_DOWN:  glitchAmount  = ofClamp(glitchAmount - 0.1f, 0.0f, 4.0f); break;
		case OF_KEY_RIGHT: fluxThreshold = ofClamp(fluxThreshold * 1.2f, 0.005f, 2.0f); break;
		case OF_KEY_LEFT:  fluxThreshold = ofClamp(fluxThreshold / 1.2f, 0.005f, 2.0f); break;
		default: break;
	}
}

void ofApp::dragEvent(ofDragInfo dragInfo) {
	if (dragInfo.files.empty()) return;
	ofImage img;
	if (img.load(dragInfo.files[0])) { stillImage = img; useStill = true; }
}

void ofApp::exit() {
	soundStream.close();
	if (camReady) cam.close();
}
