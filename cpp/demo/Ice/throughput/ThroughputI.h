// **********************************************************************
//
// Copyright (c) 2003-2005 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef THROUGHPUT_I_H
#define THROUGHPUT_I_H

#include <Ice/Ice.h>
#include <Throughput.h>

class ThroughputI : public Demo::Throughput
{
public:

    ThroughputI() :
	_byteSeq(500000, 0),
	_stringSeq(100000, "hello"),
	_structSeq(50000)
    {
        for(int i = 0; i < 50000; ++i)
	{
	    _structSeq[i].s = "hello";
	    _structSeq[i].d = 3.14;
	}
    }

    virtual void
    sendByteSeq(const Demo::ByteSeq&, const Ice::Current&)
    {
    }

    virtual Demo::ByteSeq
    recvByteSeq(const Ice::Current&)
    {
	return _byteSeq;
    }

    virtual Demo::ByteSeq
    echoByteSeq(const Demo::ByteSeq& seq, const Ice::Current&)
    {
	return seq;
    }

    virtual void
    sendStringSeq(const Demo::StringSeq&, const Ice::Current&)
    {
    }

    virtual Demo::StringSeq
    recvStringSeq(const Ice::Current&)
    {
	return _stringSeq;
    }

    virtual Demo::StringSeq
    echoStringSeq(const Demo::StringSeq& seq, const Ice::Current&)
    {
	return seq;
    }

    virtual void
    sendStructSeq(const Demo::StringDoubleSeq&, const Ice::Current&)
    {
    }

    virtual Demo::StringDoubleSeq
    recvStructSeq(const Ice::Current&)
    {
	return _structSeq;
    }

    virtual Demo::StringDoubleSeq
    echoStructSeq(const Demo::StringDoubleSeq& seq, const Ice::Current&)
    {
	return seq;
    }

    virtual void
    shutdown(const Ice::Current& c)
    {
	c.adapter->getCommunicator()->shutdown();
    }

private:

    Demo::ByteSeq _byteSeq;
    Demo::StringSeq _stringSeq;
    Demo::StringDoubleSeq _structSeq;
};

#endif
