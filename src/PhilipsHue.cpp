#include <memory>

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

#include "PhilipsHue.h"
#include "Interfaces.h"
#include "PhilipsHueCentral.h"
#include "GD.h"

#include <iomanip>

namespace PhilipsHue
{

PhilipsHue::PhilipsHue(BaseLib::SharedObjects* bl, BaseLib::Systems::IFamilyEventSink* eventHandler) : BaseLib::Systems::DeviceFamily(bl, eventHandler, HUE_FAMILY_ID, HUE_FAMILY_NAME)
{
	GD::bl = _bl;
	GD::family = this;
	GD::out.init(bl);
	GD::out.setPrefix("Module Philips hue: ");
	GD::out.printDebug("Debug: Loading module...");
	GD::interfaces = std::make_shared<Interfaces>(bl, _settings->getPhysicalInterfaceSettings());
	_physicalInterfaces = GD::interfaces;
}

PhilipsHue::~PhilipsHue()
{

}

void PhilipsHue::dispose()
{
	if(_disposed) return;
	DeviceFamily::dispose();
    _central.reset();
	GD::interfaces.reset();
	_physicalInterfaces.reset();
}

std::shared_ptr<BaseLib::Systems::ICentral> PhilipsHue::initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber)
{
	return std::make_shared<PhilipsHueCentral>(deviceId, serialNumber, 1, this);
}

void PhilipsHue::createCentral()
{
	try
	{
		if(_central) return;

		int32_t seedNumber = BaseLib::HelperFunctions::getRandomNumber(1, 9999999);
		std::ostringstream stringstream;
		stringstream << "VHC" << std::setw(7) << std::setfill('0') << std::dec << seedNumber;
		std::string serialNumber(stringstream.str());

		_central.reset(new PhilipsHueCentral(0, serialNumber, 1, this));
		GD::out.printMessage("Created central with id " + std::to_string(_central->getId()) + ", address 0x" + BaseLib::HelperFunctions::getHexString(1, 6) + " and serial number " + serialNumber);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

PVariable PhilipsHue::getPairingInfo()
{
	try
	{
		if(!_central) return std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		PVariable info = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		//{{{ General
		info->structValue->emplace("searchInterfaces", std::make_shared<BaseLib::Variable>(true));
		//}}}

		//{{{ Family settings
		PVariable familySettings = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		PVariable field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.philipshue.pairingInfo.pollingInterval")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(5000));
		familySettings->structValue->emplace("pollingInterval", field);

		info->structValue->emplace("familySettings", familySettings);
		//}}}

		//{{{ Pairing methods
		PVariable pairingMethods = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		pairingMethods->structValue->emplace("searchDevices", std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct));
		info->structValue->emplace("pairingMethods", pairingMethods);
		//}}}

		//{{{ interfaces
		PVariable interfaces = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		PVariable interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("Hue Bridge")));
		interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(true));

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.philipshue.pairingInfo.id")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("id", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.philipshue.pairingInfo.hostname")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("host", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.philipshue.pairingInfo.address")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(0));
		interface->structValue->emplace("address", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("80")));
		interface->structValue->emplace("port", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(100));
		interface->structValue->emplace("responseDelay", field);

		interfaces->structValue->emplace("huebridge", interface);

		info->structValue->emplace("interfaces", interfaces);
		//}}}

		return info;
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	return Variable::createError(-32500, "Unknown application error.");
}
}
