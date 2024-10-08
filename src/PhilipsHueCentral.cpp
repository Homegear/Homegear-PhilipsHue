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

#include "PhilipsHueCentral.h"
#include "GD.h"

#include <iomanip>

namespace PhilipsHue {

PhilipsHueCentral::PhilipsHueCentral(ICentralEventSink *eventHandler) : BaseLib::Systems::ICentral(HUE_FAMILY_ID, GD::bl, eventHandler) {
  init();
}

PhilipsHueCentral::PhilipsHueCentral(uint32_t deviceID, std::string serialNumber, int32_t address, ICentralEventSink *eventHandler) : BaseLib::Systems::ICentral(HUE_FAMILY_ID, GD::bl, deviceID, serialNumber, address, eventHandler) {
  init();
}

void PhilipsHueCentral::init() {
  _stopWorkerThread = false;
  _shuttingDown = false;
  _searching = false;
  GD::interfaces->addEventHandlers((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink *)this);

  GD::bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &PhilipsHueCentral::worker, this);
}

PhilipsHueCentral::~PhilipsHueCentral() {
  dispose();
}

void PhilipsHueCentral::dispose(bool wait) {
  try {
    if (_disposing) return;
    _disposing = true;
    _stopWorkerThread = true;
    GD::bl->threadManager.join(_searchDevicesThread);
    GD::out.printDebug("Debug: Waiting for worker thread of device " + std::to_string(_deviceId) + "...");
    _bl->threadManager.join(_workerThread);
    GD::out.printDebug("Removing device " + std::to_string(_deviceId) + " from physical device's event queue...");
    GD::interfaces->removeEventHandlers();
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

bool PhilipsHueCentral::onPacketReceived(std::string &senderID, std::shared_ptr<BaseLib::Systems::Packet> packet) {
  try {
    if (_disposing) return false;
    std::shared_ptr<PhilipsHuePacket> philipsHuePacket(std::dynamic_pointer_cast<PhilipsHuePacket>(packet));
    if (!philipsHuePacket) return false;
    std::shared_ptr<PhilipsHuePeer> peer;
    if (philipsHuePacket->getCategory() == PhilipsHuePacket::Category::light) peer = getPeer(philipsHuePacket->senderAddress());
    else {
      std::string serialNumber = "*HUE";
      std::string addressString = BaseLib::HelperFunctions::getHexString(philipsHuePacket->senderAddress());
      serialNumber.resize(12 - addressString.size(), '0');
      serialNumber.append(addressString);
      peer = getPeer(serialNumber);
    }
    if (!peer) return false;
    peer->packetReceived(philipsHuePacket);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

void PhilipsHueCentral::sendPacket(std::shared_ptr<IPhilipsHueInterface> &interface, std::shared_ptr<PhilipsHuePacket> packet) {
  try {
    if (!packet) return;
    uint32_t responseDelay = interface->responseDelay();
    std::shared_ptr<PacketManager> packetManager = _sentPackets[interface->getID()];
    if (!packetManager) {
      packetManager.reset(new PacketManager());
      _sentPackets[interface->getID()] = packetManager;
    }
    std::shared_ptr<PhilipsHuePacketInfo> packetInfo = packetManager->getInfo(packet->destinationAddress());
    packetManager->set(packet->destinationAddress(), packet);
    if (packetInfo) {
      int64_t timeDifference = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - packetInfo->time;
      if (timeDifference < responseDelay) {
        packetInfo->time += responseDelay - timeDifference; //Set to sending time
        std::this_thread::sleep_for(std::chrono::milliseconds(responseDelay - timeDifference));
      }
    }
    packetManager->keepAlive(packet->destinationAddress());
    interface->sendPacket(packet);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

uint32_t PhilipsHueCentral::getDeviceType(const std::string &manufacturer, const std::string &modelId, const std::string &type, PhilipsHuePacket::Category category) {
  try {
    if (modelId.length() < 4) return (uint32_t)DeviceType::none;
    std::string typeId = manufacturer.empty() ? modelId : manufacturer + ' ' + modelId;
    uint32_t typeNumber = GD::family->getRpcDevices()->getTypeNumberFromTypeId(typeId);
    if (typeNumber == 0 && (category == PhilipsHuePacket::Category::light || category == PhilipsHuePacket::Category::group)) {
      if (modelId.compare(0, 3, "LCT") == 0) return (uint32_t)DeviceType::LCT001;
      else if (modelId.compare(0, 3, "LLC") == 0) return (uint32_t)DeviceType::LLC001;
      else if (modelId.compare(0, 3, "LST") == 0) return (uint32_t)DeviceType::LST001;
      else if (modelId.compare(0, 3, "LWB") == 0) return (uint32_t)DeviceType::LWB004;
      else if (type == "Extended color light") return (uint32_t)DeviceType::LST001;
      else if (type == "On/Off plug-in unit") return (uint32_t)DeviceType::LST001;
      else {
        GD::out.printInfo("Info: Device type for ID \"" + typeId + "\" not found. Setting device type to LCT001.");
        return (uint32_t)DeviceType::LCT001; //default
      }
    }
    return typeNumber;
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return (uint32_t)DeviceType::none;
}

void PhilipsHueCentral::loadPeers() {
  try {
    std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
    std::vector<std::shared_ptr<PhilipsHuePeer>> teams;
    for (BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row) {
      int32_t peerID = row->second.at(0)->intValue;
      GD::out.printMessage("Loading peer " + std::to_string(peerID));
      int32_t address = row->second.at(2)->intValue;
      std::shared_ptr<PhilipsHuePeer> peer(new PhilipsHuePeer(peerID, address, row->second.at(3)->textValue, _deviceId, this));
      if (!peer->load(this)) continue;
      if (!peer->getRpcDevice()) continue;
      std::lock_guard<std::mutex> peersGuard(_peersMutex);
      if (!peer->isTeam()) _peers[peer->getAddress()] = peer;
      else teams.push_back(peer);
      if (!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
      _peersById[peerID] = peer;
    }

    for (auto team : teams) {
      std::set<uint64_t> teamPeers = team->getTeamPeers();
      for (auto teamPeer : teamPeers) {
        std::shared_ptr<PhilipsHuePeer> peer = getPeer(teamPeer);
        if (!peer) continue;
        peer->setTeamId(team->getID());
        peer->setTeamSerialNumber(team->getSerialNumber());
      }
    }
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void PhilipsHueCentral::loadVariables() {
  try {
    std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getDeviceVariables(_deviceId);
    for (BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row) {
      _variableDatabaseIds[row->second.at(2)->intValue] = row->second.at(0)->intValue;
      switch (row->second.at(2)->intValue) {
        case 0: _firmwareVersion = row->second.at(3)->intValue;
          break;
      }
    }
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void PhilipsHueCentral::savePeers(bool full) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    for (std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peers.begin(); i != _peers.end(); ++i) {
      //Necessary, because peers can be assigned to multiple virtual devices
      if (i->second->getParentID() != _deviceId) continue;
      //We are always printing this, because the init script needs it
      GD::out.printMessage("(Shutdown) => Saving peer " + std::to_string(i->second->getID()));
      i->second->save(full, full, full);
    }
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void PhilipsHueCentral::saveVariables() {
  try {
    if (_deviceId == 0) return;
    saveVariable(0, _firmwareVersion);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::shared_ptr<PhilipsHuePeer> PhilipsHueCentral::getPeer(int32_t address) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    if (_peers.find(address) != _peers.end()) return std::dynamic_pointer_cast<PhilipsHuePeer>(_peers.at(address));
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<PhilipsHuePeer>();
}

std::shared_ptr<PhilipsHuePeer> PhilipsHueCentral::getPeer(uint64_t id) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    if (_peersById.find(id) != _peersById.end()) return std::dynamic_pointer_cast<PhilipsHuePeer>(_peersById.at(id));
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<PhilipsHuePeer>();
}

std::shared_ptr<PhilipsHuePeer> PhilipsHueCentral::getPeer(std::string serialNumber) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    if (_peersBySerial.find(serialNumber) != _peersBySerial.end()) return std::dynamic_pointer_cast<PhilipsHuePeer>(_peersBySerial.at(serialNumber));
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<PhilipsHuePeer>();
}

void PhilipsHueCentral::deletePeer(uint64_t id) {
  try {
    std::shared_ptr<PhilipsHuePeer> peer(getPeer(id));
    if (!peer) return;

    if (peer->isTeam()) {
      std::set<uint64_t> teamPeers = peer->getTeamPeers();
      for (auto teamPeerId : teamPeers) {
        std::shared_ptr<PhilipsHuePeer> teamPeer = getPeer(teamPeerId);
        if (!teamPeer) continue;
        teamPeer->setTeamId(0);
        teamPeer->setTeamSerialNumber("");
      }
    } else if (peer->hasTeam()) {
      std::shared_ptr<PhilipsHuePeer> team(getPeer(peer->getTeamId()));
      if (team) {
        team->removeTeamPeer(peer->getID());
        team->saveTeamPeers();
      }
    }

    peer->deleting = true;
    PVariable deviceAddresses(new Variable(VariableType::tArray));
    deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber())));

    PVariable deviceInfo(new Variable(VariableType::tStruct));
    deviceInfo->structValue->insert(StructElement("ID", PVariable(new Variable((int32_t)peer->getID()))));
    PVariable channels(new Variable(VariableType::tArray));
    deviceInfo->structValue->insert(StructElement("CHANNELS", channels));

    std::shared_ptr<HomegearDevice> rpcDevice = peer->getRpcDevice();
    for (Functions::iterator i = rpcDevice->functions.begin(); i != rpcDevice->functions.end(); ++i) {
      deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber() + ":" + std::to_string(i->first))));
      channels->arrayValue->push_back(PVariable(new Variable(i->first)));
    }

    std::vector<uint64_t> deletedIds{id};
    raiseRPCDeleteDevices(deletedIds, deviceAddresses, deviceInfo);

    {
      std::lock_guard<std::mutex> peersGuard(_peersMutex);
      if (_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
      if (_peersById.find(id) != _peersById.end()) _peersById.erase(id);
      if (!peer->isTeam() && _peers.find(peer->getAddress()) != _peers.end()) _peers.erase(peer->getAddress());
    }

    int32_t i = 0;
    while (peer.use_count() > 1 && i < 600) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      i++;
    }
    if (i == 600) GD::out.printError("Error: Peer deletion took too long.");

    peer->deleteFromDatabase();
    GD::out.printMessage("Removed peer " + std::to_string(peer->getID()));
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::string PhilipsHueCentral::handleCliCommand(std::string command) {
  try {
    std::ostringstream stringStream;
    if (command == "help" || command == "h") {
      stringStream << "List of commands (shortcut in brackets):" << std::endl << std::endl;
      stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
      stringStream << "peers list (ls)\t\tList all peers" << std::endl;
      stringStream << "peers remove (prm)\tRemove a peer (without unpairing)" << std::endl;
      stringStream << "peers select (ps)\tSelect a peer" << std::endl;
      stringStream << "peers setname (pn)\tName a peer" << std::endl;
      stringStream << "search (sp)\t\tSearches for new devices" << std::endl;
      stringStream << "unselect (u)\t\tUnselect this device" << std::endl;
      return stringStream.str();
    }
    if (command.compare(0, 12, "peers remove") == 0 || command.compare(0, 3, "prm") == 0) {
      uint64_t peerID = 0;

      std::stringstream stream(command);
      std::string element;
      int32_t offset = (command.at(1) == 'r') ? 0 : 1;
      int32_t index = 0;
      while (std::getline(stream, element, ' ')) {
        if (index < 1 + offset) {
          index++;
          continue;
        } else if (index == 1 + offset) {
          if (element == "help") break;
          peerID = BaseLib::Math::getNumber(element, false);
          if (peerID == 0) return "Invalid id.\n";
        }
        index++;
      }
      if (index == 1 + offset) {
        stringStream << "Description: This command removes a peer without trying to unpair it first." << std::endl;
        stringStream << "Usage: peers remove PEERID" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  PEERID:\tThe id of the peer to remove. Example: 513" << std::endl;
        return stringStream.str();
      }

      if (!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
      else {
        deletePeer(peerID);
        stringStream << "Removed peer " << std::to_string(peerID) << "." << std::endl;
      }
      return stringStream.str();
    } else if (command.compare(0, 10, "peers list") == 0 || command.compare(0, 2, "pl") == 0 || command.compare(0, 2, "ls") == 0) {
      try {
        std::string filterType;
        std::string filterValue;

        std::stringstream stream(command);
        std::string element;
        int32_t offset = (command.at(1) == 'l' || command.at(1) == 's') ? 0 : 1;
        int32_t index = 0;
        while (std::getline(stream, element, ' ')) {
          if (index < 1 + offset) {
            index++;
            continue;
          } else if (index == 1 + offset) {
            if (element == "help") {
              index = -1;
              break;
            }
            filterType = BaseLib::HelperFunctions::toLower(element);
          } else if (index == 2 + offset) {
            filterValue = element;
            if (filterType == "name") BaseLib::HelperFunctions::toLower(filterValue);
          }
          index++;
        }
        if (index == -1) {
          stringStream << "Description: This command lists information about all peers." << std::endl;
          stringStream << "Usage: peers list [FILTERTYPE] [FILTERVALUE]" << std::endl << std::endl;
          stringStream << "Parameters:" << std::endl;
          stringStream << "  FILTERTYPE:\tSee filter types below." << std::endl;
          stringStream << "  FILTERVALUE:\tDepends on the filter type. If a number is required, it has to be in hexadecimal format." << std::endl << std::endl;
          stringStream << "Filter types:" << std::endl;
          stringStream << "  ID: Filter by id." << std::endl;
          stringStream << "      FILTERVALUE: The id of the peer to filter (e. g. 513)." << std::endl;
          stringStream << "  ADDRESS: Filter by address." << std::endl;
          stringStream << "      FILTERVALUE: The 3 byte address of the peer to filter (e. g. 1DA44D)." << std::endl;
          stringStream << "  SERIAL: Filter by serial number." << std::endl;
          stringStream << "      FILTERVALUE: The serial number of the peer to filter (e. g. JEQ0554309)." << std::endl;
          stringStream << "  NAME: Filter by name." << std::endl;
          stringStream << "      FILTERVALUE: The part of the name to search for (e. g. \"1st floor\")." << std::endl;
          stringStream << "  TYPE: Filter by device type." << std::endl;
          stringStream << "      FILTERVALUE: The 2 byte device type in hexadecimal format." << std::endl;
          stringStream << "  CONFIGPENDING: List peers with pending config." << std::endl;
          stringStream << "      FILTERVALUE: empty" << std::endl;
          stringStream << "  UNREACH: List all unreachable peers." << std::endl;
          stringStream << "      FILTERVALUE: empty" << std::endl;
          return stringStream.str();
        }

        if (_peers.empty()) {
          stringStream << "No peers are paired to this central." << std::endl;
          return stringStream.str();
        }
        bool firmwareUpdates = false;
        std::string bar(" │ ");
        const int32_t idWidth = 11;
        const int32_t nameWidth = 25;
        const int32_t addressWidth = 8;
        const int32_t serialWidth = 13;
        const int32_t typeWidth1 = 4;
        const int32_t typeWidth2 = 25;
        const int32_t firmwareWidth = 8;
        const int32_t configPendingWidth = 14;
        const int32_t unreachWidth = 7;
        std::string nameHeader("Name");
        nameHeader.resize(nameWidth, ' ');
        std::string typeStringHeader("Type String");
        typeStringHeader.resize(typeWidth2, ' ');
        stringStream << std::setfill(' ')
                     << std::setw(idWidth) << "ID" << bar
                     << nameHeader << bar
                     << std::setw(addressWidth) << "Address" << bar
                     << std::setw(serialWidth) << "Serial Number" << bar
                     << std::setw(typeWidth1) << "Type" << bar
                     << typeStringHeader << bar
                     << std::setw(firmwareWidth) << "Firmware" << bar
                     << std::setw(configPendingWidth) << "Config Pending" << bar
                     << std::setw(unreachWidth) << "Unreach"
                     << std::endl;
        stringStream << "────────────┼───────────────────────────┼──────────┼───────────────┼──────┼───────────────────────────┼──────────┼────────────────┼────────" << std::endl;
        stringStream << std::setfill(' ')
                     << std::setw(idWidth) << " " << bar
                     << std::setw(nameWidth) << " " << bar
                     << std::setw(addressWidth) << " " << bar
                     << std::setw(serialWidth) << " " << bar
                     << std::setw(typeWidth1) << " " << bar
                     << std::setw(typeWidth2) << " " << bar
                     << std::setw(firmwareWidth) << " " << bar
                     << std::setw(configPendingWidth) << " " << bar
                     << std::setw(unreachWidth) << " "
                     << std::endl;
        _peersMutex.lock();
        for (std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i) {
          std::shared_ptr<PhilipsHuePeer> peer(std::dynamic_pointer_cast<PhilipsHuePeer>(i->second));
          if (filterType == "id") {
            uint64_t id = BaseLib::Math::getNumber(filterValue, false);
            if (i->second->getID() != id) continue;
          } else if (filterType == "name") {
            std::string name = i->second->getName();
            if ((signed)BaseLib::HelperFunctions::toLower(name).find(filterValue) == (signed)std::string::npos) continue;
          } else if (filterType == "address") {
            int32_t address = BaseLib::Math::getNumber(filterValue, true);
            if (i->second->getAddress() != address) continue;
          } else if (filterType == "serial") {
            if (i->second->getSerialNumber() != filterValue) continue;
          } else if (filterType == "type") {
            int32_t deviceType = BaseLib::Math::getNumber(filterValue, true);
            if ((int32_t)i->second->getDeviceType() != deviceType) continue;
          } else if (filterType == "configpending") {
            if (i->second->serviceMessages) {
              if (!i->second->serviceMessages->getConfigPending()) continue;
            }
          } else if (filterType == "unreach") {
            if (i->second->serviceMessages) {
              if (!i->second->serviceMessages->getUnreach()) continue;
            }
          }

          uint64_t currentID = i->second->getID();
          std::string idString = (currentID > 999999) ? "0x" + BaseLib::HelperFunctions::getHexString(currentID, 8) : std::to_string(currentID);
          stringStream << std::setw(idWidth) << std::setfill(' ') << idString << bar;
          std::string name = i->second->getName();
          size_t nameSize = BaseLib::HelperFunctions::utf8StringSize(name);
          if (nameSize > (unsigned)nameWidth) {
            name = BaseLib::HelperFunctions::utf8Substring(name, 0, nameWidth - 3);
            name += "...";
          } else name.resize(nameWidth + (name.size() - nameSize), ' ');
          stringStream << name << bar
                       << std::setw(addressWidth) << BaseLib::HelperFunctions::getHexString(i->second->getAddress(), 8) << bar
                       << std::setw(serialWidth) << i->second->getSerialNumber() << bar
                       << std::setw(typeWidth1) << BaseLib::HelperFunctions::getHexString(i->second->getDeviceType(), 4) << bar;
          if (i->second->getRpcDevice()) {
            PSupportedDevice type = i->second->getRpcDevice()->getType(i->second->getDeviceType(), i->second->getFirmwareVersion());
            std::string typeID;
            if (type) typeID = type->id;
            if (typeID.size() > (unsigned)typeWidth2) {
              typeID.resize(typeWidth2 - 3);
              typeID += "...";
            } else typeID.resize(typeWidth2, ' ');
            stringStream << typeID << bar;
          } else stringStream << std::setw(typeWidth2) << " " << bar;
          if (i->second->getFirmwareVersion() == 0) stringStream << std::setfill(' ') << std::setw(firmwareWidth) << "?" << bar;
          else stringStream << std::setfill(' ') << std::setw(firmwareWidth) << std::dec << (uint32_t)i->second->getFirmwareVersion() << bar;
          if (i->second->serviceMessages) {
            std::string configPending(i->second->serviceMessages->getConfigPending() ? "Yes" : "No");
            std::string unreachable(i->second->serviceMessages->getUnreach() ? "Yes" : "No");
            stringStream << std::setfill(' ') << std::setw(configPendingWidth) << configPending << bar;
            stringStream << std::setfill(' ') << std::setw(unreachWidth) << unreachable;
          }
          stringStream << std::endl << std::dec;
        }
        _peersMutex.unlock();
        stringStream << "────────────┴───────────────────────────┴──────────┴───────────────┴──────┴───────────────────────────┴──────────┴────────────────┴────────" << std::endl;
        if (firmwareUpdates) stringStream << std::endl << "*: Firmware update available." << std::endl;

        return stringStream.str();
      }
      catch (const std::exception &ex) {
        _peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    } else if (command.compare(0, 13, "peers setname") == 0 || command.compare(0, 2, "pn") == 0) {
      uint64_t peerID = 0;
      std::string name;

      std::stringstream stream(command);
      std::string element;
      int32_t offset = (command.at(1) == 'n') ? 0 : 1;
      int32_t index = 0;
      while (std::getline(stream, element, ' ')) {
        if (index < 1 + offset) {
          index++;
          continue;
        } else if (index == 1 + offset) {
          if (element == "help") break;
          else {
            peerID = BaseLib::Math::getNumber(element, false);
            if (peerID == 0) return "Invalid id.\n";
          }
        } else if (index == 2 + offset) name = element;
        else name += ' ' + element;
        index++;
      }
      if (index == 1 + offset) {
        stringStream << "Description: This command sets or changes the name of a peer to identify it more easily." << std::endl;
        stringStream << "Usage: peers setname PEERID NAME" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  PEERID:\tThe id of the peer to set the name for. Example: 513" << std::endl;
        stringStream << "  NAME:\tThe name to set. Example: \"1st floor light switch\"." << std::endl;
        return stringStream.str();
      }

      if (!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
      else {
        std::shared_ptr<PhilipsHuePeer> peer = getPeer(peerID);
        peer->setName(name);
        stringStream << "Name set to \"" << name << "\"." << std::endl;
      }
      return stringStream.str();
    } else if (command.compare(0, 6, "search") == 0 || command.compare(0, 2, "sp") == 0) {
      std::stringstream stream(command);
      std::string element;
      int32_t offset = (command.at(1) == 'p') ? 0 : 1;
      int32_t index = 0;
      while (std::getline(stream, element, ' ')) {
        if (index < 1 + offset) {
          index++;
          continue;
        } else if (index == 1 + offset) {
          if (element == "help") {
            stringStream << "Description: This command searches for new devices." << std::endl;
            stringStream << "Usage: search" << std::endl << std::endl;
            stringStream << "Parameters:" << std::endl;
            stringStream << "  There are no parameters." << std::endl;
            return stringStream.str();
          }
        }
        index++;
      }

      searchHueBridges();
      searchDevicesThread("");
      stringStream << "Search completed. Please press the button on all newly added hue bridges." << std::endl;
      return stringStream.str();
    } else return "Unknown command.\n";
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return "Error executing command. See log file for more details.\n";
}

std::shared_ptr<PhilipsHuePeer> PhilipsHueCentral::createPeer(int32_t address, int32_t firmwareVersion, uint32_t deviceType, std::string serialNumber, std::shared_ptr<IPhilipsHueInterface> interface, bool save) {
  try {
    std::shared_ptr<PhilipsHuePeer> peer(new PhilipsHuePeer(_deviceId, this));
    peer->setAddress(address);
    peer->setFirmwareVersion(firmwareVersion);
    peer->setDeviceType(deviceType);
    peer->setSerialNumber(serialNumber);
    peer->setRpcDevice(GD::family->getRpcDevices()->find(deviceType, firmwareVersion, -1));
    if (!peer->getRpcDevice()) return std::shared_ptr<PhilipsHuePeer>();
    if (save) peer->save(true, true, false); //Save and create peerID
    peer->setPhysicalInterfaceId(interface->getID());
    return peer;
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<PhilipsHuePeer>();
}

std::shared_ptr<PhilipsHuePeer> PhilipsHueCentral::createTeam(int32_t address, std::string serialNumber, std::shared_ptr<IPhilipsHueInterface> interface, bool save) {
  try {
    std::shared_ptr<PhilipsHuePeer> team(new PhilipsHuePeer(_deviceId, this));
    team->setAddress(address);
    team->setDeviceType(0x1000);
    team->setSerialNumber(serialNumber);
    team->setRpcDevice(GD::family->getRpcDevices()->find(0x1000, 0, -1));
    if (!team->getRpcDevice()) return std::shared_ptr<PhilipsHuePeer>();
    if (save) team->save(true, true, false); //Save and create peerID
    team->setPhysicalInterfaceId(interface->getID());
    //Do not save team!!!
    return team;
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<PhilipsHuePeer>();
}

void PhilipsHueCentral::searchHueBridges(bool removeNotFound) {
  try {
    std::lock_guard<std::mutex> searchDevicesGuard(_searchHueBridgesMutex);
    BaseLib::Ssdp ssdp(GD::bl);
    std::string stHeader("urn:schemas-upnp-org:device:basic:1");
    std::map<std::string, BaseLib::SsdpInfo> devices;
    std::vector<BaseLib::SsdpInfo> searchResult;
    ssdp.searchDevices(stHeader, 5000, searchResult);
    for (auto device : searchResult) {
      if (devices.find(device.ip()) == devices.end()) devices.insert(std::pair<std::string, BaseLib::SsdpInfo>(device.ip(), device));
    }
    searchResult.clear();
    ssdp.searchDevices(stHeader, 5000, searchResult);
    if (_shuttingDown) return;
    for (auto device : searchResult) {
      if (devices.find(device.ip()) == devices.end()) devices.insert(std::pair<std::string, BaseLib::SsdpInfo>(device.ip(), device));
    }

    std::set<std::string> foundInterfaces;
    for (auto device : devices) {
      PVariable info = device.second.info();
      if (!info) continue;
      if (info->structValue->find("manufacturer") == info->structValue->end() || info->structValue->find("modelName") == info->structValue->end() || info->structValue->find("serialNumber") == info->structValue->end()) continue;
      if (info->structValue->at("modelName")->stringValue.compare(0, 18, "Philips hue bridge") != 0) continue;
      Systems::PPhysicalInterfaceSettings settings = std::make_shared<Systems::PhysicalInterfaceSettings>();
      settings->id = BaseLib::HelperFunctions::stringReplace(info->structValue->at("serialNumber")->stringValue, ".", "-"); //Points are not allowed as they are needed for seperation of config settings
      foundInterfaces.insert(settings->id);
      settings->host = device.second.ip();
      auto interface = GD::interfaces->getInterface(settings->id);
      if (interface && interface->getHostname() == device.second.ip()) {
        GD::out.printInfo("Info: Ignoring already known Hue Bridge with IP address " + device.second.ip() + " and serial number " + settings->id + ".");
        continue;
      }
      settings->address = GD::interfaces->getFreeAddress();
      if (settings->address > 4095) {
        GD::out.printError("Error: Can't add Hue Bridge, because there are no more free addresses available.");
        continue;
      }
      settings->type = "huebridge-auto";
      settings->port = "80";
      settings->responseDelay = 100;
      settings->interval = 10000;

      //Check if device was paired before and get address and username
      std::string name = settings->id + ".address";
      BaseLib::Systems::FamilySettings::PFamilySetting setting = GD::family->getFamilySetting(name);
      if (setting) {
        GD::out.printInfo("Info: Assigning known address " + std::to_string(setting->integerValue) + "...");
        settings->address = setting->integerValue;
        GD::interfaces->removeUsedAddress(settings->address);
      }
      name = settings->id + ".user";
      setting = GD::family->getFamilySetting(name);
      if (setting) {
        GD::out.printInfo("Info: Assigning known username...");
        settings->user = setting->stringValue;
      }

      std::shared_ptr<IPhilipsHueInterface> newInterface = GD::interfaces->addInterface(settings, true);
      if (newInterface) {
        GD::out.printInfo("Info: Found new Hue Bridge with IP address " + device.second.ip() + " and serial number " + settings->id + ".");
        newInterface->startListening();
      }
    }
    if (!foundInterfaces.empty()) GD::interfaces->addEventHandlers((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink *)this);
    if (removeNotFound) GD::interfaces->removeUnknownInterfaces(foundInterfaces);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  _searching = false;
}

std::vector<std::shared_ptr<PhilipsHuePeer>> PhilipsHueCentral::searchTeams(bool findNew) {
  try {
    std::vector<std::shared_ptr<PhilipsHuePeer>> newPeers;
    auto interfaces = GD::interfaces->getInterfaces();
    for (auto interface : interfaces) {
      auto groupsInfo = interface->getGroupInfo();

      {
        std::lock_guard<std::mutex> peerInitGuard(_peerInitMutex);
        for (auto groupInfo : groupsInfo) {
          PVariable info = groupInfo->getJson();

          std::string serialNumber = "*HUE";
          std::string addressString = BaseLib::HelperFunctions::getHexString(groupInfo->senderAddress());
          serialNumber.resize(12 - addressString.size(), '0');
          serialNumber.append(addressString);

          std::shared_ptr<PhilipsHuePeer> team = getPeer(serialNumber);
          if (team) {
            auto peers = info->structValue->find("lights");
            if (peers != info->structValue->end()) {
              std::set<uint64_t> newIds;
              for (auto peerEntry : *peers->second->arrayValue) {
                int32_t address = BaseLib::Math::getNumber(peerEntry->stringValue, false);
                if (address == 0) continue;
                address |= (team->getInterfaceAddress() << 20);

                std::shared_ptr<PhilipsHuePeer> peer = getPeer(address);
                if (peer) newIds.insert(peer->getID());
              }

              std::set<uint64_t> teamPeers = team->getTeamPeers();
              for (auto teamPeer : teamPeers) {
                if (newIds.find(teamPeer) == newIds.end()) {
                  team->removeTeamPeer(teamPeer);

                  std::shared_ptr<PhilipsHuePeer> peer = getPeer(teamPeer);
                  if (peer) {
                    peer->setTeamId(0);
                    peer->setTeamSerialNumber("");
                  }
                }
              }

              for (auto newTeamPeer : newIds) {
                if (teamPeers.find(newTeamPeer) != teamPeers.end()) continue;
                team->addTeamPeer(newTeamPeer);

                std::shared_ptr<PhilipsHuePeer> peer = getPeer(newTeamPeer);
                if (!peer) continue;
                peer->setTeamId(team->getID());
                peer->setTeamSerialNumber(team->getSerialNumber());
              }
            }
          } else if (findNew) {
            team = createTeam(groupInfo->senderAddress(), serialNumber, interface, true);
            if (team) {
              auto name = info->structValue->find("name");
              if (name != info->structValue->end()) team->setName(name->second->stringValue);
              team->initializeCentralConfig();

              {
                std::lock_guard<std::mutex> peersGuard(_peersMutex);
                _peersBySerial[team->getSerialNumber()] = team;
                _peersById[team->getID()] = team;
              }

              auto peers = info->structValue->find("lights");
              if (peers != info->structValue->end()) {
                for (auto peerEntry : *peers->second->arrayValue) {
                  int32_t address = BaseLib::Math::getNumber(peerEntry->stringValue, false);
                  if (address == 0) continue;
                  address |= (team->getInterfaceAddress() << 20);

                  std::shared_ptr<PhilipsHuePeer> peer = getPeer(address);
                  if (!peer) continue;

                  peer->setTeamId(team->getID());
                  peer->setTeamSerialNumber(team->getSerialNumber());

                  team->addTeamPeer(peer->getID());
                }
                team->saveTeamPeers();
              }

              newPeers.push_back(team);
            }
          }
        }
      }
    }
    return newPeers;
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<std::shared_ptr<PhilipsHuePeer>>();
}

void PhilipsHueCentral::searchDevicesThread(std::string interfaceId) {
  try {
    std::lock_guard<std::mutex> searchDevicesGuard(_searchDevicesMutex);
    std::vector<std::shared_ptr<PhilipsHuePeer>> newPeers;
    auto interfaces = GD::interfaces->getInterfaces();
    for (auto &interface : interfaces) {
      if (!interfaceId.empty() && interface->getID() != interfaceId) continue;

      for (int32_t i = 0; i < 3; i++)
      {
        interface->searchLights();

        if (!interface->userCreated()) {
          std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
          _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.philipshue.bridge.pressLinkButton", std::list<std::string>{interface->getID(), interface->getIpAddress()}));

          for (int32_t i = 0; i < 20; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (_stopWorkerThread) return;
          }

          continue;
        }

        break;
      }

      auto peersInfo = interface->getPeerInfo();

      {
        std::lock_guard<std::mutex> peerInitGuard(_peerInitMutex);
        for (auto &peerInfo : peersInfo) {
          PVariable info = peerInfo->getJson();
          if (info->structValue->find("modelid") == info->structValue->end() || info->structValue->find("swversion") == info->structValue->end()) continue;
          std::string manufacturer;
          if (info->structValue->find("manufacturername") != info->structValue->end()) manufacturer = BaseLib::HelperFunctions::trim(info->structValue->at("manufacturername")->stringValue);
          std::string type;
          if (info->structValue->find("type") != info->structValue->end()) type = BaseLib::HelperFunctions::trim(info->structValue->at("type")->stringValue);
          uint32_t deviceType = getDeviceType(manufacturer, BaseLib::HelperFunctions::trim(info->structValue->at("modelid")->stringValue), type, peerInfo->getCategory());

          std::shared_ptr<PhilipsHuePeer> peer = getPeer(peerInfo->senderAddress());
          if (peer) {
            if (peer->getDeviceType() == deviceType) {
              if (peer->getPhysicalInterface()->getID() != interface->getID()) peer->setPhysicalInterfaceId(interface->getID());
              continue;
            }
            deletePeer(peer->getID());
            peer.reset();
          }
          std::string swversion = info->structValue->at("swversion")->stringValue;
          auto pos = swversion.find_first_of('.');
          if (pos > 0) pos = swversion.find_first_of('.', pos + 1);
          if (pos > 0) swversion = swversion.substr(0, pos);
          BaseLib::HelperFunctions::stringReplace(swversion, ".", "");
          BaseLib::HelperFunctions::stringReplace(swversion, "V", "");
          if (swversion.size() > 4) swversion = swversion.substr(0, 4);

          std::string serialNumber = "HUE";
          std::string addressString = BaseLib::HelperFunctions::getHexString(peerInfo->senderAddress());
          serialNumber.resize(11 - addressString.size(), '0');
          serialNumber.append(addressString);
          peer = createPeer(peerInfo->senderAddress(), BaseLib::Math::getNumber(swversion, true), (uint32_t)deviceType, serialNumber, interface, true);
          if (!peer) {
            GD::out.printError(
                "Error: Could not pair device with address " + BaseLib::HelperFunctions::getHexString(peerInfo->senderAddress(), 8) + ", type " + BaseLib::HelperFunctions::getHexString((uint32_t)deviceType, 4) + " and firmware version "
                    + std::to_string(BaseLib::Math::getNumber(swversion)) + ". No matching XML file was found.");
            continue;
          }

          peer->initializeCentralConfig();
          if (info->structValue->find("name") != info->structValue->end()) peer->setName(info->structValue->at("name")->stringValue);

          {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            _peers[peer->getAddress()] = peer;
            if (!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
            _peersById[peer->getID()] = peer;
          }
          newPeers.push_back(peer);
        }
      }
    }

    std::vector<std::shared_ptr<PhilipsHuePeer>> newTeams = searchTeams();

    for (auto team : newTeams) {
      newPeers.push_back(team);
    }

    if (!newPeers.empty()) {
      std::vector<uint64_t> newIds;
      newIds.reserve(newPeers.size());
      PVariable deviceDescriptions = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
      deviceDescriptions->arrayValue->reserve(100);
      for (auto &newPeer : newPeers) {
        std::shared_ptr<std::vector<PVariable>> descriptions = newPeer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
        if (!descriptions) continue;
        newIds.push_back(newPeer->getID());
        for (auto &description : *descriptions) {
          if (deviceDescriptions->arrayValue->size() + 1 > deviceDescriptions->arrayValue->capacity()) deviceDescriptions->arrayValue->reserve(deviceDescriptions->arrayValue->size() + 100);
          deviceDescriptions->arrayValue->push_back(description);
        }

        {
          auto pairingState = std::make_shared<PairingState>();
          pairingState->peerId = newPeer->getID();
          pairingState->state = "success";
          std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
          _newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
        }
      }
      raiseRPCNewDevices(newIds, deviceDescriptions);
    }
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  _searching = false;
}

void PhilipsHueCentral::homegearShuttingDown() {
  _shuttingDown = true;
}

void PhilipsHueCentral::worker() {
  try {
    while (GD::bl->booting && !_stopWorkerThread) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::chrono::milliseconds sleepingTime(1000);
    uint32_t counter = 0;
    uint32_t countsPer10Minutes = BaseLib::HelperFunctions::getRandomNumber(10, 600);

    while (!_stopWorkerThread && !_shuttingDown) {
      try {
        std::this_thread::sleep_for(sleepingTime);
        if (_stopWorkerThread || _shuttingDown) return;
        // Update devices (most importantly the IP address)
        if (counter > countsPer10Minutes) {
          countsPer10Minutes = 600;
          counter = 0;
          searchHueBridges(false);
          searchTeams(false);
        }
        counter++;
      }
      catch (const std::exception &ex) {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    }
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

//RPC functions
PVariable PhilipsHueCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags) {
  try {
    if (serialNumber.empty()) return Variable::createError(-2, "Unknown device.");

    uint64_t peerId = 0;

    {
      std::shared_ptr<PhilipsHuePeer> peer = getPeer(serialNumber);
      if (!peer) return Variable::createError(-2, "Unknown device.");
      peerId = peer->getID();
    }

    return deleteDevice(clientInfo, peerId, flags);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable PhilipsHueCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, int32_t flags) {
  try {
    if (peerId == 0) return Variable::createError(-2, "Unknown device.");
    if (peerId >= 0x40000000) return Variable::createError(-2, "Cannot delete virtual device.");

    {
      std::shared_ptr<PhilipsHuePeer> peer = getPeer(peerId);
      if (!peer) return Variable::createError(-2, "Unknown device.");
    }

    deletePeer(peerId);

    return std::make_shared<Variable>(VariableType::tVoid);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable PhilipsHueCentral::getPairingState(BaseLib::PRpcClientInfo clientInfo) {
  try {
    auto states = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

    states->structValue->emplace("pairingModeEnabled", std::make_shared<BaseLib::Variable>(_searching));
    states->structValue->emplace("pairingModeEndTime", std::make_shared<BaseLib::Variable>(-1));

    {
      std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);

      auto pairingMessages = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
      pairingMessages->arrayValue->reserve(_pairingMessages.size());
      for (auto &message : _pairingMessages) {
        auto pairingMessage = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
        pairingMessage->structValue->emplace("messageId", std::make_shared<BaseLib::Variable>(message->messageId));
        auto variables = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
        variables->arrayValue->reserve(message->variables.size());
        for (auto &variable : message->variables) {
          variables->arrayValue->emplace_back(std::make_shared<BaseLib::Variable>(variable));
        }
        pairingMessage->structValue->emplace("variables", variables);
        pairingMessages->arrayValue->push_back(pairingMessage);
      }
      states->structValue->emplace("general", std::move(pairingMessages));

      for (auto &element : _newPeers) {
        for (auto &peer : element.second) {
          auto peerState = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
          peerState->structValue->emplace("state", std::make_shared<BaseLib::Variable>(peer->state));
          peerState->structValue->emplace("messageId", std::make_shared<BaseLib::Variable>(peer->messageId));
          auto variables = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
          variables->arrayValue->reserve(peer->variables.size());
          for (auto &variable : peer->variables) {
            variables->arrayValue->emplace_back(std::make_shared<BaseLib::Variable>(variable));
          }
          peerState->structValue->emplace("variables", variables);
          states->structValue->emplace(std::to_string(peer->peerId), std::move(peerState));
        }
      }
    }

    return states;
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable PhilipsHueCentral::searchDevices(BaseLib::PRpcClientInfo clientInfo, const std::string &interfaceId) {
  try {
    if (_searching) return std::make_shared<BaseLib::Variable>(-3);
    _searching = true;
    _bl->threadManager.start(_searchDevicesThread, true, &PhilipsHueCentral::searchDevicesThread, this, interfaceId);
    return std::make_shared<BaseLib::Variable>(-2);
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable PhilipsHueCentral::searchInterfaces(BaseLib::PRpcClientInfo clientInfo, BaseLib::PVariable metadata) {
  try {
    if (_searching) return PVariable(new Variable(0));
    _searching = true;
    _bl->threadManager.start(_searchDevicesThread, true, &PhilipsHueCentral::searchHueBridges, this, true);
    return PVariable(new Variable(-2));
  }
  catch (const std::exception &ex) {
    GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}
//End RPC functions
}
