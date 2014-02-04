/**
 *  openFrameworks version of the popular synchronization system Most Pixels Ever by Dan Shiffman
 *  original repo: https://github.com/shiffman/Most-Pixels-Ever
 *  our fork: https://github.com/FlightPhase/Most-Pixels-Ever
 *
 *  I affectionately refer to as "Most Pickles Ever" since it's gotten me out of the most pickles. ever!
 *
 *  Standing on the shoulders of the original creators:
 *  Dan Shiffman with Jeremy Rotsztain, Elie Zananiri, Chris Kairalla.
 *  Extended by James George on 5/17/11 @ Flightphase for the National Maritime Museum
 *
 *  Still need to convert the original examples to the new format
 *
 *  There is a drawback that this is not compatible with the Java MPE jar, the connections must go OF client to OF Server
 *
 */

#include "ofxMPEServer.h"
#include "ofxXmlSettings.h"
ofxMPEServer::ofxMPEServer()
{
	allconnected = false;
	framerate = 30;
	numRequiredClients = 0;
	currentFrame = 0;
	shouldTriggerFrame = false;

	newMessage = false;
	verbose = false;
	delimiter = "|";
	
	//TODO: heart beats
	timeOfNextHeartbeat = ofGetElapsedTimef();
	heartBeatInterval = 2.0;

}


void ofxMPEServer::setup(string settingsFile)
{

	ofxXmlSettings settings;
	if(!settings.loadFile(settingsFile)){
		ofLog(OF_LOG_ERROR, "MPE Server -- Couldn't load settings file: " + settingsFile);
		return;
	}

	setup(settings.getValue("settings:framerate", 30),
		  settings.getValue("settings:port", 9001),
		  settings.getValue("settings:numclients", 2),
		  settings.getValue("settings:waitForAll", true),
		  settings.getValue("settings:verbose", false));
	
	//server.setMessageDelimiter("\n");
	ofAddListener(ofEvents().exit, this, &ofxMPEServer::exit);
}

void ofxMPEServer::setup(int fps, int port, int numClients, bool waitForAll, bool verbose)
{

	close(); // in case of double set up

	//make sure vsync is OFF
	//make sure framerate is fast as it can go

	this->verbose = verbose;
	//TODO: support
	shouldWaitForAllClients = waitForAll;
	
	if(!server.setup(port, false)){
		ofLog(OF_LOG_ERROR, "MPE Serever :: Setup failed");
	}
	
	numRequiredClients = numClients;
	framerate = fps;

	shouldTriggerFrame = false;
	allconnected = false;
	lastFrameTriggeredTime = 0;
	currentFrame = 0;

	startThread(true, false);

	cout << "Setting up server with FPS " << fps << " on port " << port << " with clients " << numClients << endl;
}

void ofxMPEServer::reset()
{
	//client already connected, must have reset...
	allconnected = false;
	currentFrame = 0;
	currentMessage = "";
	shouldTriggerFrame = false;
	server.sendToAll("R");
}

void ofxMPEServer::threadedFunction()
{
	//WARNING printing to cout in this thread can cause major slowdown don't do it 
	while(isThreadRunning()){

		if(shouldTriggerFrame){
			float now = ofGetElapsedTimef();
			float elapsed = (now - lastFrameTriggeredTime);

			if(elapsed >= 1.0/framerate){

//				cout << "triggered frame with framerate error of " << fabs( elapsed - 1.0/framerate)  << endl;

	
				string message = "G"+delimiter+ofToString(currentFrame);
				if (currentMessage != ""){
					message += currentMessage;
					currentMessage = "";
				}
				//TODO: per-client messaging
				server.sendToAll(message);
				map<int, Connection>::iterator it;
				for(it = connections.begin(); it != connections.end(); it++){
					it->second.ready = false;
				}

				shouldTriggerFrame = false;
				lastFrameTriggeredTime = now;
			}
		}
		else {

			//check for dead clients
			map<int, Connection>::iterator it;
			int activeConnections = 0;
			for(it = connections.begin(); it != connections.end(); it++){
				Connection& connection = it->second;
				if(!connection.disconnected && !server.isClientConnected(connection.tcpServerIndex)){
					cout << "LOST CONNECTION TO CLIENT " << connection.id << endl;
					connection.disconnected = true;
				}else{
					activeConnections++;
				}
			}
			if(allconnected && activeConnections < numRequiredClients){
				allconnected = false;
				reset();
			}
			for(int i = 0; i < server.getLastID(); i++){
				
				if(!server.isClientConnected(i)){
					continue;
				}
				
				string response = server.receive(i);
				if(response == ""){
					continue;
				}

				vector<string> splitResponse = ofSplitString(response, delimiter, true,true);
				if(splitResponse[0].length() != 1){
					ofLogError() << "MostPixelsEver response code is not valid " << splitResponse[0];
					continue;
				}
	//			cout << "received a response " << response << endl;
				string messageCode = splitResponse[0];
				if(splitResponse[0] == "S"){
				
					if(splitResponse.size() < 2){
						ofLogError() << "MostPixelsEver Wrong number of arguments for synchronous connection. Format is S|ID#|Name. Actual Response [" << response << "]";
						continue;
					}
					
					Connection c;
					c.asynchronous = false;
					c.id = ofToInt(splitResponse[1]);
					
					if(allconnected && connections.find(c.id) != connections.end() &&
					   connections[c.id].disconnected &&
					   currentFrame != 0)
					{
						reset();
					}
					
					c.name = splitResponse.size() > 2 ? splitResponse[2] : "no-name";
					c.receiveMessages = true;
					c.ready = false;
					c.disconnected = false;
					c.tcpServerIndex = i;
					
					connections[c.id] = c;
				
					printClientStatus();

				}
				else if(splitResponse[0] == "A"){
					
					if(splitResponse.size() < 3){
						ofLogError() << "MostPixelsEver Wrong number of arguments for asynchronous connection. Format is A|ID#|Name|RecieveMessages " << response;
						continue;
					}
					
					Connection c;
					c.asynchronous = true;
					c.ready = false;
					
					c.id = ofToInt(splitResponse[1]);
					c.name = splitResponse[2];
					c.receiveMessages = splitResponse.size() > 3 && ofToLower(splitResponse[3]) == "true";
					c.tcpServerIndex = i;
					
					connections[c.id] = c;
				}
				else if(splitResponse[0] == "T"){
					
					//TODO: per-client messages
					currentMessage += delimiter;
					currentMessage += splitResponse[1];
					
				}
				else if(splitResponse[0] == "D"){


					if(!allconnected){
						continue;
					}
					
					if(splitResponse.size() < 3){
						ofLogError() << "MostPixelsEver Wrong number of arguments for Done. Format is D|ID#|Frame# .  " << response;
						return;
					}
					
					int clientID = ofToInt(splitResponse[1]);
					int frameNumber = ofToInt(splitResponse[2]);
//					cout << "Received DONE signal from client " << clientID << " client frame num " <<  frameNumber << " server frame num " << currentFrame << endl;
					if(frameNumber == currentFrame){
						//TODO: validate client id
						connections[clientID].ready = true;
					}					
				}
			}
			//if we are still waiting for everyone to be connected check to see if we are all here...
			if(!allconnected){
				int numConnected = 0;
				map<int, Connection>::iterator it;				
				for(it = connections.begin(); it != connections.end(); it++){
					Connection& connection = it->second;
					if( !connection.asynchronous && !connection.disconnected){
						numConnected++;
					}
				}
				
				//we are all here! trigger the first frame
				if(numConnected == numRequiredClients){
					allconnected = true;
					shouldTriggerFrame = true;
					cout << "All clients connected!" << endl;
				}
			}
			//All connected and going, see if we are ready for the next frame
			else {
				bool allready = true;
				map<int, Connection>::iterator it;
				for(it = connections.begin(); it != connections.end(); it++){
					Connection& connection = it->second;
					if(!connection.asynchronous && !connection.disconnected && !connection.ready){
						allready = false;
						break;
					}
				}
				
				//all clients reported in, next frame!
				if(allready){
//					cout << "All clients Ready!" << endl;
					shouldTriggerFrame = true;
					currentFrame++;
				}
			}
		}
		ofSleepMillis(1);
	}//end while
}

void ofxMPEServer::printClientStatus() {

	ofLog(OF_LOG_NOTICE, "MPE Client Status:");
	ofLog(OF_LOG_NOTICE, "  Requiring " + ofToString(numRequiredClients) + " Clients");

	map<int, Connection>::iterator it;
	for(it = connections.begin(); it != connections.end(); it++){
		ofLogNotice() << (it->second.asynchronous ? "synchronous " : "asynchronous ") <<
			" client #: " + ofToString(it->second.id) + ") "
			<< it->second.name + " server index: " << it->second.tcpServerIndex;
	}
}

void ofxMPEServer::exit(ofEventArgs& args){
	close();
}

void ofxMPEServer::close()
{
	if(isThreadRunning()){
		
		cout << " closing MPE Server " << endl;
		
		//if(server.isConnected()){
		cout << "shutting down server" << endl;
		server.close();

		
		connections.clear();
		waitForThread(true);
		
	}

}
