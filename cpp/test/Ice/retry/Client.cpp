// **********************************************************************
//
// Copyright (c) 2003-2005 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Ice/Ice.h>
#include <TestCommon.h>
#include <Test.h>

using namespace std;
using namespace Test;

int
run(int argc, char* argv[], const Ice::CommunicatorPtr& communicator)
{
    RetryPrx allTests(const Ice::CommunicatorPtr&);
    RetryPrx retry = allTests(communicator);
    retry->shutdown();
    return EXIT_SUCCESS;
}

int
main(int argc, char* argv[])
{
    int status;
    Ice::CommunicatorPtr communicator;

    try
    {
	Ice::PropertiesPtr properties = Ice::getDefaultProperties(argc, argv);

	//
	// For this test, we want to disable retries.
	//
	properties->setProperty("Ice.RetryIntervals", "-1");

	//
	// This test kills connections, so we don't want warnings.
	//
	properties->setProperty("Ice.Warn.Connections", "0");

	communicator = Ice::initialize(argc, argv);
	status = run(argc, argv, communicator);
    }
    catch(const Ice::Exception& ex)
    {
	cerr << "xxxxxxxxxxxxx" << ex << endl;
	status = EXIT_FAILURE;
    }

    if(communicator)
    {
	try
	{
	    communicator->destroy();
	}
	catch(const Ice::Exception& ex)
	{
	    cerr << ex << endl;
	    status = EXIT_FAILURE;
	}
    }

    return status;
}
