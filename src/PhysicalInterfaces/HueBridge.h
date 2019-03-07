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

#ifndef HUEBRIDGE_H
#define HUEBRIDGE_H

#include "../PhilipsHuePacket.h"
#include "IPhilipsHueInterface.h"

#include <istream>

namespace PhilipsHue
{

class HueBridge  : public IPhilipsHueInterface
{
    public:
        HueBridge(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
        virtual ~HueBridge();
        void startListening();
        void stopListening();
        void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet);
        int64_t lastAction() { return _lastAction; }
        virtual bool isOpen() { return (bool)_client; }
        virtual void searchLights();
        virtual bool userCreated() { return !_username.empty(); };
        virtual std::set<std::shared_ptr<PhilipsHuePacket>> getPeerInfo();
        virtual std::set<std::shared_ptr<PhilipsHuePacket>> getGroupInfo();
    protected:
        bool _noHost = true;
        int64_t _lastAction = 0;
        uint32_t _pollingInterval = 3000;
        int64_t _nextPoll = 0;
        int32_t _port = 80;
        std::unique_ptr<BaseLib::HttpClient> _client;
        std::unique_ptr<BaseLib::Rpc::JsonEncoder> _jsonEncoder;
        std::unique_ptr<BaseLib::Rpc::JsonDecoder> _jsonDecoder;
        std::string _username;

        void listen();
        void createUser();
        PVariable getJson(std::string& jsonString);
};

}
#endif
