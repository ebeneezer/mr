#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TMenuBar
#define Uses_TStatusLine
#define Uses_TDeskTop
#ifndef TMREDITORAPP_HPP
#define TMREDITORAPP_HPP

#include <tvision/tv.h>

class TMREditorApp : public TApplication {
 public:
	static TMenuBar *initMRMenuBar(TRect r);
	static TStatusLine *initMRStatusLine(TRect r);
	static TDeskTop *initMRDeskTop(TRect r);

	TMREditorApp();
	~TMREditorApp() override;

	void handleEvent(TEvent &event) override;
	void idle() override;
	TPalette &getPalette() const override;

 private:
	void prepareForQuit();

	bool exitPrepared_;
};

#endif
