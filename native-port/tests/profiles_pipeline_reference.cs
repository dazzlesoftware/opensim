using System;
using System.IO;
using System.Text;
using Nini.Config;
using OpenMetaverse.StructuredData;
using OpenSim.Framework.Servers.HttpServer;

class ProfilesPipelineProgram
{
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var args = line.Split(' ');
            int mode = int.Parse(args[0]), account = int.Parse(args[1]);
            Fixture.Data = new Store(mode);
            Fixture.AccountMode = account;
            Fixture.Accounts = account == 0 ? null : account == 1 ? new Remote() : account == 7 ? new OpenSim.Services.UserAccountService.Derived() : new OpenSim.Services.UserAccountService.UserAccountService();
            Fixture.PluginOverride = null;
            var config = new IniConfigSource(new StringReader("[Profiles]\nEnabled=true\nStorageProvider=store\nUserAccountService=account\nLocalServiceModule=profile\n"));
            var service = new OpenSim.Services.ProfilesService.UserProfilesService(config, "Profiles");
            Fixture.PluginOverride = (module, pluginArgs) => service;
            var server = new ConfigServer();
            var connector = new OpenSim.Server.Handlers.Profiles.UserProfilesConnector(config, server, "Profiles");
            var request = (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(args[2])));
            var response = new JsonRpcResponse();
            response.Id = request["id"].AsString();
            response.JsonRpc = "2.0";
            string method = request["method"].AsString();
            try
            {
                if (!server.Methods[method](request, ref response) && response.Error.Code == 0)
                    response.Error.Code = ErrorCode.InternalError;
            }
            catch (Exception e)
            {
                response.Error.Code = ErrorCode.InternalError;
                response.Error.Message = "[BASE HTTP SERVER]: Json-Rpc Handler Error method " + method + " - " + e.Message;
            }

            Console.WriteLine(Fixture.H(response.Serialize()));
        }
    }
}
