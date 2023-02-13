/**
 * Copyright 2016-2023 ClickHouse, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <string>
#include <vector>
#include <unordered_set>


namespace Poco { class URI; }
namespace Poco { namespace Util { class AbstractConfiguration; } }

namespace RK
{
class RemoteHostFilter
{
/**
 * This class checks if URL is allowed.
 * If primary_hosts and regexp_hosts are empty all urls are allowed.
 */
public:
    void checkURL(const Poco::URI & uri) const; /// If URL not allowed in config.xml throw UNACCEPTABLE_URL Exception

    void setValuesFromConfig(const Poco::Util::AbstractConfiguration & config);

    void checkHostAndPort(const std::string & host, const std::string & port) const; /// Does the same as checkURL, but for host and port.

private:
    std::unordered_set<std::string> primary_hosts;      /// Allowed primary (<host>) URL from config.xml
    std::vector<std::string> regexp_hosts;              /// Allowed regexp (<hots_regexp>) URL from config.xml

    /// Checks if the primary_hosts and regexp_hosts contain str. If primary_hosts and regexp_hosts are empty return true.
    bool checkForDirectEntry(const std::string & str) const;
};
}
