using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.Loader;
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Data.SQLite;

AssemblyLoadContext.Default.Resolving += (context, assembly) =>
{
    var path = Path.Combine(args[0], assembly.Name + ".dll");
    return File.Exists(path) ? context.LoadFromAssemblyPath(path) : null;
};

var store = new SQLiteHGTravelData("URI=file:" + args[1] + ";version=3;Pooling=false", "hg_traveling_data");
var session = new UUID(args[2]);
var user = new UUID(args[3]);
var travel = store.Get(session);
if (travel == null || travel.UserID != user || travel.Data["ServiceToken"] != args[4])
    throw new InvalidOperationException("Managed travel provider did not read the native record.");
var update = new HGTravelingData
{
    SessionID = session,
    UserID = user,
    Data = new Dictionary<string, string>
    {
        ["GridExternalName"] = travel.Data["GridExternalName"],
        ["ServiceToken"] = "managed-travel-token",
        ["ClientIPAddress"] = travel.Data["ClientIPAddress"]
    }
};
if (!store.Store(update))
    throw new InvalidOperationException("Managed travel provider did not update the record.");
