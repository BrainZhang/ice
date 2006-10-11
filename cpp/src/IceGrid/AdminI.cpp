// **********************************************************************
//
// Copyright (c) 2003-2006 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceUtil/UUID.h>

#include <Ice/Ice.h>
#include <Ice/LoggerUtil.h>
#include <Ice/TraceUtil.h>
#include <Ice/SliceChecksums.h>

#include <IceGrid/AdminI.h>
#include <IceGrid/RegistryI.h>
#include <IceGrid/Database.h>
#include <IceGrid/Util.h>
#include <IceGrid/DescriptorParser.h>
#include <IceGrid/DescriptorHelper.h>
#include <IceGrid/AdminSessionI.h>

using namespace std;
using namespace Ice;
using namespace IceGrid;

namespace IceGrid
{

class ServerProxyWrapper
{
public:

    ServerProxyWrapper(const DatabasePtr& database, const string& id) : _id(id)
    {
	_proxy = database->getServerWithTimeouts(id, _activationTimeout, _deactivationTimeout, _node);
    }
    
    void
    useActivationTimeout()
    {
	_proxy = ServerPrx::uncheckedCast(_proxy->ice_timeout(_activationTimeout * 1000));
    }

    void
    useDeactivationTimeout()
    {
	_proxy = ServerPrx::uncheckedCast(_proxy->ice_timeout(_deactivationTimeout * 1000));
    }

    IceProxy::IceGrid::Server* 
    operator->() const
    {
	return _proxy.get();
    }

    void
    handleException(const Ice::Exception& ex)
    {
	try
	{
	    ex.ice_throw();
	}
	catch(const Ice::UserException&)
	{
	    throw;
	}
	catch(const Ice::ObjectNotExistException&)
	{
	    throw ServerNotExistException(_id);
	}
	catch(const Ice::LocalException& e)
	{
	    ostringstream os;
	    os << e;
	    throw NodeUnreachableException(_node, os.str());
	}
    }

private:

    string _id;
    ServerPrx _proxy;
    int _activationTimeout;
    int _deactivationTimeout;
    string _node;
};

class PatchAggregator : public IceUtil::Mutex, public IceUtil::Shared
{
public:

    PatchAggregator(const AMD_Admin_patchApplicationPtr& cb, 
		    const TraceLevelsPtr& traceLevels, 
		    const string& application,
		    int nodeCount) : 
	_cb(cb), _traceLevels(traceLevels), _application(application), _count(nodeCount), _nSuccess(0), _nFailure(0)
    {
    }

    void
    finished(const string& node, const string& failure)
    {
	Lock sync(*this);
	if(failure.empty())
	{
	    if(_traceLevels->patch > 0)
	    {
		Ice::Trace out(_traceLevels->logger, _traceLevels->patchCat);
		out << "finished patching of application `" << _application << "' on node `" << node << "'";
	    }

	    ++_nSuccess;
	}
	else
	{
	    if(_traceLevels->patch > 0)
	    {
		Ice::Trace out(_traceLevels->logger, _traceLevels->patchCat);
		out << "patching of application `" << _application << "' on node `" << node <<"' failed:\n" << failure;
	    }

	    ++_nFailure;
	    _reasons.push_back(failure);
	}

	if((_nSuccess + _nFailure) == _count)
	{
	    if(_nFailure)
	    {
		sort(_reasons.begin(), _reasons.end());
		PatchException ex;
		ex.reasons = _reasons;
		_cb->ice_exception(ex);
	    }
	    else
	    {
		_cb->ice_response();
	    }
	}
    }

private:

    const AMD_Admin_patchApplicationPtr _cb;
    const TraceLevelsPtr _traceLevels;
    const string _application;
    const int _count;
    int _nSuccess;
    int _nFailure;
    Ice::StringSeq _reasons;    
};
typedef IceUtil::Handle<PatchAggregator> PatchAggregatorPtr;

class PatchCB : public AMI_Node_patch
{
public:

    PatchCB(const PatchAggregatorPtr& cb, const string& node) : 
	_cb(cb), _node(node)
    {
    }

    void
    ice_response()
    {
	_cb->finished(_node, "");
    }

    void
    ice_exception(const Ice::Exception& ex)
    {
	string reason;
	try
	{
	    ex.ice_throw();
	}
	catch(const PatchException& ex)
	{
	    if(!ex.reasons.empty())
	    {
		reason = ex.reasons[0];
	    }
	}
	catch(const NodeNotExistException&)
	{
	    reason = "patch on node `" + _node + "' failed: node doesn't exist";
	}
	catch(const NodeUnreachableException& e)
	{
	    reason = "patch on node `" + _node + "' failed: node is unreachable:\n" + e.reason;
	}
	catch(const Ice::Exception& e)
	{
	    ostringstream os;
	    os << e;
	    reason = "patch on node `" + _node + "' failed: node is unreachable:\n" + os.str();
	}	
	_cb->finished(_node, reason);
    }

private:

    const PatchAggregatorPtr _cb;
    const string _node;
};

class ServerPatchCB : public AMI_Node_patch
{
public:

    ServerPatchCB(const AMD_Admin_patchServerPtr& cb, 
		  const TraceLevelsPtr& traceLevels,
		  const string& server, 
		  const string& node) : 
	_cb(cb), _traceLevels(traceLevels), _server(server), _node(node)
    {
    }

    void
    ice_response()
    {
	if(_traceLevels->patch > 0)
	{
	    Ice::Trace out(_traceLevels->logger, _traceLevels->patchCat);
	    out << "finished patching of server `" << _server << "' on node `" << _node << "'";
	}

	_cb->ice_response();
    }

    void
    ice_exception(const Ice::Exception& ex)
    {
	string reason;
	try
	{
	    ex.ice_throw();
	}
	catch(const PatchException& ex)
	{
	    if(!ex.reasons.empty())
	    {
		reason = ex.reasons[0];
	    }
	}
	catch(const NodeNotExistException&)
	{
	    reason = "patch on node `" + _node + "' failed: node doesn't exist";
	}
	catch(const NodeUnreachableException& e)
	{
	    reason = "patch on node `" + _node + "' failed: node is unreachable:\n" + e.reason;
	}
	catch(const Ice::Exception& ex)
	{
	    ostringstream os;
	    os << ex;
	    reason = "patch on node `" + _node + "' failed: node is unreachable:\n" + os.str();
	}

	if(_traceLevels->patch > 0)
	{
	    Ice::Trace out(_traceLevels->logger, _traceLevels->patchCat);
	    out << "patching of server `" << _server << "' on node `" << _node << "' failed:\n" << reason;
	}

	PatchException e;
	e.reasons.push_back(reason);
	_cb->ice_exception(e);
    }

private:

    const AMD_Admin_patchServerPtr _cb;
    const TraceLevelsPtr _traceLevels;
    const string _server;
    const string _node;
};

}

AdminI::AdminI(const DatabasePtr& database, const RegistryIPtr& registry, const AdminSessionIPtr& session) :
    _database(database),
    _registry(registry),
    _traceLevels(_database->getTraceLevels()),
    _session(session)
{
}

AdminI::~AdminI()
{
}

void
AdminI::addApplication(const ApplicationDescriptor& descriptor, const Current&)
{
    checkIsMaster();

    ApplicationInfo info;
    info.createTime = info.updateTime = IceUtil::Time::now().toMilliSeconds();
    info.createUser = info.updateUser = _session->getId();
    info.descriptor = descriptor;
    info.revision = 1;
    info.uuid = IceUtil::generateUUID();

    _database->addApplication(info, _session.get());
}

void
AdminI::syncApplication(const ApplicationDescriptor& descriptor, const Current&)
{
    checkIsMaster();
    _database->syncApplicationDescriptor(descriptor, _session.get());
}

void
AdminI::updateApplication(const ApplicationUpdateDescriptor& descriptor, const Current&)
{
    checkIsMaster();

    ApplicationUpdateInfo update;
    update.updateTime = IceUtil::Time::now().toMilliSeconds();
    update.updateUser = _session->getId();
    update.descriptor = descriptor;
    update.revision = -1; // The database will set it.
    _database->updateApplication(update, _session.get());
}

void
AdminI::removeApplication(const string& name, const Current&)
{
    checkIsMaster();
    _database->removeApplication(name, _session.get());
}

void
AdminI::instantiateServer(const string& app, const string& node, const ServerInstanceDescriptor& desc, const Current&)
{
    checkIsMaster();
    _database->instantiateServer(app, node, desc, _session.get());
}

void
AdminI::patchApplication_async(const AMD_Admin_patchApplicationPtr& amdCB, 
			       const string& name, 
			       bool shutdown, 
			       const Current& current)
{
    ApplicationHelper helper(current.adapter->getCommunicator(), _database->getApplicationInfo(name).descriptor);
    DistributionDescriptor appDistrib;
    vector<string> nodes;
    helper.getDistributions(appDistrib, nodes);

    if(nodes.empty())
    {
	amdCB->ice_response();
	return;
    }

    PatchAggregatorPtr aggregator = new PatchAggregator(amdCB, _traceLevels, name, static_cast<int>(nodes.size()));
    for(vector<string>::const_iterator p = nodes.begin(); p != nodes.end(); ++p)
    {
	if(_traceLevels->patch > 0)
	{
	    Ice::Trace out(_traceLevels->logger, _traceLevels->patchCat);
	    out << "started patching of application `" << name << "' on node `" << *p << "'";
	}

	AMI_Node_patchPtr cb = new PatchCB(aggregator, *p);
	try
	{
	    Resolver resolve(_database->getNodeInfo(*p), _database->getCommunicator());
	    _database->getNode(*p)->patch_async(cb, name, "", resolve(appDistrib), shutdown);
	}
	catch(const Ice::Exception& ex)
	{
	    cb->ice_exception(ex);
	}
    }
}

ApplicationInfo
AdminI::getApplicationInfo(const string& name, const Current&) const
{
    return _database->getApplicationInfo(name);
}

ApplicationDescriptor
AdminI::getDefaultApplicationDescriptor(const Current& current) const
{
    Ice::PropertiesPtr properties = current.adapter->getCommunicator()->getProperties();
    string path = properties->getProperty("IceGrid.Registry.DefaultTemplates");
    if(path.empty())
    {
	throw DeploymentException("no default templates configured, you need to set "
				  "IceGrid.Registry.DefaultTemplates in the IceGrid registry configuration.");
    }

    ApplicationDescriptor desc;
    try
    {
	desc = DescriptorParser::parseDescriptor(path, current.adapter->getCommunicator());
    }
    catch(const IceXML::ParserException& ex)
    {
	throw DeploymentException("can't parse default templates:\n" + ex.reason());
    }
    desc.name = "";
    if(!desc.nodes.empty())
    {
	Ice::Warning warn(_traceLevels->logger);
	warn << "default application descriptor:\nnode definitions are not allowed.";
	desc.nodes.clear();
    }
    if(!desc.distrib.icepatch.empty() || !desc.distrib.directories.empty())
    {
	Ice::Warning warn(_traceLevels->logger);
	warn << "default application descriptor:\ndistribution is not allowed.";
	desc.distrib = DistributionDescriptor();
    }
    if(!desc.replicaGroups.empty())
    {
	Ice::Warning warn(_traceLevels->logger);
	warn << "default application descriptor:\nreplica group definitions are not allowed.";
	desc.replicaGroups.clear();
    }
    if(!desc.description.empty())
    {
	Ice::Warning warn(_traceLevels->logger);
	warn << "default application descriptor:\ndescription is not allowed.";
	desc.description = "";
    }
    if(!desc.variables.empty())
    {
	Ice::Warning warn(_traceLevels->logger);
	warn << "default application descriptor:\nvariable definitions are not allowed.";
	desc.variables.clear();
    }
    return desc;
}

Ice::StringSeq
AdminI::getAllApplicationNames(const Current&) const
{
    return _database->getAllApplications();
}

ServerInfo
AdminI::getServerInfo(const string& id, const Current&) const
{
    return _database->getServerInfo(id, true);
}

ServerState
AdminI::getServerState(const string& id, const Current&) const
{
    ServerProxyWrapper proxy(_database, id);
    try
    {
	return proxy->getState();
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
	return Inactive;
    }
}

Ice::Int
AdminI::getServerPid(const string& id, const Current&) const
{
    ServerProxyWrapper proxy(_database, id);
    try
    {
	return proxy->getPid();
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
	return 0;
    }
}

void
AdminI::startServer(const string& id, const Current&)
{
    ServerProxyWrapper proxy(_database, id);
    proxy.useActivationTimeout();
    try
    {
	proxy->start();
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
    }
}

void
AdminI::stopServer(const string& id, const Current&)
{
    ServerProxyWrapper proxy(_database, id);
    proxy.useDeactivationTimeout();
    try
    {
	proxy->stop();
    }
    catch(const Ice::TimeoutException&)
    {
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
    }
}

void
AdminI::patchServer_async(const AMD_Admin_patchServerPtr& amdCB, const string& id, bool shutdown,
			  const Current& current)
{
    ServerInfo info = _database->getServerInfo(id);
    ApplicationInfo appInfo = _database->getApplicationInfo(info.application);
    ApplicationHelper helper(current.adapter->getCommunicator(), appInfo.descriptor);
    DistributionDescriptor appDistrib;
    vector<string> nodes;
    helper.getDistributions(appDistrib, nodes, id);

    if(appDistrib.icepatch.empty() && nodes.empty())
    {
	amdCB->ice_response();
	return;
    }

    assert(nodes.size() == 1);

    vector<string>::const_iterator p = nodes.begin();
    AMI_Node_patchPtr amiCB = new ServerPatchCB(amdCB, _traceLevels, id, *p);
    try
    {
	Resolver resolve(_database->getNodeInfo(*p), _database->getCommunicator());
	_database->getNode(*p)->patch_async(amiCB, info.application, id, resolve(appDistrib), shutdown);
    }
    catch(const Ice::Exception& ex)
    {
	amiCB->ice_exception(ex);
    }
}

void
AdminI::sendSignal(const string& id, const string& signal, const Current&)
{
    ServerProxyWrapper proxy(_database, id);
    try
    {
	proxy->sendSignal(signal);
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
    }
}

void
AdminI::writeMessage(const string& id, const string& message, Int fd, const Current&)
{
    ServerProxyWrapper proxy(_database, id);
    try
    {
	proxy->writeMessage(message, fd);
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
    }
}


StringSeq
AdminI::getAllServerIds(const Current&) const
{
    return _database->getAllServers();
}

void 
AdminI::enableServer(const string& id, bool enable, const Ice::Current&)
{
    ServerProxyWrapper proxy(_database, id);
    try
    {
	proxy->setEnabled(enable);
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
    }
}

bool
AdminI::isServerEnabled(const ::std::string& id, const Ice::Current&) const
{
    ServerProxyWrapper proxy(_database, id);
    try
    {
	return proxy->isEnabled();
    }
    catch(const Ice::Exception& ex)
    {
	proxy.handleException(ex);
	return true; // Keeps the compiler happy.
    }
}

AdapterInfoSeq
AdminI::getAdapterInfo(const string& id, const Current&) const
{
    return _database->getAdapterInfo(id);
}

void
AdminI::removeAdapter(const string& adapterId, const Ice::Current&)
{
    checkIsMaster();
    _database->removeAdapter(adapterId);
}

StringSeq
AdminI::getAllAdapterIds(const Current&) const
{
    return _database->getAllAdapters();
}

void 
AdminI::addObject(const Ice::ObjectPrx& proxy, const ::Ice::Current& current)
{
    checkIsMaster();
    try
    {
	addObjectWithType(proxy, proxy->ice_id(), current);
    }
    catch(const Ice::LocalException& e)
    {
	ostringstream os;

	os << "failed to invoke ice_id() on proxy `" + current.adapter->getCommunicator()->proxyToString(proxy);
	os << "':\n" << e;
	throw DeploymentException(os.str());
    }
}

void 
AdminI::updateObject(const Ice::ObjectPrx& proxy, const ::Ice::Current& current)
{
    checkIsMaster();
    const Ice::Identity id = proxy->ice_getIdentity();
    if(id.category == _database->getInstanceName())
    {
	DeploymentException ex;
	ex.reason ="updating object `" + _database->getCommunicator()->identityToString(id) + "' is not allowed";
	throw ex;
    }
    _database->updateObject(proxy);
}

void 
AdminI::addObjectWithType(const Ice::ObjectPrx& proxy, const string& type, const ::Ice::Current& current)
{
    checkIsMaster();
    const Ice::Identity id = proxy->ice_getIdentity();
    if(id.category == _database->getInstanceName())
    {
	DeploymentException ex;
	ex.reason = "adding object `" + _database->getCommunicator()->identityToString(id) + "' is not allowed";
	throw ex;
    }

    ObjectInfo info;
    info.proxy = proxy;
    info.type = type;
    _database->addObject(info);
}

void 
AdminI::removeObject(const Ice::Identity& id, const Ice::Current& current)
{
    checkIsMaster();
    if(id.category == _database->getInstanceName())
    {
	DeploymentException ex;
	ex.reason = "removing object `" + _database->getCommunicator()->identityToString(id) + "' is not allowed";
	throw ex;
    }
    _database->removeObject(id);
}

ObjectInfo
AdminI::getObjectInfo(const Ice::Identity& id, const Ice::Current&) const
{
    return _database->getObjectInfo(id);
}

ObjectInfoSeq
AdminI::getObjectInfosByType(const string& type, const Ice::Current&) const
{
    return _database->getObjectInfosByType(type);
}

ObjectInfoSeq
AdminI::getAllObjectInfos(const string& expression, const Ice::Current&) const
{
    return _database->getAllObjectInfos(expression);
}

NodeInfo
AdminI::getNodeInfo(const string& name, const Ice::Current&) const
{
    return _database->getNodeInfo(name);
}

bool
AdminI::pingNode(const string& name, const Current&) const
{
    try
    {
	_database->getNode(name)->ice_ping();
	return true;
    }
    catch(const NodeUnreachableException&)
    {
	return false;
    }
    catch(const Ice::ObjectNotExistException&)
    {
	throw NodeNotExistException();
    }
    catch(const Ice::LocalException&)
    {
	return false;
    }
}

LoadInfo
AdminI::getNodeLoad(const string& name, const Current&) const
{
    try
    {
	return _database->getNode(name)->getLoad();
    }
    catch(const Ice::ObjectNotExistException&)
    {
	throw NodeNotExistException();
    }
    catch(const Ice::LocalException& ex)
    {
	ostringstream os;
	os << ex;
	throw NodeUnreachableException(name, os.str());
    }    
    return LoadInfo(); // Keep the compiler happy.
}

void
AdminI::shutdownNode(const string& name, const Current&)
{
    NodePrx node = _database->getNode(name);
    try
    {
	node->shutdown();
    }
    catch(const Ice::ObjectNotExistException&)
    {
	throw NodeNotExistException(name);
    }
    catch(const Ice::LocalException& ex)
    {
	ostringstream os;
	os << ex;
	throw NodeUnreachableException(name, os.str());
    }
}

string
AdminI::getNodeHostname(const string& name, const Current&) const
{
    NodePrx node = _database->getNode(name);
    try
    {
	return node->getHostname();
    }
    catch(const Ice::ObjectNotExistException&)
    {
	throw NodeNotExistException(name);
    }
    catch(const Ice::LocalException& ex)
    {
	ostringstream os;
	os << ex;
	throw NodeUnreachableException(name, os.str());
	return ""; // Keep the compiler happy.
    }
}


StringSeq
AdminI::getAllNodeNames(const Current&) const
{
    return _database->getAllNodes();
}

RegistryInfo
AdminI::getRegistryInfo(const string& name, const Ice::Current&) const
{
    if(name == _registry->getName())
    {
	return _registry->getInfo();
    }
    else
    {
	return _database->getReplicaInfo(name);
    }
}

bool
AdminI::pingRegistry(const string& name, const Current&) const
{
    if(name == _registry->getName())
    {
 	return true;
    }

    try
    {
	_database->getReplica(name)->ice_ping();
	return true;
    }
    catch(const Ice::ObjectNotExistException&)
    {
	throw RegistryNotExistException();
    }
    catch(const Ice::LocalException&)
    {
	return false;
    }
    return false;
}

void
AdminI::shutdownRegistry(const string& name, const Current&)
{
    if(name == _registry->getName())
    {
	_registry->shutdown();
	return;
    }

    InternalRegistryPrx registry = _database->getReplica(name);
    try
    {
	registry->shutdown();
    }
    catch(const Ice::ObjectNotExistException&)
    {
	throw RegistryNotExistException(name);
    }
    catch(const Ice::LocalException& ex)
    {
	ostringstream os;
	os << ex;
	throw RegistryUnreachableException(name, os.str());
    }
}

StringSeq
AdminI::getAllRegistryNames(const Current&) const
{
    Ice::StringSeq replicas = _database->getAllReplicas();
    replicas.push_back(_registry->getName());
    return replicas;
}

void
AdminI::shutdown(const Current&)
{
    _registry->shutdown();
}

SliceChecksumDict
AdminI::getSliceChecksums(const Current&) const
{
    return sliceChecksums();
}

void
AdminI::checkIsMaster() const
{
    if(!_database->isMaster())
    {
	DeploymentException ex;
	ex.reason = "this operation is only allowed on the master registry.";
	throw ex;
    }
}
