/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace opensim::services {
struct PresenceData {
    std::string user_id;
    framework::Uuid region_id, session_id;
    std::map<std::string, std::string> data;
};
struct PresenceInfo {
    std::string user_id;
    framework::Uuid region_id;
    bool operator==(const PresenceInfo&) const = default;
};
class PresenceStore {
  public:
    virtual ~PresenceStore() = default;
    virtual bool store(const std::shared_ptr<PresenceData>& data) = 0;
    virtual std::shared_ptr<PresenceData> get(const framework::Uuid& session) = 0;
    virtual std::vector<std::shared_ptr<PresenceData>> get(const std::string& field,
                                                           const std::string& value) = 0;
    virtual bool erase(const std::string& field, const std::string& value) = 0;
    virtual bool persistent() const {
        return false;
    }
    virtual bool erase_session_in_region(const framework::Uuid&, const framework::Uuid&) {
        return false;
    }
    virtual void logout_region(const framework::Uuid& region) = 0;
    virtual bool report(const framework::Uuid& session, const framework::Uuid& region) = 0;
    virtual bool verify(const framework::Uuid& agent, const framework::Uuid& secure_session) = 0;
};
class SqliteDatabase;
struct TravelRecord {
    framework::Uuid session_id, user_id;
    std::string grid, service_token, client_ip, external_ip;
};
class SqliteTravelStore {
  public:
    explicit SqliteTravelStore(const std::string& path);
    std::optional<TravelRecord> get(const framework::Uuid& session);
    std::optional<TravelRecord> get_by_user(const framework::Uuid& user);
    void store(const TravelRecord& record);
    void erase(const framework::Uuid& session);

  private:
    std::shared_ptr<SqliteDatabase> database_;
};
class SqlitePresenceStore final : public PresenceStore {
  public:
    explicit SqlitePresenceStore(const std::string& path);
    bool store(const std::shared_ptr<PresenceData>& data) override;
    std::shared_ptr<PresenceData> get(const framework::Uuid& session) override;
    std::vector<std::shared_ptr<PresenceData>> get(const std::string& field,
                                                   const std::string& value) override;
    bool erase(const std::string& field, const std::string& value) override;
    bool persistent() const override {
        return true;
    }
    bool erase_session_in_region(const framework::Uuid& session, const framework::Uuid& region) override;
    void logout_region(const framework::Uuid& region) override;
    bool report(const framework::Uuid& session, const framework::Uuid& region) override;
    bool verify(const framework::Uuid& agent, const framework::Uuid& secure_session) override;

  private:
    std::shared_ptr<SqliteDatabase> database_;
};
class NullPresenceStore final : public PresenceStore {
  public:
    NullPresenceStore();
    bool store(const std::shared_ptr<PresenceData>& data) override;
    std::shared_ptr<PresenceData> get(const framework::Uuid& session) override;
    std::vector<std::shared_ptr<PresenceData>> get(const std::string& field,
                                                   const std::string& value) override;
    bool erase(const std::string& field, const std::string& value) override;
    void logout_region(const framework::Uuid& region) override;
    bool report(const framework::Uuid& session, const framework::Uuid& region) override;
    bool verify(const framework::Uuid&, const framework::Uuid&) override {
        return false;
    }

  private:
    struct State;
    std::shared_ptr<State> state_;
};
std::unique_ptr<PresenceStore> mysql_presence_store(const std::string& connection, const std::string& realm);
std::unique_ptr<PresenceStore> postgres_presence_store(const std::string& connection,
                                                       const std::string& realm);
class PresenceCache {
  public:
    using Clock = std::function<double()>;
    // An injected millisecond clock disables the background purge timer. The
    // host/test scheduler can call purge() to reproduce timer invocations.
    explicit PresenceCache(Clock clock = {});
    ~PresenceCache();
    void purge();

  private:
    struct State;
    std::unique_ptr<State> state_;
    friend class PresenceService;
};
class IPresenceService {
  public:
    virtual ~IPresenceService() = default;
    virtual bool login(const std::string& user, const framework::Uuid& session,
                       const framework::Uuid& secure_session) = 0;
    virtual bool logout(const framework::Uuid& session) = 0;
    virtual bool logout_from_region(const framework::Uuid& session, const framework::Uuid&) {
        logout(session);
        return true;
    }
    virtual bool logout_region(const framework::Uuid& region) = 0;
    virtual bool report(const framework::Uuid& session, const framework::Uuid& region) = 0;
    virtual std::optional<PresenceInfo> agent(const framework::Uuid& session) = 0;
    virtual std::vector<PresenceInfo> agents(const std::vector<std::string>& users) = 0;
};
class PresenceService : public IPresenceService {
  public:
    explicit PresenceService(PresenceStore& store, bool allow_duplicates = false,
                             std::shared_ptr<PresenceCache> cache = {});
    bool login(const std::string& user, const framework::Uuid& session,
               const framework::Uuid& secure_session) override;
    bool logout(const framework::Uuid& session) override;
    bool logout_from_region(const framework::Uuid& session, const framework::Uuid& region) override;
    bool logout_region(const framework::Uuid& region) override;
    bool report(const framework::Uuid& session, const framework::Uuid& region) override;
    std::optional<PresenceInfo> agent(const framework::Uuid& session) override;
    std::vector<PresenceInfo> agents(const std::vector<std::string>& users) override;

  private:
    PresenceStore& store_;
    bool allow_duplicates_;
    std::shared_ptr<PresenceCache> cache_;
};
}
