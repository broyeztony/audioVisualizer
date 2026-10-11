#include "ofMain.h"
#include "ofApp.h"

int main(int argc, char** argv) {
	// --selftest <out.png> renders a single frame from a synthetic waveform
	// and exits, for checking the effect without a camera or an audio source.
	string selfTestOut, selfTestSrc;
	std::vector<std::pair<string, float>> params;
	for (int i = 1; i < argc; i++) {
		string a = argv[i];
		if (a == "--selftest" && i + 1 < argc) selfTestOut = argv[++i];
		else if (a == "--source" && i + 1 < argc) selfTestSrc = argv[++i];
		else if (a.size() > 2 && a.substr(0, 2) == "--" && i + 1 < argc) {
			params.emplace_back(a.substr(2), ofToFloat(argv[++i]));
		}
	}

	ofGLWindowSettings settings;
	settings.setSize(1280, 720);
	settings.windowMode = OF_WINDOW;
	settings.title = "audioVisualizer";

	auto window = ofCreateWindow(settings);
	auto app    = std::make_shared<ofApp>();
	if (!selfTestOut.empty()) app->setSelfTest(selfTestOut, selfTestSrc);
	for (auto& p : params) app->setParam(p.first, p.second);

	ofRunApp(window, app);
	ofRunMainLoop();
}
