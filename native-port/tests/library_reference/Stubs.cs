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
using System;
using OpenMetaverse;
namespace log4net {
    public interface ILog {void Debug(string value);void InfoFormat(string value,params object[] args);void WarnFormat(string value,params object[] args);void ErrorFormat(string value,params object[] args);}
    class Log:ILog {public void Debug(string value){}public void InfoFormat(string value,params object[] args){}public void WarnFormat(string value,params object[] args){}public void ErrorFormat(string value,params object[] args){}}
    public static class LogManager {public static ILog GetLogger(Type type)=>new Log();}
}
namespace OpenSim.Services.Base {public class ServiceBase {public ServiceBase(Nini.Config.IConfigSource config){}}}
namespace OpenSim.Framework {
    public static class Constants {public static readonly UUID m_MrOpenSimID=new UUID("11111111-1111-0000-0000-000100bba000");}
    public class osUTF8 {readonly string value;public osUTF8(string value){this.value=value;}public override string ToString()=>value;}
    public class InventoryItemBase {
        public UUID Owner,ID,AssetID,Folder;
        public string CreatorId,Description,Name;
        public int AssetType,InvType;
        public uint BasePermissions,EveryOnePermissions,CurrentPermissions,NextPermissions,GroupPermissions,Flags;
    }
}
