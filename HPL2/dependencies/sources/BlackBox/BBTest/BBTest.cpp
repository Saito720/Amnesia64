//BBTest.cpp


#include "ApplicationKit.h"


using namespace VCF;


class BBTestWindow : public Window {
public:
	BBTestWindow() {
		setCaption( "BBTest" );
		setVisible( true );

		int * p = NULL;
		*p = 123;
	}

	virtual ~BBTestWindow(){};

};




class BBTestApplication : public Application {
public:

	virtual bool initRunningApplication(){
		bool result = Application::initRunningApplication();

		lib.load( "BlackBox.dll" );

		
		Window* mainWindow = new BBTestWindow();
		setMainWindow(mainWindow);
		mainWindow->setBounds( &Rect( 100.0, 100.0, 500.0, 500.0 ) );
		
		

		return result;
	}

	Library lib;
};


int main(int argc, char *argv[])
{
	BBTestApplication app;

	Application::appMain( argc, argv );
	
	return 0;
}


