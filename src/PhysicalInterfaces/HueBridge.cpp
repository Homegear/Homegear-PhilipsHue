/* Copyright 2013-2016 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "HueBridge.h"
#include "../GD.h"

namespace PhilipsHue
{
HueBridge::HueBridge(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhilipsHueInterface(settings)
{
	_out.init(GD::bl);
	_out.setPrefix(GD::out.getPrefix() + "Philips hue bridge \"" + settings->id + "\": ");

	signal(SIGPIPE, SIG_IGN);

	if(!settings)
	{
		_out.printCritical("Critical: Error initializing. Settings pointer is empty.");
		return;
	}
	if(settings->host.empty()) _out.printCritical("Critical: Error initializing. Hostname is not defined in settings file.");
	_hostname = settings->host;
	_port = BaseLib::Math::getNumber(settings->port);
	if(_port < 1 || _port > 65535) _port = 80;
	_username = settings->user;
	if(settings->interval < 15000) settings->interval = 15000;

	_jsonEncoder.reset(new BaseLib::Rpc::JsonEncoder(GD::bl));
	_jsonDecoder.reset(new BaseLib::Rpc::JsonDecoder(GD::bl));
}

HueBridge::~HueBridge()
{
	try
	{
		_stopCallbackThread = true;
		_bl->threadManager.join(_listenThread);
		_client.reset();
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HueBridge::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(!packet)
		{
			_out.printWarning("Warning: Packet was nullptr.");
			return;
		}
		_lastAction = BaseLib::HelperFunctions::getTime();

		std::shared_ptr<PhilipsHuePacket> huePacket(std::dynamic_pointer_cast<PhilipsHuePacket>(packet));
		if(!huePacket) return;

		PVariable json = huePacket->getJson();
		if(!json) return;

		_nextPoll = BaseLib::HelperFunctions::getTime() + 2000; // No polling now

		std::string data;
		_jsonEncoder->encode(json, data);
    	std::string header = "PUT /api/" + _username + "/lights/" + std::to_string(packet->destinationAddress() & 0xFFFFFF) + "/state HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(data.size()) + "\r\nConnection: Keep-Alive\r\n\r\n";
    	data.insert(data.begin(), header.begin(), header.end());
		data.push_back('\r');
		data.push_back('\n');
    	std::string response;

    	Exception exception("");
    	for(int i = 0; i < 5; i++)
    	{
			try
			{
				if(_stopCallbackThread || GD::bl->shuttingDown) return;
				_client->sendRequest(data, response);
				exception = Exception("");
				break;
			}
			catch(Exception& ex)
			{
				exception = ex;
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
    	}
    	if(!exception.what().empty())
    	{
    		_out.printError("Error: Command was not send to Hue Bridge: " + exception.what());
    	}

    	json = getJson(response);
		if(!json) return;

		if(!json->arrayValue->empty() && json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
		{
			json = json->arrayValue->at(0)->structValue->at("error");
			if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
			else _out.printError("Unknown error sending packet. Response was: " + response);
		}

		_nextPoll = BaseLib::HelperFunctions::getTime() + 2000; // Poll in 2 seconds

		/*std::string getData = "GET /api/homegear" + _settings->id + "/lights/" + std::to_string(packet->destinationAddress()) + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nConnection: Keep-Alive\r\n\r\n";
		for(int i = 0; i < 5; i++)
    	{
			try
			{
				_client->sendRequest(getData, response);
				exception = Exception("");
				break;
			}
			catch(Exception& ex)
			{
				exception = ex;
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
    	}
    	if(!exception.what().empty())
    	{
    		_out.printWarning("Warning: Could not read updated data from Hue Bridge: " + exception.what());
    	}

		json = getJson(response);
		if(!json) return;
		if(!json->arrayValue->empty() && json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
		{
			json = json->arrayValue->at(0)->structValue->at("error");
			if(json->structValue->find("type") != json->structValue->end() && json->structValue->at("type")->integerValue == 1)
			{
				createUser();
			}
			else
			{
				if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
				else _out.printError("Unknown error during polling. Response was: " + response);
			}
			return;
		}

		if(json->structValue->find("state") != json->structValue->end())
		{
			std::shared_ptr<PhilipsHuePacket> packet(new PhilipsHuePacket(huePacket->destinationAddress(), 0, 1, json, BaseLib::HelperFunctions::getTime()));
			raisePacketReceived(packet);
		}*/

		_lastPacketSent = BaseLib::HelperFunctions::getTime();
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HueBridge::startListening()
{
	try
	{
		stopListening();
		_client = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port, false, _settings->ssl, _settings->caFile, _settings->verifyCertificate));
		if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &HueBridge::listen, this);
		else _bl->threadManager.start(_listenThread, true, &HueBridge::listen, this);
		IPhysicalInterface::startListening();
	}
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HueBridge::stopListening()
{
	try
	{
		_stopCallbackThread = true;
		_bl->threadManager.join(_listenThread);
		_stopCallbackThread = false;
		if(_client) _client->disconnect();
		IPhysicalInterface::stopListening();
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HueBridge::createUser()
{
	try
    {
		std::string data = "{\"devicetype\":\"Homegear" + _settings->id + "\"}";
    	std::string header = "POST /api HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(data.size()) + "\r\nConnection: Keep-Alive\r\n\r\n";
    	data.insert(data.begin(), header.begin(), header.end());
		data.push_back('\r');
		data.push_back('\n');
    	std::string response;

    	_client->sendRequest(data, response);

    	PVariable json = getJson(response);
		if(!json) return;

		if(!json->arrayValue->empty())
		{
			if(json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
			{
				json = json->arrayValue->at(0)->structValue->at("error");
				if(json->structValue->find("type") != json->structValue->end() && json->structValue->at("type")->integerValue == 101) _out.printError("Please press the link button on your hue bridge to initialize the connection to Homegear.");
				else if(json->structValue->find("type") != json->structValue->end() && json->structValue->at("type")->integerValue == 7) _out.printError("Error: Please change the bridge's id in philipshue.conf to only contain alphanumerical characters and \"-\".");
				else
				{
					if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
					else _out.printError("Unknown error during user creation. Response was: " + response);
				}
			}
			else if(json->arrayValue->at(0)->structValue->find("success") != json->arrayValue->at(0)->structValue->end())
			{
				json = json->arrayValue->at(0)->structValue->at("success");
				if(json->structValue->find("username") != json->structValue->end())
				{
					_username = json->structValue->at("username")->stringValue;
					_settings->user = _username;
					saveSettingToDatabase("user", _username);
				}
			}
		}
		else _out.printError("Error: Returned JSON is empty.");
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HueBridge::searchLights()
{
	try
    {
    	std::string header = "POST /api/" + _username + "/lights HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nContent-Type: application/json\r\nContent-Length: 0\r\nConnection: Keep-Alive\r\n\r\n";
    	std::string response;

    	_client->sendRequest(header, response);

    	PVariable json = getJson(response);
		if(!json) return;

		if(!json->arrayValue->empty() && json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
		{
			json = json->arrayValue->at(0)->structValue->at("error");
			if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
			else _out.printError("Unknown error during user creation. Response was: " + response);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(15000));

		header = "GET /api/" + _username + "/lights/new HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nConnection: Keep-Alive\r\n\r\n";

    	_client->sendRequest(header, response);

		json = getJson(response);
		if(!json) return;

		if(!json->arrayValue->empty() && json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
		{
			json = json->arrayValue->at(0)->structValue->at("error");
			if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
			else _out.printError("Unknown error during user creation. Response was: " + response);
		}
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::vector<std::shared_ptr<PhilipsHuePacket>> HueBridge::getPeerInfo()
{
	try
	{
		std::string getAllData = "GET /api/" + _username + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nConnection: Keep-Alive\r\n\r\n";
		std::string response;
		_client->sendRequest(getAllData, response);

		PVariable json = getJson(response);
		if(!json) return std::vector<std::shared_ptr<PhilipsHuePacket>>();

		if(!json->arrayValue->empty() && json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
		{
			json = json->arrayValue->at(0)->structValue->at("error");
			if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
			else _out.printError("Unknown error during polling. Response was: " + response);
			return std::vector<std::shared_ptr<PhilipsHuePacket>>();
		}

		std::vector<std::shared_ptr<PhilipsHuePacket>> peers;
		if(json->structValue->find("lights") != json->structValue->end())
		{
			json = json->structValue->at("lights");
			for(std::map<std::string, PVariable>::iterator i = json->structValue->begin(); i != json->structValue->end(); ++i)
			{
				std::string address = i->first;
				std::shared_ptr<PhilipsHuePacket> packet(new PhilipsHuePacket((_settings->address << 24) | BaseLib::Math::getNumber(address), 0, 1, i->second, BaseLib::HelperFunctions::getTime()));
				peers.push_back(packet);
			}
		}
		return peers;
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::vector<std::shared_ptr<PhilipsHuePacket>>();
}

PVariable HueBridge::getJson(std::string& jsonString)
{
	try
	{
		return _jsonDecoder->decode(jsonString);
	}
	catch(const BaseLib::Rpc::JsonDecoderException& ex)
	{
		_out.printError("Error parsing json: " + ex.what() + ". Data was: " + jsonString);
	}
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return PVariable();
}

void HueBridge::listen()
{
    try
    {
    	std::string getAllData = "GET /api/" + _username + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nConnection: Keep-Alive\r\n\r\n";
    	std::string response;
    	Exception exception("");

        while(!_stopCallbackThread)
        {
        	try
        	{
				while(BaseLib::HelperFunctions::getTime() < _nextPoll)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
					if(_stopCallbackThread) return;
				}
				_nextPoll = BaseLib::HelperFunctions::getTime() + _settings->interval;
				if(_username.empty())
				{
					createUser();
					getAllData = "GET /api/" + _username + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nConnection: Keep-Alive\r\n\r\n";
					_nextPoll = BaseLib::HelperFunctions::getTime() + 5000;
					continue;
				}
				for(int32_t i = 0; i < 5; i++)
				{
					try
					{
						if(_stopCallbackThread) return;
						_client->sendRequest(getAllData, response);
						exception = BaseLib::Exception("");
						break;
					}
					catch(const BaseLib::Exception& ex)
					{
						exception = ex;
						std::this_thread::sleep_for(std::chrono::milliseconds(1000));
					}
				}
				if(!exception.what().empty())
				{
					_out.printError("Error: Command was not send to Hue Bridge: " + exception.what());
					continue;
				}

				PVariable json = getJson(response);
				if(!json) return;

				if(!json->arrayValue->empty() && json->arrayValue->at(0)->structValue->find("error") != json->arrayValue->at(0)->structValue->end())
				{
					json = json->arrayValue->at(0)->structValue->at("error");
					if(json->structValue->find("type") != json->structValue->end() && json->structValue->at("type")->integerValue == 1)
					{
						createUser();
						getAllData = "GET /api/" + _username + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + _hostname + ":" + std::to_string(_port) + "\r\nConnection: Keep-Alive\r\n\r\n";
						_nextPoll = BaseLib::HelperFunctions::getTime() + 5000;
					}
					else
					{
						if(json->structValue->find("description") != json->structValue->end()) _out.printError("Error: " + json->structValue->at("description")->stringValue);
						else _out.printError("Unknown error during polling. Response was: " + response);
					}
					continue;
				}

				if(json->structValue->find("lights") != json->structValue->end())
				{
					json = json->structValue->at("lights");
					for(std::map<std::string, PVariable>::iterator i = json->structValue->begin(); i != json->structValue->end(); ++i)
					{
						std::string address = i->first;
						std::shared_ptr<PhilipsHuePacket> packet(new PhilipsHuePacket((_settings->address << 24) | BaseLib::Math::getNumber(address), 0, 1, i->second, BaseLib::HelperFunctions::getTime()));
						raisePacketReceived(packet);
					}
				}

				_lastPacketReceived = BaseLib::HelperFunctions::getTime();
			}
			catch(const std::exception& ex)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}
}
