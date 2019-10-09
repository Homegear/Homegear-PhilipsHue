/* Copyright 2013-2019 Homegear GmbH
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

#include "Interfaces.h"
#include "GD.h"
#include "PhysicalInterfaces/HueBridge.h"

namespace PhilipsHue
{

Interfaces::Interfaces(BaseLib::SharedObjects* bl, std::map<std::string, Systems::PPhysicalInterfaceSettings> physicalInterfaceSettings) : Systems::PhysicalInterfaces(bl, GD::family->getFamily(), physicalInterfaceSettings)
{
	create();
}

Interfaces::~Interfaces()
{
	_physicalInterfaces.clear();
	_defaultPhysicalInterface.reset();
    _physicalInterfaceEventhandlers.clear();
    _usedAddresses.clear();
}

void Interfaces::addEventHandlers(BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink* central)
{
	try
	{
		std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
		for(auto interface : _physicalInterfaces)
		{
			if(_physicalInterfaceEventhandlers.find(interface.first) != _physicalInterfaceEventhandlers.end()) continue;
			_physicalInterfaceEventhandlers[interface.first] = interface.second->addEventHandler(central);
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void Interfaces::removeEventHandlers()
{
	try
	{
		std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
		for(auto interface : _physicalInterfaces)
		{
			auto physicalInterfaceEventhandler = _physicalInterfaceEventhandlers.find(interface.first);
			if(physicalInterfaceEventhandler == _physicalInterfaceEventhandlers.end()) continue;
			interface.second->removeEventHandler(physicalInterfaceEventhandler->second);
			_physicalInterfaceEventhandlers.erase(physicalInterfaceEventhandler);
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

uint32_t Interfaces::getFreeAddress()
{
	uint32_t i = 256;
	while(_usedAddresses.find(i) != _usedAddresses.end()) i++;
	return i;
}

void Interfaces::removeUsedAddress(uint32_t address)
{
	if(_usedAddresses.find(address) != _usedAddresses.end()) _usedAddresses.erase(address);
}

std::vector<std::shared_ptr<IPhilipsHueInterface>> Interfaces::getInterfaces()
{
	std::vector<std::shared_ptr<IPhilipsHueInterface>> interfaces;
	try
	{
		std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        interfaces.reserve(_physicalInterfaces.size());
		for(auto interfaceBase : _physicalInterfaces)
		{
			std::shared_ptr<IPhilipsHueInterface> interface(std::dynamic_pointer_cast<IPhilipsHueInterface>(interfaceBase.second));
			if(!interface) continue;
			if(interface->isOpen()) interfaces.push_back(interface);
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	return interfaces;
}

std::shared_ptr<IPhilipsHueInterface> Interfaces::getDefaultInterface()
{
	std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
	return _defaultPhysicalInterface;
}

std::shared_ptr<IPhilipsHueInterface> Interfaces::getInterface(const std::string& name)
{
	std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
	auto interfaceBase = _physicalInterfaces.find(name);
	if(interfaceBase == _physicalInterfaces.end()) return std::shared_ptr<IPhilipsHueInterface>();
	std::shared_ptr<IPhilipsHueInterface> interface(std::dynamic_pointer_cast<IPhilipsHueInterface>(interfaceBase->second));
	return interface;
}

std::shared_ptr<IPhilipsHueInterface> Interfaces::getInterfaceByIp(const std::string& ipAddress)
{
	std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
	for(auto interfaceBase : _physicalInterfaces)
	{
		std::shared_ptr<IPhilipsHueInterface> interface(std::dynamic_pointer_cast<IPhilipsHueInterface>(interfaceBase.second));
		if(!interface) continue;
		if(interface->getHostname() == ipAddress) return interface;
	}
	return std::shared_ptr<IPhilipsHueInterface>();
}

void Interfaces::removeUnknownInterfaces(std::set<std::string>& knownInterfaces)
{
	try
	{
		std::vector<std::string> interfacesToDelete;
		std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
		for(auto interfaceBase : _physicalInterfaces)
		{
			std::shared_ptr<IPhilipsHueInterface> interface(std::dynamic_pointer_cast<IPhilipsHueInterface>(interfaceBase.second));
			if(!interface) continue;
			if(interface->getType() != "huebridge-auto" || knownInterfaces.find(interfaceBase.first) != knownInterfaces.end()) continue;
			{
				GD::out.printInfo("Removing Hue Bridge with serial number " + interfaceBase.first + " and IP address " + interface->getHostname() + ".");
				std::string name = interfaceBase.first + ".devicetype";
				GD::family->deleteFamilySettingFromDatabase(name);
				name = interfaceBase.first + ".host";
				GD::family->deleteFamilySettingFromDatabase(name);
				name = interfaceBase.first + ".port";
				GD::family->deleteFamilySettingFromDatabase(name);
				name = interfaceBase.first + ".responsedelay";
				GD::family->deleteFamilySettingFromDatabase(name);
				name = interfaceBase.first + ".interval";
				GD::family->deleteFamilySettingFromDatabase(name);
				//Don't delete address

				interfacesToDelete.push_back(interfaceBase.first);
			}
		}

		for(auto interface : interfacesToDelete)
		{
			_physicalInterfaces.erase(interface);
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

std::shared_ptr<IPhilipsHueInterface> Interfaces::addInterface(Systems::PPhysicalInterfaceSettings settings, bool storeInDatabase)
{
	try
	{
		std::shared_ptr<IPhilipsHueInterface> device;
		if(!settings || settings->type.empty()) return device;
		GD::out.printDebug("Debug: Creating physical device. Type defined in philipshue.conf is: " + settings->type);

		if(settings->type == "huebridge" || settings->type == "huebridge-auto")
		{
			if(_usedAddresses.find(settings->address) != _usedAddresses.end())
			{
				GD::out.printError("Error loading interface \"" + settings->id + "\": Address " + std::to_string(settings->address) + " is used already. Please specify a valid and unique address for this interface in \"philipshue.conf\".");
				return device;
			}
			_usedAddresses.insert(settings->address);
			device.reset(new HueBridge(settings));
		}
		else if(settings->type.empty()) //Deleted device
		{
			_usedAddresses.insert(settings->address);
			return device;
		}
		else GD::out.printError("Error: Unsupported physical device type: " + settings->type);
		if(device)
		{
			std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
			_physicalInterfaces[settings->id] = device;
			if(settings->isDefault || !_defaultPhysicalInterface || _defaultPhysicalInterface->getType() == "huebridge-temp") _defaultPhysicalInterface = device;
			if(storeInDatabase)
			{
				std::string name = settings->id + ".devicetype";
				GD::family->setFamilySetting(name, settings->type);
				name = settings->id + ".host";
				GD::family->setFamilySetting(name, settings->host);
				name = settings->id + ".port";
				GD::family->setFamilySetting(name, settings->port);
				name = settings->id + ".address";
				GD::family->setFamilySetting(name, settings->address);
				name = settings->id + ".responsedelay";
				GD::family->setFamilySetting(name, settings->responseDelay);
				name = settings->id + ".interval";
				GD::family->setFamilySetting(name, settings->interval);
			}
		}
		return device;
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	return std::shared_ptr<IPhilipsHueInterface>();
}

void Interfaces::create()
{
	try
	{
		for(auto settings : _physicalInterfaceSettings)
		{
			if(settings.second->type == "huebridge" && settings.second->address > 255) settings.second->address = 255;
			addInterface(settings.second, false);
		}
		if(!_defaultPhysicalInterface)
		{
			Systems::PPhysicalInterfaceSettings settings(new Systems::PhysicalInterfaceSettings());
			settings->type = "huebridge-temp";
			_defaultPhysicalInterface.reset(new HueBridge(settings));
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

}
